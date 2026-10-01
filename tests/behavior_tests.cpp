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
    bool ignore_minimize = false;
    bool defer_minimize = false;
    bool fail_restore = false;
    DWORD process_id = 100;
    DWORD thread_id = 200;
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
WINEVENTPROC window_event_proc = nullptr;
HWINEVENTHOOK window_event_hook = nullptr;
bool fail_tracking = false;
DWORD tracking_error = ERROR_ACCESS_DENIED;
int tracking_installs = 0;
int tracking_uninstalls = 0;
DWORD current_thread = 42;
void (*during_show)(HWND, int) = nullptr;
void (*during_identity)(HWND) = nullptr;

void RestoreEvent(HWND hwnd, DWORD event = EVENT_SYSTEM_MINIMIZEEND,
    LONG object = OBJID_WINDOW, LONG child = CHILDID_SELF)
{
    if (window_event_proc != nullptr) {
        window_event_proc(window_event_hook, event, hwnd, object, child, 200, 0);
    }
}

HWINEVENTHOOK WINAPI SetWinEventHook(DWORD first, DWORD last, HMODULE module,
    WINEVENTPROC callback, DWORD process, DWORD thread, DWORD flags)
{
    Check(first == EVENT_SYSTEM_MINIMIZEEND && last == first && module == nullptr
        && process == 0 && thread == 0 && flags == WINEVENT_OUTOFCONTEXT,
        "track only minimize-end on the current desktop without injection");
    ++tracking_installs;
    if (fail_tracking) { SetLastError(tracking_error); return nullptr; }
    window_event_proc = callback;
    window_event_hook = reinterpret_cast<HWINEVENTHOOK>(static_cast<ULONG_PTR>(tracking_installs + 10));
    return window_event_hook;
}

BOOL WINAPI UnhookWinEvent(HWINEVENTHOOK hook)
{
    Check(hook == window_event_hook, "unhook the owned restore event hook");
    ++tracking_uninstalls;
    window_event_hook = nullptr;
    window_event_proc = nullptr;
    return TRUE;
}

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
DWORD WINAPI GetWindowThreadProcessId(HWND hwnd, LPDWORD process)
{
    if (during_identity != nullptr) { during_identity(hwnd); }
    if (!test::IsWindow(hwnd)) { if (process != nullptr) { *process = 0; } return 0; }
    if (process != nullptr) { *process = GetWindow(hwnd).process_id; }
    return GetWindow(hwnd).thread_id;
}
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
    if (command == SW_RESTORE && window.fail_restore) { return FALSE; }
    if (command == SW_MINIMIZE) {
        if (!window.ignore_minimize && !window.defer_minimize) { window.minimized = true; }
    }
    else {
        window.minimized = false;
        RestoreEvent(hwnd);
    }
    if (during_show != nullptr) { during_show(hwnd, command); }
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
    during_show = nullptr;
    during_identity = nullptr;
}

std::array<SHORT, 256> keys{};
HOOKPROC keyboard_proc = nullptr;
bool fail_install = false;
bool post_succeeds = true;
int install_calls = 0;
int uninstall_calls = 0;
int posted_messages = 0;
int window_messages = 0;
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
DWORD WINAPI GetCurrentThreadId() { return current_thread; }
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

