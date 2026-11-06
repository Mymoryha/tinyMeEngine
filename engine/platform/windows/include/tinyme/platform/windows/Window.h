#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string_view>

namespace tinyme
{
struct WindowSize
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

// Owns one HWND on its creating thread. Close requests defer destruction until GPU shutdown.
class Window
{
  public:
    explicit Window(std::wstring_view title, WindowSize size = {1280, 720});
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    [[nodiscard]] HWND GetHandle() const noexcept;
    [[nodiscard]] WindowSize GetClientSize() const noexcept;
    [[nodiscard]] bool IsMinimized() const noexcept;
    [[nodiscard]] bool IsCloseRequested() const noexcept;
    [[nodiscard]] UINT GetDpi() const noexcept;
    // Retains resize/minimize/DPI transitions even when one message pump restores the old state.
    [[nodiscard]] std::uint64_t GetSurfaceRevision() const noexcept;
    void PumpMessages();
    void WaitForMessages(DWORD timeout_ms) const;
    void SetClientSize(WindowSize size);
    void Minimize() noexcept;
    void Restore() noexcept;
    void RequestClose();
    [[nodiscard]] bool ConsumeShaderReloadRequest() noexcept;

  private:
    static LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wparam,
                                            LPARAM lparam) noexcept;
    void Release() noexcept;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    DPI_AWARENESS_CONTEXT previous_dpi_ = nullptr;
    WindowSize size_;
    std::uint64_t surface_revision_ = 0;
    bool owns_class_ = false;
    bool minimized_ = false;
    bool close_requested_ = false;
    bool shader_reload_requested_ = false;
    DWORD callback_error_ = 0;
};
} // namespace tinyme
