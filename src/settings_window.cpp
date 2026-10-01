#include "settings_window.h"

bool SettingsWindow::Show(HICON icon)
{
    if (!IsWindow()) {
        RECT bounds{0, 0, 480, 320};
        if (Create(nullptr, &bounds, L"MinimizeWindows \u2014 \u8bbe\u7f6e",
                WS_OVERLAPPEDWINDOW, WS_EX_APPWINDOW) == nullptr) {
            return false;
        }
        SetIcon(icon, TRUE);
        SetIcon(icon, FALSE);
        CenterWindow();
    }
    ShowWindow(IsIconic() ? SW_RESTORE : SW_SHOW);
    // The first ShowWindow can obey the launcher's hidden/minimized STARTUPINFO.
    // A later tray activation is an explicit request to display the frame.
    if (!IsWindowVisible() || IsIconic()) { ShowWindow(SW_RESTORE); }
    ::SetForegroundWindow(m_hWnd);
    return true;
}

LRESULT SettingsWindow::OnClose(UINT, WPARAM, LPARAM, BOOL&)
{
    DestroyWindow();
    return 0;
}

LRESULT SettingsWindow::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled)
{
    // WTL 10.01's default frame handler posts WM_QUIT for a top-level frame.
    // This window owns only its UI; the tray host owns the application lifetime.
    handled = TRUE;
    return 0;
}
