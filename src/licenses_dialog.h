#pragma once

#include "wtl_support.h"
#include "resources.h"
#include <string>

constexpr wchar_t MinimizeWindowsVersion[] = L"0.1.0";

void OpenExternalLink(HWND owner, const wchar_t* url);
void DrawDarkButton(const DRAWITEMSTRUCT& draw, HFONT font);

class LicensesDialog : public ATL::CDialogImpl<LicensesDialog> {
public:
    enum { IDD = IDD_LICENSES };
    static constexpr UINT ComponentId = 310, MetadataId = 311, TextId = 312, HomepageId = 313;
    bool Show(HWND owner, bool third_party);
    BOOL PreTranslateMessage(MSG* message);

    BEGIN_MSG_MAP(LicensesDialog)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInit)
        MESSAGE_HANDLER(WM_SIZE, OnSize)
        MESSAGE_HANDLER(WM_GETMINMAXINFO, OnMinimumSize)
        MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
        MESSAGE_HANDLER(WM_CTLCOLORSTATIC, OnControlColor)
        MESSAGE_HANDLER(WM_CTLCOLOREDIT, OnControlColor)
        MESSAGE_HANDLER(WM_CTLCOLORLISTBOX, OnControlColor)
        MESSAGE_HANDLER(WM_PAINT, OnPaint)
        MESSAGE_HANDLER(WM_PRINTCLIENT, OnPrint)
        MESSAGE_HANDLER(WM_ERASEBKGND, OnErase)
        MESSAGE_HANDLER(WM_DRAWITEM, OnDrawItem)
        MESSAGE_HANDLER(WM_MEASUREITEM, OnMeasureItem)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        COMMAND_HANDLER(ComponentId, CBN_SELCHANGE, OnComponent)
        COMMAND_ID_HANDLER(HomepageId, OnHomepage)
        COMMAND_ID_HANDLER(IDCANCEL, OnCloseCommand)
        COMMAND_ID_HANDLER(IDOK, OnCloseCommand)
    END_MSG_MAP()

private:
    void Layout();
    void UpdateContent();
    int Px(int value) const { return MulDiv(value, dpi_, 96); }
    LRESULT OnInit(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnMinimumSize(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDpiChanged(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnControlColor(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPaint(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnPrint(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnErase(UINT, WPARAM, LPARAM, BOOL&) { return 1; }
    LRESULT OnDrawItem(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnMeasureItem(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnComponent(WORD, WORD, HWND, BOOL&);
    LRESULT OnHomepage(WORD, WORD, HWND, BOOL&);
    LRESULT OnCloseCommand(WORD, WORD, HWND, BOOL&);
    HWND component_label_ = nullptr;
    HFONT font_ = nullptr;
    HBRUSH background_ = nullptr, panel_ = nullptr;
    UINT dpi_ = 96;
    bool third_party_ = true;
    int component_ = 0;
};
