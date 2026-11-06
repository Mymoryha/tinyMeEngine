#include <tinyme/runtime/WindowedSession.h>

#include "ProfileReport.h"

#include <tinyme/platform/windows/Window.h>
#include <tinyme/render/d3d12/ClearRenderer.h>
#include <tinyme/runtime/Session.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace tinyme
{
WindowedRunResult RunWindowedSession(std::wstring_view title, WindowedRunOptions options)
{
    if (!IsValid(options.parameters) || !IsValid(options.profile) || !IsValid(options.camera))
    {
        throw std::invalid_argument("Invalid session parameters/camera/profile budget.");
    }
    if (options.profile.enabled && options.frame_limit != 0 &&
        options.frame_limit < options.profile.warmup_frames + options.profile.sample_frames)
    {
        throw std::invalid_argument("Frame budget cannot cover profile samples.");
    }
    if (!options.report_path.empty() &&
        (!options.profile.enabled || std::filesystem::exists(options.report_path)))
    {
        throw std::invalid_argument("Report requires profiling and a new output path.");
    }
    Window window(title);
    RendererOptions renderer_options{options.debug_layer, options.gpu_validation,
                                     options.profile.enabled};
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
    const auto initial_size = window.GetClientSize();
    const auto initial_surface_revision = window.GetSurfaceRevision();
    const auto initial_generation = renderer.GetPipelineGeneration();
    bool environment_changed = false;
    std::vector<FrameTiming> samples;
    if (options.profile.enabled)
    {
        samples.reserve(options.profile.sample_frames);
    }
    auto collect = [&]()
    {
        if (!options.profile.enabled)
        {
            return;
        }
        for (const auto& timing : renderer.TakeCompletedTimings())
        {
            if (timing.presentation <= options.profile.warmup_frames ||
                timing.presentation > options.profile.warmup_frames + options.profile.sample_frames)
            {
                continue;
            }
            if (timing.pipeline_generation != initial_generation ||
                timing.size.width != initial_size.width ||
                timing.size.height != initial_size.height)
            {
                environment_changed = true;
                continue;
            }
            samples.push_back(timing);
        }
    };
    std::cout << "parameters scale=" << options.parameters.scale
              << " tint=" << options.parameters.tint[0] << ',' << options.parameters.tint[1] << ','
              << options.parameters.tint[2] << ',' << options.parameters.tint[3]
              << " bytes=" << sizeof(TriangleParameters) << '\n';
    if (options.triangle)
    {
        const auto geometry = renderer.GetTriangleGeometryInfo();
        std::cout << "indexed_geometry vertices=" << geometry.vertex_count
                  << " indices=" << geometry.index_count << " stride=" << geometry.vertex_stride
                  << " upload_fence=" << geometry.upload_fence << " matrix_bytes=64\n"
                  << "camera eye=" << options.camera.eye[0] << ',' << options.camera.eye[1] << ','
                  << options.camera.eye[2] << " target=" << options.camera.target[0] << ','
                  << options.camera.target[1] << ',' << options.camera.target[2]
                  << " vertical_fov_degrees=" << options.camera.vertical_fov_degrees << '\n';
    }
    constexpr std::array<float, 4> kClearColor{0.025F, 0.055F, 0.09F, 1.0F};
    std::cout << "window client=" << window.GetClientSize().width << 'x'
              << window.GetClientSize().height << " dpi=" << window.GetDpi()
              << " controls=close-or-Escape clear_color=0.025,0.055,0.09,1\n"
              << std::flush;
    while (options.frame_limit == 0 || result.presented_frames < options.frame_limit)
    {
        window.PumpMessages();
        collect();
        if (window.GetSurfaceRevision() != initial_surface_revision)
        {
            environment_changed = true;
        }
        if (window.IsCloseRequested())
        {
            result.closed_by_user = true;
            break;
        }
        if (window.ConsumeShaderReloadRequest() && options.triangle)
        {
            environment_changed = true;
            static_cast<void>(reload());
        }
        const auto size = window.GetClientSize();
        if (size.width != initial_size.width || size.height != initial_size.height)
        {
            environment_changed = true;
        }
        if (window.IsMinimized() || size.width == 0 || size.height == 0)
        {
            environment_changed = true;
            window.WaitForMessages(100);
            continue;
        }
        renderer.Resize(size);
        if (!renderer.Render(kClearColor, options.parameters, options.camera))
        {
            environment_changed = true;
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
    collect();
    if (options.profile.enabled)
    {
        std::sort(samples.begin(), samples.end(),
                  [](const auto& a, const auto& b) { return a.presentation < b.presentation; });
        const auto dropped = renderer.GetDroppedTimingSamples();
        result.profile_complete = samples.size() == options.profile.sample_frames &&
                                  !environment_changed && !result.closed_by_user && dropped == 0 &&
                                  result.validation_warnings == 0;
        options.shader_path = shader;
        result.profile_json =
            BuildProfileReport(options, renderer.GetInfo(), initial_size, title, initial_generation,
                               renderer.IsDebugLayerEnabled(),
                               {result.profile_complete, environment_changed, result.closed_by_user,
                                dropped, result.validation_warnings},
                               samples, renderer.GetTriangleGeometryInfo());
        std::cout << "profile samples=" << samples.size()
                  << " warmup=" << options.profile.warmup_frames
                  << " requested=" << options.profile.sample_frames
                  << " complete=" << result.profile_complete
                  << " environment_changed=" << environment_changed << " dropped=" << dropped
                  << '\n';
        if (!options.report_path.empty())
        {
            SaveProfileReport(options.report_path, result.profile_json);
        }
    }
    return result;
}
} // namespace tinyme
