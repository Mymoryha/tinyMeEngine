#include "Application.h"

#include <tinyme/core/LaunchOptions.h>
#include <tinyme/core/Version.h>
#include <tinyme/runtime/Session.h>
#include <tinyme/runtime/WindowedSession.h>

#include <exception>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

namespace tinyme
{
int RunApplication(ApplicationKind kind, int argc, wchar_t* argv[])
{
    try
    {
        const std::string_view name =
            kind == ApplicationKind::Editor ? "tinyme_editor" : "tinyme_runtime";
        std::vector<std::string> owned_arguments;
        for (int index = 1; index < argc; ++index)
        {
            const auto utf8 = std::filesystem::path(argv[index]).u8string();
            owned_arguments.emplace_back(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        }
        std::vector<std::string_view> arguments;
        for (const auto& argument : owned_arguments)
        {
            arguments.emplace_back(argument);
        }
        const auto parsed = ParseCommandLine(arguments);
        if (!parsed.IsValid())
        {
            std::cerr << "error: " << parsed.error << '\n';
            return 2;
        }
        if (parsed.options.action == LaunchAction::Help)
        {
            std::cout
                << "Usage: " << name
                << " [--help | --version | --headless | --windowed | --offscreen] [options]\n"
                << "M3: D3D12 scene + optional fullscreen postprocess; pinned DXC.\n"
                << "--experiment triangle|clear: GPU modes only, default triangle.\n"
                << "--shader PATH: triangle source; default executable/shaders/Triangle.hlsl.\n"
                << "--postprocess none|copy|grayscale: same scene input, default none.\n"
                << "--post-shader PATH: enabled postprocess source; default "
                   "executable/shaders/PostProcess.hlsl.\n"
                << "--width N / --height N: GPU target dimensions [1,2048], default 1280x720.\n"
                << "--capture NEW.png: offscreen only, last completed RGBA8 frame; never "
                   "overwrites.\n"
                << "--scale [0.01,2] / --tint R G B A [0,1]: finite triangle parameters.\n"
                << "--camera-eye X Y Z / --camera-target X Y Z / --fov [10,120]: triangle only.\n"
                << "Camera: left-handed perspective, default eye 0,0,-2 target 0,0,0 FOV 90.\n"
                << "--profile [--warmup N] [--samples N] [--report NEW.json]: GPU modes only.\n"
                << "Profile defaults: 30 warmup + 120 samples; auto finite frame budget unless "
                   "--frames set.\n"
                << "Samples/warmup max 100000; samples >=1. Output parents must exist; reports "
                   "and captures never overwrite existing files.\n"
                << "CPU recording, GPU scene clear/draw and GPU post draw are independent "
                   "scopes; not full-frame/FPS. Capture/PNG, present/VSYNC and reload excluded.\n"
                << "--headless: no window/GPU, default 1 logical tick.\n"
                << "--windowed: RTX 5080 window, runs until close/Escape unless --frames is set; "
                   "F5 reloads enabled scene/post shaders.\n"
                << "--offscreen: RTX 5080 GPU without window/swapchain/Present, default 1 frame; "
                   "vsync off.\n"
                << "--frames N: [1,1000000]; logical ticks for headless, presentations for "
                   "windowed, submitted GPU frames for offscreen.\n"
                << "--debug-layer / --gpu-validation: GPU modes only; GPU validation implies "
                   "debug.\n"
                << "Debug builds always enable Debug Layer; optimized builds enable it "
                   "explicitly.\n"
                << "--help (-h) and --version must be used alone.\n"
                << "Exit codes: 0 success, 1 runtime failure, 2 invalid arguments.\n";
            return 0;
        }
        if (parsed.options.action == LaunchAction::Version)
        {
            std::cout << name << ' ' << kVersion << '\n';
            return 0;
        }

        if (parsed.options.mode == ExecutionMode::WindowedGpu ||
            parsed.options.mode == ExecutionMode::OffscreenGpu)
        {
            const bool offscreen = parsed.options.mode == ExecutionMode::OffscreenGpu;
            const auto title = kind == ApplicationKind::Editor
                                   ? L"TinyMeEngine | Editor | M3 Render Experiment"
                                   : L"TinyMeEngine | Runtime | M3 Render Experiment";
            const auto path = [](std::string_view text)
            {
                std::u8string utf8;
                for (const auto character : text)
                {
                    utf8.push_back(static_cast<char8_t>(character));
                }
                return std::filesystem::path(utf8);
            };
            WindowedRunOptions options;
            options.offscreen = offscreen;
            options.frame_limit =
                offscreen || parsed.options.has_frame_limit ? parsed.options.frame_count : 0;
            options.debug_layer = parsed.options.debug_layer;
            options.gpu_validation = parsed.options.gpu_validation;
            options.triangle = parsed.options.experiment == "triangle";
            options.shader_path = path(parsed.options.shader_path);
            options.parameters = parsed.options.parameters;
            options.camera = parsed.options.camera;
            options.profile = parsed.options.profile;
            options.report_path = path(parsed.options.report_path);
            options.postprocess = parsed.options.postprocess;
            options.post_shader_path = path(parsed.options.post_shader_path);
            options.capture_path = path(parsed.options.capture_path);
            options.width = parsed.options.width;
            options.height = parsed.options.height;
            const auto result = offscreen ? RunOffscreenSession(title, options)
                                          : RunWindowedSession(title, options);
            if (offscreen)
            {
                std::cout << name
                          << " mode=offscreen-gpu rendered_frames=" << result.rendered_frames
                          << " presented_frames=" << result.presented_frames
                          << " window_created=0 vsync=off";
            }
            else
            {
                std::cout << name
                          << " mode=windowed-gpu presented_frames=" << result.presented_frames;
            }
            std::cout << " closed_by_user=" << result.closed_by_user
                      << " validation_warnings=" << result.validation_warnings;
            if (!offscreen)
            {
                std::cout << " rendered_frames=" << result.rendered_frames
                          << " window_created=1 vsync=on";
            }
            std::cout << '\n' << std::flush;
            if (options.profile.enabled)
            {
                if (options.report_path.empty())
                {
                    std::cout << result.profile_json << std::flush;
                }
                if (!result.profile_complete)
                {
                    std::cerr << "error: Profile incomplete or environment changed; see report.\n";
                    return 1;
                }
            }
            return 0;
        }

        Session session;
        constexpr double kFixedDeltaSeconds = 1.0 / 60.0;
        for (std::uint32_t frame = 0; frame < parsed.options.frame_count; ++frame)
        {
            if (!session.Tick(kFixedDeltaSeconds))
            {
                std::cerr << "error: Session rejected the fixed timestep.\n";
                return 1;
            }
        }
        const auto& stats = session.GetStats();
        std::cout << name << " mode=headless-logic ticks=" << stats.tick_count
                  << " simulated_seconds=" << std::fixed << std::setprecision(6)
                  << stats.simulated_seconds << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
} // namespace tinyme
