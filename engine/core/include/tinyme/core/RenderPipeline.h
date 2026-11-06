#pragma once

#include <cstdint>
#include <string_view>

namespace tinyme
{
enum class PostProcess : std::uint32_t
{
    None,
    Copy,
    Grayscale
};
[[nodiscard]] constexpr bool IsValid(PostProcess value) noexcept
{
    return value == PostProcess::None || value == PostProcess::Copy ||
           value == PostProcess::Grayscale;
}
[[nodiscard]] constexpr std::string_view ToString(PostProcess value) noexcept
{
    switch (value)
    {
    case PostProcess::None:
        return "none";
    case PostProcess::Copy:
        return "copy";
    case PostProcess::Grayscale:
        return "grayscale";
    }
    return "invalid";
}
inline constexpr std::uint32_t kMaxRenderDimension = 2048;
} // namespace tinyme
