#pragma once

#include <tinyme/core/Camera.h>
#include <tinyme/core/ExperimentParameters.h>
#include <tinyme/core/RenderPipeline.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace tinyme
{
struct WindowedRunOptions
{
    TriangleParameters parameters;
    CameraParameters camera;
    std::uint32_t frame_limit = 0;
    bool debug_layer = false;
    bool gpu_validation = false;
    bool triangle = true;
    std::filesystem::path shader_path;
    ProfileOptions profile;
    std::filesystem::path report_path;
    PostProcess postprocess = PostProcess::None;
    std::filesystem::path post_shader_path;
    std::filesystem::path capture_path;
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool offscreen = false;
};

struct WindowedRunResult
{
    std::uint64_t presented_frames = 0;
    std::uint64_t rendered_frames = 0;
    std::uint64_t validation_warnings = 0;
    bool closed_by_user = false;
    bool profile_complete = false;
    std::string profile_json;
};

// Zero frame limit means interactive execution until close/Escape. Main thread only.
[[nodiscard]] WindowedRunResult RunWindowedSession(std::wstring_view title,
                                                   WindowedRunOptions options);
// Finite GPU-only batch, no HWND/message pump/swapchain/Present; same scene/pass inputs.
[[nodiscard]] WindowedRunResult RunOffscreenSession(std::wstring_view title,
                                                    WindowedRunOptions options);
} // namespace tinyme
