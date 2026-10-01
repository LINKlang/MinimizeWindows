// Include the actual CLI formatter/writer to verify Unicode and UTF-8 redirection.
#include "../src/main.cpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

int RunTrayApplication(HINSTANCE, const MonitorTarget&)
{
    // These tests exercise enumeration and formatting only, never the GUI path.
    std::abort();
}

namespace {

void Require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s (Win32 error %lu)\n", message, GetLastError());
        std::exit(1);
    }
}

bool EqualBounds(const RECT& left, const RECT& right)
{
    return EqualRect(&left, &right) != FALSE;
}

bool ParseArguments(std::initializer_list<const wchar_t*> arguments,
    CommandLineOptions& options, std::wstring& error)
{
    return ParseOptions(static_cast<int>(arguments.size()), arguments.begin(), options, error);
}

void TestOptionsAndSelection()
{
    CommandLineOptions options;
    std::wstring error;
    Require(ParseArguments({L"app.exe"}, options, error)
        && options.mode == CommandLineOptions::Mode::Run && options.monitor_device.empty(),
        "no arguments select default startup monitor");
    Require(ParseArguments({L"app.exe", L"--monitor", L"\\\\.\\display2"}, options, error)
        && options.mode == CommandLineOptions::Mode::Run && options.monitor_device == L"\\\\.\\display2",
        "explicit monitor name accepted");
    Require(ParseArguments({L"app.exe", L"--help"}, options, error)
        && options.mode == CommandLineOptions::Mode::Help, "help is an exit mode");
    Require(ParseArguments({L"app.exe", L"--list-monitors"}, options, error)
        && options.mode == CommandLineOptions::Mode::ListMonitors, "listing is an exit mode");

    const std::vector<std::vector<const wchar_t*>> invalid = {
        {L"app.exe", L"--monitor"},
        {L"app.exe", L"--monitor", L""},
        {L"app.exe", L"--monitor", L"--help"},
        {L"app.exe", L"--monitor", L"\\\\.\\DISPLAY1", L"--monitor", L"\\\\.\\DISPLAY2"},
        {L"app.exe", L"--help", L"--monitor", L"\\\\.\\DISPLAY1"},
        {L"app.exe", L"--monitor", L"\\\\.\\DISPLAY1", L"--help"},
        {L"app.exe", L"--list-monitors", L"--monitor", L"\\\\.\\DISPLAY1"},
        {L"app.exe", L"--monitor", L"\\\\.\\DISPLAY1", L"--list-monitors"},
        {L"app.exe", L"--help", L"--list-monitors"},
        {L"app.exe", L"--list-monitors", L"--list-monitors"},
        {L"app.exe", L"--unknown"},
        {L"app.exe", L"2"}
    };
    for (const auto& arguments : invalid) {
        Require(!ParseOptions(static_cast<int>(arguments.size()), arguments.data(), options, error)
            && !error.empty(), "invalid arguments report an error");
    }

    MonitorSnapshot snapshot;
    MonitorInfo secondary;
    secondary.device_name = L"\\\\.\\DISPLAY2";
    secondary.handle = reinterpret_cast<HMONITOR>(2);
    MonitorInfo primary;
    primary.device_name = L"\\\\.\\DISPLAY1";
    primary.handle = reinterpret_cast<HMONITOR>(1);
    primary.primary = true;
    snapshot.monitors = {secondary, primary, primary}; // Clones share a logical primary source.
    MonitorTarget target;
    Require(SelectMonitorTarget(snapshot, L"", target, error) && target.device_name == primary.device_name,
        "default selection finds primary even if it is not first and has cloned outputs");
    Require(SelectMonitorTarget(snapshot, L"\\\\.\\display2", target, error)
        && target.device_name == secondary.device_name, "selection canonicalizes case-insensitive device name");
    Require(!SelectMonitorTarget(snapshot, L"2", target, error) && target.device_name.empty(),
        "debug or settings number cannot select a device");
    Require(!SelectMonitorTarget(snapshot, L"\\\\.\\DISPLAY99", target, error) && !error.empty(),
        "unknown target is rejected");
    snapshot.monitors = {secondary};
    Require(!SelectMonitorTarget(snapshot, L"", target, error), "missing primary does not select another monitor");
    snapshot.monitors[0].handle = nullptr;
    Require(!SelectMonitorTarget(snapshot, secondary.device_name, target, error),
        "target without a runtime handle is unavailable");
    snapshot.monitors.clear();
    Require(!SelectMonitorTarget(snapshot, L"", target, error), "empty topology cannot start the hook");
}

} // namespace

