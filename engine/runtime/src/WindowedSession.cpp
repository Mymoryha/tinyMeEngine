#include <tinyme/runtime/WindowedSession.h>

#include <tinyme/platform/windows/Window.h>
#include <tinyme/render/d3d12/ClearRenderer.h>
#include <tinyme/runtime/Session.h>

#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace tinyme
{
WindowedRunResult RunWindowedSession(std::wstring_view title, WindowedRunOptions options)
{
    Window window(title);
    RendererOptions renderer_options{options.debug_layer, options.gpu_validation};
#ifdef _DEBUG
    renderer_options.debug_layer = true;
#endif
    ClearRenderer renderer(window.GetHandle(), window.GetClientSize(), renderer_options);
    auto shader = options.shader_path;
    if (options.triangle && shader.empty())
    {
        std::vector<wchar_t> executable(32768);
        const auto length =
            GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
        if (length == 0 || length >= executable.size())
        {
            throw std::runtime_error("Cannot locate default shader next to executable.");
        }
        shader = std::filesystem::path(executable.data()).parent_path() / L"shaders/Triangle.hlsl";
    }
    auto reload = [&]()
    {
        std::string diagnostics;
        const bool success = renderer.ReloadTriangle(shader, diagnostics);
        const auto utf8_path = shader.generic_u8string();
        std::cout << "shader_reload success=" << success
                  << " generation=" << renderer.GetPipelineGeneration() << " source=\""
                  << reinterpret_cast<const char*>(utf8_path.c_str()) << "\"\n";
        if (!diagnostics.empty())
        {
            std::cout << "shader_diagnostics: " << diagnostics << '\n';
        }
        std::cout << std::flush;
        return success;
    };
    if (options.triangle && !reload())
    {
        throw std::runtime_error("Initial triangle shader failed; no valid pipeline exists.");
    }
    std::cout << "experiment=" << (options.triangle ? "triangle" : "clear") << '\n';
    Session session;
    WindowedRunResult result;
    constexpr std::array<float, 4> kClearColor{0.025F, 0.055F, 0.09F, 1.0F};
    std::cout << "window client=" << window.GetClientSize().width << 'x'
              << window.GetClientSize().height << " dpi=" << window.GetDpi()
              << " controls=close-or-Escape clear_color=0.025,0.055,0.09,1\n"
              << std::flush;
    while (options.frame_limit == 0 || result.presented_frames < options.frame_limit)
    {
        window.PumpMessages();
        if (window.IsCloseRequested())
        {
            result.closed_by_user = true;
            break;
        }
        if (window.ConsumeShaderReloadRequest() && options.triangle)
        {
            static_cast<void>(reload());
        }
        const auto size = window.GetClientSize();
        if (window.IsMinimized() || size.width == 0 || size.height == 0)
        {
            window.WaitForMessages(100);
            continue;
        }
        renderer.Resize(size);
        if (!renderer.Render(kClearColor))
        {
            window.WaitForMessages(100);
            continue;
        }
        if (!session.Tick(1.0 / 60.0))
        {
            throw std::runtime_error("Windowed session rejected the fixed logical timestep.");
        }
        ++result.presented_frames;
    }
    renderer.Shutdown();
    result.validation_warnings = renderer.GetValidationStats().warnings;
    return result;
}
} // namespace tinyme
