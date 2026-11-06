#pragma once

#include <tinyme/platform/windows/Window.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace tinyme
{
struct RendererOptions
{
    bool debug_layer = false;
    bool gpu_validation = false;
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
    ~ClearRenderer();
    ClearRenderer(const ClearRenderer&) = delete;
    ClearRenderer& operator=(const ClearRenderer&) = delete;

    // Destructive resize failure faults the renderer; then only Shutdown() is allowed.
    void Resize(WindowSize size);
    // False means occluded: caller should wait for window messages before retrying.
    [[nodiscard]] bool Render(const std::array<float, 4>& clear_color);
    // Compile/confirmed shader-contract rejection keeps the previous pipeline.
    // Unknown API/GPU failures throw and fault the renderer; PSO classification needs Debug Layer.
    [[nodiscard]] bool ReloadTriangle(const std::filesystem::path& source,
                                      std::string& diagnostics);
    [[nodiscard]] std::uint64_t GetPipelineGeneration() const noexcept;
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
