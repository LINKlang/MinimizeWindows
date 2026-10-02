#pragma once

#include "wtl_support.h"

class SettingsPage : public ATL::CWindowImpl<SettingsPage> {
public:
    DECLARE_WND_CLASS_EX(L"MinimizeWindows.SettingsPage", 0, 0)
    static constexpr UINT OpenStartupId = 340, CreateStartupId = 341;
    bool CreatePage(HWND parent, const RECT& bounds);
    void SetDpi(UINT dpi);

    BEGIN_MSG_MAP(SettingsPage)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_PRINTCLIENT, OnPrint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnErase)
        MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        COMMAND_HANDLER(OpenStartupId, BN_CLICKED, OnOpenStartup)
        COMMAND_HANDLER(CreateStartupId, BN_CLICKED, OnCreateStartup)
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
    LRESULT OnOpenStartup(WORD, WORD, HWND, BOOL&);
    LRESULT OnCreateStartup(WORD, WORD, HWND, BOOL&);
    HFONT body_font_ = nullptr, title_font_ = nullptr;
    UINT dpi_ = 96, font_dpi_ = 0;
};
