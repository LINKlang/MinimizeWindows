#include "licenses_dialog.h"
#include "display_page.h"

#include <shellapi.h>
#include <algorithm>

namespace {

struct LicenseInfo {
    const wchar_t* name;
    const wchar_t* version;
    const wchar_t* license;
    const wchar_t* url;
    const wchar_t* attribution;
    UINT resource;
};

const LicenseInfo Licenses[] = {
    {L"MinimizeWindows", MinimizeWindowsVersion, L"MIT License", L"https://github.com/LINKlang/MinimizeWindows",
        L"Copyright (c) 2026 LINKlang", IDR_APP_LICENSE},
    {L"Windows Template Library (WTL)", L"10.01", L"Microsoft Public License (MS-PL)",
        L"https://sourceforge.net/projects/wtl/",
        L"Copyright (C) Microsoft Corporation, WTL Team. All rights reserved.", IDR_WTL_LICENSE},
    {L"nlohmann/json", L"3.12.0", L"MIT License", L"https://github.com/nlohmann/json",
        L"Copyright (c) 2013-2025 Niels Lohmann\r\n\r\n"
        L"Bundled copyright / source notices from json.hpp:\r\n"
        L"SPDX-FileCopyrightText: 2016 - 2021 Evan Nemerson <evan@nemerson.com>\r\n"
        L"Hedley - https://nemequ.github.io/hedley\r\n"
        L"SPDX-FileCopyrightText: 2018 The Abseil Authors\r\n"
        L"https://github.com/abseil/abseil-cpp/blob/10cb35e459f5ecca5b2ff107635da0bfa41011b4/absl/utility/utility.h\r\n"
        L"which is part of Google Abseil (https://github.com/abseil/abseil-cpp), licensed under the Apache License 2.0.\r\n"
        L"SPDX-FileCopyrightText: 2008 - 2009 Bj\u00f6rn Hoehrmann <bjoern@hoehrmann.de>\r\n"
        L"Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>\r\n"
        L"http://bjoern.hoehrmann.de/utf-8/decoder/dfa/\r\n"
        L"SPDX-FileCopyrightText: 2009 Florian Loitsch <https://florian.loitsch.com/>\r\n"
        L"The code is distributed under the MIT license, Copyright (c) 2009 Florian Loitsch.\r\n"
        L"http://florian.loitsch.com/publications (bench.tar.gz).", IDR_JSON_LICENSE}
};

bool ReadLicense(UINT id, std::wstring& text)
{
    const HINSTANCE instance = _Module.GetResourceInstance();
    const HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (resource == nullptr) { return false; }
    const DWORD size = SizeofResource(instance, resource);
    const char* bytes = static_cast<const char*>(LockResource(LoadResource(instance, resource)));
    if (bytes == nullptr || size == 0 || size > INT_MAX) { return false; }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, size, nullptr, 0);
    if (length == 0) { return false; }
    std::wstring decoded(length, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, size, &decoded[0], length)) { return false; }
    text.clear();
    for (size_t i = 0; i < decoded.size(); ++i) {
        if (decoded[i] == L'\n' && (i == 0 || decoded[i - 1] != L'\r')) { text += L'\r'; }
        text += decoded[i];
    }
    return true;
}

} // namespace

void OpenExternalLink(HWND owner, const wchar_t* url)
{
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", url, nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        MessageBoxW(owner, L"Unable to open the link in your browser.", L"MinimizeWindows", MB_OK | MB_ICONERROR);
    }
}