int main()
{
    TestOptionsAndSelection();
    using GetContext = HANDLE (WINAPI*)();
    using SetContext = HANDLE (WINAPI*)(HANDLE);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto get_context = reinterpret_cast<GetContext>(GetProcAddress(user32, "GetThreadDpiAwarenessContext"));
    const auto set_context = reinterpret_cast<SetContext>(GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
    const HANDLE original = get_context != nullptr ? get_context() : nullptr;
    const BOOL original_process_aware = IsProcessDPIAware();

    MonitorSnapshot snapshot;
    Require(MonitorEnumerator{}.Enumerate(snapshot), "native monitor enumeration");
    Require(!snapshot.monitors.empty(), "active displays exist on this test machine");
    Require((get_context == nullptr || get_context() == original)
        && IsProcessDPIAware() == original_process_aware, "enumeration restores DPI context and process mode");

    const HANDLE previous = set_context != nullptr
        ? set_context(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-3))) : nullptr;
    for (const auto& monitor : snapshot.monitors) {
        DEVMODEW current{};
        current.dmSize = sizeof(current);
        Require(EnumDisplaySettingsExW(monitor.device_name.c_str(), ENUM_CURRENT_SETTINGS, &current, 0) != FALSE,
            "independent current display mode lookup");
        Require(current.dmPosition.x == monitor.bounds_px.left && current.dmPosition.y == monitor.bounds_px.top
            && current.dmPelsWidth == static_cast<DWORD>(monitor.desktop_size_px.cx)
            && current.dmPelsHeight == static_cast<DWORD>(monitor.desktop_size_px.cy), "current desktop layout matches");
        const auto matches = std::count_if(snapshot.monitors.begin(), snapshot.monitors.end(),
            [&](const MonitorInfo& item) { return item.device_name == monitor.device_name; });
        if (matches == 1 && monitor.rotation_degrees.available && (current.dmFields & DM_DISPLAYORIENTATION) != 0) {
            Require(monitor.rotation_degrees.value == current.dmDisplayOrientation * 90,
                "GDI and CCD Windows orientation labels agree");
        }
        if (previous != nullptr && monitor.handle != nullptr) {
            MONITORINFO info{};
            info.cbSize = sizeof(info);
            Require(GetMonitorInfoW(monitor.handle, &info) != FALSE
                && EqualBounds(info.rcMonitor, monitor.bounds_px), "independent physical monitor rectangle matches");
            Require(monitor.work_area_px.available && EqualBounds(info.rcWork, monitor.work_area_px.value),
                "native physical work area matches");
        }
    }
    if (previous != nullptr) {
        const RECT expected = {GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
        Require(EqualBounds(expected, snapshot.virtual_bounds_px), "virtual desktop matches native screen metrics");
        set_context(previous);
    }

    snapshot.monitors.front().friendly_name = L"\u663E\u793A\u5668 \U0001F5A5";
    const std::wstring formatted = FormatSnapshot(snapshot);
    Require(formatted.find(snapshot.monitors.front().friendly_name) != std::wstring::npos, "Unicode display name retained");
    HANDLE reader = nullptr;
    HANDLE writer = nullptr;
    Require(CreatePipe(&reader, &writer, nullptr, static_cast<DWORD>(formatted.size() * 4 + 1024)) != FALSE,
        "create redirected output pipe");
    Require(WriteText(writer, formatted), "write UTF-8 redirected monitor output");
    CloseHandle(writer);
    std::string bytes;
    char buffer[4096];
    DWORD count = 0;
    while (ReadFile(reader, buffer, sizeof(buffer), &count, nullptr) && count != 0) {
        bytes.append(buffer, count);
    }
    CloseHandle(reader);
    Require(bytes.compare(0, 3, "\xEF\xBB\xBF") != 0, "redirected UTF-8 has no BOM");
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    Require(size > 0, "redirected bytes are valid UTF-8");
    std::wstring decoded(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), &decoded[0], size);
    Require(decoded == formatted, "UTF-8 output round-trips Chinese and surrogate pairs");
    std::printf("Monitor options/selection and native Unicode tests passed (%zu display(s)).\n", snapshot.monitors.size());
    return 0;
}
