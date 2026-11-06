#include <tinyme/runtime/WindowedSession.h>

#include "ImageCapture.h"
#include "ProfileReport.h"

#include <tinyme/render/d3d12/ClearRenderer.h>
#include <tinyme/runtime/Session.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace tinyme
{
WindowedRunResult RunOffscreenSession(std::wstring_view title, WindowedRunOptions options)
{
    options.offscreen = true;
    ResolveSessionShaderPaths(options);
    ValidateGpuSessionOptions(options);
    const WindowSize initial_size{options.width, options.height};
    RendererOptions renderer_options{options.debug_layer, options.gpu_validation,
                                     options.profile.enabled, options.postprocess};
#ifdef _DEBUG
    renderer_options.debug_layer = true;
#endif
    ClearRenderer renderer(initial_size, renderer_options);
    const auto reload = [&](bool post)
    {
        const auto& path = post ? options.post_shader_path : options.shader_path;
        std::string diagnostics;
        const bool success = post ? renderer.ReloadPostProcess(path, diagnostics)
                                  : renderer.ReloadTriangle(path, diagnostics);
        const auto utf8_path = path.generic_u8string();
        std::cout << (post ? "post_shader_reload" : "shader_reload") << " success=" << success
                  << " generation="
                  << (post ? renderer.GetPostGeneration() : renderer.GetPipelineGeneration())
                  << " source=\"" << reinterpret_cast<const char*>(utf8_path.c_str()) << "\"\n";
        if (!diagnostics.empty())
        {
            std::cout << (post ? "post_shader_diagnostics: " : "shader_diagnostics: ")
                      << diagnostics << '\n';
        }
        std::cout << std::flush;
        return success;
    };
    if (options.triangle && !reload(false))
    {
        throw std::runtime_error("Initial triangle shader failed; no capture was generated.");
    }
    if (options.postprocess != PostProcess::None && !reload(true))
    {
        throw std::runtime_error("Initial postprocess shader failed; no capture was generated.");
    }
    const auto initial_generation = renderer.GetPipelineGeneration();
    const auto initial_post_generation = renderer.GetPostGeneration();
    WindowedRunResult result;
    Session session;
    bool environment_changed = false;
    std::vector<FrameTiming> samples;
    if (options.profile.enabled)
    {
        samples.reserve(options.profile.sample_frames);
    }
    const auto collect = [&]()
    {
        if (!options.profile.enabled)
        {
            return;
        }
        for (const auto& timing : renderer.TakeCompletedTimings())
        {
            if (timing.presentation != 0 || timing.pipeline_generation != initial_generation ||
                timing.post_generation != initial_post_generation ||
                timing.postprocess != options.postprocess ||
                timing.size.width != initial_size.width ||
                timing.size.height != initial_size.height)
            {
                environment_changed = true;
                continue;
            }
            if (timing.frame_number > options.profile.warmup_frames &&
                timing.frame_number <=
                    options.profile.warmup_frames + options.profile.sample_frames)
            {
                samples.push_back(timing);
            }
        }
    };
    std::cout << "experiment=" << (options.triangle ? "triangle" : "clear")
              << " postprocess=" << ToString(options.postprocess) << " width=" << initial_size.width
              << " height=" << initial_size.height << " window_created=0 vsync=off\n"
              << std::flush;
    constexpr std::array<float, 4> kClearColor{0.025F, 0.055F, 0.09F, 1.0F};
    while (result.rendered_frames < options.frame_limit)
    {
        if (!renderer.Render(kClearColor, options.parameters, options.camera))
        {
            throw std::runtime_error("Offscreen renderer did not submit a GPU frame.");
        }
        ++result.rendered_frames;
        if (!session.Tick(1.0 / 60.0))
        {
            throw std::runtime_error("Offscreen session rejected the fixed logical timestep.");
        }
        collect();
    }
    std::vector<std::uint8_t> capture;
    if (!options.capture_path.empty())
    {
        // Explicit last-frame wait/readback boundary, outside CPU/GPU timing scopes.
        capture = renderer.CaptureRgba();
        collect();
    }
    renderer.Shutdown();
    collect();
    const auto validation = renderer.GetValidationStats();
    result.validation_warnings = validation.warnings;
    environment_changed = environment_changed || validation.errors != 0;
    const auto dropped = renderer.GetDroppedTimingSamples();
    std::sort(samples.begin(), samples.end(),
              [](const auto& a, const auto& b) { return a.frame_number < b.frame_number; });
    bool sequence_complete = samples.size() == options.profile.sample_frames;
    for (std::size_t index = 0; index < samples.size(); ++index)
    {
        if (samples[index].frame_number != options.profile.warmup_frames + index + 1)
        {
            sequence_complete = false;
        }
    }
    if (options.profile.enabled)
    {
        result.profile_complete = sequence_complete && !environment_changed && dropped == 0 &&
                                  result.validation_warnings == 0;
    }
    bool capture_saved = false;
    if (!options.capture_path.empty() && !environment_changed && validation.warnings == 0 &&
        dropped == 0 && (!options.profile.enabled || result.profile_complete))
    {
        SaveCapturePng(options.capture_path, initial_size, capture);
        capture_saved = true;
        const auto utf8_path = options.capture_path.generic_u8string();
        std::cout << "capture saved=1 path=\"" << reinterpret_cast<const char*>(utf8_path.c_str())
                  << "\" width=" << initial_size.width << " height=" << initial_size.height
                  << " format=PNG\n";
    }
    if (options.profile.enabled)
    {
        result.profile_json = BuildProfileReport(
            options, renderer.GetInfo(), initial_size, title, initial_generation,
            renderer.IsDebugLayerEnabled(),
            {result.profile_complete, environment_changed, false, dropped,
             result.validation_warnings, capture_saved, validation.errors},
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
    else if (environment_changed || result.validation_warnings != 0 || dropped != 0)
    {
        throw std::runtime_error("Offscreen GPU validation failed; no capture was saved.");
    }
    std::cout << "offscreen rendered_frames=" << result.rendered_frames
              << " window_created=0 vsync=off\n"
              << std::flush;
    return result;
}
} // namespace tinyme
