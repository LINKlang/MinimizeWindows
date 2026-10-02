#pragma once

#include "wtl_support.h"
#include "display_page.h"
#include "about_page.h"

class SettingsWindow : public WTL::CFrameWindowImpl<SettingsWindow>, public WTL::CMessageFilter {
public:
    DECLARE_FRAME_WND_CLASS_EX(L"MinimizeWindows.Settings", 0, 0, COLOR_WINDOW)

    static constexpr UINT DisplayId = 200, DisplayTabId = 199, AboutId = 210, AboutTabId = 198;
    SettingsWindow() : about_(licenses_) { }
    bool Show(HICON icon, const std::vector<std::wstring>& devices, SaveMonitorSelection save);
    BOOL PreTranslateMessage(MSG* message) override;

    BEGIN_MSG_MAP(SettingsWindow)
        MESSAGE_HANDLER(WM_CREATE, OnCreate)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_GETMINMAXINFO, OnMinimumSize)
        MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
        MESSAGE_HANDLER(WM_DISPLAYCHANGE, OnDisplayChange)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_PRINTCLIENT, OnPrint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnErase)
        MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
        COMMAND_HANDLER(DisplayTabId, BN_CLICKED, OnTab)
        COMMAND_HANDLER(AboutTabId, BN_CLICKED, OnTab)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        CHAIN_MSG_MAP(WTL::CFrameWindowImpl<SettingsWindow>)
    END_MSG_MAP()

private:
    void Layout();
    void PaintHeader(HDC dc);
    LRESULT OnCreate(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnMinimumSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDisplayChange(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPrint(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnErase(UINT, WPARAM, LPARAM, BOOL&) { return 1; }
    LRESULT OnDrawItem(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnTab(WORD, WORD, HWND, BOOL&);
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled);
    std::vector<std::wstring> configured_devices_;
    SaveMonitorSelection save_;
    DisplayPage display_;
    LicensesDialog licenses_;
    AboutPage about_;
    HWND tab_ = nullptr, about_tab_ = nullptr;
    bool showing_about_ = false;
    UINT dpi_ = 96;
    HFONT tab_font_ = nullptr;
    UINT tab_dpi_ = 0;
    WTL::CMessageLoop* loop_ = nullptr;
};
