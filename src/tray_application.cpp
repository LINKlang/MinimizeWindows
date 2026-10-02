#include "tray_application.h"
#include "keyboard_hook.h"
#include "settings_window.h"
#include "resources.h"

#include <shellapi.h>
#include <new>

WTL::CAppModule _Module;

class TrayApplication : public ATL::CWindowImpl<TrayApplication> {
public:
    DECLARE_WND_CLASS_EX(L"MinimizeWindows.TrayHost", 0, COLOR_WINDOW)
    static constexpr UINT TrayCallback = WM_APP + 2;
    static constexpr UINT TrayIconId = 1;
    static constexpr UINT ExitCommand = 100;

    TrayApplication(const ConfigStore& store, const AppConfig& config, WTL::CMessageLoop& loop)
        : store_(store), config_(config), loop_(loop)
    {
        for (const auto& device : config.monitor_devices) { targets_.push_back({device}); }
        keyboard_hook_.SetEnabled(!targets_.empty());
    }
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
        icon_ = static_cast<HICON>(LoadImageW(_Module.GetResourceInstance(), MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
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
        if (!closing_ && !settings_.Show(icon_, config_.monitor_devices,
                [this](const std::vector<std::wstring>& devices, std::wstring& error) {
                    return SaveConfiguration(devices, error);
                })) {
            OutputDebugStringW(L"MinimizeWindows: settings window creation failed.\n");
        }
    }

    bool SaveConfiguration(const std::vector<std::wstring>& devices, std::wstring& error)
    {
        // Allocate the complete new runtime state before committing the file.
        AppConfig next{devices};
        std::vector<MonitorTarget> targets;
        targets.reserve(devices.size());
        for (const auto& device : devices) { targets.push_back({device}); }
        if (!store_.Save(next, error)) { return false; }
        config_ = std::move(next);
        targets_ = std::move(targets);
        desktop_manager_.DiscardUnselectedRecords(targets_);
        keyboard_hook_.SetEnabled(!targets_.empty());
        return true;
    }

    void ShowMenu()
    {
        // Version-4 WM_CONTEXTMENU does not define a position in wParam.
        // Query the cursor in the same DPI context used to display the menu.
        POINT point{};
        if (!GetCursorPos(&point)) { return; }
        const HMENU menu = CreatePopupMenu();
        if (menu == nullptr) { return; }
        AppendMenuW(menu, MF_STRING, ExitCommand, L"Exit");
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

    LRESULT OnTrayCallback(UINT, WPARAM icon_or_position, LPARAM notification, BOOL&)
    {
        if (closing_) { return 0; }
        // Explorer sends V4 events; some docks forward pre-V4 (ID, event) messages.
        // Only fall back when there is no packed icon ID; reject other nonzero V4 IDs.
        const bool legacy = HIWORD(notification) == 0;
        if (legacy ? icon_or_position != TrayIconId : HIWORD(notification) != TrayIconId) { return 0; }
        const UINT event = LOWORD(notification);
        if (event == WM_LBUTTONDBLCLK || event == NIN_KEYSELECT) { ShowSettings(); }
        else if (event == WM_CONTEXTMENU || (legacy && event == WM_RBUTTONUP)) { ShowMenu(); }
        return 0;
    }

    LRESULT OnWinD(UINT, WPARAM, LPARAM, BOOL&)
    {
        if (!closing_) { desktop_manager_.ToggleDesktop(targets_); }
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

    const ConfigStore& store_;
    AppConfig config_;
    std::vector<MonitorTarget> targets_;
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

int RunTrayApplication(HINSTANCE instance, const ConfigStore& store, const AppConfig& config)
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
            TrayApplication application(store, config, loop);
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
