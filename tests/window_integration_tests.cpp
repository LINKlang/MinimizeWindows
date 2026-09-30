#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <imm.h>

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <algorithm>
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

struct TestMonitor {
    HMONITOR handle;
    MONITORINFOEXW info;
};

BOOL CALLBACK CollectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM data)
{
    TestMonitor monitor{handle, {}};
    monitor.info.cbSize = sizeof(monitor.info);
    if (!GetMonitorInfoW(handle, &monitor.info)) { return FALSE; }
    reinterpret_cast<std::vector<TestMonitor>*>(data)->push_back(monitor);
    return TRUE;
}

bool GetSpanningRect(const RECT& target, const RECT& other, RECT& rect)
{
    const LONG top = std::max(target.top, other.top);
    const LONG bottom = std::min(target.bottom, other.bottom);
    const LONG left = std::max(target.left, other.left);
    const LONG right = std::min(target.right, other.right);
    if (bottom - top > 300 && (target.right == other.left || target.left == other.right)) {
        const LONG seam = target.right == other.left ? target.right : target.left;
        rect = {seam - (target.right == other.left ? 300 : 100), top + 40,
            seam + (target.right == other.left ? 100 : 300), top + 240};
        return true;
    }
    if (right - left > 500 && (target.bottom == other.top || target.top == other.bottom)) {
        const LONG seam = target.bottom == other.top ? target.bottom : target.top;
        rect = {left + 40, seam - (target.bottom == other.top ? 150 : 50), left + 440,
            seam + (target.bottom == other.top ? 50 : 150)};
        return true;
    }
    return false;
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

int wmain(int argument_count, wchar_t* arguments[])
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
    // Fixtures do not accept text input. Prevent system IME UI from becoming
    // extra visible windows while their async minimize/restore messages run.
    ImmDisableIME(0);
    using SetContext = HANDLE (WINAPI*)(HANDLE);
    const auto set_context = reinterpret_cast<SetContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"),
        "SetThreadDpiAwarenessContext"));
    const HANDLE previous_context = set_context != nullptr
        ? set_context(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-3))) : nullptr;
    KeyboardHook keyboard_hook;
    Check(keyboard_hook.Install(), "install native keyboard hook on isolated desktop");

    const HINSTANCE module = GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.hInstance = module;
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.lpszClassName = L"MinimizeWindowsIntegrationTest";
    Check(RegisterClassW(&window_class) != 0, "register test window class");

    std::vector<TestMonitor> monitors;
    Check(EnumDisplayMonitors(nullptr, nullptr, CollectMonitor,
        reinterpret_cast<LPARAM>(&monitors)) != FALSE && !monitors.empty(), "enumerate monitors");
    const auto primary = std::find_if(monitors.begin(), monitors.end(), [](const TestMonitor& monitor) {
        return (monitor.info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    });
    Check(primary != monitors.end(), "find primary monitor for startup tests");
    std::iter_swap(monitors.begin(), primary);
    const auto create_window = [&](const RECT& bounds, DWORD ex_style = 0) {
        HWND window = CreateWindowExW(ex_style, window_class.lpszClassName, L"Test window",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, bounds.left + 80, bounds.top + 80,
            400, 300, nullptr, nullptr, module, nullptr);
        Check(window != nullptr, "create test window");
        return window;
    };

    const MonitorTarget target{monitors.front().info.szDevice};
    const HWND chrome = create_window(monitors.front().info.rcWork);
    const HWND code = create_window(monitors.front().info.rcWork);
    const HWND manual = create_window(monitors.front().info.rcWork);
    const HWND tool = create_window(monitors.front().info.rcWork, WS_EX_TOOLWINDOW);
    HWND foreign = nullptr;
    HWND foreign_manual = nullptr;
    HWND spanning = nullptr;
    if (monitors.size() > 1) {
        foreign = create_window(monitors[1].info.rcWork);
        foreign_manual = create_window(monitors[1].info.rcWork);
        ShowWindow(foreign_manual, SW_MINIMIZE);
        RECT rect;
        if (GetSpanningRect(monitors.front().info.rcMonitor, monitors[1].info.rcMonitor, rect)) {
            spanning = CreateWindowExW(0, window_class.lpszClassName, L"Spanning test window",
                WS_OVERLAPPEDWINDOW | WS_VISIBLE, rect.left, rect.top,
                rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, module, nullptr);
            Check(spanning != nullptr
                && MonitorFromWindow(spanning, MONITOR_DEFAULTTONEAREST) == monitors.front().handle,
                "spanning window belongs to the monitor with most intersection");
        }
    }
    ShowWindow(code, SW_SHOWMAXIMIZED);
    ShowWindow(manual, SW_MINIMIZE);
    RECT chrome_bounds;
    Check(GetWindowRect(chrome, &chrome_bounds) != FALSE, "read original window bounds");

    DesktopManager manager;
    manager.ToggleDesktop(target);
    WaitFor([&] { return IsIconic(chrome) && IsIconic(code) && IsIconic(manual)
        && (spanning == nullptr || IsIconic(spanning)); }, "minimize only target monitor windows");
    Check(!IsIconic(tool), "leave tool window alone");
    Check(foreign == nullptr || (!IsIconic(foreign) && IsIconic(foreign_manual)),
        "other monitor retains visible and manually minimized windows");

    const HWND notepad = create_window(monitors.front().info.rcWork);
    manager.ToggleDesktop(target);
    WaitFor([&] { return IsIconic(chrome) && IsIconic(code) && IsIconic(manual) && IsIconic(notepad); },
        "new window causes another minimize");
    Check(IsIconic(chrome) && IsIconic(code), "previous windows stay minimized");

    DesktopManager fresh_manager;
    fresh_manager.ToggleDesktop(target);
    WaitFor([&] { return !IsIconic(chrome) && !IsIconic(code)
        && !IsIconic(manual) && !IsIconic(notepad) && (spanning == nullptr || !IsIconic(spanning)); },
        "restore all target windows without history despite visible windows on another monitor");
    Check(foreign == nullptr || (!IsIconic(foreign) && IsIconic(foreign_manual)),
        "target restoration does not restore other monitor's minimized windows");
    Check(IsZoomed(code) != FALSE, "restore maximized window as maximized");
    RECT restored_bounds;
    Check(GetWindowRect(chrome, &restored_bounds) != FALSE
        && EqualRect(&chrome_bounds, &restored_bounds), "restore normal window position and size");

    if (foreign != nullptr) {
        manager.ToggleDesktop(target);
        WaitFor([&] { return IsIconic(chrome) && IsIconic(code) && IsIconic(manual) && IsIconic(notepad); },
            "prepare first monitor as clean desktop");
        const MonitorTarget second{monitors[1].info.szDevice};
        manager.ToggleDesktop(second);
        WaitFor([&] { return IsIconic(foreign) && IsIconic(foreign_manual); }, "minimize non-primary target");
        manager.ToggleDesktop(second);
        WaitFor([&] { return !IsIconic(foreign) && !IsIconic(foreign_manual); }, "restore non-primary target");
        Check(IsIconic(chrome) && IsIconic(code) && IsIconic(notepad),
            "switching the target never restores the first monitor");
    }

    if (argument_count > 1) {
        // Starting a GUI process may create visible IME overlays on the desktop.
        // Start with ordinary windows visible so the first command is minimize.
        for (const HWND window : {chrome, code, notepad, spanning, foreign}) {
            if (window != nullptr) { ShowWindow(window, SW_RESTORE); }
        }
        ShowWindow(manual, SW_MINIMIZE);
        if (foreign_manual != nullptr) { ShowWindow(foreign_manual, SW_MINIMIZE); }
        WaitFor([&] { return !IsIconic(chrome) && !IsIconic(code) && IsIconic(manual) && !IsIconic(notepad)
            && (spanning == nullptr || !IsIconic(spanning)); }, "prepare native app startup test");

        // All child hooks/windows remain on this private desktop. The job also
        // guarantees a failed test cannot leave an orphaned application running.
        const HANDLE child_job = CreateJobObjectW(nullptr, nullptr);
        Check(child_job != nullptr, "create child cleanup job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        Check(SetInformationJobObject(child_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE,
            "configure child cleanup job");
        const auto run_child = [&](const std::wstring& options, const auto& first_state, const auto& second_state) {
            std::wstring command = std::wstring(L"\"") + arguments[1] + L"\"" + options;
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.lpDesktop = desktop_name;
            PROCESS_INFORMATION child{};
            Check(CreateProcessW(arguments[1], &command[0], nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                nullptr, nullptr, &startup, &child) != FALSE, "start app on the isolated desktop");
            if (!AssignProcessToJobObject(child_job, child.hProcess)) {
                TerminateProcess(child.hProcess, 1);
                CloseHandle(child.hThread);
                CloseHandle(child.hProcess);
                Check(false, "assign app to cleanup job");
            }
            Check(ResumeThread(child.hThread) != static_cast<DWORD>(-1), "resume isolated app");
            Check(WaitForInputIdle(child.hProcess, 5000) == 0, "app installs hook and enters message loop");
            Check(PostThreadMessageW(child.dwThreadId, KeyboardHook::WinDMessage, 0, 0) != FALSE,
                "notify native app of first Win+D");
            WaitFor(first_state, "native app selects the expected monitor on first message");
            Check(PostThreadMessageW(child.dwThreadId, KeyboardHook::WinDMessage, 0, 0) != FALSE,
                "notify native app of second Win+D");
            WaitFor(second_state, "native app selects the expected monitor on second message");
            Check(PostThreadMessageW(child.dwThreadId, WM_QUIT, 0, 0) != FALSE, "request normal app shutdown");
            Check(WaitForSingleObject(child.hProcess, 5000) == WAIT_OBJECT_0, "native app exits");
            DWORD exit_code = 1;
            Check(GetExitCodeProcess(child.hProcess, &exit_code) != FALSE && exit_code == 0,
                "native app returns success");
            CloseHandle(child.hThread);
            CloseHandle(child.hProcess);
        };
        run_child(L"", [&] { return IsIconic(chrome) && IsIconic(code) && IsIconic(manual) && IsIconic(notepad)
            && (foreign == nullptr || (!IsIconic(foreign) && IsIconic(foreign_manual))); },
            [&] { return !IsIconic(chrome) && !IsIconic(code) && !IsIconic(manual) && !IsIconic(notepad)
                && (foreign == nullptr || (!IsIconic(foreign) && IsIconic(foreign_manual))); });
        if (foreign != nullptr) {
            std::wstring device = monitors[1].info.szDevice;
            for (auto& letter : device) {
                if (letter >= L'A' && letter <= L'Z') { letter += L'a' - L'A'; }
            }
            run_child(L" --monitor \"" + device + L"\"",
                [&] { return IsIconic(foreign) && IsIconic(foreign_manual)
                    && !IsIconic(chrome) && !IsIconic(code) && !IsIconic(notepad); },
                [&] { return !IsIconic(foreign) && !IsIconic(foreign_manual)
                    && !IsIconic(chrome) && !IsIconic(code) && !IsIconic(notepad); });
        }
        CloseHandle(child_job);
    }

    for (const HWND window : {chrome, code, manual, tool, notepad, foreign, foreign_manual, spanning}) {
        if (window != nullptr) { Check(DestroyWindow(window) != FALSE, "destroy test window"); }
    }
    UnregisterClassW(window_class.lpszClassName, module);
    keyboard_hook.Uninstall();
    if (previous_context != nullptr) { set_context(previous_context); }
    Check(SetThreadDesktop(original_desktop) != FALSE, "detach from test desktop");
    Check(CloseDesktop(test_desktop) != FALSE, "close isolated desktop");
    std::printf("Native window/hook/app tests passed (%zu monitor(s), target isolation and normal/maximized restore).\n",
        monitors.size());
    return 0;
}
