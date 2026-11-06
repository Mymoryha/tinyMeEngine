#include "ShaderFixture.h"

#include <tinyme/platform/windows/Window.h>
#include <tinyme/render/d3d12/ClearRenderer.h>
#include <tinyme/runtime/WindowedSession.h>

#include <dwmapi.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
int checks = 0;

void Check(bool condition, const char* description)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(description);
    }
}

void RenderFrames(tinyme::Window& window, tinyme::ClearRenderer& renderer, unsigned count,
                  const tinyme::TriangleParameters& parameters = {},
                  const tinyme::CameraParameters& camera = {})
{
    constexpr std::array<float, 4> kColor{0.025F, 0.055F, 0.09F, 1.0F};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    unsigned presented = 0;
    while (presented < count)
    {
        window.PumpMessages();
        Check(!window.IsCloseRequested(), "Unexpected window close during GPU test");
        Check(std::chrono::steady_clock::now() < deadline, "Presentation deadline exceeded");
        const auto size = window.GetClientSize();
        if (window.IsMinimized() || size.width == 0 || size.height == 0)
        {
            window.WaitForMessages(20);
            continue;
        }
        renderer.Resize(size);
        if (renderer.Render(kColor, parameters, camera))
        {
            ++presented;
        }
        else
        {
            window.WaitForMessages(20);
        }
    }
}

