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
    if (options.offscreen)
    {
        throw std::invalid_argument("Windowed entry does not accept offscreen configuration.");
    }
    ResolveSessionShaderPaths(options);
    ValidateGpuSessionOptions(options);
    Window window(title, {options.width, options.height});
    RendererOptions renderer_options{options.debug_layer, options.gpu_validation,
                                     options.profile.enabled, options.postprocess};
#ifdef _DEBUG
    renderer_options.debug_layer = true;
#endif
    ClearRenderer renderer(window.GetHandle(), window.GetClientSize(), renderer_options);
    const auto& shader = options.shader_path;
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
    auto reload_post = [&]()
    {
        std::string diagnostics;
        const bool success = renderer.ReloadPostProcess(options.post_shader_path, diagnostics);
        const auto utf8_path = options.post_shader_path.generic_u8string();
        std::cout << "post_shader_reload success=" << success
                  << " generation=" << renderer.GetPostGeneration() << " source=\""
                  << reinterpret_cast<const char*>(utf8_path.c_str()) << "\"\n";
        if (!diagnostics.empty())
        {
            std::cout << "post_shader_diagnostics: " << diagnostics << '\n';
        }
        std::cout << std::flush;
        return success;
    };
    if (options.postprocess != PostProcess::None && !reload_post())
    {
        throw std::runtime_error("Initial postprocess shader failed; no valid pipeline exists.");
    }
    std::cout << "experiment=" << (options.triangle ? "triangle" : "clear")
              << " postprocess=" << ToString(options.postprocess) << '\n';
    Session session;
    WindowedRunResult result;
    const auto initial_size = window.GetClientSize();
    const auto initial_surface_revision = window.GetSurfaceRevision();
    const auto initial_generation = renderer.GetPipelineGeneration();
    const auto initial_post_generation = renderer.GetPostGeneration();
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
                timing.post_generation != initial_post_generation ||
                timing.postprocess != options.postprocess ||
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
        if (window.ConsumeShaderReloadRequest())
        {
            environment_changed = true;
            if (options.triangle)
            {
                static_cast<void>(reload());
            }
            if (options.postprocess != PostProcess::None)
            {
                static_cast<void>(reload_post());
            }
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
        ++result.rendered_frames;
        if (!session.Tick(1.0 / 60.0))
        {
            throw std::runtime_error("Windowed session rejected the fixed logical timestep.");
        }
        ++result.presented_frames;
    }
    renderer.Shutdown();
    const auto validation = renderer.GetValidationStats();
    result.validation_warnings = validation.warnings;
    environment_changed = environment_changed || validation.errors != 0;
    collect();
    if (options.profile.enabled)
    {
        std::sort(samples.begin(), samples.end(),
                  [](const auto& a, const auto& b) { return a.presentation < b.presentation; });
        const auto dropped = renderer.GetDroppedTimingSamples();
        bool sequence_complete = samples.size() == options.profile.sample_frames;
        for (std::size_t index = 0; index < samples.size(); ++index)
        {
            if (samples[index].presentation != options.profile.warmup_frames + index + 1)
            {
                sequence_complete = false;
            }
        }
        result.profile_complete = sequence_complete && !environment_changed &&
                                  !result.closed_by_user && dropped == 0 &&
                                  result.validation_warnings == 0;
        result.profile_json = BuildProfileReport(
            options, renderer.GetInfo(), initial_size, title, initial_generation,
            renderer.IsDebugLayerEnabled(),
            {result.profile_complete, environment_changed, result.closed_by_user, dropped,
             result.validation_warnings, false, validation.errors},
            samples, renderer.GetTriangleGeometryInfo(), initial_post_generation);
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
