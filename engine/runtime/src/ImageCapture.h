#pragma once

#include <tinyme/platform/windows/Window.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace tinyme
{
// Packed RGBA8, first row at the top; CPU-only WIC encoding, no window or GPU.
[[nodiscard]] std::vector<std::uint8_t> EncodePng(WindowSize size,
                                                  std::span<const std::uint8_t> rgba);
// Encodes in memory, then CREATE_NEW writes without overwriting an existing file.
void SaveCapturePng(const std::filesystem::path& path, WindowSize size,
                    std::span<const std::uint8_t> rgba);
} // namespace tinyme
