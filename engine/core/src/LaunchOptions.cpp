#include <tinyme/core/LaunchOptions.h>

#include <charconv>
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
    for (std::size_t index = 0; index < arguments.size(); ++index)
    {
        const auto argument = arguments[index];
        if (argument == "--headless" || argument == "--windowed")
        {
            if (has_mode)
            {
                result.error = "Execution modes are mutually exclusive and may only appear once.";
                return result;
            }
            has_mode = true;
            result.options.mode = argument == "--headless" ? ExecutionMode::HeadlessLogic
                                                           : ExecutionMode::WindowedGpu;
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
        result.error = "Execution requires --headless or --windowed.";
        return result;
    }
    if (result.options.mode == ExecutionMode::HeadlessLogic &&
        (result.options.debug_layer || result.options.gpu_validation))
    {
        result.error =
            "GPU validation options require --windowed; headless logic never initializes GPU.";
        return result;
    }
    if ((has_experiment || has_shader) && result.options.mode != ExecutionMode::WindowedGpu)
    {
        result.error = "Rendering experiment options require --windowed.";
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
    result.options.has_frame_limit = has_frames;
    result.options.action = LaunchAction::Run;
    return result;
}
} // namespace tinyme
