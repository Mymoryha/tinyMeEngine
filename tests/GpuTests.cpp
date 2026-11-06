#include "ShaderFixture.h"

#include <tinyme/platform/windows/Window.h>
#include <tinyme/render/d3d12/ClearRenderer.h>

#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

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

void RenderFrames(tinyme::Window& window, tinyme::ClearRenderer& renderer, unsigned count)
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
        if (renderer.Render(kColor))
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
    const std::string unbound =
        "float4 VSMain(uint id:SV_VertexID):SV_Position { return float4(0,0,0,1); }\n"
        "cbuffer Parameters : register(b0) { float4 color; };\n"
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
        std::cout << "gpu_total checks=" << checks << " failures=0\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: GPU lifecycle test: " << error.what() << '\n';
        return 1;
    }
}
