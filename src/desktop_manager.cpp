#include "desktop_manager.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwmapi.h>

#include <cwchar>

namespace {

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
    if (IsOrdinaryWindow(hwnd) && !IsIconic(hwnd)) {
        *reinterpret_cast<bool*>(data) = true;
    }
    return TRUE;
}

BOOL CALLBACK ApplyWindowCommand(HWND hwnd, LPARAM data)
{
    if (!IsOrdinaryWindow(hwnd)) {
        return TRUE;
    }

    const int command = static_cast<int>(data);
    const bool minimized = IsIconic(hwnd) != FALSE;
    if ((command == SW_MINIMIZE && !minimized)
        || (command == SW_RESTORE && minimized)) {
        ShowWindowAsync(hwnd, command);
    }
    return TRUE;
}

} // namespace

void DesktopManager::ToggleDesktop()
{
    bool has_visible_window = false;
    if (!EnumWindows(FindVisibleWindow,
            reinterpret_cast<LPARAM>(&has_visible_window))) {
        return;
    }

    const int command = has_visible_window ? SW_MINIMIZE : SW_RESTORE;
    EnumWindows(ApplyWindowCommand, static_cast<LPARAM>(command));
}
