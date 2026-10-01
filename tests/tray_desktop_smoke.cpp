// Opt-in real Shell registration check. Notifications are sent directly to the
// owned child window; this does not verify physical mouse interaction in Explorer.
// Never inject global input or infer icon ownership from screen coordinates.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cwchar>
#include <functional>
#include <stdexcept>

namespace {

void Check(bool condition, const char* message)
{
    if (!condition) { throw std::runtime_error(message); }
}

void WaitFor(const std::function<bool()>& predicate, const char* message)
{
    const ULONGLONG deadline = GetTickCount64() + 8000;
    do { if (predicate()) { return; } Sleep(50); } while (GetTickCount64() < deadline);
    throw std::runtime_error(message);
}

HWND FindOwnedWindow(DWORD process, const wchar_t* class_name)
{
    struct Query { DWORD process; const wchar_t* name; HWND result = nullptr; } query{process, class_name};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& query = *reinterpret_cast<Query*>(data);
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        wchar_t name[128]{};
        GetClassNameW(window, name, ARRAYSIZE(name));
        if (process == query.process && std::wcscmp(name, query.name) == 0) {
            query.result = window;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&query));
    return query.result;
}

struct ChildProcess {
    PROCESS_INFORMATION process{};
    ~ChildProcess()
    {
        if (process.hProcess == nullptr) { return; }
        if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0) {
            const HWND host = FindOwnedWindow(process.dwProcessId, L"MinimizeWindows.TrayHost");
            if (host != nullptr) { PostMessageW(host, WM_CLOSE, 0, 0); }
            if (WaitForSingleObject(process.hProcess, 2000) != WAIT_OBJECT_0) {
                TerminateProcess(process.hProcess, 1); // Only the child created by this test.
                WaitForSingleObject(process.hProcess, 2000);
            }
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
};

void Notify(HWND host, UINT event)
{
    DWORD_PTR result = 0;
    Check(SendMessageTimeoutW(host, WM_APP + 2, 0, MAKELPARAM(event, 1),
        SMTO_ABORTIFHUNG, 2000, &result) != 0,
        "could not deliver notification to the owned tray host");
}

} // namespace

int wmain(int count, wchar_t* arguments[])
{
    if (count != 2) { std::fprintf(stderr, "Usage: tray_desktop_smoke.exe APP_PATH\n"); return 1; }
    try {
        ChildProcess child;
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        Check(CreateProcessW(arguments[1], nullptr, nullptr, nullptr, FALSE, 0, nullptr, nullptr,
            &startup, &child.process) != FALSE, "could not launch the hidden tray application");
        HWND host = nullptr;
        WaitFor([&] { host = FindOwnedWindow(child.process.dwProcessId, L"MinimizeWindows.TrayHost"); return host != nullptr; },
            "tray host did not initialize");
        Check(!IsWindowVisible(host) && FindOwnedWindow(child.process.dwProcessId, L"MinimizeWindows.Settings") == nullptr,
            "normal startup showed a visible application window");
        NOTIFYICONIDENTIFIER identity{};
        identity.cbSize = sizeof(identity);
        identity.hWnd = host;
        identity.uID = 1;
        RECT icon{};
        WaitFor([&] { return SUCCEEDED(Shell_NotifyIconGetRect(&identity, &icon)); }, "real tray icon was not registered");
        HWND settings = nullptr;
        const auto opened = [&] {
            settings = FindOwnedWindow(child.process.dwProcessId, L"MinimizeWindows.Settings");
            return settings != nullptr && IsWindowVisible(settings) && !IsIconic(settings);
        };
        Notify(host, WM_LBUTTONDBLCLK);
        WaitFor(opened, "double-click notification did not open settings");
        const HWND original_settings = settings;
        Notify(host, WM_LBUTTONDBLCLK);
        WaitFor(opened, "repeat notification did not activate settings");
        Check(settings == original_settings, "repeat notification created another frame");
        ShowWindowAsync(settings, SW_MINIMIZE);
        WaitFor([&] { return IsIconic(settings) != FALSE; }, "settings did not minimize");
        Notify(host, WM_LBUTTONDBLCLK);
        WaitFor(opened, "notification did not restore minimized settings");
        Check(PostMessageW(settings, WM_SYSCOMMAND, SC_CLOSE, 0) != FALSE, "could not close settings");
        WaitFor([&] { return FindOwnedWindow(child.process.dwProcessId, L"MinimizeWindows.Settings") == nullptr; },
            "settings close failed");
        Check(WaitForSingleObject(child.process.hProcess, 0) == WAIT_TIMEOUT && IsWindow(host),
            "closing settings exited the core application");
        Notify(host, WM_LBUTTONDBLCLK);
        WaitFor(opened, "settings could not reopen after close");
        Check(PostMessageW(host, WM_CLOSE, 0, 0) != FALSE, "could not request application shutdown");
        Check(WaitForSingleObject(child.process.hProcess, 8000) == WAIT_OBJECT_0, "shutdown did not end the process");
        DWORD exit_code = 1;
        Check(GetExitCodeProcess(child.process.hProcess, &exit_code) != FALSE && exit_code == 0,
            "application shutdown returned an error");
        Check(FAILED(Shell_NotifyIconGetRect(&identity, &icon)), "tray icon remained registered after shutdown");
        std::puts("Real Shell registration, directed notifications, settings lifecycle and icon removal passed.");
        std::puts("Physical tray double-click and context-menu interaction require manual acceptance.");
        return 0;
    }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
}

