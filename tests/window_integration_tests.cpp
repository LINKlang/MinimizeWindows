#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>

#include "../src/desktop_manager.h"
#include "../src/keyboard_hook.h"

namespace {

void Check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s (Win32 error %lu)\n", message, GetLastError());
        std::exit(1);
    }
}

BOOL CALLBACK CollectMonitor(HMONITOR, HDC, LPRECT bounds, LPARAM data)
{
    reinterpret_cast<std::vector<RECT>*>(data)->push_back(*bounds);
    return TRUE;
}

template<class Predicate>
void WaitFor(Predicate predicate, const char* message)
{
    const ULONGLONG deadline = GetTickCount64() + 5000;
    do {
        MSG event;
        while (PeekMessageW(&event, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&event);
            DispatchMessageW(&event);
        }
        if (predicate()) {
            return;
        }
        Sleep(10);
    } while (GetTickCount64() < deadline);
    EnumWindows([](HWND window, LPARAM) -> BOOL {
        wchar_t name[128];
        GetClassNameW(window, name, ARRAYSIZE(name));
        DWORD cloaked = 0;
        DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        DWORD process_id = 0;
        GetWindowThreadProcessId(window, &process_id);
        std::fwprintf(stderr, L"Window %p class=%ls pid=%lu visible=%d minimized=%d cloaked=%lu exstyle=%lx\n",
            window, name, process_id, IsWindowVisible(window), IsIconic(window), cloaked,
            static_cast<unsigned long>(GetWindowLongPtrW(window, GWL_EXSTYLE)));
        return TRUE;
    }, 0);
    Check(false, message);
}

} // namespace

int main()
{
    // Use a private, never activated desktop so user windows are not enumerated.
    const HDESK original_desktop = GetThreadDesktop(GetCurrentThreadId());
    wchar_t desktop_name[64];
    swprintf_s(desktop_name, L"MinimizeWindowsTests_%lu", GetCurrentProcessId());
    const HDESK test_desktop = CreateDesktopW(desktop_name, nullptr, nullptr, 0,
        DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS
            | DESKTOP_HOOKCONTROL, nullptr);
    Check(test_desktop != nullptr, "create isolated test desktop");
    Check(SetThreadDesktop(test_desktop) != FALSE, "attach to isolated desktop");
    KeyboardHook keyboard_hook;
    Check(keyboard_hook.Install(), "install native keyboard hook on isolated desktop");

    const HINSTANCE module = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.hInstance = module;
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.lpszClassName = L"MinimizeWindowsIntegrationTest";
    Check(RegisterClassW(&window_class) != 0, "register test window class");

    std::vector<RECT> monitors;
    Check(EnumDisplayMonitors(nullptr, nullptr, CollectMonitor,
        reinterpret_cast<LPARAM>(&monitors)) != FALSE && !monitors.empty(), "enumerate monitors");
    const auto create_window = [&](const RECT& bounds, DWORD ex_style = 0) {
        HWND window = CreateWindowExW(ex_style, window_class.lpszClassName, L"Test window",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, bounds.left + 80, bounds.top + 80,
            400, 300, nullptr, nullptr, module, nullptr);
        Check(window != nullptr, "create test window");
        return window;
    };

    const HWND chrome = create_window(monitors.front());
    const HWND code = create_window(monitors.size() > 1 ? monitors[1] : monitors.front());
    const HWND manual = create_window(monitors.front());
    const HWND tool = create_window(monitors.front(), WS_EX_TOOLWINDOW);
    ShowWindow(code, SW_SHOWMAXIMIZED);
    ShowWindow(manual, SW_MINIMIZE);
    RECT chrome_bounds;
    Check(GetWindowRect(chrome, &chrome_bounds) != FALSE, "read original window bounds");

    DesktopManager manager;
    manager.ToggleDesktop();
    WaitFor([&] { return IsIconic(chrome) && IsIconic(code) && IsIconic(manual); },
        "minimize ordinary windows across monitors");
    Check(!IsIconic(tool), "leave tool window alone");

    const HWND notepad = create_window(monitors.front());
    manager.ToggleDesktop();
    WaitFor([&] { return IsIconic(chrome) && IsIconic(code) && IsIconic(manual) && IsIconic(notepad); },
        "new window causes another minimize");
    Check(IsIconic(chrome) && IsIconic(code), "previous windows stay minimized");

    DesktopManager fresh_manager;
    fresh_manager.ToggleDesktop();
    WaitFor([&] { return !IsIconic(chrome) && !IsIconic(code)
        && !IsIconic(manual) && !IsIconic(notepad); }, "restore all windows without history");
    Check(IsZoomed(code) != FALSE, "restore maximized window as maximized");
    RECT restored_bounds;
    Check(GetWindowRect(chrome, &restored_bounds) != FALSE
        && EqualRect(&chrome_bounds, &restored_bounds), "restore normal window position and size");

    for (const HWND window : {chrome, code, manual, tool, notepad}) {
        Check(DestroyWindow(window) != FALSE, "destroy test window");
    }
    UnregisterClassW(window_class.lpszClassName, module);
    keyboard_hook.Uninstall();
    Check(SetThreadDesktop(original_desktop) != FALSE, "detach from test desktop");
    Check(CloseDesktop(test_desktop) != FALSE, "close isolated desktop");
    std::printf("Native window/hook tests passed (%zu monitor(s), normal and maximized restore).\n", monitors.size());
    return 0;
}
