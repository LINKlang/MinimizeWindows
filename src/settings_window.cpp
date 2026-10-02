#include "settings_window.h"

#include <algorithm>

bool SettingsWindow::Show(HICON icon, const std::vector<std::wstring>& devices, SaveMonitorSelection save)
{
    const bool creating = !IsWindow();
    if (creating) {
        selected_tab_ = DisplayTabId;
        configured_devices_ = devices;
        save_ = std::move(save);
        dpi_ = DisplayWindowDpi(nullptr);
        RECT bounds{0, 0, MulDiv(800, dpi_, 96), MulDiv(640, dpi_, 96)};
        DisplayAdjustWindowRectForDpi(&bounds, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW, dpi_);
        // _U_RECT uses the rectangle's width and height, not its original offset.
        OffsetRect(&bounds, -bounds.left, -bounds.top);
        if (Create(nullptr, &bounds, L"MinimizeWindows \u2014 Settings",
                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, WS_EX_APPWINDOW) == nullptr) { return false; }
        // Creation establishes the window's actual monitor DPI.
        bounds = {0, 0, MulDiv(800, dpi_, 96), MulDiv(640, dpi_, 96)};
        DisplayAdjustWindowRectForDpi(&bounds, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW, dpi_);
        SetWindowPos(nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        const HICON large_icon = static_cast<HICON>(LoadImageW(_Module.GetResourceInstance(),
            MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
        SetIcon(large_icon != nullptr ? large_icon : icon, TRUE);
        SetIcon(icon, FALSE);
        CenterWindow();
    }
    display_.SetConfiguration(devices);
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
    if (licenses_.PreTranslateMessage(message)) { return TRUE; }
    if (!IsWindow() || (message->hwnd != m_hWnd && !::IsChild(m_hWnd, message->hwnd))) { return FALSE; }
    return ::IsDialogMessageW(m_hWnd, message);
}

LRESULT SettingsWindow::OnCreate(UINT, WPARAM, LPARAM, BOOL&)
{
    dpi_ = DisplayWindowDpi(m_hWnd);
    tab_ = CreateWindowExW(0, L"BUTTON", L"Display", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(DisplayTabId)),
        _Module.GetModuleInstance(), nullptr);
    settings_tab_ = CreateWindowExW(0, L"BUTTON", L"Settings", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(SettingsTabId)),
        _Module.GetModuleInstance(), nullptr);
    about_tab_ = CreateWindowExW(0, L"BUTTON", L"About", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(AboutTabId)),
        _Module.GetModuleInstance(), nullptr);
    RECT empty{};
    if (tab_ == nullptr || settings_tab_ == nullptr || about_tab_ == nullptr
        || !display_.CreatePage(m_hWnd, empty, configured_devices_, save_)
        || !settings_.CreatePage(m_hWnd, empty) || !about_.CreatePage(m_hWnd, empty)) { return -1; }
    display_.SetDlgCtrlID(DisplayId);
    settings_.SetDlgCtrlID(SettingsId);
    about_.SetDlgCtrlID(AboutId);
    display_.SetDpi(dpi_);
    settings_.SetDpi(dpi_);
    about_.SetDpi(dpi_);
    loop_ = _Module.GetMessageLoop();
    if (loop_ != nullptr) { loop_->AddMessageFilter(this); }
    Layout();
    return 0;
}

