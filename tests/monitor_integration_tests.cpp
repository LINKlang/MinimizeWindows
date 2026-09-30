// Include the actual CLI formatter/writer to verify Unicode and UTF-8 redirection.
#include "../src/main.cpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

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

} // namespace

int main()
{
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
    std::printf("Native monitor/Unicode tests passed (%zu display(s)).\n", snapshot.monitors.size());
    return 0;
}
