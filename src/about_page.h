#pragma once

#include "licenses_dialog.h"

class AboutPage : public ATL::CWindowImpl<AboutPage> {
public:
    DECLARE_WND_CLASS_EX(L"MinimizeWindows.AboutPage", 0, 0)
    static constexpr UINT GitHubId = 320, LicenseId = 321, ThirdPartyId = 322, OriginalProjectId = 323;
    explicit AboutPage(LicensesDialog& licenses) : licenses_(licenses) { }
    bool CreatePage(HWND parent, const RECT& bounds);

    BEGIN_MSG_MAP(AboutPage)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_PRINTCLIENT, OnPrint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnErase)
        MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        COMMAND_ID_HANDLER(GitHubId, OnGitHub)
        COMMAND_ID_HANDLER(LicenseId, OnLicense)
        COMMAND_ID_HANDLER(ThirdPartyId, OnLicense)
        COMMAND_ID_HANDLER(OriginalProjectId, OnOriginalProject)
    END_MSG_MAP()

private:
    int Px(int value) const { return MulDiv(value, dpi_, 96); }
    void Layout();
    void Paint(HDC dc);
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPrint(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnErase(UINT, WPARAM, LPARAM, BOOL&) { return 1; }
    LRESULT OnDrawItem(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnGitHub(WORD, WORD, HWND, BOOL&);
    LRESULT OnLicense(WORD, WORD, HWND, BOOL&);
    LRESULT OnOriginalProject(WORD, WORD, HWND, BOOL&);
    LicensesDialog& licenses_;
    HFONT body_font_ = nullptr, title_font_ = nullptr, heading_font_ = nullptr;
    UINT dpi_ = 96;
};
