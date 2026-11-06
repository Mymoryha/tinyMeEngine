#include <tinyme/render/d3d12/ClearRenderer.h>

#include "GpuTimer.h"
#include "ShaderCompiler.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl/client.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace tinyme
{
using Microsoft::WRL::ComPtr;
namespace
{
constexpr UINT kBufferCount = 2;
constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DWORD kFenceTimeoutMs = 10000;
struct TriangleVertex
{
    std::array<float, 3> position;
    std::array<float, 3> color;
};
static_assert(sizeof(TriangleVertex) == 24 && offsetof(TriangleVertex, color) == 12);
constexpr std::array<TriangleVertex, 4> kTriangleVertices{{
    {{2.31111111F, -1.3F, 0.0F}, {0.15F, 1.0F, 0.15F}},
    {{20.0F, 20.0F, 0.0F}, {0.0F, 0.0F, 0.0F}},
    {{-2.31111111F, -1.3F, 0.0F}, {0.15F, 0.15F, 1.0F}},
    {{0.0F, 1.3F, 0.0F}, {1.0F, 0.15F, 0.15F}},
}};
// The unreferenced vertex makes accidental non-indexed drawing observably incorrect.
constexpr std::array<std::uint16_t, 3> kTriangleIndices{3, 0, 2};
constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 2> kTriangleInput{{
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
     0},
    {"COLOR", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
}};

std::string Hex(HRESULT value)
{
    std::ostringstream text;
    text << "0x" << std::hex << static_cast<unsigned long>(value);
    return text.str();
}

void Check(HRESULT result, const char* operation)
{
    if (FAILED(result))
    {
        throw std::runtime_error(std::string(operation) + " failed: " + Hex(result));
    }
}

std::string Utf8(const wchar_t* value)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1)
    {
        return {};
    }
    std::string text(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, text.data(), length, nullptr, nullptr);
    text.pop_back();
    return text;
}
} // namespace

struct ClearRenderer::Impl
{
    struct FrameContext
    {
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12Resource> buffer;
        UINT64 last_fence = 0;
    };

    RendererOptions options;
    WindowSize size;
    ValidationStats validation;
    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGISwapChain3> swapchain;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12RootSignature> root_signature;
    ComPtr<ID3D12PipelineState> triangle_pipeline;
    ComPtr<ID3D12Resource> triangle_vertices;
    ComPtr<ID3D12Resource> triangle_indices;
    ComPtr<ID3D12Resource> triangle_upload;
    D3D12_VERTEX_BUFFER_VIEW triangle_vertex_view{};
    D3D12_INDEX_BUFFER_VIEW triangle_index_view{};
    TriangleGeometryInfo geometry_info;
    std::uint64_t pipeline_generation = 0;
    std::uint64_t presentations = 0;
    RendererInfo renderer_info;
    std::unique_ptr<GpuTimer> timer;
    ComPtr<ID3D12DescriptorHeap> rtv_heap;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12InfoQueue> info_queue;
    ComPtr<IDXGIInfoQueue> dxgi_info_queue;
    std::array<FrameContext, kBufferCount> frames;
    HANDLE fence_event = nullptr;
    UINT rtv_stride = 0;
    UINT64 next_fence = 1;
    bool occluded = false;
    bool stopped = false;
    bool shutdown_complete = false;
    bool faulted = false;

    ~Impl()
    {
        try
        {
            Shutdown();
        }
        catch (const std::exception& error)
        {
            std::cerr << "error: renderer cleanup: " << error.what() << '\n';
            if (!shutdown_complete)
            {
                std::cerr << "fatal: GPU completion cannot be established; refusing unsafe "
                             "resource release.\n";
                std::terminate();
            }
        }
    }

    void CheckDevice(HRESULT result, const char* operation) const
    {
        if (FAILED(result) && device)
        {
            throw std::runtime_error(
                std::string(operation) + " failed: " + Hex(result) +
                "; device_removed_reason=" + Hex(device->GetDeviceRemovedReason()));
        }
        Check(result, operation);
    }

