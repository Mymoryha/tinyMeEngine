#pragma once

#include <tinyme/render/d3d12/ClearRenderer.h>
#include <tinyme/runtime/WindowedSession.h>

#include <span>
#include <string>
#include <windows.h>

namespace tinyme
{
struct ProfileStatus
{
    bool complete = false;
    bool environment_changed = false;
    bool closed_by_user = false;
    std::uint64_t dropped = 0;
    std::uint64_t validation_warnings = 0;
};

[[nodiscard]] std::string
BuildProfileReport(const WindowedRunOptions& options, const RendererInfo& info,
                   WindowSize initial_size, std::wstring_view application, std::uint64_t generation,
                   bool debug_layer, ProfileStatus status, std::span<const FrameTiming> samples,
                   TriangleGeometryInfo geometry = {});
// CREATE_NEW: never overwrites an existing user report. Parent directory must exist.
void SaveProfileReport(const std::filesystem::path& path, std::string_view json);

// Per-call seam for report I/O failure tests; not a public runtime API.
struct ProfileFileOperations
{
    decltype(&::WriteFile) write = &::WriteFile;
    decltype(&::FlushFileBuffers) flush = &::FlushFileBuffers;
    decltype(&::SetFileInformationByHandle) dispose = &::SetFileInformationByHandle;
    decltype(&::CloseHandle) close = &::CloseHandle;
};
void SaveProfileReport(const std::filesystem::path& path, std::string_view json,
                       const ProfileFileOperations& operations);
} // namespace tinyme
