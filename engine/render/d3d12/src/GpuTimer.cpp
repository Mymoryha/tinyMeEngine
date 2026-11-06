#include "GpuTimer.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace tinyme
{
namespace
{
void CheckTimer(HRESULT result, const char* operation)
{
    if (FAILED(result))
    {
        throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
    }
}
} // namespace
GpuTimer::GpuTimer(ID3D12Device* device, ID3D12CommandQueue* queue)
{
    CheckTimer(queue->GetTimestampFrequency(&frequency_), "Timestamp frequency");
    if (frequency_ == 0)
    {
        throw std::runtime_error("Zero GPU timestamp frequency.");
    }
    D3D12_QUERY_HEAP_DESC query{};
    query.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    query.Count = 4;
    CheckTimer(device->CreateQueryHeap(&query, IID_PPV_ARGS(&queries_)), "Create timestamp heap");
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource.Width = 4 * sizeof(UINT64);
    resource.Height = 1;
    resource.DepthOrArraySize = 1;
    resource.MipLevels = 1;
    resource.SampleDesc.Count = 1;
    resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    CheckTimer(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &resource,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&readback_)),
               "Create timestamp readback");
    const D3D12_RANGE read_range{0, 4 * sizeof(UINT64)};
    CheckTimer(readback_->Map(0, &read_range, reinterpret_cast<void**>(&mapped_)),
               "Map timestamp readback");
}
GpuTimer::~GpuTimer()
{
    ReleaseResources();
}
void GpuTimer::ReleaseResources() noexcept
{
    if (mapped_)
    {
        const D3D12_RANGE no_writes{0, 0};
        readback_->Unmap(0, &no_writes);
        mapped_ = nullptr;
    }
    readback_.Reset();
    queries_.Reset();
}
void GpuTimer::Begin(UINT slot, ID3D12GraphicsCommandList* commands)
{
    if (slot >= slots_.size() || slots_[slot].fence != 0 || !mapped_)
    {
        throw std::logic_error("Timestamp slot is not safe to reuse.");
    }
    commands->EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 2);
}
void GpuTimer::End(UINT slot, ID3D12GraphicsCommandList* commands)
{
    commands->EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 2 + 1);
    commands->ResolveQueryData(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, slot * 2, 2,
                               readback_.Get(), slot * 2 * sizeof(UINT64));
}
void GpuTimer::Submitted(UINT slot, UINT64 fence_value, FrameTiming timing)
{
    slots_.at(slot) = {fence_value, timing};
}
void GpuTimer::Poll(UINT64 completed_fence)
{
    if (completed_fence == std::numeric_limits<UINT64>::max())
    {
        throw std::runtime_error("Device removed while polling timestamp fence.");
    }
    for (std::size_t index = 0; index < slots_.size(); ++index)
    {
        auto& slot = slots_[index];
        if (slot.fence == 0 || slot.fence > completed_fence)
        {
            continue;
        }
        UINT64 values[2]{};
        std::memcpy(values, mapped_ + index * 2 * sizeof(UINT64), sizeof(values));
        if (values[1] < values[0])
        {
            throw std::runtime_error("GPU timestamps out of order.");
        }
        slot.timing.gpu_pass_ms =
            static_cast<double>(values[1] - values[0]) * 1000.0 / static_cast<double>(frequency_);
        if (completed_.size() == 256)
        {
            ++dropped_;
        }
        else
        {
            completed_.push_back(slot.timing);
        }
        slot.fence = 0;
    }
}
std::vector<FrameTiming> GpuTimer::TakeCompleted()
{
    std::vector<FrameTiming> output;
    output.swap(completed_);
    std::sort(output.begin(), output.end(),
              [](const auto& a, const auto& b) { return a.presentation < b.presentation; });
    return output;
}
} // namespace tinyme
