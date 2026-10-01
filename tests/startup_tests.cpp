// Exercise the real entry point with fake hook boundaries; no OS hook is installed.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include "../src/monitor_enumerator.h"
#include "../src/desktop_manager.h"
#include "../src/keyboard_hook.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace startup {

void Check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

bool fail_tracking = false;
bool fail_keyboard = false;
bool no_monitor = false;
int tracking_calls = 0;
int keyboard_calls = 0;
int tracking_cleanup = 0;
int keyboard_cleanup = 0;
int message_loops = 0;
int enumeration_calls = 0;
std::wstring command_line;

HWINEVENTHOOK WINAPI SetWinEventHook(DWORD first, DWORD last, HMODULE, WINEVENTPROC,
    DWORD process, DWORD thread, DWORD flags)
{
    Check(first == EVENT_SYSTEM_MINIMIZEEND && last == first && process == 0 && thread == 0
        && flags == WINEVENT_OUTOFCONTEXT, "startup installs the single required event hook");
    ++tracking_calls;
    if (fail_tracking) { SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    return reinterpret_cast<HWINEVENTHOOK>(1);
}

BOOL WINAPI UnhookWinEvent(HWINEVENTHOOK) { ++tracking_cleanup; return TRUE; }

HHOOK WINAPI SetWindowsHookExW(int type, HOOKPROC, HINSTANCE, DWORD)
{
    Check(type == WH_KEYBOARD_LL && tracking_calls > tracking_cleanup, "event tracking precedes keyboard hook");
    ++keyboard_calls;
    return fail_keyboard ? nullptr : reinterpret_cast<HHOOK>(2);
}

BOOL WINAPI UnhookWindowsHookEx(HHOOK) { ++keyboard_cleanup; return TRUE; }
BOOL WINAPI GetMessageW(LPMSG, HWND, UINT, UINT) { ++message_loops; return FALSE; }
LPWSTR WINAPI GetCommandLineW() { return &command_line[0]; }

class MonitorEnumerator {
public:
    bool Enumerate(MonitorSnapshot& snapshot) const
    {
        ++enumeration_calls;
        snapshot = {};
        if (!no_monitor) {
            MonitorInfo primary;
            primary.device_name = L"\\\\.\\DISPLAY1";
            primary.handle = reinterpret_cast<HMONITOR>(1);
            primary.primary = true;
            primary.bounds_px = {0, 0, 1920, 1080};
            primary.desktop_size_px = {1920, 1080};
            snapshot.monitors.push_back(primary);
            snapshot.virtual_bounds_px = primary.bounds_px;
        }
        return true;
    }
};

} // namespace startup

#define SetWinEventHook startup::SetWinEventHook
#define UnhookWinEvent startup::UnhookWinEvent
#include "../src/desktop_manager.cpp"
#undef SetWinEventHook
#undef UnhookWinEvent
#define SetWindowsHookExW startup::SetWindowsHookExW
#define UnhookWindowsHookEx startup::UnhookWindowsHookEx
#include "../src/keyboard_hook.cpp"
#undef SetWindowsHookExW
#undef UnhookWindowsHookEx
#define MonitorEnumerator startup::MonitorEnumerator
#define GetMessageW startup::GetMessageW
#define GetCommandLineW startup::GetCommandLineW
#include "../src/main.cpp"
#undef MonitorEnumerator
#undef GetMessageW
#undef GetCommandLineW

int RunEntry(const wchar_t* arguments, std::string& output)
{
    startup::command_line = std::wstring(L"MinimizeWindows.exe") + arguments;
    HANDLE reader = nullptr, writer = nullptr;
    startup::Check(CreatePipe(&reader, &writer, nullptr, 8192) != FALSE, "create startup output pipe");
    const HANDLE previous_output = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE previous_error = GetStdHandle(STD_ERROR_HANDLE);
    SetStdHandle(STD_OUTPUT_HANDLE, writer);
    SetStdHandle(STD_ERROR_HANDLE, writer);
    const int result = wWinMain(nullptr, nullptr, nullptr, 0);
    SetStdHandle(STD_OUTPUT_HANDLE, previous_output);
    SetStdHandle(STD_ERROR_HANDLE, previous_error);
    CloseHandle(writer);
    output.clear();
    char buffer[2048];
    DWORD read = 0;
    while (ReadFile(reader, buffer, sizeof(buffer), &read, nullptr) && read != 0) { output.append(buffer, read); }
    CloseHandle(reader);
    return result;
}

int main()
{
    using namespace startup;
    std::string output;
    fail_tracking = true;
    Check(RunEntry(L"", output) == 1 && keyboard_calls == 0 && message_loops == 0
        && output.find("Window restore event tracking") != std::string::npos
        && output.find("Usage:") != std::string::npos, "tracking failure exits before installing keyboard hook");
    fail_tracking = false;
    fail_keyboard = true;
    Check(RunEntry(L"", output) == 1 && keyboard_calls == 1 && tracking_cleanup == 1 && message_loops == 0,
        "keyboard installation failure releases event tracking");
    fail_keyboard = false;
    Check(RunEntry(L"", output) == 0 && keyboard_calls == 2 && tracking_cleanup == 2
        && keyboard_cleanup == 1 && message_loops == 1, "normal exit releases both hooks");
    const int installed = tracking_calls;
    const int enumerated = enumeration_calls;
    Check(RunEntry(L" --help", output) == 0 && output.find("Usage:") != std::string::npos
        && tracking_calls == installed && enumeration_calls == enumerated, "help exits without either hook or enumeration");
    Check(RunEntry(L" --list-monitors", output) == 0 && output.find("Active displays: 1") != std::string::npos
        && tracking_calls == installed, "listing exits without either hook");
    Check(RunEntry(L" --monitor", output) == 1 && tracking_calls == installed,
        "invalid arguments exit without either hook");
    no_monitor = true;
    Check(RunEntry(L"", output) == 1 && tracking_calls == installed,
        "unavailable startup monitor exits before event tracking");
    std::puts("Entry point startup ordering, failure cleanup and exit-mode tests passed.");
    return 0;
}
