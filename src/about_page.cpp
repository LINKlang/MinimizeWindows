#include "about_page.h"
#include "display_page.h"

bool AboutPage::CreatePage(HWND parent, const RECT& bounds)
{
    RECT rectangle = bounds;
    return Create(parent, &rectangle, nullptr, WS_CHILD | WS_CLIPCHILDREN, WS_EX_CONTROLPARENT) != nullptr;
}

LRESULT AboutPage::OnCreate(UINT, WPARAM, LPARAM, BOOL&)
{
    dpi_ = DisplayWindowDpi(m_hWnd);
    const wchar_t* captions[] = {L"GitHub", L"View License", L"Third-party licenses...", L"View original project"};
    for (UINT i = 0; i < ARRAYSIZE(captions); ++i) {
        if (CreateWindowExW(0, L"BUTTON", captions[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(GitHubId + i)),
                _Module.GetModuleInstance(), nullptr) == nullptr) { return -1; }
    }
    Layout();
    return 0;
}

void AboutPage::SetDpi(UINT dpi)
{
    dpi_ = dpi;
    if (IsWindow()) { Layout(); }
}

void AboutPage::Layout()
{
    if (body_font_ == nullptr || font_dpi_ != dpi_) {
        if (body_font_ != nullptr) { DeleteObject(body_font_); }
        if (title_font_ != nullptr) { DeleteObject(title_font_); }
        if (heading_font_ != nullptr) { DeleteObject(heading_font_); }
        auto font = [this](int size, int weight) {
            return CreateFontW(-Px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        };
        body_font_ = font(14, FW_NORMAL);
        title_font_ = font(26, FW_SEMIBOLD);
        heading_font_ = font(16, FW_SEMIBOLD);
        font_dpi_ = dpi_;
    }
    auto place = [this](UINT id, int x, int y, int width) {
        ::SetWindowPos(GetDlgItem(id), nullptr, Px(x), Px(y), Px(width), Px(32), SWP_NOZORDER | SWP_NOACTIVATE);
        SendDlgItemMessage(id, WM_SETFONT, reinterpret_cast<WPARAM>(body_font_), TRUE);
    };
    place(GitHubId, 24, 180, 100);
    place(LicenseId, 136, 180, 124);
    place(ThirdPartyId, 24, 304, 212);
    place(OriginalProjectId, 24, 384, 192);
    Invalidate(FALSE);
}

void AboutPage::Paint(HDC dc)
{
    RECT client{};
    GetClientRect(&client);
    SetDCBrushColor(dc, RGB(32, 32, 32));
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(dc, TRANSPARENT);
    auto text = [this, dc, client](const wchar_t* value, int y, int height, HFONT font, COLORREF color) {
        const HGDIOBJ old = SelectObject(dc, font != nullptr ? font : GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(dc, color);
        RECT bounds{Px(24), Px(y), client.right - Px(24), Px(y + height)};
        DrawTextW(dc, value, -1, &bounds, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
    };
    constexpr COLORREF foreground = RGB(245, 245, 245), muted = RGB(173, 173, 173);
    text(L"MinimizeWindows", 18, 32, title_font_, foreground);
    const std::wstring version = std::wstring(L"Version ") + MinimizeWindowsVersion;
    text(version.c_str(), 52, 20, body_font_, muted);
    text(L"A lightweight configurable Win+D replacement\nfor multi-monitor Windows setups.", 80, 40, body_font_, foreground);
    text(L"Copyright \u00a9 2026 LINKlang", 128, 20, body_font_, foreground);
    text(L"Licensed under the MIT License", 148, 20, body_font_, muted);
    text(L"Third-party software", 228, 24, heading_font_, foreground);
    text(L"Windows Template Library (WTL) 10.01 \u2014 MS-PL", 254, 20, body_font_, muted);
    text(L"nlohmann/json 3.12.0 \u2014 MIT", 274, 20, body_font_, muted);
    text(L"Inspired by deadem/minimize-windows", 354, 20, body_font_, foreground);
}

LRESULT AboutPage::OnSize(UINT, WPARAM, LPARAM, BOOL&) { Layout(); return 0; }
LRESULT AboutPage::OnPaint(UINT, WPARAM, LPARAM, BOOL&)
{
    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(&paint);
    Paint(dc);
    EndPaint(&paint);
    return 0;
}
LRESULT AboutPage::OnPrint(UINT, WPARAM dc, LPARAM, BOOL&) { Paint(reinterpret_cast<HDC>(dc)); return 0; }
LRESULT AboutPage::OnDrawItem(UINT, WPARAM, LPARAM parameter, BOOL&)
{
    DrawDarkButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(parameter), body_font_);
    return TRUE;
}
LRESULT AboutPage::OnGitHub(WORD, WORD, HWND, BOOL&)
{
    OpenExternalLink(m_hWnd, L"https://github.com/LINKlang/MinimizeWindows");
    return 0;
}
LRESULT AboutPage::OnOriginalProject(WORD, WORD, HWND, BOOL&)
{
    OpenExternalLink(m_hWnd, L"https://github.com/deadem/minimize-windows");
    return 0;
}
LRESULT AboutPage::OnLicense(WORD, WORD id, HWND, BOOL&)
{
    if (!licenses_.Show(::GetAncestor(m_hWnd, GA_ROOT), id == ThirdPartyId)) {
        ::MessageBoxW(m_hWnd, L"Unable to open the license window.", L"MinimizeWindows", MB_OK | MB_ICONERROR);
    }
    return 0;
}
LRESULT AboutPage::OnDestroy(UINT, WPARAM, LPARAM, BOOL&)
{
    if (body_font_ != nullptr) { DeleteObject(body_font_); body_font_ = nullptr; }
    if (title_font_ != nullptr) { DeleteObject(title_font_); title_font_ = nullptr; }
    if (heading_font_ != nullptr) { DeleteObject(heading_font_); heading_font_ = nullptr; }
    return 0;
}
