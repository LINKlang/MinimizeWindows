#pragma once

#include "wtl_support.h"

class SettingsWindow : public WTL::CFrameWindowImpl<SettingsWindow> {
public:
    DECLARE_FRAME_WND_CLASS_EX(L"MinimizeWindows.Settings", 0, 0, COLOR_WINDOW)

    bool Show(HICON icon);

    BEGIN_MSG_MAP(SettingsWindow)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
        CHAIN_MSG_MAP(WTL::CFrameWindowImpl<SettingsWindow>)
    END_MSG_MAP()

private:
    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&);
    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL& handled);
};
