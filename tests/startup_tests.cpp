// Exercise CLI routing without starting the tray UI or installing OS hooks.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include "../src/monitor_enumerator.h"
#include "../src/desktop_manager.h"
#include "../src/keyboard_hook.h"
#include "../src/tray_application.h"
#include "../src/config_store.h"

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
std::wstring configured_device;
bool fail_config = false;
int configuration_calls = 0;
bool fail_save = false;
int save_calls = 0;

class ConfigStore {
public:
    explicit ConfigStore(const std::wstring&) { }
    static bool UserFilePath(std::wstring& path, std::wstring& error)
    {
        path = L"isolated-config.json";
        error.clear();
        return true;
    }
    bool LoadOrCreate(AppConfig& config, std::wstring& error) const
    {
        ++configuration_calls;
        if (fail_config) { error = L"Configuration file is invalid."; return false; }
        config.monitor_device = configured_device;
        error.clear();
        return true;
    }
    bool Save(const AppConfig& config, std::wstring& error) const
    {
        ++save_calls;
        if (fail_save) { error = L"Configuration file cannot be saved."; return false; }
        configured_device = config.monitor_device;
        error.clear();
        return true;
    }
};

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
            MonitorInfo secondary = primary;
            secondary.device_name = L"\\\\.\\DISPLAY2";
            secondary.handle = reinterpret_cast<HMONITOR>(2);
            secondary.primary = false;
            snapshot.monitors.push_back(secondary);
            snapshot.virtual_bounds_px = primary.bounds_px;
        }
        return true;
    }
};

} // namespace startup

#define MonitorEnumerator startup::MonitorEnumerator
#define RunTrayApplication startup::RunTrayApplication
#define GetCommandLineW startup::GetCommandLineW
#define ConfigStore startup::ConfigStore
#include "../src/main.cpp"
#undef ConfigStore
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
    Check(configured_device == L"\\\\.\\DISPLAY1", "default startup persists the resolved primary device");
    const int unchanged_saves = save_calls;
    Check(RunEntry(L" --monitor \\\\.\\display1", output) == 0 && tray_calls == 3
        && selected_device == L"\\\\.\\DISPLAY1", "explicit case-insensitive target is canonicalized before UI startup");
    Check(save_calls == unchanged_saves, "an unchanged target does not rewrite configuration");
    configured_device = L"\\\\.\\display2";
    Check(RunEntry(L"", output) == 0 && selected_device == L"\\\\.\\DISPLAY2",
        "saved device is canonicalized and used without a command-line override");
    Check(RunEntry(L" --monitor \\\\.\\DISPLAY1", output) == 0 && selected_device == L"\\\\.\\DISPLAY1"
        && configured_device == L"\\\\.\\DISPLAY1", "command-line selection updates saved configuration");
    Check(RunEntry(L" --monitor \\\\.\\display2", output) == 0 && configured_device == L"\\\\.\\DISPLAY2",
        "explicit secondary selection saves the canonical device name");
    Check(RunEntry(L"", output) == 0 && selected_device == L"\\\\.\\DISPLAY2",
        "subsequent default startup uses the previously saved secondary device");
    const int installed = tray_calls;
    const int enumerated = enumeration_calls;
    const int configurations = configuration_calls;
    const int saved = save_calls;
    Check(RunEntry(L" --help", output) == 0 && output.find("Usage:") != std::string::npos
        && tray_calls == installed && enumeration_calls == enumerated && configuration_calls == configurations,
        "help exits without UI, enumeration or configuration access");
    Check(RunEntry(L" --list-monitors", output) == 0 && output.find("Active displays: 2") != std::string::npos
        && tray_calls == installed && configuration_calls == configurations, "listing exits without UI or configuration access");
    Check(RunEntry(L" --monitor", output) == 1 && tray_calls == installed,
        "invalid arguments exit without UI");
    Check(save_calls == saved, "help, listing and invalid arguments do not save configuration");
    fail_save = true;
    Check(RunEntry(L" --monitor \\\\.\\DISPLAY1", output) == 1 && tray_calls == installed
        && configured_device == L"\\\\.\\DISPLAY2" && output.find("cannot be saved") != std::string::npos,
        "save failure stops startup and preserves the saved target");
    fail_save = false;
    fail_config = true;
    const int before_config_failure = enumeration_calls;
    Check(RunEntry(L"", output) == 1 && tray_calls == installed && enumeration_calls == before_config_failure
        && output.find("Configuration") != std::string::npos, "configuration errors stop startup before enumeration or UI");
    fail_config = false;
    configured_device = L"\\\\.\\DISPLAY99";
    const int before_unavailable = save_calls;
    Check(RunEntry(L"", output) == 1 && tray_calls == installed && save_calls == before_unavailable,
        "unavailable saved target is neither replaced nor used for another device");
    Check(RunEntry(L" --monitor \\\\.\\DISPLAY99", output) == 1 && save_calls == before_unavailable,
        "unavailable command-line selection does not overwrite configuration");
    configured_device.clear();
    no_monitor = true;
    Check(RunEntry(L"", output) == 1 && tray_calls == installed,
        "unavailable startup monitor exits before UI startup");
    std::puts("Entry point tray routing, startup error and CLI exit-mode tests passed.");
    return 0;
}
