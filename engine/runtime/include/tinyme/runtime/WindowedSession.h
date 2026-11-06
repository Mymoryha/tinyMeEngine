#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace tinyme
{
struct WindowedRunOptions
{
    std::uint32_t frame_limit = 0;
    bool debug_layer = false;
    bool gpu_validation = false;
    bool triangle = true;
    std::filesystem::path shader_path;
};

struct WindowedRunResult
{
    std::uint64_t presented_frames = 0;
    std::uint64_t validation_warnings = 0;
    bool closed_by_user = false;
};

// Zero frame limit means interactive execution until close/Escape. Main thread only.
[[nodiscard]] WindowedRunResult RunWindowedSession(std::wstring_view title,
                                                   WindowedRunOptions options);
} // namespace tinyme
