#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace tinyme
{
// b0: two HLSL 16-byte registers. Scale is dimensionless in clip-space XY.
struct alignas(16) TriangleParameters
{
    std::array<float, 4> tint{1.0F, 1.0F, 1.0F, 1.0F};
    float scale = 1.0F;
    std::array<float, 3> reserved{};
};
inline constexpr std::uint32_t kTriangleParameterDwords = 8;
static_assert(std::is_standard_layout_v<TriangleParameters> &&
              std::is_trivially_copyable_v<TriangleParameters>);
static_assert(sizeof(float) == 4 && sizeof(TriangleParameters) == 32);
static_assert(alignof(TriangleParameters) == 16 && offsetof(TriangleParameters, tint) == 0 &&
              offsetof(TriangleParameters, scale) == 16 &&
              offsetof(TriangleParameters, reserved) == 20);

[[nodiscard]] inline bool IsValid(const TriangleParameters& parameters) noexcept
{
    if (!std::isfinite(parameters.scale) || parameters.scale < 0.01F || parameters.scale > 2.0F)
    {
        return false;
    }
    for (float value : parameters.tint)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
        {
            return false;
        }
    }
    for (float value : parameters.reserved)
    {
        if (value != 0.0F)
        {
            return false;
        }
    }
    return true;
}

struct ProfileOptions
{
    bool enabled = false;
    std::uint32_t warmup_frames = 30;
    std::uint32_t sample_frames = 120;
};
[[nodiscard]] inline bool IsValid(const ProfileOptions& options) noexcept
{
    return options.warmup_frames <= 100000 && options.sample_frames > 0 &&
           options.sample_frames <= 100000;
}
} // namespace tinyme
