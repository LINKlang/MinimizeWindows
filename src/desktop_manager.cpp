#include "desktop_manager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

#include <cwchar>

namespace {

struct MonitorLookup {
    const std::wstring& device_name;
    HMONITOR monitor = nullptr;
};

BOOL CALLBACK FindTargetMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
    auto& lookup = *reinterpret_cast<MonitorLookup*>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return FALSE;
    }
    if (_wcsicmp(info.szDevice, lookup.device_name.c_str()) == 0) {
        lookup.monitor = monitor;
    }
    return TRUE;
}

struct WindowCommand {
    HMONITOR monitor;
    bool has_visible_window = false;
    int command = SW_MINIMIZE;
};

bool IsOrdinaryWindow(HWND hwnd)
{
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) {
        return false;
    }

    if (hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) {
        return false;
    }

    if ((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) != 0
        || (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0) {
        return false;
    }

    wchar_t class_name[256];
    if (GetClassNameW(hwnd, class_name, ARRAYSIZE(class_name)) == 0) {
        return false;
    }

    if (std::wcscmp(class_name, L"Progman") == 0
        || std::wcscmp(class_name, L"WorkerW") == 0
        || std::wcscmp(class_name, L"Shell_TrayWnd") == 0
        || std::wcscmp(class_name, L"Shell_SecondaryTrayWnd") == 0) {
        return false;
    }

    DWORD cloaked = 0;
    // Older systems may not support DWMWA_CLOAKED; keep the basic filters there.
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED,
            &cloaked, sizeof(cloaked))) && cloaked != 0) {
        return false;
    }

    return true;
}

BOOL CALLBACK FindVisibleWindow(HWND hwnd, LPARAM data)
{
    auto& operation = *reinterpret_cast<WindowCommand*>(data);
    if (IsOrdinaryWindow(hwnd) && !IsIconic(hwnd)
        && MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) == operation.monitor) {
        operation.has_visible_window = true;
    }
    return TRUE;
}

BOOL CALLBACK ApplyWindowCommand(HWND hwnd, LPARAM data)
{
    const auto& operation = *reinterpret_cast<const WindowCommand*>(data);
    if (!IsOrdinaryWindow(hwnd)
        || MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) != operation.monitor) {
        return TRUE;
    }

    const int command = operation.command;
    const bool minimized = IsIconic(hwnd) != FALSE;
    if ((command == SW_MINIMIZE && !minimized)
        || (command == SW_RESTORE && minimized)) {
        ShowWindowAsync(hwnd, command);
    }
    return TRUE;
}

} // namespace

void DesktopManager::ToggleDesktop(const MonitorTarget& target)
{
    if (target.device_name.empty()) {
        return;
    }

    MonitorLookup lookup{target.device_name};
    if (!EnumDisplayMonitors(nullptr, nullptr, FindTargetMonitor,
            reinterpret_cast<LPARAM>(&lookup)) || lookup.monitor == nullptr) {
        return;
    }

    WindowCommand operation{lookup.monitor};
    if (!EnumWindows(FindVisibleWindow,
            reinterpret_cast<LPARAM>(&operation))) {
        return;
    }

    // A topology change during the scan must not redirect the operation.
    MONITORINFOEXW current{};
    current.cbSize = sizeof(current);
    if (!GetMonitorInfoW(lookup.monitor, &current)
        || _wcsicmp(current.szDevice, target.device_name.c_str()) != 0) {
        return;
    }

    operation.command = operation.has_visible_window ? SW_MINIMIZE : SW_RESTORE;
    EnumWindows(ApplyWindowCommand, reinterpret_cast<LPARAM>(&operation));
}
