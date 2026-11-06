#include <tinyme/platform/windows/Window.h>

#include <stdexcept>
#include <string>

namespace tinyme
{
namespace
{
constexpr wchar_t kWindowClass[] = L"TinyMeEngine.Window.M1";
constexpr DWORD kWindowStyle = WS_OVERLAPPEDWINDOW;

void CheckWindows(BOOL success, const char* operation)
{
    if (!success)
    {
        throw std::runtime_error(std::string(operation) +
                                 " failed; Win32 error=" + std::to_string(GetLastError()));
    }
}

RECT MakeWindowRect(WindowSize size, UINT dpi)
{
    if (size.width == 0 || size.height == 0 || size.width > 16384 || size.height > 16384)
    {
        throw std::invalid_argument("Window client size must be in [1, 16384].");
    }
    RECT rect{0, 0, static_cast<LONG>(size.width), static_cast<LONG>(size.height)};
    CheckWindows(AdjustWindowRectExForDpi(&rect, kWindowStyle, FALSE, 0, dpi),
                 "AdjustWindowRectExForDpi");
    return rect;
}
} // namespace

Window::Window(std::wstring_view title, WindowSize size)
{
    try
    {
        previous_dpi_ = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        CheckWindows(previous_dpi_ != nullptr, "SetThreadDpiAwarenessContext");
        instance_ = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class{};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = WindowProcedure;
        window_class.hInstance = instance_;
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = kWindowClass;
        owns_class_ = RegisterClassExW(&window_class) != 0;
        if (!owns_class_ && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            CheckWindows(FALSE, "RegisterClassExW");
        }
        const auto rect = MakeWindowRect(size, GetDpiForSystem());
        const std::wstring owned_title(title);
        hwnd_ = CreateWindowExW(0, kWindowClass, owned_title.c_str(), kWindowStyle, CW_USEDEFAULT,
                                CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                                nullptr, nullptr, instance_, this);
        CheckWindows(hwnd_ != nullptr, "CreateWindowExW");
        SetClientSize(size);
        ShowWindow(hwnd_, SW_SHOW);
        // The first ShowWindow can obey the parent's STARTUPINFO instead of SW_SHOW.
        ShowWindow(hwnd_, SW_RESTORE);
        RECT client{};
        CheckWindows(GetClientRect(hwnd_, &client), "Initial GetClientRect");
        size_ = {static_cast<std::uint32_t>(client.right - client.left),
                 static_cast<std::uint32_t>(client.bottom - client.top)};
        minimized_ = IsIconic(hwnd_) != FALSE;
    }
    catch (...)
    {
        Release();
        throw;
    }
}

Window::~Window()
{
    Release();
}

void Window::Release() noexcept
{
    if (hwnd_)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (owns_class_)
    {
        UnregisterClassW(kWindowClass, instance_);
        owns_class_ = false;
    }
    if (previous_dpi_)
    {
        SetThreadDpiAwarenessContext(previous_dpi_);
        previous_dpi_ = nullptr;
    }
}

HWND Window::GetHandle() const noexcept
{
    return hwnd_;
}

WindowSize Window::GetClientSize() const noexcept
{
    return size_;
}

bool Window::IsMinimized() const noexcept
{
    return minimized_;
}

bool Window::IsCloseRequested() const noexcept
{
    return close_requested_;
}

UINT Window::GetDpi() const noexcept
{
    return GetDpiForWindow(hwnd_);
}

void Window::PumpMessages()
{
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        if (message.message == WM_QUIT)
        {
            close_requested_ = true;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (callback_error_)
    {
        throw std::runtime_error("Window callback failed; Win32 error=" +
                                 std::to_string(callback_error_));
    }
}

void Window::WaitForMessages(DWORD timeout_ms) const
{
    const auto result =
        MsgWaitForMultipleObjectsEx(0, nullptr, timeout_ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    CheckWindows(result != WAIT_FAILED, "MsgWaitForMultipleObjectsEx");
}

void Window::SetClientSize(WindowSize size)
{
    const auto rect = MakeWindowRect(size, GetDpi());
    CheckWindows(SetWindowPos(hwnd_, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                              SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE),
                 "SetWindowPos");
}

void Window::Minimize() noexcept
{
    ShowWindow(hwnd_, SW_MINIMIZE);
}

void Window::Restore() noexcept
{
    ShowWindow(hwnd_, SW_RESTORE);
}

void Window::RequestClose()
{
    CheckWindows(PostMessageW(hwnd_, WM_CLOSE, 0, 0), "PostMessageW(WM_CLOSE)");
}

bool Window::ConsumeShaderReloadRequest() noexcept
{
    const bool requested = shader_reload_requested_;
    shader_reload_requested_ = false;
    return requested;
}

LRESULT CALLBACK Window::WindowProcedure(HWND hwnd, UINT message, WPARAM wparam,
                                         LPARAM lparam) noexcept
{
    auto* window = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        window = static_cast<Window*>(creation->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }
    if (window)
    {
        switch (message)
        {
        case WM_CLOSE:
            window->close_requested_ = true;
            return 0;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE)
            {
                window->close_requested_ = true;
                return 0;
            }
            if (wparam == VK_F5 && !(lparam & (1LL << 30)))
            {
                window->shader_reload_requested_ = true;
                return 0;
            }
            break;
        case WM_SIZE:
            window->minimized_ = wparam == SIZE_MINIMIZED;
            window->size_ = {LOWORD(lparam), HIWORD(lparam)};
            return 0;
        case WM_DPICHANGED:
        {
            const auto* rect = reinterpret_cast<const RECT*>(lparam);
            if (!SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left,
                              rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE))
            {
                window->callback_error_ = GetLastError();
                window->close_requested_ = true;
            }
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_NCDESTROY:
            window->close_requested_ = true;
            window->hwnd_ = nullptr;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
} // namespace tinyme
