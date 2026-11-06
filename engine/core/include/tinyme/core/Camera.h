#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace tinyme
{
struct CameraParameters
{
    std::array<float, 3> eye{0.0F, 0.0F, -2.0F};
    std::array<float, 3> target{0.0F, 0.0F, 0.0F};
    std::array<float, 3> up{0.0F, 1.0F, 0.0F};
    float vertical_fov_degrees = 90.0F;
    float near_plane = 0.1F;
    float far_plane = 100.0F;
};

// b1: row-major View * Projection, row vectors, left-handed, D3D depth [0,1].
struct alignas(16) CameraTransform
{
    std::array<float, 16> world_to_clip{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};
inline constexpr std::uint32_t kCameraTransformDwords = 16;
static_assert(std::is_standard_layout_v<CameraTransform> &&
              std::is_trivially_copyable_v<CameraTransform>);
static_assert(sizeof(float) == 4 && sizeof(CameraTransform) == 64 &&
              alignof(CameraTransform) == 16 && offsetof(CameraTransform, world_to_clip) == 0);

[[nodiscard]] bool IsValid(const CameraParameters& camera) noexcept;
[[nodiscard]] bool IsValid(const CameraTransform& transform) noexcept;
// Rejects nonfinite/degenerate inputs before normalization or float matrix conversion.
[[nodiscard]] CameraTransform BuildCameraTransform(const CameraParameters& camera,
                                                   float aspect_ratio);
} // namespace tinyme
