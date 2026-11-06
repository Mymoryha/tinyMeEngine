#pragma once

#include <d3d12.h>
#include <tinyme/render/d3d12/ClearRenderer.h>
#include <wrl/client.h>

#include <array>
#include <vector>

namespace tinyme
{
// Two disjoint query/readback slots match the renderer's frame contexts. No fence waits here.
class GpuTimer
{
  public:
    GpuTimer(ID3D12Device* device, ID3D12CommandQueue* queue);
    ~GpuTimer();
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;
    void Begin(UINT slot, ID3D12GraphicsCommandList* commands);
    void End(UINT slot, ID3D12GraphicsCommandList* commands);
    void Submitted(UINT slot, UINT64 fence_value, FrameTiming timing);
    void Poll(UINT64 completed_fence);
    [[nodiscard]] std::vector<FrameTiming> TakeCompleted();
    [[nodiscard]] UINT64 GetFrequency() const noexcept
    {
        return frequency_;
    }
    [[nodiscard]] std::uint64_t GetDroppedSamples() const noexcept
    {
        return dropped_;
    }
    // Must only be called after GPU completion/device removal; results remain readable.
    void ReleaseResources() noexcept;

  private:
    struct Slot
    {
        UINT64 fence = 0;
        FrameTiming timing;
    };
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> queries_;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback_;
    unsigned char* mapped_ = nullptr;
    std::array<Slot, 2> slots_;
    std::vector<FrameTiming> completed_;
    UINT64 frequency_ = 0;
    std::uint64_t dropped_ = 0;
};
} // namespace tinyme