BOOL WINAPI PostMessageW(HWND window, UINT message, WPARAM, LPARAM)
{
    Check(test::IsWindow(window) && GetWindow(window).thread_id == current_thread
        && message == WM_APP + 1, "notification goes to the installing thread's window");
    if (!post_succeeds) { return FALSE; }
    ++window_messages;
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
#define GetWindowThreadProcessId test::GetWindowThreadProcessId
#define GetCurrentThreadId test::GetCurrentThreadId
#define SetWinEventHook test::SetWinEventHook
#define UnhookWinEvent test::UnhookWinEvent
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
#undef GetWindowThreadProcessId
#undef GetCurrentThreadId
#undef SetWinEventHook
#undef UnhookWinEvent

#define SetWindowsHookExW test::SetWindowsHookExW
#define UnhookWindowsHookEx test::UnhookWindowsHookEx
#define PeekMessageW test::PeekMessageW
#define GetCurrentThreadId test::GetCurrentThreadId
#define GetAsyncKeyState test::GetAsyncKeyState
#define CallNextHookEx test::CallNextHookEx
#define PostThreadMessageW test::PostThreadMessageW
#define PostMessageW test::PostMessageW
#define GetWindowThreadProcessId test::GetWindowThreadProcessId
#define SendInput test::SendInput
#include "../src/keyboard_hook.cpp"
#undef SetWindowsHookExW
#undef UnhookWindowsHookEx
#undef PeekMessageW
#undef GetCurrentThreadId
#undef GetAsyncKeyState
#undef CallNextHookEx
#undef PostThreadMessageW
#undef PostMessageW
#undef GetWindowThreadProcessId
#undef SendInput

struct DesktopFixture {
    DesktopManager manager;
    MonitorTarget primary{L"\\\\.\\DISPLAY1"};
    MonitorTarget secondary{L"\\\\.\\display2"};
    DesktopFixture()
    {
        test::ResetWindows();
        test::Check(manager.StartTracking(), "start restore tracking for fixture");
    }
    void Toggle() { manager.ToggleDesktop({primary}); }
};

void TestTracking()
{
    using namespace test;
    ResetWindows();
    const HWND window = AddWindow();
    const int before = tracking_uninstalls;
    WINEVENTPROC saved_callback = nullptr;
    HWINEVENTHOOK saved_hook = nullptr;
    {
        DesktopManager manager;
        manager.ToggleDesktop({{L"\\\\.\\DISPLAY1"}});
        Check(commands.empty(), "no operation before tracking is initialized");
        fail_tracking = true;
        Check(!manager.StartTracking() && GetLastError() == ERROR_ACCESS_DENIED,
            "tracking initialization failure preserves API error");
        tracking_error = 0;
        Check(!manager.StartTracking() && GetLastError() == ERROR_GEN_FAILURE,
            "tracking failure without API error gets a useful error");
        fail_tracking = false;
        tracking_error = ERROR_ACCESS_DENIED;
        Check(manager.StartTracking(), "tracking can retry after failure");
        const int installed = tracking_installs;
        Check(manager.StartTracking() && installed == tracking_installs, "tracking initialization is idempotent");
        DesktopManager second;
        Check(!second.StartTracking() && GetLastError() == ERROR_ALREADY_EXISTS, "reject another active tracker");
        current_thread = 43;
        Check(!manager.StartTracking() && GetLastError() == ERROR_INVALID_THREAD_ID, "enforce tracking thread");
        manager.ToggleDesktop({{L"\\\\.\\DISPLAY1"}});
        Check(commands.empty(), "wrong-thread toggle performs no operation");
        current_thread = 42;
        saved_callback = window_event_proc;
        saved_hook = window_event_hook;
    }
    Check(tracking_uninstalls == before + 1 && window_event_proc == nullptr, "destructor releases restore hook");
    saved_callback(saved_hook, EVENT_SYSTEM_MINIMIZEEND, window, OBJID_WINDOW, CHILDID_SELF, 200, 0);
    Check(commands.empty(), "late callback after destruction is harmless");
}

void TestDesktop()
{
    using namespace test;
    TestTracking();
    {
        DesktopFixture fixture;
        const HWND chrome = AddWindow();
        const HWND code = AddWindow();
        const HWND manual = AddWindow();
        GetWindow(manual).minimized = true;
        const HWND foreign = AddWindow(MonitorAt(1));
        const HWND foreign_manual = AddWindow(MonitorAt(1));
        GetWindow(foreign_manual).minimized = true;
        fixture.Toggle();
        Check(GetWindow(chrome).minimized && GetWindow(code).minimized && !GetWindow(foreign).minimized,
            "only visible target windows are minimized");
        commands.clear();
        fixture.Toggle();
        Check(commands.size() == 2 && commands[0].first == code && commands[1].first == chrome,
            "restore owned windows in reverse order despite visible foreign windows");
        Check(GetWindow(manual).minimized && GetWindow(foreign_manual).minimized,
            "manual minimizations on either monitor remain minimized");
        for (int repeat = 0; repeat < 2; ++repeat) {
            fixture.Toggle();
            fixture.Toggle();
            Check(GetWindow(manual).minimized && !GetWindow(chrome).minimized,
                "repeated toggles do not acquire manually minimized windows");
        }
    }
    {
        DesktopFixture fixture;
        const HWND code = AddWindow();
        fixture.Toggle();
        const HWND notepad = AddWindow();
        fixture.Toggle();
        commands.clear();
        fixture.Toggle();
        Check(commands.size() == 1 && commands[0].first == notepad && GetWindow(code).minimized,
            "successful new batch replaces rather than accumulates older windows");
        fixture.Toggle();
        fixture.Toggle();
        Check(GetWindow(code).minimized && !GetWindow(notepad).minimized, "later toggles only manage Notepad");
    }
    {
        DesktopFixture fixture;
        const HWND notepad = AddWindow(MonitorAt(1));
        fixture.manager.ToggleDesktop({fixture.secondary});
        commands.clear();
        fixture.Toggle();
        Check(commands.empty() && GetWindow(notepad).minimized, "foreign candidate is neither promoted nor restored");
        fixture.manager.ToggleDesktop({{L"\\\\.\\DISPLAY2"}});
        Check(commands.size() == 1 && !GetWindow(notepad).minimized, "return to original device preserves candidate");
    }
    {
        DesktopFixture fixture;
        const HWND notepad = AddWindow(MonitorAt(1));
        fixture.manager.ToggleDesktop({fixture.secondary});
        const HWND blocker = AddWindow(MonitorAt(1));
        GetWindow(blocker).ignore_minimize = true;
        fixture.manager.ToggleDesktop({fixture.secondary}); // Notepad is now the restore batch.
        commands.clear();
        fixture.Toggle();
        Check(commands.empty() && GetWindow(notepad).minimized, "foreign restore batch is not traversed or cleared");
        GetWindow(blocker).alive = false;
        fixture.manager.ToggleDesktop({fixture.secondary});
        Check(commands.size() == 1 && commands[0].first == notepad, "foreign candidate and restore survive target switch");
    }
    for (bool accepted_without_effect : {false, true}) {
        DesktopFixture fixture;
        const HWND chrome = AddWindow();
        const HWND blocker = AddWindow();
        GetWindow(blocker).fail_show = !accepted_without_effect;
        GetWindow(blocker).ignore_minimize = accepted_without_effect;
        fixture.Toggle();
        Check(GetWindow(chrome).minimized && !GetWindow(blocker).minimized, "one failed minimize does not stop another");
        fixture.Toggle();
        fixture.Toggle();
        GetWindow(blocker).alive = false;
        commands.clear();
        fixture.Toggle();
        Check(commands.size() == 1 && commands[0].first == chrome && !GetWindow(chrome).minimized,
            "failed or ineffective new requests do not discard last useful restore batch");
    }
    {
        DesktopFixture fixture;
        const HWND window = AddWindow();
        GetWindow(window).defer_minimize = true;
        fixture.Toggle();
        Check(!GetWindow(window).minimized, "async request need not finish before toggle returns");
        GetWindow(window).minimized = true; // Deliver the queued request later, with no minimize-start tracking.
        commands.clear();
        fixture.Toggle();
        Check(commands.size() == 1 && !GetWindow(window).minimized, "delayed request is validated when needed");
    }
    for (bool promote_first : {false, true}) {
        DesktopFixture fixture;
        const HWND window = AddWindow();
        fixture.Toggle();
        HWND blocker = nullptr;
        if (promote_first) {
            blocker = AddWindow();
            GetWindow(blocker).ignore_minimize = true;
            fixture.Toggle();
        }
        // Restore then re-minimize before event delivery: current state alone cannot detect it.
        GetWindow(window).minimized = false;
        GetWindow(window).minimized = true;
        RestoreEvent(window);
        if (blocker != nullptr) { GetWindow(blocker).alive = false; }
        commands.clear();
        fixture.Toggle();
        fixture.Toggle();
        Check(commands.empty() && GetWindow(window).minimized, "restore event permanently revokes candidate or restore entry");
    }
    {
        DesktopFixture fixture;
        const HWND window = AddWindow();
        during_show = [](HWND hwnd, int command) { if (command == SW_MINIMIZE) { RestoreEvent(hwnd); } };
        fixture.Toggle();
        during_show = nullptr;
        commands.clear();
        fixture.Toggle();
        Check(commands.empty() && GetWindow(window).minimized, "restore during API call cannot be re-added after return");
    }
    {
        DesktopFixture fixture;
        const HWND first = AddWindow();
        const HWND last = AddWindow();
        fixture.Toggle();
        RestoreEvent(first, EVENT_SYSTEM_MINIMIZESTART);
        RestoreEvent(first, EVENT_SYSTEM_MINIMIZEEND, OBJID_CLIENT);
        RestoreEvent(first, EVENT_SYSTEM_MINIMIZEEND, OBJID_WINDOW, 1);
        window_event_proc(reinterpret_cast<HWINEVENTHOOK>(999), EVENT_SYSTEM_MINIMIZEEND,
            first, OBJID_WINDOW, CHILDID_SELF, 200, 0);
        during_show = [](HWND, int command) {
            if (command == SW_RESTORE) { RestoreEvent(reinterpret_cast<HWND>(&windows.front())); }
        };
        commands.clear();
        fixture.Toggle();
        Check(commands.size() == 1 && commands[0].first == last && GetWindow(first).minimized,
            "reentrant callback revokes a later restore without invalidating iteration");
    }
    {
        DesktopFixture fixture;
        const HWND window = AddWindow();
        fixture.Toggle();
        during_identity = [](HWND hwnd) { during_identity = nullptr; RestoreEvent(hwnd); };
        commands.clear();
        fixture.Toggle();
        Check(commands.empty() && GetWindow(window).minimized, "revocation during candidate validation never resurrects entry");
    }
    for (int invalidation = 0; invalidation < 9; ++invalidation) {
        DesktopFixture fixture;
        const HWND window = AddWindow();
        fixture.Toggle();
        auto& current = GetWindow(window);
        switch (invalidation) {
        case 0: current.alive = false; break;
        case 1: ++current.process_id; break;
        case 2: ++current.thread_id; break;
        case 3: current.monitor = MonitorAt(1); break;
        case 4: current.visible = false; break;
        case 5: current.ex_style = WS_EX_TOOLWINDOW; break;
        case 6: current.ex_style = WS_EX_TOPMOST; break;
        case 7: current.cloaked = DWM_CLOAKED_SHELL; break;
        case 8: current.style = WS_CHILD; break;
        }
        commands.clear();
        fixture.Toggle();
        Check(commands.empty(), "invalid identity, screen, visibility or classification cannot be restored");
        current = Window{};
        current.monitor = MonitorAt(0);
        current.minimized = true;
        fixture.Toggle();
        Check(commands.empty(), "invalid record was removed rather than retained for later");
    }
    {
        DesktopFixture fixture;
        const HWND failed = AddWindow();
        const HWND successful = AddWindow();
        fixture.Toggle();
        GetWindow(failed).fail_restore = true;
        commands.clear();
        fixture.Toggle();
        Check(commands.size() == 2 && GetWindow(failed).minimized && !GetWindow(successful).minimized,
            "restore failure retains entry and continues with other records");
        GetWindow(successful).minimized = true; // Manual minimization, not owned again.
        GetWindow(failed).fail_restore = false;
        commands.clear();
        fixture.Toggle();
        Check(commands.size() == 1 && commands[0].first == failed && GetWindow(successful).minimized,
            "retry only restores the retained failed entry");
    }
    {
        DesktopFixture fixture;
        desktop = AddWindow();
        shell = AddWindow();
        for (const wchar_t* name : {L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd"}) {
            GetWindow(AddWindow()).class_name = name;
        }
        GetWindow(AddWindow()).ex_style = WS_EX_TOOLWINDOW;
        GetWindow(AddWindow()).style = WS_CHILD;
        GetWindow(AddWindow()).visible = false;
        GetWindow(AddWindow()).cloaked = DWM_CLOAKED_SHELL;
        const HWND exempt = AddWindow();
        GetWindow(exempt).ex_style = WS_EX_TOPMOST;
        const HWND topmost = AddWindow();
        GetWindow(topmost).ex_style = WS_EX_TOPMOST;
        GetWindow(topmost).style = WS_MINIMIZEBOX;
        const HWND ordinary = AddWindow(); // No minimize box, but not topmost.
        GetWindow(ordinary).dwm_result = E_INVALIDARG;
        GetWindow(ordinary).cloaked = DWM_CLOAKED_SHELL; // Failed attribute query must ignore the output.
        fixture.Toggle();
        Check(commands.size() == 2 && !GetWindow(exempt).minimized && GetWindow(topmost).minimized
            && GetWindow(ordinary).minimized, "classification distinguishes topmost exception, button presence and Win7 fallback");
        commands.clear();
        fixture.Toggle();
        Check(commands.size() == 2 && !GetWindow(topmost).minimized && !GetWindow(ordinary).minimized,
            "exempt visible window does not prevent restoring ordinary windows");
    }
    for (int failure = 0; failure < 4; ++failure) {
        DesktopFixture fixture;
        const HWND window = AddWindow();
        fixture.Toggle();
        if (failure == 0) { fail_monitor_enum = true; }
        if (failure == 1) { monitors.back().fail_info = true; }
        if (failure == 2) { fail_enum_call = enum_calls + 1; }
        if (failure == 3) { monitors.front().alive = false; }
        commands.clear();
        fixture.Toggle();
        Check(commands.empty(), "failed query does not perform window operations");
        fail_monitor_enum = false;
        monitors.back().fail_info = false;
        fail_enum_call = 0;
        monitors.front().alive = true;
        fixture.Toggle();
        Check(commands.size() == 1 && commands[0].first == window, "failed query preserves pending batch");
    }
    {
        DesktopFixture fixture;
        const HWND window = AddWindow();
        fixture.Toggle();
        commands.clear();
        monitors.front().alive = false;
        fixture.Toggle();
        const HMONITOR replacement = AddMonitor(L"\\\\.\\DISPLAY1", {2000, -1000, 4560, 440});
        GetWindow(window).monitor = replacement;
        fixture.Toggle();
        Check(commands.size() == 1 && !GetWindow(window).minimized, "reconnected monitor is resolved by device name");
    }
    for (bool move : {false, true}) {
        DesktopFixture fixture;
        AddWindow();
        const HWND stays = AddWindow();
        after_first_enum = move ? +[] { windows.front().monitor = MonitorAt(1); }
            : +[] { windows.front().alive = false; };
        fixture.Toggle();
        Check(commands.size() == 1 && commands[0].first == stays, "recheck live windows between collection and operation");
    }
    for (bool rename : {false, true}) {
        DesktopFixture fixture;
        AddWindow();
        after_first_enum = rename ? +[] { monitors.front().device_name = L"\\\\.\\DISPLAY3"; }
            : +[] { monitors.front().alive = false; };
        fixture.Toggle();
        Check(commands.empty(), "topology change during enumeration cancels before candidate replacement");
    }
    {
        DesktopFixture fixture;
        fixture.manager.ToggleDesktop({});
        fixture.manager.ToggleDesktop({{L"\\\\.\\DISPLAY99"}});
        fixture.Toggle();
        Check(commands.empty(), "empty target, unknown target and empty desktop are no-ops");
    }
    {
        DesktopFixture fixture;
        const HMONITOR third = AddMonitor(L"\\\\.\\DISPLAY3", {2560, 0, 4480, 1080});
        const HWND first = AddWindow(), second = AddWindow(MonitorAt(1)), foreign = AddWindow(third);
        const HWND manual = AddWindow(MonitorAt(1));
        GetWindow(manual).minimized = true;
        const std::vector<MonitorTarget> targets{fixture.primary, fixture.secondary, {L"\\\\.\\display1"}};
        fixture.manager.ToggleDesktop(targets);
        Check(GetWindow(first).minimized && GetWindow(second).minimized && !GetWindow(foreign).minimized
            && commands.size() == 2 && enum_calls == 1, "grouped targets minimize once without touching unselected screens");
        commands.clear();
        fixture.manager.ToggleDesktop(targets);
        Check(commands.size() == 2 && commands[0].first == second && commands[1].first == first
            && GetWindow(manual).minimized, "clean target group restores the owned batch in reverse order");
    }
    for (bool dirty_secondary : {false, true}) {
        DesktopFixture fixture;
        const HWND first = AddWindow();
        fixture.Toggle();
        const HWND second = dirty_secondary ? AddWindow(MonitorAt(1)) : nullptr;
        const std::vector<MonitorTarget> expanded{fixture.primary, fixture.secondary};
        commands.clear();
        fixture.manager.ToggleDesktop(expanded);
        if (!dirty_secondary) {
            Check(commands.size() == 1 && commands[0].first == first && !GetWindow(first).minimized,
                "adding a clean screen restores the previous screen's batch");
        }
        else {
            Check(commands.size() == 1 && commands[0].first == second && GetWindow(first).minimized,
                "adding a dirty screen minimizes without restoring the other screen");
            commands.clear();
            fixture.manager.ToggleDesktop(expanded);
            Check(commands.size() == 1 && commands[0].first == second && GetWindow(first).minimized,
                "new valid group batch replaces all records from the previous screen");
        }
    }
    {
        DesktopFixture fixture;
        const HWND first = AddWindow(), second = AddWindow(MonitorAt(1));
        fixture.manager.ToggleDesktop({fixture.primary, fixture.secondary});
        fixture.manager.DiscardUnselectedRecords({fixture.secondary});
        commands.clear();
        fixture.manager.ToggleDesktop({fixture.secondary});
        Check(commands.size() == 1 && commands[0].first == second && GetWindow(first).minimized,
            "saving target removal revokes only removed screen records");
        GetWindow(second).minimized = true;
        RestoreEvent(second);
        commands.clear();
        fixture.manager.ToggleDesktop({fixture.primary, fixture.secondary});
        Check(commands.empty(), "reselecting a removed screen does not reacquire discarded records");
    }
    {
        DesktopFixture fixture;
        const HWND first = AddWindow();
        fixture.Toggle();
        const HWND blocker = AddWindow(MonitorAt(1));
        GetWindow(blocker).ignore_minimize = true;
        fixture.manager.ToggleDesktop({fixture.primary, fixture.secondary});
        fixture.manager.ToggleDesktop({fixture.primary, fixture.secondary});
        GetWindow(blocker).alive = false;
        commands.clear();
        fixture.manager.ToggleDesktop({fixture.primary, fixture.secondary});
        Check(commands.size() == 1 && commands[0].first == first,
            "an ineffective new request on another screen does not erase a useful group batch");
    }
    {
        DesktopFixture fixture;
        const HWND first = AddWindow(), second = AddWindow(MonitorAt(1));
        fixture.manager.ToggleDesktop({fixture.primary, fixture.secondary});
        monitors[1].alive = false;
        commands.clear();
        fixture.manager.ToggleDesktop({fixture.primary, fixture.secondary});
        Check(commands.size() == 1 && commands[0].first == first && GetWindow(second).minimized,
            "offline targets do not block restoration on online screens");
        monitors[1].alive = true;
        commands.clear();
        fixture.manager.ToggleDesktop({fixture.secondary});
        Check(commands.size() == 1 && commands[0].first == second, "offline restore records remain usable after reconnection");
    }
    Check(window_event_hook == nullptr && tracking_uninstalls == tracking_installs - 2,
        "all successful tracking hooks are released; two failed starts own no hook");
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

    hook.SetEnabled(false);
    Check(Key('D', WM_KEYDOWN) == PassedThrough && Key('D', WM_KEYDOWN) == PassedThrough
        && Key('D', WM_KEYUP) == PassedThrough && posted_messages == 3, "disabled interception passes Win+D through without notifications");
    Check(Key('D', WM_KEYDOWN) == PassedThrough, "disabled gesture starts native");
    hook.SetEnabled(true);
    Check(Key('D', WM_KEYDOWN) == PassedThrough && Key('D', WM_KEYUP) == PassedThrough,
        "enabling during a native gesture does not intercept a partial press");
    Check(Key('D', WM_KEYDOWN) == 1, "re-enabled interception handles the next complete gesture");
    hook.SetEnabled(false);
    Check(Key('D', WM_KEYDOWN) == 1 && Key('D', WM_KEYUP) == 1, "disabling during an intercepted gesture still consumes its repeat and release");
    hook.SetEnabled(true);

    hook.Uninstall();
    hook.Uninstall();
    Check(uninstall_calls == 1, "uninstall is idempotent");
    Check(other.Install(), "another instance can install after uninstall");
    other.Uninstall();
    const HWND notification = AddWindow();
    Check(!other.Install(notification) && GetLastError() == ERROR_INVALID_WINDOW_HANDLE,
        "window notification cannot target another thread");
    GetWindow(notification).thread_id = current_thread;
    Check(other.Install(notification) && other.Install(notification), "install with an owned window destination");
    Check(!other.Install() && GetLastError() == ERROR_ALREADY_EXISTS, "cannot silently change a live notification destination");
    const int before_window = posted_messages;
    Check(Key('D', WM_KEYDOWN) == 1 && Key('D', WM_KEYDOWN) == 1 && Key('D', WM_KEYUP) == 1
        && window_messages == 1 && posted_messages == before_window + 1,
        "window notification preserves interception and repeat deduplication");
    post_succeeds = false;
    Check(Key('D', WM_KEYDOWN) == PassedThrough && Key('D', WM_KEYDOWN) == PassedThrough
        && Key('D', WM_KEYUP) == PassedThrough, "failed window notification passes the complete gesture through");
    post_succeeds = true;
}

int main()
{
    TestDesktop();
    TestKeyboard();
    test::Check(test::uninstall_calls == 3, "destructor uninstalls the remaining hook");
    std::puts("Desktop and keyboard behavior tests passed.");
    return 0;
}