void SettingsWindow::Layout()
{
    RECT client{};
    GetClientRect(&client);
    const UINT dpi = dpi_;
    if (dpi != tab_dpi_ || tab_font_ == nullptr) {
        if (tab_font_ != nullptr) { DeleteObject(tab_font_); }
        tab_font_ = CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        tab_dpi_ = dpi;
    }
    const int header = MulDiv(56, dpi, 96);
    ::SetWindowPos(tab_, nullptr, MulDiv(20, dpi, 96), MulDiv(10, dpi, 96),
        MulDiv(112, dpi, 96), MulDiv(38, dpi, 96), SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowPos(settings_tab_, nullptr, MulDiv(140, dpi, 96), MulDiv(10, dpi, 96),
        MulDiv(112, dpi, 96), MulDiv(38, dpi, 96), SWP_NOZORDER | SWP_NOACTIVATE);
    ::SetWindowPos(about_tab_, nullptr, MulDiv(260, dpi, 96), MulDiv(10, dpi, 96),
        MulDiv(112, dpi, 96), MulDiv(38, dpi, 96), SWP_NOZORDER | SWP_NOACTIVATE);
    if (display_.IsWindow()) {
        display_.SetWindowPos(nullptr, 0, header, client.right, (std::max<int>)(1, client.bottom - header),
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (settings_.IsWindow()) {
        settings_.SetWindowPos(nullptr, 0, header, client.right, (std::max<int>)(1, client.bottom - header),
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (about_.IsWindow()) {
        about_.SetWindowPos(nullptr, 0, header, client.right, (std::max<int>)(1, client.bottom - header),
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
    const int y = MulDiv(55, dpi_, 96);
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
    RECT bounds{0, 0, MulDiv(640, dpi_, 96), MulDiv(520, dpi_, 96)};
    DisplayAdjustWindowRectForDpi(&bounds, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW, dpi_);
    auto minimum = reinterpret_cast<MINMAXINFO*>(parameter);
    minimum->ptMinTrackSize = {bounds.right - bounds.left, bounds.bottom - bounds.top};
    return 0;
}

LRESULT SettingsWindow::OnDpiChanged(UINT, WPARAM dpi, LPARAM parameter, BOOL&)
{
    // Child GetDpiForWindow values can still be old while the parent handles this message.
    dpi_ = LOWORD(dpi);
    display_.SetDpi(dpi_);
    settings_.SetDpi(dpi_);
    about_.SetDpi(dpi_);
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
    if (draw->CtlID != DisplayTabId && draw->CtlID != SettingsTabId && draw->CtlID != AboutTabId) { handled = FALSE; return 0; }
    const bool selected = selected_tab_ == draw->CtlID;
    SetDCBrushColor(draw->hDC, selected ? RGB(42, 42, 42) : RGB(32, 32, 32));
    FillRect(draw->hDC, &draw->rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(draw->hDC, TRANSPARENT);
    SetTextColor(draw->hDC, RGB(245, 245, 245));
    const HGDIOBJ font = SelectObject(draw->hDC,
        tab_font_ != nullptr ? tab_font_ : GetStockObject(DEFAULT_GUI_FONT));
    RECT text = draw->rcItem;
    const wchar_t* caption = draw->CtlID == DisplayTabId ? L"Display" : draw->CtlID == SettingsTabId ? L"Settings" : L"About";
    DrawTextW(draw->hDC, caption, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(draw->hDC, font);
    if (selected) {
        RECT accent = draw->rcItem;
        accent.top = accent.bottom - MulDiv(3, dpi_, 96);
        SetDCBrushColor(draw->hDC, RGB(0, 120, 212));
        FillRect(draw->hDC, &accent, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    }
    if (draw->itemState & ODS_FOCUS) {
        RECT focus = draw->rcItem;
        InflateRect(&focus, -4, -4);
        DrawFocusRect(draw->hDC, &focus);
    }
    return TRUE;
}

LRESULT SettingsWindow::OnTab(WORD, WORD id, HWND, BOOL&)
{
    selected_tab_ = id;
    display_.ShowWindow(id == DisplayTabId ? SW_SHOW : SW_HIDE);
    settings_.ShowWindow(id == SettingsTabId ? SW_SHOW : SW_HIDE);
    about_.ShowWindow(id == AboutTabId ? SW_SHOW : SW_HIDE);
    ::InvalidateRect(tab_, nullptr, FALSE);
    ::InvalidateRect(settings_tab_, nullptr, FALSE);
    ::InvalidateRect(about_tab_, nullptr, FALSE);
    const HWND focus = id == DisplayTabId ? display_.GetDlgItem(DisplayPage::TopologyId)
        : id == SettingsTabId ? settings_.GetDlgItem(SettingsPage::OpenStartupId) : about_.GetDlgItem(AboutPage::GitHubId);
    ::SetFocus(focus);
    return 0;
}

LRESULT SettingsWindow::OnClose(UINT, WPARAM, LPARAM, BOOL&)
{
    DestroyWindow();
    return 0;
}

LRESULT SettingsWindow::OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled)
{
    if (licenses_.IsWindow()) { licenses_.DestroyWindow(); }
    if (loop_ != nullptr) { loop_->RemoveMessageFilter(this); loop_ = nullptr; }
    if (tab_font_ != nullptr) { DeleteObject(tab_font_); tab_font_ = nullptr; }
    // Settings owns only its UI; the tray host owns the application lifetime.
    handled = TRUE;
    return 0;
}
