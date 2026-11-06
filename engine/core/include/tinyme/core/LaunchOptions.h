#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace tinyme
{
enum class LaunchAction
{
    Help,
    Version,
    Run
};

enum class ExecutionMode
{
    HeadlessLogic,
    WindowedGpu
};

inline constexpr std::uint32_t kMaxHeadlessFrames = 1'000'000;

struct LaunchOptions
{
    LaunchAction action = LaunchAction::Help;
    ExecutionMode mode = ExecutionMode::HeadlessLogic;
    std::uint32_t frame_count = 1;
    bool has_frame_limit = false;
    bool debug_layer = false;
    bool gpu_validation = false;
    std::string experiment = "triangle";
    std::string shader_path;
};

struct ParseResult
{
    LaunchOptions options;
    std::string error;

    [[nodiscard]] bool IsValid() const noexcept;
};

[[nodiscard]] ParseResult ParseCommandLine(std::span<const std::string_view> arguments);
} // namespace tinyme