void DrawDarkButton(const DRAWITEMSTRUCT& draw, HFONT font)
{
    const COLORREF color = (draw.itemState & ODS_SELECTED) ? RGB(65, 65, 65) : RGB(56, 56, 56);
    SetDCBrushColor(draw.hDC, color);
    FillRect(draw.hDC, &draw.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(draw.hDC, TRANSPARENT);
    SetTextColor(draw.hDC, RGB(245, 245, 245));
    const HGDIOBJ old = SelectObject(draw.hDC, font != nullptr ? font : GetStockObject(DEFAULT_GUI_FONT));
    wchar_t caption[128]{};
    GetWindowTextW(draw.hwndItem, caption, ARRAYSIZE(caption));
    RECT text = draw.rcItem;
    DrawTextW(draw.hDC, caption, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(draw.hDC, old);
    if ((draw.itemState & ODS_FOCUS) != 0) {
        RECT focus = draw.rcItem;
        InflateRect(&focus, -4, -4);
        DrawFocusRect(draw.hDC, &focus);
    }
}

bool LicensesDialog::Show(HWND owner, bool third_party)
{
    third_party_ = third_party;
    const bool creating = !IsWindow();
    if (creating) {
        component_ = 0;
        if (Create(owner) == nullptr) { return false; }
        CenterWindow(owner);
    }
    UpdateContent();
    Layout();
    ShowWindow(IsIconic() ? SW_RESTORE : SW_SHOW);
    ::SetForegroundWindow(m_hWnd);
    ::SetFocus(GetDlgItem(third_party_ ? ComponentId : TextId));
    return true;
}

BOOL LicensesDialog::PreTranslateMessage(MSG* message)
{
    if (!IsWindow() || (message->hwnd != m_hWnd && !::IsChild(m_hWnd, message->hwnd))) { return FALSE; }
    if (message->message == WM_KEYDOWN && message->wParam == VK_ESCAPE) {
        DestroyWindow();
        return TRUE;
    }
    if (message->message == WM_KEYDOWN && message->wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)
        && (message->hwnd == GetDlgItem(TextId) || message->hwnd == GetDlgItem(MetadataId))) {
        ::SendMessageW(message->hwnd, EM_SETSEL, 0, -1);
        return TRUE;
    }
    return ::IsDialogMessageW(m_hWnd, message);
}

LRESULT LicensesDialog::OnInit(UINT, WPARAM, LPARAM, BOOL&)
{
    background_ = CreateSolidBrush(RGB(32, 32, 32));
    panel_ = CreateSolidBrush(RGB(23, 23, 23));
    component_label_ = CreateWindowExW(0, L"STATIC", L"Component", WS_CHILD | WS_VISIBLE,
        0, 0, 0, 0, m_hWnd, nullptr, _Module.GetModuleInstance(), nullptr);
    auto control = [this](UINT id, const wchar_t* type, const wchar_t* caption, DWORD style, DWORD ex = 0) {
        return CreateWindowExW(ex, type, caption, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
            0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), _Module.GetModuleInstance(), nullptr);
    };
    if (background_ == nullptr || panel_ == nullptr || component_label_ == nullptr
        || control(ComponentId, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL) == nullptr
        || control(MetadataId, L"EDIT", L"", ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL) == nullptr
        || control(TextId, L"EDIT", L"", ES_READONLY | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL, WS_EX_CLIENTEDGE) == nullptr
        || control(HomepageId, L"BUTTON", L"Open Homepage", BS_OWNERDRAW) == nullptr
        || control(IDCANCEL, L"BUTTON", L"Close", BS_OWNERDRAW) == nullptr) {
        DestroyWindow();
        return FALSE;
    }
    SendDlgItemMessage(ComponentId, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Windows Template Library (WTL) 10.01"));
    SendDlgItemMessage(ComponentId, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"nlohmann/json 3.12.0"));
    SendDlgItemMessage(TextId, EM_SETLIMITTEXT, 0, 0);
    dpi_ = DisplayWindowDpi(m_hWnd);
    RECT bounds{0, 0, Px(720), Px(580)};
    AdjustWindowRectEx(&bounds, GetStyle(), FALSE, GetExStyle());
    SetWindowPos(nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    Layout();
    return TRUE;
}

void LicensesDialog::UpdateContent()
{
    const LicenseInfo& info = Licenses[third_party_ ? component_ + 1 : 0];
    SetWindowTextW(third_party_ ? L"Third-party software licenses" : L"MinimizeWindows License");
    SendDlgItemMessage(ComponentId, CB_SETCURSEL, component_, 0);
    const std::wstring metadata = std::wstring(L"Name: ") + info.name + L"\r\nVersion: " + info.version
        + L"\r\nLicense: " + info.license + L"\r\nHomepage: " + info.url
        + L"\r\nCopyright / attribution:\r\n" + info.attribution;
    SetDlgItemTextW(MetadataId, metadata.c_str());
    std::wstring license;
    if (!ReadLicense(info.resource, license)) { license = L"Unable to load the embedded license text."; }
    SetDlgItemTextW(TextId, license.c_str());
    ::ShowWindow(component_label_, third_party_ ? SW_SHOW : SW_HIDE);
    GetDlgItem(ComponentId).ShowWindow(third_party_ ? SW_SHOW : SW_HIDE);
}

void LicensesDialog::Layout()
{
    if (GetDlgItem(TextId) == nullptr) { return; }
    const UINT dpi = DisplayWindowDpi(m_hWnd);
    if (font_ == nullptr || dpi != dpi_) {
        const HFONT next = CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        if (font_ != nullptr) { DeleteObject(font_); }
        font_ = next;
        dpi_ = dpi;
        for (HWND child = ::GetWindow(m_hWnd, GW_CHILD); child != nullptr; child = ::GetWindow(child, GW_HWNDNEXT)) {
            ::SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        }
        SendDlgItemMessage(ComponentId, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), Px(24));
        SendDlgItemMessage(ComponentId, CB_SETITEMHEIGHT, 0, Px(24));
    }
    RECT client{};
    GetClientRect(&client);
    const int margin = Px(20), width = (std::max)(1L, client.right - 2 * margin);
    auto place = [this](HWND control, int x, int y, int w, int h) {
        ::SetWindowPos(control, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    place(component_label_, margin, Px(14), width, Px(20));
    place(GetDlgItem(ComponentId), margin, Px(38), width, Px(180));
    const int metadata_top = third_party_ ? Px(80) : margin;
    const int text_top = metadata_top + Px(144);
    const int footer = client.bottom - Px(52);
    place(GetDlgItem(MetadataId), margin, metadata_top, width, Px(128));
    place(GetDlgItem(TextId), margin, text_top, width, (std::max)(Px(80), footer - text_top - Px(16)));
    place(GetDlgItem(HomepageId), client.right - margin - Px(252), footer, Px(156), Px(32));
    place(GetDlgItem(IDCANCEL), client.right - margin - Px(84), footer, Px(84), Px(32));
    Invalidate(FALSE);
}

LRESULT LicensesDialog::OnSize(UINT, WPARAM size, LPARAM, BOOL&)
{
    if (size != SIZE_MINIMIZED) { Layout(); }
    return 0;
}

LRESULT LicensesDialog::OnMinimumSize(UINT, WPARAM, LPARAM parameter, BOOL&)
{
    const UINT dpi = DisplayWindowDpi(m_hWnd);
    RECT bounds{0, 0, MulDiv(600, dpi, 96), MulDiv(440, dpi, 96)};
    AdjustWindowRectEx(&bounds, GetStyle(), FALSE, GetExStyle());
    reinterpret_cast<MINMAXINFO*>(parameter)->ptMinTrackSize = {bounds.right - bounds.left, bounds.bottom - bounds.top};
    return 0;
}

LRESULT LicensesDialog::OnDpiChanged(UINT, WPARAM, LPARAM parameter, BOOL&)
{
    const RECT* bounds = reinterpret_cast<const RECT*>(parameter);
    if (bounds != nullptr) { SetWindowPos(nullptr, bounds, SWP_NOZORDER | SWP_NOACTIVATE); }
    Layout();
    return 0;
}

LRESULT LicensesDialog::OnControlColor(UINT, WPARAM parameter, LPARAM control, BOOL&)
{
    const HDC dc = reinterpret_cast<HDC>(parameter);
    const bool label = reinterpret_cast<HWND>(control) == component_label_;
    SetTextColor(dc, RGB(245, 245, 245));
    SetBkColor(dc, label ? RGB(32, 32, 32) : RGB(23, 23, 23));
    return reinterpret_cast<LRESULT>(label ? background_ : panel_);
}

LRESULT LicensesDialog::OnPaint(UINT, WPARAM, LPARAM, BOOL&)
{
    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(&paint);
    RECT client{};
    GetClientRect(&client);
    FillRect(dc, &client, background_);
    EndPaint(&paint);
    return 0;
}

LRESULT LicensesDialog::OnPrint(UINT, WPARAM parameter, LPARAM, BOOL&)
{
    RECT client{};
    GetClientRect(&client);
    FillRect(reinterpret_cast<HDC>(parameter), &client, background_);
    return 0;
}

LRESULT LicensesDialog::OnDrawItem(UINT, WPARAM, LPARAM parameter, BOOL&)
{
    const auto& draw = *reinterpret_cast<const DRAWITEMSTRUCT*>(parameter);
    if (draw.CtlID != ComponentId) { DrawDarkButton(draw, font_); return TRUE; }
    SetDCBrushColor(draw.hDC, (draw.itemState & ODS_SELECTED) ? RGB(0, 120, 212) : RGB(23, 23, 23));
    FillRect(draw.hDC, &draw.rcItem, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    if (draw.itemID != static_cast<UINT>(-1)) {
        const wchar_t* name = draw.itemID == 0 ? L"Windows Template Library (WTL) 10.01" : L"nlohmann/json 3.12.0";
        RECT text = draw.rcItem;
        text.left += Px(8);
        SetBkMode(draw.hDC, TRANSPARENT);
        SetTextColor(draw.hDC, RGB(245, 245, 245));
        const HGDIOBJ old = SelectObject(draw.hDC, font_ != nullptr ? font_ : GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(draw.hDC, name, -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(draw.hDC, old);
    }
    if (draw.itemState & ODS_FOCUS) { DrawFocusRect(draw.hDC, &draw.rcItem); }
    return TRUE;
}

LRESULT LicensesDialog::OnMeasureItem(UINT, WPARAM, LPARAM parameter, BOOL&)
{
    reinterpret_cast<MEASUREITEMSTRUCT*>(parameter)->itemHeight = Px(24);
    return TRUE;
}

LRESULT LicensesDialog::OnComponent(WORD, WORD, HWND, BOOL&)
{
    const LRESULT selected = SendDlgItemMessage(ComponentId, CB_GETCURSEL);
    if (selected >= 0 && selected < 2) { component_ = static_cast<int>(selected); UpdateContent(); }
    return 0;
}

LRESULT LicensesDialog::OnHomepage(WORD, WORD, HWND, BOOL&)
{
    OpenExternalLink(m_hWnd, Licenses[third_party_ ? component_ + 1 : 0].url);
    return 0;
}

LRESULT LicensesDialog::OnClose(UINT, WPARAM, LPARAM, BOOL&) { DestroyWindow(); return 0; }
LRESULT LicensesDialog::OnCloseCommand(WORD, WORD, HWND, BOOL&) { DestroyWindow(); return 0; }

LRESULT LicensesDialog::OnDestroy(UINT, WPARAM, LPARAM, BOOL&)
{
    if (font_ != nullptr) { DeleteObject(font_); font_ = nullptr; }
    if (background_ != nullptr) { DeleteObject(background_); background_ = nullptr; }
    if (panel_ != nullptr) { DeleteObject(panel_); panel_ = nullptr; }
    return 0;
}