    void Initialize(HWND hwnd, WindowSize initial_size, RendererOptions renderer_options)
    {
        if (!IsWindow(hwnd) || initial_size.width == 0 || initial_size.height == 0)
        {
            throw std::invalid_argument("Renderer requires a live HWND and nonzero client size.");
        }
        options = renderer_options;
        options.debug_layer = options.debug_layer || options.gpu_validation;
        size = initial_size;
        if (options.debug_layer)
        {
            ComPtr<ID3D12Debug> debug;
            const auto result = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
            if (FAILED(result))
            {
                throw std::runtime_error(
                    "D3D12 Debug Layer is unavailable. Install Windows Graphics Tools; " +
                    Hex(result));
            }
            debug->EnableDebugLayer();
            if (options.gpu_validation)
            {
                ComPtr<ID3D12Debug1> debug1;
                Check(debug.As(&debug1), "ID3D12Debug1");
                debug1->SetEnableGPUBasedValidation(TRUE);
            }
            Check(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgi_info_queue)),
                  "DXGI debug interface");
            dxgi_info_queue->ClearStoredMessages(DXGI_DEBUG_ALL);
        }
        Check(CreateDXGIFactory2(options.debug_layer ? DXGI_CREATE_FACTORY_DEBUG : 0,
                                 IID_PPV_ARGS(&factory)),
              "CreateDXGIFactory2");

        ComPtr<IDXGIAdapter1> selected;
        UINT matches = 0;
        for (UINT index = 0;; ++index)
        {
            ComPtr<IDXGIAdapter1> adapter;
            const auto result = factory->EnumAdapterByGpuPreference(
                index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
            if (result == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }
            Check(result, "EnumAdapterByGpuPreference");
            DXGI_ADAPTER_DESC1 description{};
            Check(adapter->GetDesc1(&description), "GetDesc1");
            const auto name = Utf8(description.Description);
            std::cout << "adapter index=" << index << " name=\"" << name
                      << "\" vram_mib=" << description.DedicatedVideoMemory / (1024 * 1024) << '\n';
            if (description.VendorId == 0x10DE &&
                !(description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                name.find("RTX 5080") != std::string::npos)
            {
                selected = adapter;
                ++matches;
            }
        }
        if (matches != 1)
        {
            throw std::runtime_error(
                "Expected exactly one RTX 5080 adapter; no integrated/WARP fallback.");
        }
        DXGI_ADAPTER_DESC1 description{};
        Check(selected->GetDesc1(&description), "Selected adapter description");
        LARGE_INTEGER driver{};
        const auto driver_result = selected->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driver);
        renderer_info.adapter = Utf8(description.Description);
        std::ostringstream driver_text;
        if (SUCCEEDED(driver_result))
        {
            driver_text << HIWORD(driver.HighPart) << '.' << LOWORD(driver.HighPart) << '.'
                        << HIWORD(driver.LowPart) << '.' << LOWORD(driver.LowPart);
        }
        else
        {
            driver_text << "unavailable(" << Hex(driver_result) << ')';
        }
        renderer_info.driver = driver_text.str();
        std::cout << "selected_adapter=\"" << renderer_info.adapter
                  << "\" driver=" << renderer_info.driver << '\n';
        Check(D3D12CreateDevice(selected.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)),
              "D3D12CreateDevice(feature level 12.0)");
        if (options.debug_layer)
        {
            Check(device.As(&info_queue), "ID3D12InfoQueue");
        }
        LogCapabilities();

        D3D12_COMMAND_QUEUE_DESC queue_description{};
        queue_description.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&queue_description, IID_PPV_ARGS(&queue)),
              "CreateCommandQueue");
        Check(queue->SetName(L"TinyMe.DirectQueue"), "Queue name");
        Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
        fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event)
        {
            throw std::runtime_error("CreateEventW failed: " + std::to_string(GetLastError()));
        }
        for (auto& frame : frames)
        {
            Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 IID_PPV_ARGS(&frame.allocator)),
                  "CreateCommandAllocator");
        }
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        frames[0].allocator.Get(), nullptr,
                                        IID_PPV_ARGS(&commands)),
              "CreateCommandList");
        Check(commands->Close(), "Initial command list Close");
        D3D12_DESCRIPTOR_HEAP_DESC heap_description{};
        heap_description.NumDescriptors = kBufferCount;
        heap_description.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        Check(device->CreateDescriptorHeap(&heap_description, IID_PPV_ARGS(&rtv_heap)),
              "Create RTV heap");
        rtv_stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        DXGI_SWAP_CHAIN_DESC1 swap_description{};
        swap_description.Width = size.width;
        swap_description.Height = size.height;
        swap_description.Format = kBackBufferFormat;
        swap_description.SampleDesc.Count = 1;
        swap_description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_description.BufferCount = kBufferCount;
        swap_description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> initial_swapchain;
        Check(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &swap_description, nullptr,
                                              nullptr, &initial_swapchain),
              "CreateSwapChainForHwnd");
        Check(initial_swapchain.As(&swapchain), "IDXGISwapChain3");
        Check(factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER), "MakeWindowAssociation");
        CreateBuffers();
        if (options.profiling)
        {
            timer = std::make_unique<GpuTimer>(device.Get(), queue.Get());
            renderer_info.timestamp_frequency = timer->GetFrequency();
            std::cout << "timestamp_frequency_hz=" << renderer_info.timestamp_frequency << '\n';
        }
        std::cout << "renderer=d3d12 buffers=2 format=RGBA8_UNORM vsync=on debug_layer="
                  << options.debug_layer << " gpu_validation=" << options.gpu_validation << '\n';
        CheckDiagnostics();
    }

    void LogCapabilities()
    {
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1,
                                         D3D_FEATURE_LEVEL_12_0};
        D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels{3, levels, D3D_FEATURE_LEVEL_12_0};
        Check(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &feature_levels,
                                          sizeof(feature_levels)),
              "Feature levels");
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
        D3D12_FEATURE_DATA_D3D12_OPTIONS6 options6{};
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
        Check(
            device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)),
            "Raytracing capabilities");
        Check(
            device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS6, &options6, sizeof(options6)),
            "VRS capabilities");
        Check(
            device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)),
            "Mesh shader capabilities");
        D3D12_FEATURE_DATA_SHADER_MODEL shader_model{};
        for (int model = static_cast<int>(D3D_SHADER_MODEL_6_8);
             model >= static_cast<int>(D3D_SHADER_MODEL_6_0); --model)
        {
            shader_model.HighestShaderModel = static_cast<D3D_SHADER_MODEL>(model);
            const auto result = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,
                                                            &shader_model, sizeof(shader_model));
            if (SUCCEEDED(result))
            {
                break;
            }
            if (result != E_INVALIDARG || model == static_cast<int>(D3D_SHADER_MODEL_6_0))
            {
                Check(result, "Shader model capabilities");
            }
        }
        std::cout << "capabilities feature_level=" << std::hex
                  << static_cast<unsigned>(feature_levels.MaxSupportedFeatureLevel)
                  << " shader_model=" << static_cast<unsigned>(shader_model.HighestShaderModel)
                  << std::dec
                  << " raytracing_tier=" << static_cast<unsigned>(options5.RaytracingTier)
                  << " vrs_tier=" << static_cast<unsigned>(options6.VariableShadingRateTier)
                  << " mesh_shader_tier=" << static_cast<unsigned>(options7.MeshShaderTier) << '\n';
    }

    void CheckDiagnostics()
    {
        if (info_queue)
        {
            const auto count = info_queue->GetNumStoredMessagesAllowedByRetrievalFilter();
            for (UINT64 index = 0; index < count; ++index)
            {
                SIZE_T length = 0;
                Check(info_queue->GetMessage(index, nullptr, &length), "D3D12 message size");
                std::vector<unsigned char> storage(length);
                auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                Check(info_queue->GetMessage(index, message, &length), "D3D12 message");
                if (message->ID == D3D12_MESSAGE_ID_LIVE_DEVICE ||
                    message->ID == D3D12_MESSAGE_ID_LIVE_OBJECT_SUMMARY)
                {
                    continue;
                }
                const bool live_child =
                    std::string_view(message->pDescription).starts_with("Live ID3D12");
                if (live_child || message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
                {
                    const bool error =
                        live_child || message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR;
                    error ? ++validation.errors : ++validation.warnings;
                    std::cout << "validation source=d3d12 severity="
                              << (error ? "error" : "warning") << " id=" << message->ID << " "
                              << message->pDescription << '\n';
                }
            }
            info_queue->ClearStoredMessages();
        }
        if (dxgi_info_queue)
        {
            const auto count = dxgi_info_queue->GetNumStoredMessages(DXGI_DEBUG_ALL);
            for (UINT64 index = 0; index < count; ++index)
            {
                SIZE_T length = 0;
                Check(dxgi_info_queue->GetMessage(DXGI_DEBUG_ALL, index, nullptr, &length),
                      "DXGI message size");
                std::vector<unsigned char> storage(length);
                auto* message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(storage.data());
                Check(dxgi_info_queue->GetMessage(DXGI_DEBUG_ALL, index, message, &length),
                      "DXGI message");
                if (message->Severity <= DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING)
                {
                    const bool error = message->Severity <= DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR;
                    error ? ++validation.errors : ++validation.warnings;
                    std::cout << "validation source=dxgi severity=" << (error ? "error" : "warning")
                              << " id=" << message->ID << " " << message->pDescription << '\n';
                }
            }
            dxgi_info_queue->ClearStoredMessages(DXGI_DEBUG_ALL);
        }
        if (validation.errors)
        {
            throw std::runtime_error(
                "GPU validation reported error/corruption; see validation output.");
        }
    }

    bool ReloadTriangle(const std::filesystem::path& source, std::string& diagnostics)
    {
        if (stopped || faulted)
        {
            throw std::logic_error("Cannot reload a stopped/faulted renderer.");
        }
        const auto snapshot = ReadShaderSource(source, diagnostics);
        if (snapshot.empty())
        {
            return false;
        }
        const auto vertex = CompileShader(source, snapshot, L"VSMain", L"vs_6_0");
        const auto pixel = CompileShader(source, snapshot, L"PSMain", L"ps_6_0");
        diagnostics = vertex.diagnostics + pixel.diagnostics;
        if (!vertex.IsValid() || !pixel.IsValid())
        {
            return false;
        }
        faulted = true;
        WaitIdle();
        CheckDiagnostics();
        ComPtr<ID3D12RootSignature> candidate_root;
        ComPtr<ID3D12PipelineState> candidate_pipeline;
        try
        {
            std::array<D3D12_ROOT_PARAMETER, 2> parameters{};
            parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameters[0].Constants.ShaderRegister = 0;
            parameters[0].Constants.Num32BitValues = kTriangleParameterDwords;
            parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
            parameters[1].Constants.ShaderRegister = 1;
            parameters[1].Constants.Num32BitValues = sizeof(CameraTransform) / sizeof(float);
            parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
            D3D12_ROOT_SIGNATURE_DESC root_desc{};
            root_desc.NumParameters = static_cast<UINT>(parameters.size());
            root_desc.pParameters = parameters.data();
            root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            ComPtr<ID3DBlob> root_blob, root_errors;
            Check(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob,
                                              &root_errors),
                  "Serialize root signature");
            CheckDevice(device->CreateRootSignature(0, root_blob->GetBufferPointer(),
                                                    root_blob->GetBufferSize(),
                                                    IID_PPV_ARGS(&candidate_root)),
                        "Create root signature");
            D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
            desc.pRootSignature = candidate_root.Get();
            desc.VS = {vertex.bytecode.data(), vertex.bytecode.size()};
            desc.PS = {pixel.bytecode.data(), pixel.bytecode.size()};
            desc.InputLayout = {kTriangleInput.data(), static_cast<UINT>(kTriangleInput.size())};
            desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
            desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
            desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
            desc.BlendState.RenderTarget[0].LogicOp = D3D12_LOGIC_OP_NOOP;
            desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
            desc.SampleMask = UINT_MAX;
            desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
            desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            desc.RasterizerState.DepthClipEnable = TRUE;
            desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            desc.NumRenderTargets = 1;
            desc.RTVFormats[0] = kBackBufferFormat;
            desc.SampleDesc.Count = 1;
            CheckDevice(
                device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&candidate_pipeline)),
                "Create triangle PSO");
        }
        catch (const std::exception& error)
        {
            if (FAILED(device->GetDeviceRemovedReason()))
            {
                faulted = true;
                throw;
            }
            diagnostics += error.what();
            bool confirmed_shader_rejection = false;
            if (info_queue)
            {
                const auto count = info_queue->GetNumStoredMessagesAllowedByRetrievalFilter();
                for (UINT64 index = 0; index < count; ++index)
                {
                    SIZE_T length = 0;
                    Check(info_queue->GetMessage(index, nullptr, &length),
                          "Candidate message size");
                    std::vector<unsigned char> storage(length);
                    auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                    Check(info_queue->GetMessage(index, message, &length), "Candidate message");
                    const bool shader_contract =
                        message->ID ==
                            D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_VS_ROOT_SIGNATURE_MISMATCH ||
                        message->ID ==
                            D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_PS_ROOT_SIGNATURE_MISMATCH ||
                        message->ID == D3D12_MESSAGE_ID_CREATEINPUTLAYOUT_MISSINGELEMENT ||
                        message->ID == D3D12_MESSAGE_ID_CREATEINPUTLAYOUT_TYPE_MISMATCH ||
                        message->ID ==
                            D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_INPUTLAYOUT_SHADER_MISMATCH ||
                        (message->ID >=
                             D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_SHADER_LINKAGE_REGISTERINDEX &&
                         message->ID <=
                             D3D12_MESSAGE_ID_CREATEGRAPHICSPIPELINESTATE_SHADER_LINKAGE_SEMANTICNAME_NOT_FOUND);
                    confirmed_shader_rejection = confirmed_shader_rejection || shader_contract;
                    if (!shader_contract && message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
                    {
                        faulted = true;
                        CheckDiagnostics();
                        throw;
                    }
                    diagnostics += "\nrejected_candidate id=" + std::to_string(message->ID) + " " +
                                   message->pDescription;
                }
                info_queue->ClearStoredMessages();
            }
            if (!confirmed_shader_rejection)
            {
                throw;
            }
            faulted = false;
            return false;
        }
        if (!geometry_info.resources_ready)
        {
            CreateTriangleGeometry();
        }
        CheckDiagnostics();
        root_signature = std::move(candidate_root);
        triangle_pipeline = std::move(candidate_pipeline);
        ++pipeline_generation;
        faulted = false;
        return true;
    }

    void CreateTriangleGeometry()
    {
        const auto create_buffer = [&](UINT64 bytes, D3D12_HEAP_TYPE heap_type,
                                       D3D12_RESOURCE_STATES state,
                                       ComPtr<ID3D12Resource>& resource, const wchar_t* name)
        {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = heap_type;
            heap.CreationNodeMask = 1;
            heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = bytes;
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            CheckDevice(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state,
                                                        nullptr, IID_PPV_ARGS(&resource)),
                        "Create triangle buffer");
            Check(resource->SetName(name), "Triangle resource name");
        };
        constexpr UINT vertex_bytes = sizeof(kTriangleVertices);
        constexpr UINT index_bytes = sizeof(kTriangleIndices);
        create_buffer(vertex_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
                      triangle_vertices, L"TinyMe.Triangle.Vertices");
        create_buffer(index_bytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON,
                      triangle_indices, L"TinyMe.Triangle.Indices");
        create_buffer(vertex_bytes + index_bytes, D3D12_HEAP_TYPE_UPLOAD,
                      D3D12_RESOURCE_STATE_GENERIC_READ, triangle_upload,
                      L"TinyMe.Triangle.Upload");
        geometry_info.upload_pending = true;
        void* mapped = nullptr;
        const D3D12_RANGE no_read{0, 0};
        CheckDevice(triangle_upload->Map(0, &no_read, &mapped), "Triangle upload Map");
        std::memcpy(mapped, kTriangleVertices.data(), vertex_bytes);
        std::memcpy(static_cast<unsigned char*>(mapped) + vertex_bytes, kTriangleIndices.data(),
                    index_bytes);
        const D3D12_RANGE written{0, vertex_bytes + index_bytes};
        triangle_upload->Unmap(0, &written);
        CheckDevice(frames[0].allocator->Reset(), "Triangle upload allocator Reset");
        CheckDevice(commands->Reset(frames[0].allocator.Get(), nullptr),
                    "Triangle upload list Reset");
        std::array<D3D12_RESOURCE_BARRIER, 2> barriers{};
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition = {triangle_vertices.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                  D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST};
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition = {triangle_indices.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                                  D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST};
        commands->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        commands->CopyBufferRegion(triangle_vertices.Get(), 0, triangle_upload.Get(), 0,
                                   vertex_bytes);
        commands->CopyBufferRegion(triangle_indices.Get(), 0, triangle_upload.Get(), vertex_bytes,
                                   index_bytes);
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        commands->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        CheckDevice(commands->Close(), "Triangle upload list Close");
        ID3D12CommandList* submitted[]{commands.Get()};
        queue->ExecuteCommandLists(1, submitted);
        geometry_info.upload_fence = Signal();
        WaitForFence(geometry_info.upload_fence);
        triangle_upload.Reset();
        geometry_info.upload_pending = false;
        triangle_vertex_view = {triangle_vertices->GetGPUVirtualAddress(), vertex_bytes,
                                sizeof(TriangleVertex)};
        triangle_index_view = {triangle_indices->GetGPUVirtualAddress(), index_bytes,
                               DXGI_FORMAT_R16_UINT};
        geometry_info.vertex_count = static_cast<std::uint32_t>(kTriangleVertices.size());
        geometry_info.index_count = static_cast<std::uint32_t>(kTriangleIndices.size());
        geometry_info.vertex_stride = sizeof(TriangleVertex);
        geometry_info.resources_ready = true;
    }

    void CreateBuffers()
    {
        auto handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        for (UINT index = 0; index < kBufferCount; ++index)
        {
            CheckDevice(swapchain->GetBuffer(index, IID_PPV_ARGS(&frames[index].buffer)),
                        "GetBuffer");
            const auto name = L"TinyMe.BackBuffer." + std::to_wstring(index);
            Check(frames[index].buffer->SetName(name.c_str()), "Back buffer name");
            device->CreateRenderTargetView(frames[index].buffer.Get(), nullptr, handle);
            handle.ptr += rtv_stride;
        }
    }

    void WaitForFence(UINT64 value)
    {
        const auto start = GetTickCount64();
        bool registered = false;
        for (;;)
        {
            const auto completed = fence->GetCompletedValue();
            if (completed == std::numeric_limits<UINT64>::max())
            {
                CheckDevice(DXGI_ERROR_DEVICE_REMOVED, "Fence completion");
            }
            if (completed >= value)
            {
                return;
            }
            if (!registered)
            {
                CheckDevice(fence->SetEventOnCompletion(value, fence_event),
                            "SetEventOnCompletion");
                registered = true;
            }
            const auto elapsed = GetTickCount64() - start;
            if (elapsed >= kFenceTimeoutMs)
            {
                throw std::runtime_error("GPU fence timed out; device_removed_reason=" +
                                         Hex(device->GetDeviceRemovedReason()));
            }
            const auto result =
                WaitForSingleObject(fence_event, static_cast<DWORD>(kFenceTimeoutMs - elapsed));
            if (result != WAIT_OBJECT_0 && result != WAIT_TIMEOUT)
            {
                throw std::runtime_error("GPU fence wait failed; Win32 error=" +
                                         std::to_string(GetLastError()));
            }
            // Shared events can wake for an older registration. Completion is the authority.
        }
    }

    UINT64 Signal()
    {
        if (next_fence == std::numeric_limits<UINT64>::max())
        {
            throw std::runtime_error("Fence value budget exhausted.");
        }
        const auto value = next_fence++;
        CheckDevice(queue->Signal(fence.Get(), value), "Queue Signal");
        return value;
    }

    void WaitIdle()
    {
        if (queue && fence && fence_event)
        {
            WaitForFence(Signal());
        }
    }

    void Resize(WindowSize new_size)
    {
        if (stopped || faulted)
        {
            throw std::logic_error("Renderer is shut down or faulted; only Shutdown is allowed.");
        }
        if (new_size.width == 0 || new_size.height == 0 || new_size.width > 16384 ||
            new_size.height > 16384)
        {
            throw std::invalid_argument(
                "Renderer size must be in [1, 16384]; skip minimized windows.");
        }
        if (new_size.width == size.width && new_size.height == size.height)
        {
            return;
        }
        faulted = true;
        WaitIdle();
        for (auto& frame : frames)
        {
            frame.buffer.Reset();
            frame.last_fence = 0;
        }
        CheckDevice(swapchain->ResizeBuffers(kBufferCount, new_size.width, new_size.height,
                                             kBackBufferFormat, 0),
                    "ResizeBuffers");
        CreateBuffers();
        CheckDiagnostics();
        size = new_size;
        occluded = false;
        faulted = false;
    }

    bool Render(const std::array<float, 4>& clear_color, const TriangleParameters& parameters,
                const CameraParameters& camera)
    {
        if (stopped || faulted)
        {
            throw std::logic_error("Renderer is shut down or faulted; only Shutdown is allowed.");
        }
        for (const auto component : clear_color)
        {
            if (!std::isfinite(component))
            {
                throw std::invalid_argument("Clear color must be finite.");
            }
        }
        if (!IsValid(parameters))
        {
            throw std::invalid_argument("Invalid triangle parameter layout/values.");
        }
        if (!IsValid(camera))
        {
            throw std::invalid_argument("Invalid camera parameters.");
        }
        CameraTransform transform;
        if (triangle_pipeline)
        {
            transform = BuildCameraTransform(camera, static_cast<float>(size.width) /
                                                         static_cast<float>(size.height));
        }
        faulted = true;
        if (occluded)
        {
            const auto visibility = swapchain->Present(0, DXGI_PRESENT_TEST);
            if (visibility == DXGI_STATUS_OCCLUDED)
            {
                faulted = false;
                return false;
            }
            CheckDevice(visibility, "Present visibility test");
            occluded = false;
        }
        const auto index = swapchain->GetCurrentBackBufferIndex();
        auto& frame = frames[index];
        WaitForFence(frame.last_fence);
        if (timer)
        {
            timer->Poll(fence->GetCompletedValue());
        }
        const auto record_start = std::chrono::steady_clock::now();
        CheckDevice(frame.allocator->Reset(), "Allocator Reset");
        CheckDevice(commands->Reset(frame.allocator.Get(), nullptr), "Command list Reset");
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = frame.buffer.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commands->ResourceBarrier(1, &barrier);
        auto handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * rtv_stride;
        if (timer)
        {
            timer->Begin(index, commands.Get());
        }
        commands->ClearRenderTargetView(handle, clear_color.data(), 0, nullptr);
        if (triangle_pipeline)
        {
            const D3D12_VIEWPORT viewport{
                0.0F, 0.0F, static_cast<float>(size.width), static_cast<float>(size.height),
                0.0F, 1.0F};
            const D3D12_RECT scissor{0, 0, static_cast<LONG>(size.width),
                                     static_cast<LONG>(size.height)};
            commands->RSSetViewports(1, &viewport);
            commands->RSSetScissorRects(1, &scissor);
            commands->OMSetRenderTargets(1, &handle, FALSE, nullptr);
            commands->SetGraphicsRootSignature(root_signature.Get());
            commands->SetGraphicsRoot32BitConstants(0, kTriangleParameterDwords, &parameters, 0);
            commands->SetGraphicsRoot32BitConstants(1, sizeof(CameraTransform) / sizeof(float),
                                                    transform.world_to_clip.data(), 0);
            commands->SetPipelineState(triangle_pipeline.Get());
            commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commands->IASetVertexBuffers(0, 1, &triangle_vertex_view);
            commands->IASetIndexBuffer(&triangle_index_view);
            commands->DrawIndexedInstanced(geometry_info.index_count, 1, 0, 0, 0);
        }
        if (timer)
        {
            timer->End(index, commands.Get());
        }
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        commands->ResourceBarrier(1, &barrier);
        CheckDevice(commands->Close(), "Command list Close");
        const auto cpu_record_ms = std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - record_start)
                                       .count();
        ID3D12CommandList* submitted[]{commands.Get()};
        queue->ExecuteCommandLists(1, submitted);
        const auto present_result = swapchain->Present(1, 0);
        frame.last_fence = Signal();
        if (triangle_pipeline)
        {
            geometry_info.last_use_fence = frame.last_fence;
        }
        CheckDevice(present_result, "Present");
        occluded = present_result == DXGI_STATUS_OCCLUDED;
        if (!occluded)
        {
            ++presentations;
        }
        if (timer)
        {
            timer->Submitted(
                index, frame.last_fence,
                {occluded ? 0 : presentations, pipeline_generation, size, cpu_record_ms, 0.0});
        }
        CheckDiagnostics();
        faulted = false;
        return !occluded;
    }

    void Shutdown()
    {
        if (shutdown_complete)
        {
            return;
        }
        stopped = true;
        std::exception_ptr failure;
        bool device_removed = false;
        try
        {
            WaitIdle();
        }
        catch (...)
        {
            if (!device || SUCCEEDED(device->GetDeviceRemovedReason()))
            {
                throw;
            }
            device_removed = true;
            failure = std::current_exception();
        }
        if (timer)
        {
            try
            {
                if (!device_removed)
                {
                    timer->Poll(fence->GetCompletedValue());
                }
            }
            catch (...)
            {
                if (!failure)
                {
                    failure = std::current_exception();
                }
            }
            timer->ReleaseResources();
        }
        commands.Reset();
        triangle_pipeline.Reset();
        root_signature.Reset();
        triangle_upload.Reset();
        triangle_indices.Reset();
        triangle_vertices.Reset();
        geometry_info.resources_ready = false;
        geometry_info.upload_pending = false;
        for (auto& frame : frames)
        {
            frame.buffer.Reset();
            frame.allocator.Reset();
        }
        swapchain.Reset();
        rtv_heap.Reset();
        queue.Reset();
        fence.Reset();
        if (fence_event)
        {
            CloseHandle(fence_event);
            fence_event = nullptr;
        }
        try
        {
            if (options.debug_layer && device)
            {
                ComPtr<ID3D12DebugDevice> debug_device;
                Check(device.As(&debug_device), "ID3D12DebugDevice");
                Check(debug_device->ReportLiveDeviceObjects(static_cast<D3D12_RLDO_FLAGS>(
                          D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL)),
                      "ReportLiveDeviceObjects");
            }
            CheckDiagnostics();
        }
        catch (...)
        {
            if (!failure)
            {
                failure = std::current_exception();
            }
        }
        info_queue.Reset();
        device.Reset();
        factory.Reset();
        try
        {
            CheckDiagnostics();
        }
        catch (...)
        {
            if (!failure)
            {
                failure = std::current_exception();
            }
        }
        dxgi_info_queue.Reset();
        shutdown_complete = true;
        std::cout << "renderer_shutdown gpu_idle=" << !device_removed
                  << " device_removed=" << device_removed
                  << " validation_warnings=" << validation.warnings
                  << " validation_errors=" << validation.errors
                  << " live_child_check=" << options.debug_layer << '\n'
                  << std::flush;
        if (failure)
        {
            std::rethrow_exception(failure);
        }
    }
};

