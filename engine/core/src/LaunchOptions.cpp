#include <tinyme/core/LaunchOptions.h>

#include <charconv>
#include <cmath>
#include <cstddef>
#include <system_error>

namespace tinyme
{
bool ParseResult::IsValid() const noexcept
{
    return error.empty();
}

ParseResult ParseCommandLine(std::span<const std::string_view> arguments)
{
    ParseResult result;
    if (arguments.empty())
    {
        return result;
    }
    if (arguments.size() == 1 && (arguments[0] == "--help" || arguments[0] == "-h"))
    {
        return result;
    }
    if (arguments.size() == 1 && arguments[0] == "--version")
    {
        result.options.action = LaunchAction::Version;
        return result;
    }

    bool has_mode = false;
    bool has_frames = false;
    bool has_experiment = false;
    bool has_shader = false;
    bool has_scale = false;
    bool has_tint = false;
    bool has_warmup = false;
    bool has_samples = false;
    bool has_report = false;
    bool has_camera_eye = false;
    bool has_camera_target = false;
    bool has_fov = false;
    bool has_postprocess = false;
    bool has_post_shader = false;
    bool has_width = false;
    bool has_height = false;
    bool has_capture = false;
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        const auto argument = arguments[index];
        if (argument == "--headless" || argument == "--windowed" || argument == "--offscreen")
        {
            if (has_mode)
            {
                result.error = "Execution modes are mutually exclusive and may only appear once.";
                return result;
            }
            has_mode = true;
            result.options.mode = argument == "--headless"   ? ExecutionMode::HeadlessLogic
                                  : argument == "--windowed" ? ExecutionMode::WindowedGpu
                                                             : ExecutionMode::OffscreenGpu;
        }
        else if (argument == "--debug-layer" || argument == "--gpu-validation")
        {
            auto& enabled = argument == "--debug-layer" ? result.options.debug_layer
                                                        : result.options.gpu_validation;
            if (enabled)
            {
                result.error = "Duplicate validation option.";
                return result;
            }
            enabled = true;
        }
        else if (argument == "--profile")
        {
            if (result.options.profile.enabled)
            {
                result.error = "Duplicate --profile.";
                return result;
            }
            result.options.profile.enabled = true;
        }
        else if (argument == "--scale" || argument == "--tint")
        {
            auto& seen = argument == "--scale" ? has_scale : has_tint;
            const std::size_t components = argument == "--scale" ? 1 : 4;
            if (seen || arguments.size() - index - 1 < components)
            {
                result.error =
                    "--scale requires one float; --tint requires RGBA, each option once.";
                return result;
            }
            seen = true;
            for (std::size_t component = 0; component < components; ++component)
            {
                const auto value = arguments[++index];
                float number = 0.0F;
                if (value.empty())
                {
                    result.error = "Empty float parameter.";
                    return result;
                }
                const auto parsed =
                    std::from_chars(value.data(), value.data() + value.size(), number);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                {
                    result.error = "Invalid float parameter.";
                    return result;
                }
                if (argument == "--scale")
                {
                    result.options.parameters.scale = number;
                }
                else
                {
                    result.options.parameters.tint[component] = number;
                }
            }
        }
        else if (argument == "--camera-eye" || argument == "--camera-target" || argument == "--fov")
        {
            auto& seen = argument == "--camera-eye"      ? has_camera_eye
                         : argument == "--camera-target" ? has_camera_target
                                                         : has_fov;
            const std::size_t components = argument == "--fov" ? 1 : 3;
            if (seen || arguments.size() - index - 1 < components)
            {
                result.error = "--camera-eye / --camera-target require XYZ; --fov one degree "
                               "value, each once.";
                return result;
            }
            seen = true;
            for (std::size_t component = 0; component < components; ++component)
            {
                const auto value = arguments[++index];
                float number = 0.0F;
                if (value.empty())
                {
                    result.error = "Empty camera parameter.";
                    return result;
                }
                const auto parsed =
                    std::from_chars(value.data(), value.data() + value.size(), number);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                    !std::isfinite(number))
                {
                    result.error = "Camera parameters must be complete finite floats.";
                    return result;
                }
                if (argument == "--fov")
                {
                    result.options.camera.vertical_fov_degrees = number;
                }
                else
                {
                    auto& destination = argument == "--camera-eye" ? result.options.camera.eye
                                                                   : result.options.camera.target;
                    destination[component] = number;
                }
            }
        }
        else if (argument == "--warmup" || argument == "--samples")
        {
            auto& seen = argument == "--warmup" ? has_warmup : has_samples;
            if (seen || index + 1 == arguments.size())
            {
                result.error = "Missing/duplicate sample budget.";
                return result;
            }
            seen = true;
            const auto value = arguments[++index];
            std::uint32_t count = 0;
            if (value.empty())
            {
                result.error = "Empty sample budget.";
                return result;
            }
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                count > 100000 || (argument == "--samples" && count == 0))
            {
                result.error = "--warmup: [0,100000]; --samples: [1,100000].";
                return result;
            }
            if (argument == "--warmup")
            {
                result.options.profile.warmup_frames = count;
            }
            else
            {
                result.options.profile.sample_frames = count;
            }
        }
        else if (argument == "--postprocess")
        {
            if (has_postprocess || index + 1 == arguments.size())
            {
                result.error = "--postprocess requires one value and may only appear once.";
                return result;
            }
            has_postprocess = true;
            const auto value = arguments[++index];
            if (value == "none")
            {
                result.options.postprocess = PostProcess::None;
            }
            else if (value == "copy")
            {
                result.options.postprocess = PostProcess::Copy;
            }
            else if (value == "grayscale")
            {
                result.options.postprocess = PostProcess::Grayscale;
            }
            else
            {
                result.error = "--postprocess must be none, copy, or grayscale.";
                return result;
            }
        }
        else if (argument == "--post-shader" || argument == "--capture")
        {
            auto& seen = argument == "--post-shader" ? has_post_shader : has_capture;
            if (seen || index + 1 == arguments.size() || arguments[index + 1].empty() ||
                arguments[index + 1].starts_with("-"))
            {
                result.error = "--post-shader / --capture require one nonempty path, each once.";
                return result;
            }
            seen = true;
            auto& destination = argument == "--post-shader" ? result.options.post_shader_path
                                                            : result.options.capture_path;
            destination = arguments[++index];
            if (argument == "--capture")
            {
                const auto extension = std::string_view(destination);
                if (extension.size() <= 4 || extension[extension.size() - 5] == '/' ||
                    extension[extension.size() - 5] == '\\' ||
                    extension[extension.size() - 4] != '.' ||
                    (extension[extension.size() - 3] != 'p' &&
                     extension[extension.size() - 3] != 'P') ||
                    (extension[extension.size() - 2] != 'n' &&
                     extension[extension.size() - 2] != 'N') ||
                    (extension.back() != 'g' && extension.back() != 'G'))
                {
                    result.error = "--capture requires a PNG path (.png, case-insensitive).";
                    return result;
                }
            }
        }
        else if (argument == "--width" || argument == "--height")
        {
            auto& seen = argument == "--width" ? has_width : has_height;
            if (seen || index + 1 == arguments.size())
            {
                result.error = "--width / --height require one integer, each once.";
                return result;
            }
            seen = true;
            const auto value = arguments[++index];
            std::uint32_t dimension = 0;
            if (value.empty())
            {
                result.error = "Render dimensions cannot be empty.";
                return result;
            }
            const auto parsed =
                std::from_chars(value.data(), value.data() + value.size(), dimension);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                dimension == 0 || dimension > kMaxRenderDimension)
            {
                result.error = "--width / --height must be integers in [1,2048].";
                return result;
            }
            auto& destination =
                argument == "--width" ? result.options.width : result.options.height;
            destination = dimension;
        }
        else if (argument == "--report")
        {
            if (has_report || index + 1 == arguments.size() || arguments[index + 1].empty() ||
                arguments[index + 1].starts_with("--"))
            {
                result.error = "Missing/duplicate report path.";
                return result;
            }
            has_report = true;
            result.options.report_path = arguments[++index];
        }
        else if (argument == "--experiment" || argument == "--shader")
        {
            auto& seen = argument == "--experiment" ? has_experiment : has_shader;
            if (seen || index + 1 == arguments.size() || arguments[index + 1].empty() ||
                arguments[index + 1].starts_with("--"))
            {
                result.error =
                    "--experiment / --shader require one nonempty value and may only appear once.";
                return result;
            }
            seen = true;
            auto& destination =
                argument == "--experiment" ? result.options.experiment : result.options.shader_path;
            destination = arguments[++index];
        }
        else if (argument == "--frames")
        {
            if (has_frames || index + 1 == arguments.size())
            {
                result.error = "--frames requires one value and may only appear once.";
                return result;
            }
            has_frames = true;
            const auto value = arguments[++index];
            std::uint32_t count = 0;
            if (value.empty())
            {
                result.error = "--frames cannot be empty.";
                return result;
            }
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                count == 0 || count > kMaxHeadlessFrames)
            {
                result.error = "--frames must be an integer in [1, 1000000].";
                return result;
            }
            result.options.frame_count = count;
        }
        else
        {
            result.error = "Unknown or conflicting argument: " + std::string(argument);
            return result;
        }
    }
    if (!has_mode)
    {
        result.error = "Execution requires --headless, --windowed, or --offscreen.";
        return result;
    }
    if (result.options.mode == ExecutionMode::HeadlessLogic &&
        (result.options.debug_layer || result.options.gpu_validation))
    {
        result.error = "GPU validation options require --windowed or --offscreen; "
                       "headless logic never initializes GPU.";
        return result;
    }
    if ((has_experiment || has_shader) && result.options.mode == ExecutionMode::HeadlessLogic)
    {
        result.error = "Rendering experiment options require --windowed or --offscreen.";
        return result;
    }
    if (result.options.experiment != "clear" && result.options.experiment != "triangle")
    {
        result.error = "--experiment must be clear or triangle.";
        return result;
    }
    if (has_shader && result.options.experiment != "triangle")
    {
        result.error = "--shader requires the triangle experiment.";
        return result;
    }
    if ((has_scale || has_tint || result.options.profile.enabled || has_warmup || has_samples ||
         has_report) &&
        result.options.mode == ExecutionMode::HeadlessLogic)
    {
        result.error = "Parameters/profiling require --windowed or --offscreen.";
        return result;
    }
    if ((has_scale || has_tint) && result.options.experiment != "triangle")
    {
        result.error = "Triangle parameters cannot be used with clear.";
        return result;
    }
    if (!IsValid(result.options.parameters) || !IsValid(result.options.profile))
    {
        result.error = "--scale must be finite [0.01,2]; --tint finite RGBA [0,1].";
        return result;
    }
    if ((has_camera_eye || has_camera_target || has_fov) &&
        (result.options.mode == ExecutionMode::HeadlessLogic ||
         result.options.experiment != "triangle"))
    {
        result.error = "Camera options require the windowed or offscreen triangle experiment.";
        return result;
    }
    if (!IsValid(result.options.camera))
    {
        result.error = "Invalid camera: finite eye/target [-10000,10000], nondegenerate view, "
                       "and --fov [10,120] required.";
        return result;
    }
    if ((has_warmup || has_samples || has_report) && !result.options.profile.enabled)
    {
        result.error = "--warmup / --samples / --report require --profile.";
        return result;
    }
    if ((has_postprocess || has_post_shader || has_width || has_height || has_capture) &&
        result.options.mode == ExecutionMode::HeadlessLogic)
    {
        result.error = "Pipeline, dimensions, and capture require a GPU execution mode.";
        return result;
    }
    if (has_post_shader && result.options.postprocess == PostProcess::None)
    {
        result.error = "--post-shader requires --postprocess copy or grayscale.";
        return result;
    }
    if (has_capture && result.options.mode != ExecutionMode::OffscreenGpu)
    {
        result.error = "--capture requires --offscreen.";
        return result;
    }
    if (has_capture && has_report && result.options.capture_path == result.options.report_path)
    {
        result.error = "--capture and --report must use different paths.";
        return result;
    }
    if (result.options.profile.enabled)
    {
        const auto budget =
            result.options.profile.warmup_frames + result.options.profile.sample_frames;
        if (has_frames && result.options.frame_count < budget)
        {
            result.error = "--frames must cover warmup plus requested samples.";
            return result;
        }
        if (!has_frames)
        {
            result.options.frame_count = budget;
            has_frames = true;
        }
    }
    result.options.has_frame_limit =
        has_frames || result.options.mode == ExecutionMode::OffscreenGpu;
    result.options.action = LaunchAction::Run;
    return result;
}
} // namespace tinyme
