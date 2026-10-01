#include "tray_application.h"
#include "keyboard_hook.h"
#include "settings_window.h"

#include <shellapi.h>
#include <new>

WTL::CAppModule _Module;

class TrayApplication : public ATL::CWindowImpl<TrayApplication> {
public:
    DECLARE_WND_CLASS_EX(L"MinimizeWindows.TrayHost", 0, COLOR_WINDOW)
    static constexpr UINT TrayCallback = WM_APP + 2;
    static constexpr UINT TrayIconId = 1;
    static constexpr UINT ExitCommand = 100;

    TrayApplication(const MonitorTarget& target, WTL::CMessageLoop& loop)
        : target_(target), loop_(loop) { }
    ~TrayApplication() { Shutdown(); }
    TrayApplication(const TrayApplication&) = delete;
    TrayApplication& operator=(const TrayApplication&) = delete;

    bool Initialize()
    {
        taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");
        if (taskbar_created_ == 0) { return false; }
        RECT bounds{0, 0, 0, 0};
        if (Create(nullptr, &bounds, L"MinimizeWindows", WS_POPUP, WS_EX_TOOLWINDOW) == nullptr) {
            return false;
        }
        icon_ = static_cast<HICON>(LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
        if (icon_ == nullptr || !AddTrayIcon()) { return false; }
        if (!desktop_manager_.StartTracking()) { return false; }
        return keyboard_hook_.Install(m_hWnd);
    }

    int Run()
    {
        running_ = true;
        const int result = loop_.Run();
        running_ = false;
        Shutdown();
        return result;
    }

    void Shutdown()
    {
        CloseResources();
        if (IsWindow()) { DestroyWindow(); }
    }

    BEGIN_MSG_MAP(TrayApplication)
        MESSAGE_HANDLER(TrayCallback, OnTrayCallback)
        MESSAGE_HANDLER(KeyboardHook::WinDMessage, OnWinD)
        MESSAGE_HANDLER(taskbar_created_, OnTaskbarCreated)
        MESSAGE_HANDLER(WM_CLOSE, OnClose)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()

private:
    bool AddTrayIcon()
    {
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = m_hWnd;
        data.uID = TrayIconId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = TrayCallback;
        data.hIcon = icon_;
        wcscpy_s(data.szTip, L"MinimizeWindows");
        SetLastError(ERROR_SUCCESS);
        if (!Shell_NotifyIconW(NIM_ADD, &data)) {
            if (GetLastError() == ERROR_SUCCESS) { SetLastError(ERROR_GEN_FAILURE); }
            return false;
        }
        icon_added_ = true;
        data.uVersion = NOTIFYICON_VERSION_4;
        if (!Shell_NotifyIconW(NIM_SETVERSION, &data)) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return false;
        }
        return true;
    }

    void RemoveTrayIcon()
    {
        if (!icon_added_) { return; }
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = m_hWnd;
        data.uID = TrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        icon_added_ = false;
    }

    void CloseResources()
    {
        if (closing_) { return; }
        closing_ = true;
        keyboard_hook_.Uninstall();
        if (settings_.IsWindow()) { settings_.DestroyWindow(); }
        RemoveTrayIcon();
    }

    void ShowSettings()
    {
        if (!closing_ && !settings_.Show(icon_)) {
            OutputDebugStringW(L"MinimizeWindows: settings window creation failed.\n");
        }
    }

    void ShowMenu()
    {
        // Version-4 WM_CONTEXTMENU does not define a position in wParam.
        // Query the cursor in the same DPI context used to display the menu.
        POINT point{};
        if (!GetCursorPos(&point)) { return; }
        const HMENU menu = CreatePopupMenu();
        if (menu == nullptr) { return; }
        AppendMenuW(menu, MF_STRING, ExitCommand, L"\u9000\u51fa");
        ::SetForegroundWindow(m_hWnd);
        const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            point.x, point.y, m_hWnd, nullptr);
        DestroyMenu(menu);
        if (command == ExitCommand && !closing_) {
            Shutdown();
        }
        else if (!closing_) {
            ::PostMessageW(m_hWnd, WM_NULL, 0, 0);
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = m_hWnd;
            data.uID = TrayIconId;
            Shell_NotifyIconW(NIM_SETFOCUS, &data);
        }
    }

    LRESULT OnTrayCallback(UINT, WPARAM, LPARAM notification, BOOL&)
    {
        if (closing_ || HIWORD(notification) != TrayIconId) { return 0; }
        const UINT event = LOWORD(notification);
        if (event == WM_LBUTTONDBLCLK || event == NIN_KEYSELECT) { ShowSettings(); }
        else if (event == WM_CONTEXTMENU) { ShowMenu(); }
        return 0;
    }

    LRESULT OnWinD(UINT, WPARAM, LPARAM, BOOL&)
    {
        if (!closing_) { desktop_manager_.ToggleDesktop(target_); }
        return 0;
    }

    LRESULT OnTaskbarCreated(UINT, WPARAM, LPARAM, BOOL&)
    {
        if (!closing_) {
            icon_added_ = false;
            if (!AddTrayIcon()) { OutputDebugStringW(L"MinimizeWindows: tray icon re-registration failed.\n"); }
        }
        return 0;
    }

    LRESULT OnClose(UINT, WPARAM, LPARAM, BOOL&)
    {
        Shutdown();
        return 0;
    }

    LRESULT OnDestroy(UINT, WPARAM, LPARAM, BOOL&)
    {
        CloseResources();
        if (running_) { PostQuitMessage(0); }
        return 0;
    }

    MonitorTarget target_;
    WTL::CMessageLoop& loop_;
    DesktopManager desktop_manager_;
    KeyboardHook keyboard_hook_;
    SettingsWindow settings_;
    UINT taskbar_created_ = 0;
    HICON icon_ = nullptr; // LR_SHARED: the system owns this icon.
    bool icon_added_ = false;
    bool closing_ = false;
    bool running_ = false;
};

int RunTrayApplication(HINSTANCE instance, const MonitorTarget& target)
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) { SetLastError(ERROR_GEN_FAILURE); return 1; }
    const HRESULT module = _Module.Init(nullptr, instance);
    if (FAILED(module)) {
        _Module.Term();
        CoUninitialize();
        SetLastError(module == E_OUTOFMEMORY ? ERROR_NOT_ENOUGH_MEMORY : ERROR_GEN_FAILURE);
        return 1;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    WTL::CMessageLoop loop;
    int result = 1;
    DWORD error = ERROR_SUCCESS;
    SetLastError(ERROR_SUCCESS);
    if (!InitCommonControlsEx(&controls)) {
        error = GetLastError() != ERROR_SUCCESS ? GetLastError() : ERROR_GEN_FAILURE;
    }
    else if (!_Module.AddMessageLoop(&loop)) { error = ERROR_NOT_ENOUGH_MEMORY; }
    else {
        try {
            TrayApplication application(target, loop);
            if (application.Initialize()) { result = application.Run(); }
            else { error = GetLastError(); }
        }
        catch (const std::bad_alloc&) { error = ERROR_NOT_ENOUGH_MEMORY; }
        _Module.RemoveMessageLoop();
    }
    _Module.Term();
    CoUninitialize();
    if (result != 0) { SetLastError(error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE); }
    return result;
}
