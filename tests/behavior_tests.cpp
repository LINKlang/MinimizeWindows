#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#undef GetWindowLongPtrW

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace test {

void Check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

struct Window {
    bool alive = true;
    bool visible = true;
    bool minimized = false;
    LONG_PTR style = 0;
    LONG_PTR ex_style = 0;
    std::wstring class_name = L"TestApplication";
    DWORD cloaked = 0;
    HRESULT dwm_result = S_OK;
    bool fail_show = false;
    HMONITOR monitor = nullptr;
};

struct Monitor {
    std::wstring device_name;
    RECT bounds;
    bool alive = true;
    bool fail_info = false;
};

std::deque<Monitor> monitors;
std::deque<Window> windows;
std::vector<std::pair<HWND, int>> commands;
HWND desktop = nullptr;
HWND shell = nullptr;
int enum_calls = 0;
int fail_enum_call = 0;
void (*before_second_enum)() = nullptr;
void (*after_first_enum)() = nullptr;
bool fail_monitor_enum = false;
int monitor_enum_calls = 0;

HMONITOR AddMonitor(const wchar_t* device_name, RECT bounds)
{
    monitors.push_back({device_name, bounds});
    return reinterpret_cast<HMONITOR>(&monitors.back());
}

HMONITOR MonitorAt(size_t index)
{
    return reinterpret_cast<HMONITOR>(&monitors[index]);
}

HWND AddWindow(HMONITOR monitor = nullptr)
{
    windows.emplace_back();
    windows.back().monitor = monitor != nullptr ? monitor : MonitorAt(0);
    return reinterpret_cast<HWND>(&windows.back());
}

Window& GetWindow(HWND hwnd)
{
    return *reinterpret_cast<Window*>(hwnd);
}

BOOL WINAPI IsWindow(HWND hwnd)
{
    for (auto& window : windows) {
        if (reinterpret_cast<HWND>(&window) == hwnd) {
            return window.alive;
        }
    }
    return FALSE;
}

BOOL WINAPI IsWindowVisible(HWND hwnd) { return GetWindow(hwnd).visible; }
BOOL WINAPI IsIconic(HWND hwnd) { return GetWindow(hwnd).minimized; }
HWND WINAPI GetDesktopWindow() { return desktop; }
HWND WINAPI GetShellWindow() { return shell; }

LONG_PTR WINAPI GetWindowLongPtrW(HWND hwnd, int index)
{
    return index == GWL_STYLE ? GetWindow(hwnd).style : GetWindow(hwnd).ex_style;
}

int WINAPI GetClassNameW(HWND hwnd, LPWSTR output, int capacity)
{
    const auto& name = GetWindow(hwnd).class_name;
    if (static_cast<int>(name.size()) >= capacity) {
        return 0;
    }
    std::wmemcpy(output, name.c_str(), name.size() + 1);
    return static_cast<int>(name.size());
}

HRESULT WINAPI DwmGetWindowAttribute(HWND hwnd, DWORD, PVOID output, DWORD)
{
    const auto& window = GetWindow(hwnd);
    *static_cast<DWORD*>(output) = window.cloaked;
    return window.dwm_result;
}

BOOL WINAPI EnumDisplayMonitors(HDC dc, LPCRECT clip, MONITORENUMPROC callback, LPARAM data)
{
    Check(dc == nullptr && clip == nullptr, "resolve target across the whole desktop");
    ++monitor_enum_calls;
    if (fail_monitor_enum) { return FALSE; }
    for (auto& monitor : monitors) {
        if (monitor.alive && !callback(reinterpret_cast<HMONITOR>(&monitor), nullptr, &monitor.bounds, data)) {
            return FALSE;
        }
    }
    return TRUE;
}

BOOL WINAPI GetMonitorInfoW(HMONITOR handle, LPMONITORINFOEXW info)
{
    for (auto& monitor : monitors) {
        if (reinterpret_cast<HMONITOR>(&monitor) == handle && monitor.alive && !monitor.fail_info) {
            Check(info->cbSize == sizeof(*info), "monitor info size initialized");
            info->rcMonitor = info->rcWork = monitor.bounds;
            wcscpy_s(info->szDevice, monitor.device_name.c_str());
            return TRUE;
        }
    }
    return FALSE;
}

HMONITOR WINAPI MonitorFromWindow(HWND hwnd, DWORD flags)
{
    Check(flags == MONITOR_DEFAULTTONEAREST, "use largest intersection or nearest monitor");
    // The OS uses the pre-minimize rectangle; a minimized window keeps its monitor.
    const HMONITOR handle = GetWindow(hwnd).monitor;
    for (auto& monitor : monitors) {
        if (reinterpret_cast<HMONITOR>(&monitor) == handle && monitor.alive) { return handle; }
    }
    return nullptr;
}

BOOL WINAPI EnumWindows(WNDENUMPROC callback, LPARAM data)
{
    ++enum_calls;
    if (enum_calls == 2 && before_second_enum != nullptr) {
        before_second_enum();
    }
    if (enum_calls == fail_enum_call) {
        return FALSE;
    }
    for (auto& window : windows) {
        if (!callback(reinterpret_cast<HWND>(&window), data)) {
            return FALSE;
        }
    }
    if (enum_calls == 1 && after_first_enum != nullptr) { after_first_enum(); }
    return TRUE;
}

BOOL WINAPI ShowWindowAsync(HWND hwnd, int command)
{
    commands.emplace_back(hwnd, command);
    auto& window = GetWindow(hwnd);
    if (!window.alive || window.fail_show) {
        return FALSE;
    }
    window.minimized = command == SW_MINIMIZE;
    return TRUE;
}

void ResetWindows()
{
    windows.clear();
    monitors.clear();
    AddMonitor(L"\\\\.\\DISPLAY1", {0, 0, 2560, 1440});
    AddMonitor(L"\\\\.\\DISPLAY2", {-1080, -559, 0, 1361});
    commands.clear();
    desktop = nullptr;
    shell = nullptr;
    enum_calls = 0;
    fail_enum_call = 0;
    before_second_enum = nullptr;
    after_first_enum = nullptr;
    fail_monitor_enum = false;
    monitor_enum_calls = 0;
}

std::array<SHORT, 256> keys{};
HOOKPROC keyboard_proc = nullptr;
bool fail_install = false;
bool post_succeeds = true;
int install_calls = 0;
int uninstall_calls = 0;
int posted_messages = 0;
int dummy_events = 0;
constexpr LRESULT PassedThrough = 73;

HHOOK WINAPI SetWindowsHookExW(int kind, HOOKPROC proc, HINSTANCE module, DWORD thread)
{
    Check(kind == WH_KEYBOARD_LL && module == nullptr && thread == 0,
        "hook installation parameters");
    ++install_calls;
    if (fail_install) {
        SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
    keyboard_proc = proc;
    return reinterpret_cast<HHOOK>(1);
}

BOOL WINAPI UnhookWindowsHookEx(HHOOK) { ++uninstall_calls; return TRUE; }
BOOL WINAPI PeekMessageW(LPMSG, HWND, UINT, UINT, UINT) { return FALSE; }
DWORD WINAPI GetCurrentThreadId() { return 42; }
SHORT WINAPI GetAsyncKeyState(int key) { return keys[key]; }
LRESULT WINAPI CallNextHookEx(HHOOK, int, WPARAM, LPARAM) { return PassedThrough; }

BOOL WINAPI PostThreadMessageW(DWORD thread, UINT message, WPARAM, LPARAM)
{
    Check(thread == 42 && message == WM_APP + 1, "notification destination");
    if (!post_succeeds) {
        return FALSE;
    }
    ++posted_messages;
    return TRUE;
}

LRESULT Key(DWORD key, WPARAM message, int code = HC_ACTION)
{
    KBDLLHOOKSTRUCT event{};
    event.vkCode = key;
    return keyboard_proc(code, message, reinterpret_cast<LPARAM>(&event));
}

UINT WINAPI SendInput(UINT count, LPINPUT input, int size)
{
    Check(count == 2 && size == sizeof(INPUT), "dummy key event parameters");
    Check(input[0].type == INPUT_KEYBOARD && input[1].type == INPUT_KEYBOARD
        && input[0].ki.wVk == 0xFF && input[1].ki.wVk == 0xFF
        && input[0].ki.dwFlags == 0 && input[1].ki.dwFlags == KEYEVENTF_KEYUP,
        "dummy key uses a balanced down/up pair");
    Check(Key(0xFF, WM_KEYDOWN) == PassedThrough && Key(0xFF, WM_KEYUP) == PassedThrough,
        "injected dummy events pass through without triggering the hook");
    dummy_events += static_cast<int>(count);
    return count;
}

void Hold(int key) { keys[key] = static_cast<SHORT>(0x8000); }

} // namespace test

// Replace OS calls at this boundary; exercise the production implementations.
// No global hook is installed and no user window receives a command.
#undef GetWindowLongPtrW
#define IsWindow test::IsWindow
#define IsWindowVisible test::IsWindowVisible
#define IsIconic test::IsIconic
#define GetDesktopWindow test::GetDesktopWindow
#define GetShellWindow test::GetShellWindow
#define GetWindowLongPtrW test::GetWindowLongPtrW
#define GetClassNameW test::GetClassNameW
#define DwmGetWindowAttribute test::DwmGetWindowAttribute
#define EnumWindows test::EnumWindows
#define ShowWindowAsync test::ShowWindowAsync
#define EnumDisplayMonitors test::EnumDisplayMonitors
#define GetMonitorInfoW test::GetMonitorInfoW
#define MonitorFromWindow test::MonitorFromWindow
#include "../src/desktop_manager.cpp"
#undef IsWindow
#undef IsWindowVisible
#undef IsIconic
#undef GetDesktopWindow
#undef GetShellWindow
#undef GetWindowLongPtrW
#undef GetClassNameW
#undef DwmGetWindowAttribute
#undef EnumWindows
#undef ShowWindowAsync
#undef EnumDisplayMonitors
#undef GetMonitorInfoW
#undef MonitorFromWindow

#define SetWindowsHookExW test::SetWindowsHookExW
#define UnhookWindowsHookEx test::UnhookWindowsHookEx
#define PeekMessageW test::PeekMessageW
#define GetCurrentThreadId test::GetCurrentThreadId
#define GetAsyncKeyState test::GetAsyncKeyState
#define CallNextHookEx test::CallNextHookEx
#define PostThreadMessageW test::PostThreadMessageW
#define SendInput test::SendInput
#include "../src/keyboard_hook.cpp"
#undef SetWindowsHookExW
#undef UnhookWindowsHookEx
#undef PeekMessageW
#undef GetCurrentThreadId
#undef GetAsyncKeyState
#undef CallNextHookEx
#undef PostThreadMessageW
#undef SendInput

void TestDesktop()
{
    using namespace test;
    DesktopManager manager;
    const MonitorTarget target{L"\\\\.\\DISPLAY1"};
    const MonitorTarget second{L"\\\\.\\display2"};
    ResetWindows();
    const HWND chrome = AddWindow();
    const HWND code = AddWindow();
    const HWND foreign = AddWindow(MonitorAt(1));
    const HWND foreign_manual = AddWindow(MonitorAt(1));
    GetWindow(foreign_manual).minimized = true;
    manager.ToggleDesktop(target);
    Check(GetWindow(chrome).minimized && GetWindow(code).minimized,
        "all target ordinary windows minimized");
    Check(!GetWindow(foreign).minimized && GetWindow(foreign_manual).minimized,
        "other monitor remains unchanged during minimize");
    const HWND notepad = AddWindow();
    commands.clear();
    manager.ToggleDesktop(target);
    Check(commands.size() == 1 && commands[0].first == notepad
        && commands[0].second == SW_MINIMIZE, "new window causes another minimize");
    manager.ToggleDesktop(target);
    Check(!GetWindow(chrome).minimized && !GetWindow(code).minimized
        && !GetWindow(notepad).minimized, "foreign visible window does not stop target restoration");
    Check(GetWindow(foreign_manual).minimized, "restoration leaves other monitor's minimized windows alone");
    commands.clear();
    manager.ToggleDesktop(second);
    Check(commands.size() == 1 && commands[0].first == foreign && GetWindow(foreign_manual).minimized,
        "case-insensitive target selects a non-primary monitor");
    commands.clear();
    manager.ToggleDesktop(second);
    Check(commands.size() == 2 && !GetWindow(foreign).minimized && !GetWindow(foreign_manual).minimized,
        "second monitor restores all its minimized windows regardless of first monitor");

    ResetWindows();
    const HWND manual = AddWindow();
    GetWindow(manual).minimized = true;
    DesktopManager fresh_manager;
    fresh_manager.ToggleDesktop(target);
    Check(!GetWindow(manual).minimized && commands[0].second == SW_RESTORE,
        "fresh manager restores manually minimized window without history");

    ResetWindows();
    desktop = AddWindow();
    shell = AddWindow();
    for (const wchar_t* name : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"}) {
        GetWindow(AddWindow()).class_name = name;
    }
    GetWindow(AddWindow()).ex_style = WS_EX_TOOLWINDOW;
    GetWindow(AddWindow()).style = WS_CHILD;
    GetWindow(AddWindow()).visible = false;
    GetWindow(AddWindow()).cloaked = DWM_CLOAKED_SHELL;
    const HWND ordinary = AddWindow();
    GetWindow(ordinary).minimized = true;
    manager.ToggleDesktop(target);
    Check(commands.size() == 1 && commands[0].first == ordinary
        && commands[0].second == SW_RESTORE, "shell, hidden, tool, child and cloaked windows ignored");

    ResetWindows();
    const HWND unsupported_dwm = AddWindow();
    GetWindow(unsupported_dwm).dwm_result = E_INVALIDARG;
    manager.ToggleDesktop(target);
    Check(GetWindow(unsupported_dwm).minimized, "unsupported DWM query retains basic filtering");

    ResetWindows();
    AddWindow();
    fail_enum_call = 1;
    manager.ToggleDesktop(target);
    Check(commands.empty() && enum_calls == 1, "failed scan performs no window operation");
    enum_calls = 0;
    fail_enum_call = 2;
    manager.ToggleDesktop(target);
    Check(commands.empty(), "failed action enumeration is harmless");

    ResetWindows();
    const HWND failed = AddWindow();
    const HWND successful = AddWindow();
    GetWindow(failed).fail_show = true;
    manager.ToggleDesktop(target);
    Check(!GetWindow(failed).minimized && GetWindow(successful).minimized,
        "one failed operation does not block other windows");
    GetWindow(failed).fail_show = false;
    commands.clear();
    manager.ToggleDesktop(target);
    Check(commands.size() == 1 && commands[0].second == SW_MINIMIZE,
        "next press follows actual window state after failure");

    ResetWindows();
    AddWindow();
    AddWindow();
    before_second_enum = [] { windows.front().alive = false; };
    manager.ToggleDesktop(target);
    Check(commands.size() == 1 && windows.back().minimized,
        "window closed between enumerations is skipped");

    ResetWindows();
    AddWindow();
    const HWND stays = AddWindow();
    before_second_enum = [] { windows.front().monitor = MonitorAt(1); };
    manager.ToggleDesktop(target);
    Check(commands.size() == 1 && commands[0].first == stays,
        "window moved to another monitor between enumerations is skipped");

    ResetWindows();
    const HWND reconnect = AddWindow();
    manager.ToggleDesktop(target);
    commands.clear();
    monitors.front().alive = false;
    manager.ToggleDesktop(target);
    Check(commands.empty(), "disconnected target never falls back to another monitor");
    const HMONITOR replacement = AddMonitor(L"\\\\.\\DISPLAY1", {2000, -1000, 4560, 440});
    GetWindow(reconnect).monitor = replacement;
    manager.ToggleDesktop(target);
    Check(!GetWindow(reconnect).minimized && commands.size() == 1 && monitor_enum_calls == 3,
        "reconnected device is resolved to its new handle on every press");

    ResetWindows();
    AddWindow();
    fail_monitor_enum = true;
    manager.ToggleDesktop(target);
    Check(commands.empty() && enum_calls == 0, "failed monitor enumeration performs no window operation");
    fail_monitor_enum = false;
    monitors.back().fail_info = true;
    manager.ToggleDesktop(target);
    Check(commands.empty() && enum_calls == 0, "incomplete monitor lookup performs no window operation");

    ResetWindows();
    AddWindow();
    after_first_enum = [] { monitors.front().alive = false; };
    manager.ToggleDesktop(target);
    Check(commands.empty() && enum_calls == 1, "target disconnected during scan cancels the action");

    ResetWindows();
    AddWindow();
    after_first_enum = [] { monitors.front().device_name = L"\\\\.\\DISPLAY3"; };
    manager.ToggleDesktop(target);
    Check(commands.empty() && enum_calls == 1, "reassigned monitor handle during scan cancels the action");

    ResetWindows();
    manager.ToggleDesktop(MonitorTarget{});
    manager.ToggleDesktop(MonitorTarget{L"\\\\.\\DISPLAY99"});
    Check(commands.empty() && enum_calls == 0, "empty or unknown device is never interpreted as primary");
    manager.ToggleDesktop(target);
    Check(commands.empty(), "empty desktop is a no-op");
}

void TestKeyboard()
{
    using namespace test;
    KeyboardHook hook;
    fail_install = true;
    Check(!hook.Install() && GetLastError() == ERROR_ACCESS_DENIED,
        "installation failure reports the OS error");
    fail_install = false;
    Check(hook.Install(), "install after failure");
    const int calls = install_calls;
    Check(hook.Install() && install_calls == calls, "installation is idempotent");
    KeyboardHook other;
    Check(!other.Install() && GetLastError() == ERROR_ALREADY_EXISTS,
        "second active instance rejected");
    Check(Key('D', WM_KEYDOWN, -1) == PassedThrough, "negative hook code passed through");
    Check(Key('A', WM_KEYDOWN) == PassedThrough, "unrelated key passed through");
    Check(Key('D', WM_KEYDOWN) == PassedThrough && Key('D', WM_KEYUP) == PassedThrough,
        "plain D passed through");

    Hold(VK_LWIN);
    Check(Key('D', WM_KEYDOWN) == 1 && posted_messages == 1, "left Win+D intercepted");
    Check(dummy_events == 2, "handled Win+D suppresses standalone Win behavior");
    Check(Key('D', WM_KEYDOWN) == 1 && posted_messages == 1, "held D posts once");
    Check(dummy_events == 2, "held D injects no extra dummy events");
    keys.fill(0);
    Check(Key('D', WM_KEYDOWN) == 1 && Key('D', WM_KEYUP) == 1,
        "repeat and key-up stay intercepted after Win release");
    Check(Key('D', WM_KEYUP) == PassedThrough, "interception reset after key-up");

    Hold(VK_RWIN);
    Check(Key('D', WM_SYSKEYDOWN) == 1 && Key('D', WM_SYSKEYUP) == 1
        && posted_messages == 2, "right Win and system key messages supported");
    for (int modifier : {VK_CONTROL, VK_MENU, VK_SHIFT}) {
        Hold(modifier);
        Check(Key('D', WM_KEYDOWN) == PassedThrough && Key('D', WM_KEYUP) == PassedThrough,
            "additional modifier preserved");
        keys[modifier] = 0;
    }
    Check(posted_messages == 2, "other shortcuts do not notify desktop manager");

    post_succeeds = false;
    Check(Key('D', WM_KEYDOWN) == PassedThrough && Key('D', WM_KEYDOWN) == PassedThrough
        && Key('D', WM_KEYUP) == PassedThrough, "failed notification keeps full D gesture native");
    Check(dummy_events == 4, "failed notification injects no dummy events");
    post_succeeds = true;
    Check(Key('D', WM_KEYDOWN) == 1 && Key('D', WM_KEYUP) == 1
        && posted_messages == 3, "next gesture recovers after notification failure");

    keys.fill(0);
    Check(Key('D', WM_KEYDOWN) == PassedThrough, "ordinary D starts native");
    Hold(VK_LWIN);
    Check(Key('D', WM_KEYDOWN) == PassedThrough && Key('D', WM_KEYUP) == PassedThrough
        && posted_messages == 3, "adding Win during a held D does not intercept a partial gesture");

    hook.Uninstall();
    hook.Uninstall();
    Check(uninstall_calls == 1, "uninstall is idempotent");
    Check(other.Install(), "another instance can install after uninstall");
}

int main()
{
    TestDesktop();
    TestKeyboard();
    test::Check(test::uninstall_calls == 2, "destructor uninstalls the remaining hook");
    std::puts("Desktop and keyboard behavior tests passed.");
    return 0;
}
