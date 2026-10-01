#include "settings_window.h"

#include <algorithm>

bool SettingsWindow::Show(HICON icon, const MonitorTarget& target)
{
    const bool creating = !IsWindow();
    if (creating) {
        initial_target_ = target;
        const UINT dpi = DisplayWindowDpi(nullptr);
        RECT bounds{0, 0, MulDiv(800, dpi, 96), MulDiv(640, dpi, 96)};
        AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW);
        // _U_RECT uses the rectangle's width and height, not its original offset.
        OffsetRect(&bounds, -bounds.left, -bounds.top);
        if (Create(nullptr, &bounds, L"MinimizeWindows \u2014 Settings",
                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, WS_EX_APPWINDOW) == nullptr) { return false; }
        SetIcon(icon, TRUE);
        SetIcon(icon, FALSE);
        CenterWindow();
    }
    display_.Refresh();
    ShowWindow(IsIconic() ? SW_RESTORE : SW_SHOW);
    // Override the launcher's hidden/minimized STARTUPINFO on explicit activation.
    if (!IsWindowVisible() || IsIconic()) { ShowWindow(SW_RESTORE); }
    ::SetForegroundWindow(m_hWnd);
    if (creating) { ::SetFocus(GetDlgItem(DisplayId).GetDlgItem(DisplayPage::TopologyId)); }
    return true;
}

BOOL SettingsWindow::PreTranslateMessage(MSG* message)
{
    if (!IsWindow() || (message->hwnd != m_hWnd && !::IsChild(m_hWnd, message->hwnd))) { return FALSE; }
    return ::IsDialogMessageW(m_hWnd, message);
}

LRESULT SettingsWindow::OnCreate(UINT, WPARAM, LPARAM, BOOL&)
{
    tab_ = CreateWindowExW(0, L"BUTTON", L"Display", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(DisplayTabId)),
        _Module.GetModuleInstance(), nullptr);
    RECT empty{};
    if (tab_ == nullptr || !display_.CreatePage(m_hWnd, empty, initial_target_)) { return -1; }
    display_.SetDlgCtrlID(DisplayId);
    loop_ = _Module.GetMessageLoop();
    if (loop_ != nullptr) { loop_->AddMessageFilter(this); }
    Layout();
    return 0;
}

void SettingsWindow::Layout()
{
    RECT client{};
    GetClientRect(&client);
    const UINT dpi = DisplayWindowDpi(m_hWnd);
    if (dpi != tab_dpi_ || tab_font_ == nullptr) {
        if (tab_font_ != nullptr) { DeleteObject(tab_font_); }
        tab_font_ = CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        tab_dpi_ = dpi;
    }
    const int header = MulDiv(56, dpi, 96);
    ::SetWindowPos(tab_, nullptr, MulDiv(20, dpi, 96), MulDiv(10, dpi, 96),
        MulDiv(112, dpi, 96), MulDiv(38, dpi, 96), SWP_NOZORDER | SWP_NOACTIVATE);
    if (display_.IsWindow()) {
        display_.SetWindowPos(nullptr, 0, header, client.right, (std::max<int>)(1, client.bottom - header),
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    Invalidate(FALSE);
}

void SettingsWindow::PaintHeader(HDC dc)
{
    RECT client{};
    GetClientRect(&client);
    SetDCBrushColor(dc, RGB(32, 32, 32));
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    const int y = MulDiv(55, DisplayWindowDpi(m_hWnd), 96);
    RECT line{0, y, client.right, y + 1};
    SetDCBrushColor(dc, RGB(55, 55, 55));
    FillRect(dc, &line, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

LRESULT SettingsWindow::OnSize(UINT, WPARAM size, LPARAM, BOOL&)
{
    if (size != SIZE_MINIMIZED) { Layout(); }
    return 0;
}

LRESULT SettingsWindow::OnMinimumSize(UINT, WPARAM, LPARAM parameter, BOOL&)
{
    const UINT dpi = DisplayWindowDpi(m_hWnd);
    RECT bounds{0, 0, MulDiv(640, dpi, 96), MulDiv(520, dpi, 96)};
    AdjustWindowRectEx(&bounds, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW);
    auto minimum = reinterpret_cast<MINMAXINFO*>(parameter);
    minimum->ptMinTrackSize = {bounds.right - bounds.left, bounds.bottom - bounds.top};
    return 0;
}

LRESULT SettingsWindow::OnDpiChanged(UINT, WPARAM, LPARAM parameter, BOOL&)
{
    const auto bounds = reinterpret_cast<RECT*>(parameter);
    if (bounds != nullptr) { SetWindowPos(nullptr, bounds, SWP_NOZORDER | SWP_NOACTIVATE); }
    Layout();
    return 0;
}

LRESULT SettingsWindow::OnDisplayChange(UINT, WPARAM, LPARAM, BOOL&)
{
    if (display_.IsWindow()) { display_.Refresh(); }
    return 0;
}

LRESULT SettingsWindow::OnPaint(UINT, WPARAM, LPARAM, BOOL&)
{
    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(&paint);
    PaintHeader(dc);
    EndPaint(&paint);
    return 0;
}

LRESULT SettingsWindow::OnPrint(UINT, WPARAM dc, LPARAM, BOOL&) { PaintHeader(reinterpret_cast<HDC>(dc)); return 0; }

LRESULT SettingsWindow::OnDrawItem(UINT, WPARAM, LPARAM parameter, BOOL& handled)
{
    const auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(parameter);
    if (draw->CtlID != DisplayTabId) { handled = FALSE; return 0; }
    SetDCBrushColor(draw->hDC, RGB(42, 42, 42));
    FillRect(draw->hDC, &draw->rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, RGB(245, 245, 245));
    const HGDIOBJ font = SelectObject(draw->hDC,
        tab_font_ != nullptr ? tab_font_ : GetStockObject(DEFAULT_GUI_FONT));
    RECT text = draw->rcItem;
    DrawTextW(draw->hDC, L"Display", -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(draw->hDC, font);
    RECT accent = draw->rcItem;
    accent.top = accent.bottom - MulDiv(3, DisplayWindowDpi(m_hWnd), 96);
    SetDCBrushColor(draw->hDC, RGB(0, 120, 212));
    FillRect(draw->hDC, &accent, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if (draw->itemState & ODS_FOCUS) {
        RECT focus = draw->rcItem;
        InflateRect(&focus, -4, -4);
        DrawFocusRect(draw->hDC, &focus);
    }
    return TRUE;
}

LRESULT SettingsWindow::OnTab(WORD, WORD, HWND, BOOL&)
{
    ::SetFocus(display_.GetDlgItem(DisplayPage::TopologyId));
    return 0;
}

LRESULT SettingsWindow::OnClose(UINT, WPARAM, LPARAM, BOOL&)
{
    DestroyWindow();
    return 0;
}

LRESULT SettingsWindow::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled)
{
    if (loop_ != nullptr) { loop_->RemoveMessageFilter(this); loop_ = nullptr; }
    if (tab_font_ != nullptr) { DeleteObject(tab_font_); tab_font_ = nullptr; }
    // Settings owns only its UI; the tray host owns the application lifetime.
    handled = TRUE;
    return 0;
}