ClearRenderer::ClearRenderer(HWND hwnd, WindowSize size, RendererOptions options)
    : impl_(std::make_unique<Impl>())
{
    impl_->Initialize(hwnd, size, options);
}

ClearRenderer::~ClearRenderer() = default;

void ClearRenderer::Resize(WindowSize size)
{
    impl_->Resize(size);
}

bool ClearRenderer::Render(const std::array<float, 4>& clear_color,
                           const TriangleParameters& parameters, const CameraParameters& camera)
{
    return impl_->Render(clear_color, parameters, camera);
}

TriangleGeometryInfo ClearRenderer::GetTriangleGeometryInfo() const noexcept
{
    return impl_->geometry_info;
}

std::vector<FrameTiming> ClearRenderer::TakeCompletedTimings()
{
    if (!impl_->timer)
    {
        return {};
    }
    if (!impl_->shutdown_complete)
    {
        try
        {
            impl_->timer->Poll(impl_->fence->GetCompletedValue());
        }
        catch (...)
        {
            impl_->faulted = true;
            throw;
        }
    }
    return impl_->timer->TakeCompleted();
}

std::uint64_t ClearRenderer::GetDroppedTimingSamples() const noexcept
{
    return impl_->timer ? impl_->timer->GetDroppedSamples() : 0;
}

const RendererInfo& ClearRenderer::GetInfo() const noexcept
{
    return impl_->renderer_info;
}

void ClearRenderer::Shutdown()
{
    impl_->Shutdown();
}

bool ClearRenderer::ReloadTriangle(const std::filesystem::path& source, std::string& diagnostics)
{
    return impl_->ReloadTriangle(source, diagnostics);
}

std::uint64_t ClearRenderer::GetPipelineGeneration() const noexcept
{
    return impl_->pipeline_generation;
}

WindowSize ClearRenderer::GetSize() const noexcept
{
    return impl_->size;
}

ValidationStats ClearRenderer::GetValidationStats() const noexcept
{
    return impl_->validation;
}

bool ClearRenderer::IsDebugLayerEnabled() const noexcept
{
    return impl_->options.debug_layer;
}
} // namespace tinyme
