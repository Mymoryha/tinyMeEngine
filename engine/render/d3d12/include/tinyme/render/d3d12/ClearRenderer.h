#pragma once

#include <tinyme/core/Camera.h>
#include <tinyme/core/ExperimentParameters.h>
#include <tinyme/core/RenderPipeline.h>
#include <tinyme/platform/windows/Window.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace tinyme
{
struct RendererOptions
{
    bool debug_layer = false;
    bool gpu_validation = false;
    bool profiling = false;
    PostProcess postprocess = PostProcess::None;
};

struct FrameTiming
{
    std::uint64_t presentation = 0; // Zero denotes a submitted but occluded frame.
    std::uint64_t pipeline_generation = 0;
    WindowSize size;
    double cpu_record_ms = 0.0;
    double gpu_pass_ms = 0.0;       // Scene clear + indexed draw only, preserving M2 scope.
    std::uint64_t frame_number = 0; // Submitted GPU frame; offscreen never presents.
    std::uint64_t post_generation = 0;
    double gpu_post_ms = 0.0;
    PostProcess postprocess = PostProcess::None;
};

struct RendererInfo
{
    std::string adapter;
    std::string driver;
    std::uint64_t timestamp_frequency = 0;
};

struct TriangleGeometryInfo
{
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    std::uint32_t vertex_stride = 0;
    std::uint64_t upload_fence = 0;
    std::uint64_t last_use_fence = 0;
    bool resources_ready = false;
    bool upload_pending = false;
};

struct ValidationStats
{
    std::uint64_t warnings = 0;
    std::uint64_t errors = 0;
};

// Concrete D3D12 renderer. The borrowed HWND must outlive this object and Shutdown().
class ClearRenderer
{
  public:
    ClearRenderer(HWND hwnd, WindowSize size, RendererOptions options);
    // GPU-only target: creates no window or swapchain.
    ClearRenderer(WindowSize size, RendererOptions options);
    ~ClearRenderer();
    ClearRenderer(const ClearRenderer&) = delete;
    ClearRenderer& operator=(const ClearRenderer&) = delete;

    // Destructive resize failure faults the renderer; then only Shutdown() is allowed.
    void Resize(WindowSize size);
    // False means occluded: caller should wait for window messages before retrying.
    [[nodiscard]] bool Render(const std::array<float, 4>& clear_color,
                              const TriangleParameters& parameters = {},
                              const CameraParameters& camera = {});
    [[nodiscard]] TriangleGeometryInfo GetTriangleGeometryInfo() const noexcept;
    // Never waits. Call regularly; retained completed queue is bounded to 256 entries.
    [[nodiscard]] std::vector<FrameTiming> TakeCompletedTimings();
    [[nodiscard]] std::uint64_t GetDroppedTimingSamples() const noexcept;
    [[nodiscard]] const RendererInfo& GetInfo() const noexcept;
    // Compile/confirmed shader-contract rejection keeps the previous pipeline.
    // Unknown API/GPU failures throw and fault the renderer; PSO classification needs Debug Layer.
    [[nodiscard]] bool ReloadTriangle(const std::filesystem::path& source,
                                      std::string& diagnostics);
    [[nodiscard]] std::uint64_t GetPipelineGeneration() const noexcept;
    [[nodiscard]] bool ReloadPostProcess(const std::filesystem::path& source,
                                         std::string& diagnostics);
    [[nodiscard]] std::uint64_t GetPostGeneration() const noexcept;
    void SetPostProcess(PostProcess mode);
    // Explicit batch capture boundary; waits for last output and strips GPU row padding.
    [[nodiscard]] std::vector<std::uint8_t> CaptureRgba();
    // Waits for GPU use, releases children and checks debug-layer live child objects.
    void Shutdown();
    [[nodiscard]] WindowSize GetSize() const noexcept;
    [[nodiscard]] ValidationStats GetValidationStats() const noexcept;
    [[nodiscard]] bool IsDebugLayerEnabled() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace tinyme
