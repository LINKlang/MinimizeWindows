// Exercise CLI routing without starting the tray UI or installing OS hooks.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include "../src/monitor_enumerator.h"
#include "../src/desktop_manager.h"
#include "../src/keyboard_hook.h"
#include "../src/tray_application.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace startup {

void Check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

bool no_monitor = false;
bool fail_tray = false;
int tray_calls = 0;
std::wstring selected_device;
int enumeration_calls = 0;
std::wstring command_line;

int RunTrayApplication(HINSTANCE, const MonitorTarget& target)
{
    ++tray_calls;
    selected_device = target.device_name;
    if (fail_tray) { SetLastError(ERROR_ACCESS_DENIED); return 1; }
    return 0;
}
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

#define MonitorEnumerator startup::MonitorEnumerator
#define RunTrayApplication startup::RunTrayApplication
#define GetCommandLineW startup::GetCommandLineW
#include "../src/main.cpp"
#undef MonitorEnumerator
#undef RunTrayApplication
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
    fail_tray = true;
    Check(RunEntry(L"", output) == 1 && tray_calls == 1
        && output.find("Tray application startup") != std::string::npos
        && output.find("Usage:") != std::string::npos, "tray startup failure reports error and usage");
    fail_tray = false;
    Check(RunEntry(L"", output) == 0 && tray_calls == 2 && selected_device == L"\\\\.\\DISPLAY1",
        "normal startup passes the default primary device to the tray application");
    Check(RunEntry(L" --monitor \\\\.\\display1", output) == 0 && tray_calls == 3
        && selected_device == L"\\\\.\\DISPLAY1", "explicit case-insensitive target is canonicalized before UI startup");
    const int installed = tray_calls;
    const int enumerated = enumeration_calls;
    Check(RunEntry(L" --help", output) == 0 && output.find("Usage:") != std::string::npos
        && tray_calls == installed && enumeration_calls == enumerated, "help exits without UI or enumeration");
    Check(RunEntry(L" --list-monitors", output) == 0 && output.find("Active displays: 1") != std::string::npos
        && tray_calls == installed, "listing exits without UI");
    Check(RunEntry(L" --monitor", output) == 1 && tray_calls == installed,
        "invalid arguments exit without UI");
    no_monitor = true;
    Check(RunEntry(L"", output) == 1 && tray_calls == installed,
        "unavailable startup monitor exits before UI startup");
    std::puts("Entry point tray routing, startup error and CLI exit-mode tests passed.");
    return 0;
}