void TestLifecycle(bool gpu_validation)
{
    const auto original_dpi = GetThreadDpiAwarenessContext();
    for (unsigned cycle = 0; cycle < 3; ++cycle)
    {
        HWND handle = nullptr;
        {
            tinyme::Window window(L"TinyMeEngine | M1 GPU lifecycle validation", {640, 360});
            handle = window.GetHandle();
            window.PumpMessages();
            Check(IsWindow(handle), "HWND creation");
            Check(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(),
                                               DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2),
                  "Window thread must be PMv2 DPI aware");
            Check(window.GetDpi() > 0, "Actual window DPI");
            Check(window.GetClientSize().width == 640 && window.GetClientSize().height == 360,
                  "Initial client dimensions");
            tinyme::ClearRenderer renderer(handle, window.GetClientSize(), {true, gpu_validation});
            Check(renderer.IsDebugLayerEnabled(), "Test must not silently disable Debug Layer");
            RenderFrames(window, renderer, 12);

            for (unsigned resize = 0; resize < 8; ++resize)
            {
                const tinyme::WindowSize target{640 + (resize + 1) * 17, 360 + (resize + 1) * 11};
                window.SetClientSize(target);
                window.PumpMessages();
                Check(window.GetClientSize().width == target.width &&
                          window.GetClientSize().height == target.height,
                      "Resize client dimensions");
                RenderFrames(window, renderer, 4);
                Check(renderer.GetSize().width == target.width &&
                          renderer.GetSize().height == target.height,
                      "Swapchain follows client dimensions");
                window.Minimize();
                window.PumpMessages();
                Check(window.IsMinimized(), "Minimized state");
                Check(!window.IsCloseRequested(), "Minimize is not close");
                window.WaitForMessages(5);
                window.Restore();
                window.PumpMessages();
                Check(!window.IsMinimized(), "Restore state");
                RenderFrames(window, renderer, 4);
            }

            const auto size_before = renderer.GetSize();
            bool rejected = false;
            try
            {
                renderer.Resize({0, 0});
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            Check(rejected && renderer.GetSize().width == size_before.width,
                  "Zero resize rejects without mutation");
            rejected = false;
            try
            {
                const auto nan = std::numeric_limits<float>::quiet_NaN();
                static_cast<void>(renderer.Render({nan, 0.0F, 0.0F, 1.0F}));
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            Check(rejected, "Nonfinite color rejected");
            RenderFrames(window, renderer, 2);
            window.RequestClose();
            window.PumpMessages();
            Check(window.IsCloseRequested(), "WM_CLOSE records intent");
            Check(IsWindow(handle), "HWND must survive pending GPU work");
            renderer.Shutdown();
            renderer.Shutdown();
            const auto validation = renderer.GetValidationStats();
            Check(validation.errors == 0, "Debug Layer error/corruption");
            Check(validation.warnings == 0, "Unexpected warning/live GPU child");
            rejected = false;
            try
            {
                static_cast<void>(renderer.Render({0.0F, 0.0F, 0.0F, 1.0F}));
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
            Check(rejected, "Rendering after shutdown rejected");
            Check(IsWindow(handle), "Renderer shutdown must not own/destroy HWND");
        }
        Check(!IsWindow(handle), "Window RAII destroys HWND after renderer");
        Check(AreDpiAwarenessContextsEqual(GetThreadDpiAwarenessContext(), original_dpi),
              "Thread DPI context restored");
    }
}
void TestTriangle(bool gpu_validation)
{
    tinyme::tests::ShaderFixture fixture;
    const auto default_shader = tinyme::tests::ExecutableDirectory() / L"shaders/Triangle.hlsl";
    tinyme::Window window(L"TinyMeEngine | M2 Triangle reload validation", {640, 360});
    tinyme::ClearRenderer renderer(window.GetHandle(), window.GetClientSize(),
                                   {true, gpu_validation});
    std::string diagnostics;
    Check(renderer.ReloadTriangle(default_shader, diagnostics), "Initial triangle compile/PSO");
    Check(renderer.GetPipelineGeneration() == 1, "Initial pipeline generation");
    RenderFrames(window, renderer, 8);
    for (unsigned cycle = 0; cycle < 4; ++cycle)
    {
        fixture.Write(L"broken.hlsl", "syntax error!");
        const auto generation = renderer.GetPipelineGeneration();
        Check(!renderer.ReloadTriangle(fixture.Path(L"broken.hlsl"), diagnostics) &&
                  !diagnostics.empty(),
              "Broken source diagnosed without replacing PSO");
        Check(renderer.GetPipelineGeneration() == generation,
              "Compile failure preserves generation");
        RenderFrames(window, renderer, 3);
        Check(!renderer.ReloadTriangle(fixture.Path(L"missing.hlsl"), diagnostics),
              "Missing source preserves PSO");
        Check(renderer.GetPipelineGeneration() == generation,
              "Missing source preserves generation");
        Check(renderer.ReloadTriangle(default_shader, diagnostics),
              "Recover and replace after failure");
        Check(renderer.GetPipelineGeneration() == generation + 1,
              "Exactly one successful replacement");
        window.SetClientSize({700 + cycle * 13, 400 + cycle * 9});
        RenderFrames(window, renderer, 4);
        window.Minimize();
        window.PumpMessages();
        Check(window.IsMinimized(), "Triangle minimizes");
        window.Restore();
        window.PumpMessages();
        RenderFrames(window, renderer, 4);
    }
    fixture.Write(L"bad-input.hlsl",
                  "float4 VSMain(float3 p:NORMAL0):SV_Position{return float4(p,1);}\n"
                  "float4 PSMain():SV_Target0{return float4(1,1,1,1);}\n");
    const auto before_input_rejection = renderer.GetPipelineGeneration();
    Check(!renderer.ReloadTriangle(fixture.Path(L"bad-input.hlsl"), diagnostics),
          "Incompatible vertex semantic rejects only candidate PSO");
    Check(renderer.GetPipelineGeneration() == before_input_rejection,
          "Input-layout rejection preserves previous pipeline");
    std::cout << "expected_input_rejection: " << diagnostics << '\n';
    RenderFrames(window, renderer, 2);
    const std::string unbound =
        "float4 VSMain(float3 position:POSITION0,float3 color:COLOR0):SV_Position "
        "{ return float4(position,1); }\n"
        "cbuffer Parameters : register(b2) { float4 color; };\n"
        "float4 PSMain():SV_Target0 { return color; }\n";
    fixture.Write(L"unbound.hlsl", unbound);
    const auto previous = renderer.GetPipelineGeneration();
    Check(!renderer.ReloadTriangle(fixture.Path(L"unbound.hlsl"), diagnostics),
          "Unbound shader resource rejects candidate PSO");
    Check(renderer.GetPipelineGeneration() == previous, "PSO rejection keeps previous pipeline");
    std::cout << "expected_shader_rejection: " << diagnostics << '\n';
    RenderFrames(window, renderer, 4);
    Check(PostMessageW(window.GetHandle(), WM_KEYDOWN, VK_F5, 0), "Post F5");
    window.PumpMessages();
    Check(window.ConsumeShaderReloadRequest(), "F5 reaches runtime boundary");
    Check(!window.ConsumeShaderReloadRequest(), "F5 request consumed once");
    Check(renderer.ReloadTriangle(default_shader, diagnostics), "Reload after rejected PSO");
    renderer.Shutdown();
    const auto validation = renderer.GetValidationStats();
    Check(validation.errors == 0 && validation.warnings == 0,
          "Triangle live objects/runtime validation clean");
    std::cout << "triangle_reload generation=" << renderer.GetPipelineGeneration()
              << " rejected_candidate=1 gpu_validation=" << gpu_validation << " failures=0\n";
}

COLORREF CapturePixel(tinyme::Window& window, int x, int y)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Check(SUCCEEDED(DwmFlush()), "Desktop composition flush for test capture");
    POINT point{x, y};
    Check(ClientToScreen(window.GetHandle(), &point), "Client-to-desktop capture coordinates");
    const auto dc = GetDC(nullptr);
    Check(dc != nullptr, "Desktop capture DC");
    const auto pixel = GetPixel(dc, point.x, point.y);
    ReleaseDC(nullptr, dc);
    Check(pixel != CLR_INVALID, "Desktop capture pixel");
    return pixel;
}

void TestParameters(bool gpu_validation)
{
    tinyme::Window window(L"TinyMeEngine | M2 parameter pixel validation", {640, 360});
    tinyme::ClearRenderer renderer(window.GetHandle(), window.GetClientSize(),
                                   {true, gpu_validation});
    std::string diagnostics;
    const auto shader = tinyme::tests::ExecutableDirectory() / L"shaders/Triangle.hlsl";
    Check(renderer.ReloadTriangle(shader, diagnostics), "Parameter shader compile");
    RenderFrames(window, renderer, 3);
    const auto original = CapturePixel(window, 320, 180);
    const auto edge = CapturePixel(window, 390, 180);
    Check(GetRValue(original) > 100 && GetGValue(original) > 60 && GetBValue(original) > 60,
          "Default shader actually produces interpolated RGB");
    Check(GetRValue(edge) > 40, "Reference point is inside full-size triangle");
    tinyme::TriangleParameters parameters;
    for (unsigned i = 0; i < 12; ++i)
    {
        parameters.tint =
            i % 2 ? std::array<float, 4>{1, 0, 0, 1} : std::array<float, 4>{0, 1, 0, 1};
        RenderFrames(window, renderer, 1, parameters);
    }
    RenderFrames(window, renderer, 2, parameters);
    const auto tinted = CapturePixel(window, 320, 180);
    std::cout << "tint_capture rgb=" << static_cast<int>(GetRValue(tinted)) << '/'
              << static_cast<int>(GetGValue(tinted)) << '/' << static_cast<int>(GetBValue(tinted))
              << '\n'
              << std::flush;
    Check(GetRValue(tinted) > 100 && GetGValue(tinted) <= 2 && GetBValue(tinted) <= 2,
          "Per-frame root constants apply red tint, not stale in-flight data");
    parameters.scale = 0.5F;
    RenderFrames(window, renderer, 3, parameters);
    const auto scaled = CapturePixel(window, 390, 180);
    std::cout << "scale_capture rgb=" << static_cast<int>(GetRValue(scaled)) << '/'
              << static_cast<int>(GetGValue(scaled)) << '/' << static_cast<int>(GetBValue(scaled))
              << '\n'
              << std::flush;
    Check(std::abs(static_cast<int>(GetRValue(scaled)) - 6) <= 2 &&
              std::abs(static_cast<int>(GetGValue(scaled)) - 14) <= 2 &&
              std::abs(static_cast<int>(GetBValue(scaled)) - 23) <= 2,
          "Scale shrinks triangle and exposes the clear color");
    Check(renderer.ReloadTriangle(shader, diagnostics), "Reload with non-default parameters");
    RenderFrames(window, renderer, 3, parameters);
    const auto reloaded = CapturePixel(window, 320, 180);
    Check(GetRValue(reloaded) > 100 && GetGValue(reloaded) <= 2 && GetBValue(reloaded) <= 2,
          "Parameters survive pipeline replacement");
    parameters.reserved[0] = 1.0F;
    bool rejected = false;
    try
    {
        static_cast<void>(renderer.Render({0, 0, 0, 1}, parameters));
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Check(rejected, "Invalid parameters rejected before command recording");
    RenderFrames(window, renderer, 1);
    Check(renderer.TakeCompletedTimings().empty() && renderer.GetInfo().timestamp_frequency == 0,
          "Profiling disabled does not create timestamp resources");
    renderer.Shutdown();
    Check(renderer.GetValidationStats().warnings == 0 && renderer.GetValidationStats().errors == 0,
          "Parameter resource/live-child validation clean");
    std::cout << "parameter_pixels original=" << static_cast<int>(GetRValue(original)) << '/'
              << static_cast<int>(GetGValue(original)) << '/'
              << static_cast<int>(GetBValue(original))
              << " tinted=" << static_cast<int>(GetRValue(tinted)) << '/'
              << static_cast<int>(GetGValue(tinted)) << '/' << static_cast<int>(GetBValue(tinted))
              << " scaled_clear=" << static_cast<int>(GetRValue(scaled)) << '/'
              << static_cast<int>(GetGValue(scaled)) << '/' << static_cast<int>(GetBValue(scaled))
              << " gpu_validation=" << gpu_validation << '\n';
}

bool IsClearPixel(COLORREF pixel)
{
    return std::abs(static_cast<int>(GetRValue(pixel)) - 6) <= 2 &&
           std::abs(static_cast<int>(GetGValue(pixel)) - 14) <= 2 &&
           std::abs(static_cast<int>(GetBValue(pixel)) - 23) <= 2;
}

void TestCamera(bool gpu_validation)
{
    tinyme::Window window(L"TinyMeEngine | M2 indexed camera validation", {640, 360});
    tinyme::ClearRenderer renderer(window.GetHandle(), window.GetClientSize(),
                                   {true, gpu_validation, true});
    const auto empty = renderer.GetTriangleGeometryInfo();
    Check(empty.vertex_count == 0 && empty.upload_fence == 0 && !empty.resources_ready &&
              !empty.upload_pending,
          "Clear renderer has no mesh resources or upload");
    const auto shader = tinyme::tests::ExecutableDirectory() / L"shaders/Triangle.hlsl";
    std::string diagnostics;
    Check(renderer.ReloadTriangle(shader, diagnostics), "Indexed camera shader and PSO accepted");
    const auto initialized = renderer.GetTriangleGeometryInfo();
    Check(initialized.vertex_count == 4 && initialized.index_count == 3 &&
              initialized.vertex_stride == 24 && initialized.upload_fence > 0 &&
              initialized.resources_ready && !initialized.upload_pending &&
              initialized.last_use_fence == 0,
          "DEFAULT VB/IB upload completes before first indexed draw");
    RenderFrames(window, renderer, 3);
    const auto original = CapturePixel(window, 320, 180);
    Check(std::abs(static_cast<int>(GetRValue(original)) - 146) <= 2 &&
              std::abs(static_cast<int>(GetGValue(original)) - 93) <= 2 &&
              std::abs(static_cast<int>(GetBValue(original)) - 92) <= 2,
          "Indexed geometry and default perspective preserve original 16:9 RGB reference");
    tinyme::CameraParameters shifted;
    shifted.eye = {1.5F, 0.3F, -2.0F};
    shifted.target = {1.5F, 0.3F, 0.0F};
    RenderFrames(window, renderer, 3, {}, shifted);
    Check(IsClearPixel(CapturePixel(window, 320, 180)),
          "Non-symmetric camera translation moves original center outside triangle");
    const auto moved = CapturePixel(window, 185, 207);
    Check(GetRValue(moved) > 100 && GetGValue(moved) > 60 && GetBValue(moved) > 60,
          "Camera moves geometry to known projected screen point, not transposed matrix");
    for (unsigned frame = 0; frame < 24; ++frame)
    {
        RenderFrames(window, renderer, 1, {},
                     frame % 2 == 0 ? tinyme::CameraParameters{} : shifted);
    }
    Check(IsClearPixel(CapturePixel(window, 320, 180)),
          "Alternating root matrices across frame slots leave final camera, not stale data");
    Check(renderer.ReloadTriangle(shader, diagnostics), "Reload while camera is non-default");
    RenderFrames(window, renderer, 2, {}, shifted);
    Check(IsClearPixel(CapturePixel(window, 320, 180)), "Camera survives PSO replacement");
    const auto reloaded = renderer.GetTriangleGeometryInfo();
    Check(reloaded.upload_fence == initialized.upload_fence && reloaded.resources_ready &&
              !reloaded.upload_pending && reloaded.last_use_fence > initialized.upload_fence,
          "F5 reuses static resources and records actual last-use fence");
    tinyme::CameraParameters farther;
    farther.eye[2] = -6.0F;
    RenderFrames(window, renderer, 3, {}, farther);
    Check(IsClearPixel(CapturePixel(window, 390, 180)),
          "Perspective camera distance shrinks geometry");
    tinyme::CameraParameters narrow;
    narrow.vertical_fov_degrees = 60.0F;
    RenderFrames(window, renderer, 3, {}, narrow);
    Check(!IsClearPixel(CapturePixel(window, 470, 180)), "Narrower FOV expands projected geometry");
    tinyme::CameraParameters angled;
    angled.eye = {2.0F, 0.0F, -2.0F};
    RenderFrames(window, renderer, 2, {}, angled);
    const auto angled_pixel = CapturePixel(window, 320, 139);
    Check(GetRValue(angled_pixel) > 190 && GetGValue(angled_pixel) < 75 &&
              GetBValue(angled_pixel) < 75,
          "Non-axis camera orientation produces perspective-correct projected color");
    tinyme::CameraParameters clipped;
    clipped.far_plane = 1.0F;
    RenderFrames(window, renderer, 1, {}, clipped);
    Check(IsClearPixel(CapturePixel(window, 320, 180)), "D3D far-plane clipping uses camera depth");
    clipped.far_plane = 100.0F;
    clipped.near_plane = 3.0F;
    RenderFrames(window, renderer, 1, {}, clipped);
    Check(IsClearPixel(CapturePixel(window, 320, 180)),
          "D3D near-plane clipping uses camera depth");
    window.SetClientSize({800, 360});
    RenderFrames(window, renderer, 3);
    Check(!IsClearPixel(CapturePixel(window, 480, 180)) &&
              IsClearPixel(CapturePixel(window, 530, 180)),
          "Resize recomputes aspect: geometry keeps pixel width instead of stretching");
    const auto before_invalid = renderer.GetTriangleGeometryInfo().last_use_fence;
    tinyme::CameraParameters invalid;
    invalid.eye = invalid.target;
    bool rejected = false;
    try
    {
        static_cast<void>(renderer.Render({0, 0, 0, 1}, {}, invalid));
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    Check(rejected && renderer.GetTriangleGeometryInfo().last_use_fence == before_invalid,
          "Degenerate camera rejected before GPU submission, renderer remains usable");
    RenderFrames(window, renderer, 1);
    renderer.Shutdown();
    const auto released = renderer.GetTriangleGeometryInfo();
    Check(!released.resources_ready && !released.upload_pending &&
              released.upload_fence == initialized.upload_fence,
          "Shutdown releases VB/IB/staging at GPU-safe boundary and retains diagnostics");
    const auto timings = renderer.TakeCompletedTimings();
    Check(timings.size() == 46 && renderer.GetDroppedTimingSamples() == 0,
          "Initialization upload and invalid camera do not invent profile presentations");
    for (std::size_t index = 0; index < timings.size(); ++index)
    {
        Check(timings[index].presentation == index + 1,
              "Camera frames keep unique presentation sequence through reload and resize");
    }
    Check(renderer.GetValidationStats().warnings == 0 && renderer.GetValidationStats().errors == 0,
          "Indexed buffers and camera pass Debug Layer/GBV and live-child validation");
    std::cout << "indexed_camera vertices=4 indices=3 stride=24 default_rgb="
              << static_cast<int>(GetRValue(original)) << '/'
              << static_cast<int>(GetGValue(original)) << '/'
              << static_cast<int>(GetBValue(original))
              << " upload_fence=" << initialized.upload_fence << " samples=" << timings.size()
              << " gpu_validation=" << gpu_validation << '\n';
}

void TestTiming(bool gpu_validation)
{
    tinyme::Window window(L"TinyMeEngine | M2 timestamp validation", {640, 360});
    tinyme::ClearRenderer renderer(window.GetHandle(), window.GetClientSize(),
                                   {true, gpu_validation, true});
    Check(renderer.GetInfo().timestamp_frequency > 0 && !renderer.GetInfo().adapter.empty() &&
              !renderer.GetInfo().driver.empty(),
          "Timestamp frequency and actual adapter/driver available");
    Check(renderer.TakeCompletedTimings().empty(), "No unsubmitted samples");
    std::vector<tinyme::FrameTiming> samples;
    const auto collect = [&]()
    {
        auto ready = renderer.TakeCompletedTimings();
        samples.insert(samples.end(), ready.begin(), ready.end());
    };
    for (unsigned i = 0; i < 12; ++i)
    {
        RenderFrames(window, renderer, 1);
        collect();
    }
    const auto shader = tinyme::tests::ExecutableDirectory() / L"shaders/Triangle.hlsl";
    std::string diagnostics;
    Check(renderer.ReloadTriangle(shader, diagnostics), "Enable triangle during profiling");
    RenderFrames(window, renderer, 3);
    collect();
    window.SetClientSize({700, 400});
    RenderFrames(window, renderer, 3);
    collect();
    renderer.Shutdown();
    collect();
    Check(samples.size() == 18 && renderer.GetDroppedTimingSamples() == 0,
          "Fence-safe polling and shutdown preserve all samples including tail");
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        const auto& sample = samples[i];
        Check(sample.presentation == i + 1, "Presentation samples ordered exactly once");
        Check(std::isfinite(sample.cpu_record_ms) && sample.cpu_record_ms >= 0 &&
                  std::isfinite(sample.gpu_pass_ms) && sample.gpu_pass_ms >= 0,
              "CPU/GPU times are finite nonnegative");
        Check(sample.pipeline_generation == (i < 12 ? 0U : 1U),
              "Per-submission generation retained");
        Check(sample.size.width == (i < 15 ? 640U : 700U) &&
                  sample.size.height == (i < 15 ? 360U : 400U),
              "Per-submission dimensions retained across resize");
    }
    Check(renderer.TakeCompletedTimings().empty(), "Completed timings consumed once");
    Check(renderer.GetValidationStats().warnings == 0 && renderer.GetValidationStats().errors == 0,
          "Timestamp resources released before live-child validation");

    tinyme::ClearRenderer bounded(window.GetHandle(), window.GetClientSize(),
                                  {true, gpu_validation, true});
    RenderFrames(window, bounded, 260);
    bounded.Shutdown();
    Check(bounded.TakeCompletedTimings().size() == 256 && bounded.GetDroppedTimingSamples() == 4,
          "Bounded unconsumed queue reports exact dropped samples");
    Check(bounded.GetValidationStats().warnings == 0 && bounded.GetValidationStats().errors == 0,
          "Bounded timer validation clean");
    std::cout << "timing samples=18 bounded_dropped=4 gpu_validation=" << gpu_validation << '\n';
}

void TestIncompleteProfiles(bool gpu_validation,
                            tinyme::PostProcess postprocess = tinyme::PostProcess::None)
{
    for (unsigned action = 0; action < 4; ++action)
    {
        const std::wstring title = L"TinyMeEngine | profile interruption " +
                                   std::to_wstring(GetCurrentProcessId()) + L"-" +
                                   std::to_wstring(action);
        std::atomic<bool> injected = false;
        std::jthread driver(
            [&](std::stop_token stop)
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                HWND handle = nullptr;
                while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline)
                {
                    handle = FindWindowW(nullptr, title.c_str());
                    DWORD process = 0;
                    if (handle && GetWindowThreadProcessId(handle, &process) &&
                        process == GetCurrentProcessId())
                    {
                        break;
                    }
                    handle = nullptr;
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
                if (!handle || stop.stop_requested())
                {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                if (stop.stop_requested())
                {
                    return;
                }
                bool success = true;
                if (action == 1)
                {
                    success = PostMessageW(handle, WM_KEYDOWN, VK_F5, 0) != FALSE;
                }
                else if (action == 2)
                {
                    success = ShowWindowAsync(handle, SW_MINIMIZE) != FALSE;
                    std::this_thread::sleep_for(std::chrono::milliseconds(150));
                    success = ShowWindowAsync(handle, SW_RESTORE) != FALSE && success;
                }
                else if (action == 3)
                {
                    RECT rect{};
                    success = GetWindowRect(handle, &rect) != FALSE &&
                              SetWindowPos(handle, nullptr, 0, 0, rect.right - rect.left + 100,
                                           rect.bottom - rect.top + 60,
                                           SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                injected = PostMessageW(handle, WM_CLOSE, 0, 0) != FALSE && success;
            });
        tinyme::WindowedRunOptions options;
        options.frame_limit = 240;
        options.profile = {true, 0, 240};
        options.debug_layer = true;
        options.gpu_validation = gpu_validation;
        options.postprocess = postprocess;
        const auto result = tinyme::RunWindowedSession(title, options);
        driver.request_stop();
        driver.join();
        Check(injected, "Interruption injected into this test process's window");
        Check(result.closed_by_user && !result.profile_complete && result.validation_warnings == 0,
              "Interrupted profile is incomplete with safe GPU shutdown");
        Check(result.profile_json.find("\"closed_by_user\":true") != std::string::npos,
              "User close reported explicitly");
        Check(result.profile_json.find(action == 0
                                           ? "\"environment_changed\":false"
                                           : "\"environment_changed\":true") != std::string::npos,
              "Reload/minimize/resize are distinguished from plain close");
    }
    std::cout << "incomplete_profiles cases=4 gpu_validation=" << gpu_validation << '\n';
}
} // namespace

int main(int argc, char* argv[])
{
    try
    {
        const bool gpu_validation = argc == 2 && std::string_view(argv[1]) == "--gpu-validation";
        if (argc > 2 || (argc == 2 && !gpu_validation))
        {
            throw std::invalid_argument("Usage: tinyme_gpu_tests [--gpu-validation]");
        }
        TestLifecycle(gpu_validation);
        std::cout << "gpu_lifecycle checks=" << checks << " cycles=3 resizes=24 minimize_restore=24"
                  << " gpu_validation=" << gpu_validation << " failures=0\n";
        TestTriangle(gpu_validation);
        TestParameters(gpu_validation);
        TestCamera(gpu_validation);
        TestTiming(gpu_validation);
        TestIncompleteProfiles(gpu_validation);
        TestIncompleteProfiles(gpu_validation, tinyme::PostProcess::Copy);
        std::cout << "gpu_total checks=" << checks << " failures=0\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: GPU lifecycle test: " << error.what() << '\n';
        return 1;
    }
}
