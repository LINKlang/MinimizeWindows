// Real WTL windows on an isolated desktop; tray and keyboard OS hooks are replaced.
#include "../src/wtl_support.h"
#include "../src/tray_application.h"
#include "../src/keyboard_hook.h"
#include "../src/settings_window.h"
#include <shellapi.h>
#include <imm.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace tray_test {

void Check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", message, GetLastError()); std::exit(1); }
}

void Stage(const char* message) { std::puts(message); std::fflush(stdout); }

bool fail_add = false, fail_version = false, fail_tracking = false, fail_keyboard = false;
int adds = 0, versions = 0, deletes = 0, keyboard_installs = 0, keyboard_removals = 0;
int tracking_installs = 0, tracking_removals = 0;
int menu_calls = 0;
POINT cursor_position{50, 50}, menu_position{};
bool fail_cursor = false;
bool fail_save = false;
int saves = 0;
std::vector<std::wstring> saved_devices;
NOTIFYICONDATAW notification{};
enum class MenuAction { Cancel, CoreThenCancel, Exit };
MenuAction menu_action = MenuAction::Cancel;
HWND core_window = nullptr;

void Pump()
{
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        Check(message.message != WM_QUIT, "closing a settings window must not quit the application");
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

HWINEVENTHOOK WINAPI Track(DWORD first, DWORD last, HMODULE module, WINEVENTPROC callback,
    DWORD process, DWORD thread, DWORD flags)
{
    ++tracking_installs;
    if (fail_tracking) { SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    return ::SetWinEventHook(first, last, module, callback, process, thread, flags);
}

BOOL WINAPI Untrack(HWINEVENTHOOK hook) { ++tracking_removals; return ::UnhookWinEvent(hook); }

HHOOK WINAPI InstallKeyboard(int type, HOOKPROC, HINSTANCE, DWORD)
{
    Check(type == WH_KEYBOARD_LL, "install the low-level keyboard hook");
    ++keyboard_installs;
    if (fail_keyboard) { SetLastError(ERROR_ACCESS_DENIED); return nullptr; }
    return reinterpret_cast<HHOOK>(1);
}

BOOL WINAPI UninstallKeyboard(HHOOK) { ++keyboard_removals; return TRUE; }

BOOL WINAPI Notify(DWORD operation, PNOTIFYICONDATAW data)
{
    if (operation == NIM_ADD) {
        ++adds;
        notification = *data;
        Check(IsWindow(data->hWnd) && !IsWindowVisible(data->hWnd)
            && (GetWindowLongPtrW(data->hWnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0,
            "tray owner is a hidden top-level tool window");
        Check(data->uFlags == (NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP)
            && data->hIcon != nullptr && std::wcscmp(data->szTip, L"MinimizeWindows") == 0,
            "tray icon and tooltip are present without a balloon");
        if (fail_add) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    }
    if (operation == NIM_SETVERSION) {
        ++versions;
        Check(data->uVersion == NOTIFYICON_VERSION_4, "set notification version 4 after every add");
        if (fail_version) { return FALSE; }
    }
    if (operation == NIM_DELETE) { ++deletes; }
    return TRUE;
}

BOOL WINAPI CursorPosition(LPPOINT point)
{
    if (fail_cursor) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    *point = cursor_position;
    return TRUE;
}

UINT WINAPI Menu(HMENU menu, UINT flags, int x, int y, HWND owner, LPTPMPARAMS)
{
    ++menu_calls;
    menu_position = {x, y};
    Stage("Tray lifecycle: menu callback received");
    wchar_t label[16];
    GetMenuStringW(menu, 0, label, ARRAYSIZE(label), MF_BYPOSITION);
    Check(GetMenuItemCount(menu) == 1 && std::wcscmp(label, L"Exit") == 0
        && (flags & TPM_RETURNCMD) != 0, "context menu contains only Exit");
    if (menu_action == MenuAction::CoreThenCancel) {
        Check(PostMessageW(owner, KeyboardHook::WinDMessage, 0, 0) != FALSE, "post core message during modal menu");
        Pump(); // A modal menu loop dispatches window messages, not thread messages.
        Check(IsIconic(core_window), "core window message is handled inside the menu loop");
    }
    return menu_action == MenuAction::Exit ? 100 : 0;
}

void Reset()
{
    fail_add = fail_version = fail_tracking = fail_keyboard = false;
    adds = versions = deletes = keyboard_installs = keyboard_removals = 0;
    tracking_installs = tracking_removals = 0;
    menu_calls = 0;
    cursor_position = {50, 50};
    menu_position = {};
    fail_cursor = false;
    notification = {};
    menu_action = MenuAction::Cancel;
    fail_save = false;
    saves = 0;
    saved_devices.clear();
}

HWND Settings() { return FindWindowW(L"MinimizeWindows.Settings", nullptr); }

void TrayEvent(UINT event, UINT icon = 1)
{
    SendMessageW(notification.hWnd, notification.uCallbackMessage, MAKELPARAM(50, 50), MAKELPARAM(event, icon));
}

void LegacyTrayEvent(UINT event, UINT icon = 1)
{
    SendMessageW(notification.hWnd, notification.uCallbackMessage, icon, event);
}

} // namespace tray_test

bool ConfigStore::Save(const AppConfig& config, std::wstring& error) const
{
    ++tray_test::saves;
    if (tray_test::fail_save) { error = L"Test configuration is locked."; return false; }
    tray_test::saved_devices = config.monitor_devices;
    error.clear();
    return true;
}

#define SetWinEventHook tray_test::Track
#define UnhookWinEvent tray_test::Untrack
#include "../src/desktop_manager.cpp"
#undef SetWinEventHook
#undef UnhookWinEvent
#define SetWindowsHookExW tray_test::InstallKeyboard
#define UnhookWindowsHookEx tray_test::UninstallKeyboard
#include "../src/keyboard_hook.cpp"
#undef SetWindowsHookExW
#undef UnhookWindowsHookEx
#include "../src/settings_window.cpp"
#define Shell_NotifyIconW tray_test::Notify
#define TrackPopupMenuEx tray_test::Menu
#define GetCursorPos tray_test::CursorPosition
#include "../src/tray_application.cpp"
#undef GetCursorPos
#undef Shell_NotifyIconW
#undef TrackPopupMenuEx

int main()
{
    using namespace tray_test;
    const HDESK original = GetThreadDesktop(GetCurrentThreadId());
    wchar_t desktop_name[64];
    swprintf_s(desktop_name, L"MinimizeWindowsTrayTests_%lu", GetCurrentProcessId());
    const HDESK isolated = CreateDesktopW(desktop_name, nullptr, nullptr, 0,
        DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | DESKTOP_HOOKCONTROL, nullptr);
    Check(isolated != nullptr && SetThreadDesktop(isolated) != FALSE, "attach to isolated UI desktop");
    ImmDisableIME(0);
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize test COM apartment");
    Check(SUCCEEDED(_Module.Init(nullptr, GetModuleHandleW(nullptr))), "initialize WTL module");
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    Check(InitCommonControlsEx(&controls) != FALSE, "initialize common controls");
    MONITORINFOEXW primary{};
    primary.cbSize = sizeof(primary);
    Check(GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &primary) != FALSE,
        "resolve primary target for native UI tests");
    const AppConfig config{{primary.szDevice}};
    const ConfigStore store(L"unused-isolated-config.json");
    WTL::CMessageLoop loop;
    Check(_Module.AddMessageLoop(&loop) != FALSE, "register WTL message loop");

    Reset();
    {
        Stage("Tray lifecycle: initialize/open/close");
        TrayApplication app(store, config, loop);
        Check(app.Initialize(), "initialize tray and core");
        Check(Settings() == nullptr && adds == 1 && versions == 1, "startup does not create a settings frame");
        TrayEvent(WM_LBUTTONDBLCLK, 99);
        Check(Settings() == nullptr, "version-4 callback rejects another icon ID");
        LegacyTrayEvent(WM_LBUTTONDBLCLK, 99);
        Check(Settings() == nullptr, "legacy callback rejects another icon ID");
        SendMessageW(notification.hWnd, notification.uCallbackMessage, 1, MAKELPARAM(WM_LBUTTONDBLCLK, 99));
        Check(Settings() == nullptr, "wrong V4 icon is not accepted as a legacy event with matching wParam");
        LegacyTrayEvent(WM_LBUTTONUP);
        Check(Settings() == nullptr, "legacy single-click retains the double-click activation behavior");
        TrayEvent(NIN_SELECT);
        Check(Settings() == nullptr, "single mouse selection does not open settings");
        TrayEvent(WM_LBUTTONDBLCLK);
        const HWND window = Settings();
        Check(window != nullptr && IsWindowVisible(window), "double-click opens settings");
        wchar_t title[80];
        GetWindowTextW(window, title, ARRAYSIZE(title));
        Check(std::wcscmp(title, L"MinimizeWindows \u2014 Settings") == 0, "settings title is correct");
        TrayEvent(WM_LBUTTONDBLCLK);
        Check(Settings() == window, "repeat double-click reuses the existing frame");
        TrayEvent(NIN_KEYSELECT);
        Check(Settings() == window, "version-4 keyboard activation reuses the same frame");
        ShowWindow(window, SW_MINIMIZE);
        TrayEvent(WM_LBUTTONDBLCLK);
        Check(!IsIconic(window), "double-click restores an iconic frame");
        SendMessageW(window, WM_CLOSE, 0, 0);
        Stage("Tray lifecycle: closed first frame");
        Pump();
        Check(Settings() == nullptr && IsWindow(notification.hWnd) && keyboard_removals == 0,
            "frame close leaves tray and core alive");
        LegacyTrayEvent(WM_LBUTTONDBLCLK);
        const HWND legacy_window = Settings();
        Check(legacy_window != nullptr, "legacy double-click reopens settings after close");
        LegacyTrayEvent(WM_LBUTTONDBLCLK);
        Check(Settings() == legacy_window, "legacy repeat double-click reuses the frame");
        ShowWindow(legacy_window, SW_MINIMIZE);
        LegacyTrayEvent(WM_LBUTTONDBLCLK);
        Check(!IsIconic(legacy_window), "legacy double-click restores a minimized frame");
        SendMessageW(Settings(), WM_SYSCOMMAND, SC_CLOSE, 0); // Alt+F4's standard close command.
        Stage("Tray lifecycle: closed second frame");
        Pump();
        Check(Settings() == nullptr, "system close follows the same non-exiting lifetime");

        SendMessageW(notification.hWnd, RegisterWindowMessageW(L"TaskbarCreated"), 0, 0);
        Check(adds == 2 && versions == 2, "taskbar recreation re-adds and re-versions the icon");
        core_window = CreateWindowExW(0, L"STATIC", L"Tray core fixture", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            primary.rcWork.left + 40, primary.rcWork.top + 40, 300, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(core_window != nullptr, "create private core fixture");
        PostMessageW(notification.hWnd, KeyboardHook::WinDMessage, 0, 0);
        Pump();
        Check(IsIconic(core_window), "core still minimizes after settings is closed");
        PostMessageW(notification.hWnd, KeyboardHook::WinDMessage, 0, 0);
        Pump();
        Check(!IsIconic(core_window), "core still restores its batch after settings is closed");
        menu_action = MenuAction::CoreThenCancel;
        Stage("Tray lifecycle: modal core delivery");
        TrayEvent(WM_CONTEXTMENU);
        Check(menu_calls == 1 && IsIconic(core_window), "right-click dispatched the menu and its nested core message");
        menu_action = MenuAction::Cancel;
        cursor_position = {480, 320};
        SendMessageW(notification.hWnd, notification.uCallbackMessage,
            MAKELPARAM(30000, 30000), MAKELPARAM(WM_CONTEXTMENU, 1));
        Check(menu_calls == 2 && menu_position.x == 480 && menu_position.y == 320,
            "menu uses the cursor position, not undefined version-4 context notification coordinates");
        cursor_position = {-1920, -250};
        SendMessageW(notification.hWnd, notification.uCallbackMessage,
            MAKEWPARAM(-1, -1), MAKELPARAM(WM_CONTEXTMENU, 1));
        Check(menu_calls == 3 && menu_position.x == -1920 && menu_position.y == -250,
            "menu preserves signed cursor coordinates on left and upper monitors");
        fail_cursor = true;
        TrayEvent(WM_CONTEXTMENU);
        Check(menu_calls == 3, "failed cursor query does not open a menu at a fabricated position");
        fail_cursor = false;
        cursor_position = {320, 240};
        LegacyTrayEvent(WM_CONTEXTMENU);
        Check(menu_calls == 4 && menu_position.x == 320 && menu_position.y == 240,
            "legacy context event opens the menu at the cursor, not at the icon ID");
        cursor_position = {-800, -100};
        LegacyTrayEvent(WM_RBUTTONUP);
        Check(menu_calls == 5 && menu_position.x == -800 && menu_position.y == -100,
            "legacy right-button release opens the menu and preserves negative coordinates");
        TrayEvent(WM_RBUTTONUP);
        LegacyTrayEvent(WM_CONTEXTMENU, 99);
        LegacyTrayEvent(WM_RBUTTONUP, 99);
        SendMessageW(notification.hWnd, notification.uCallbackMessage, 1, MAKELPARAM(WM_CONTEXTMENU, 99));
        Check(menu_calls == 5, "wrong icon IDs and V4 right-button release do not open extra menus");
        DestroyWindow(core_window);
        app.Shutdown();
        Check(!IsWindow(notification.hWnd) && deletes == 1 && keyboard_removals == 1,
            "shutdown cleans host, icon and keyboard hook exactly once");
    }
    Check(tracking_removals == 1, "application destruction releases restore tracking");

    Reset();
    {
        TrayApplication app(store, config, loop);
        Check(app.Initialize(), "initialize for configuration save integration");
        saved_devices = config.monitor_devices;
        core_window = CreateWindowExW(0, L"STATIC", L"Configuration core fixture", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            primary.rcWork.left + 40, primary.rcWork.top + 40, 300, 200, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(core_window != nullptr, "create save integration window");
        const auto page = [&] { return GetDlgItem(Settings(), SettingsWindow::DisplayId); };
        const auto click_button = [&](UINT id) {
            SendMessageW(page(), WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(page(), id)));
        };
        TrayEvent(WM_LBUTTONDBLCLK);
        click_button(DisplayPage::ConfigureId);
        SendMessageW(GetDlgItem(page(), DisplayPage::ListId), WM_KEYDOWN, VK_HOME, 0);
        SendMessageW(GetDlgItem(page(), DisplayPage::ListId), WM_KEYDOWN, VK_SPACE, 0);
        fail_save = true;
        click_button(DisplayPage::SaveId);
        Check(saves == 1 && saved_devices == config.monitor_devices
            && IsWindowEnabled(GetDlgItem(page(), DisplayPage::SaveId)), "failed save retains old configuration and edit mode");
        SendMessageW(Settings(), WM_CLOSE, 0, 0);
        SendMessageW(notification.hWnd, KeyboardHook::WinDMessage, 0, 0);
        Pump();
        Check(IsIconic(core_window), "failed save leaves the previous runtime target active");
        SendMessageW(notification.hWnd, KeyboardHook::WinDMessage, 0, 0);
        Pump();
        Check(!IsIconic(core_window), "restore after save failure remains available");
        fail_save = false;
        TrayEvent(WM_LBUTTONDBLCLK);
        click_button(DisplayPage::ConfigureId);
        SendMessageW(GetDlgItem(page(), DisplayPage::ListId), WM_KEYDOWN, VK_HOME, 0);
        SendMessageW(GetDlgItem(page(), DisplayPage::ListId), WM_KEYDOWN, VK_SPACE, 0);
        click_button(DisplayPage::SaveId);
        Check(saved_devices.empty() && !IsWindowEnabled(GetDlgItem(page(), DisplayPage::SaveId)),
            "empty selection is saved and leaves edit mode");
        SendMessageW(Settings(), WM_CLOSE, 0, 0);
        SendMessageW(notification.hWnd, KeyboardHook::WinDMessage, 0, 0);
        Pump();
        Check(!IsIconic(core_window), "saved empty selection immediately stops core window operations");
        TrayEvent(WM_LBUTTONDBLCLK);
        click_button(DisplayPage::ConfigureId);
        SendMessageW(GetDlgItem(page(), DisplayPage::ListId), WM_KEYDOWN, VK_HOME, 0);
        SendMessageW(GetDlgItem(page(), DisplayPage::ListId), WM_KEYDOWN, VK_SPACE, 0);
        click_button(DisplayPage::SaveId);
        Check(saved_devices == config.monitor_devices, "saved targets can be re-enabled without restarting");
        SendMessageW(Settings(), WM_CLOSE, 0, 0);
        SendMessageW(notification.hWnd, KeyboardHook::WinDMessage, 0, 0);
        Pump();
        Check(IsIconic(core_window), "re-enabled configuration immediately updates the running core");
        DestroyWindow(core_window);
        app.Shutdown();
    }
    Pump();

    for (int failure = 0; failure < 4; ++failure) {
        Stage("Tray lifecycle: startup failure cleanup");
        Reset();
        fail_add = failure == 0;
        fail_version = failure == 1;
        fail_tracking = failure == 2;
        fail_keyboard = failure == 3;
        {
            TrayApplication app(store, config, loop);
            Check(!app.Initialize(), "injected tray or core startup failure is reported");
        }
        Check(!IsWindow(notification.hWnd) && Settings() == nullptr, "startup failure destroys UI windows");
        Check(deletes == (failure == 0 ? 0 : 1), "startup failure removes only an added icon");
        Check(keyboard_installs == (failure == 3 ? 1 : 0), "earlier startup failure does not install keyboard hook");
        Check(tracking_removals == (failure == 3 ? 1 : 0), "startup failure releases only successful event hooks");
        Pump();
    }

    for (const bool legacy : {false, true}) {
        Reset();
        {
            Stage("Tray lifecycle: Exit menu loop");
            TrayApplication app(store, config, loop);
            Check(app.Initialize(), "initialize for tray Exit test");
            TrayEvent(WM_LBUTTONDBLCLK);
            menu_action = MenuAction::Exit;
            Check(PostMessageW(notification.hWnd, notification.uCallbackMessage, legacy ? 1 : 0,
                legacy ? static_cast<LPARAM>(WM_RBUTTONUP) : MAKELPARAM(WM_CONTEXTMENU, 1)) != FALSE,
                "post Exit-menu request to the tray host");
            Check(app.Run() == 0 && Settings() == nullptr && !IsWindow(notification.hWnd)
                && deletes == 1 && keyboard_removals == 1, "tray Exit closes frame, stops core and exits loop");
        }
        Check(tracking_removals == 1, "tray Exit releases restore tracking");
        Pump();
    }
    Reset();
    {
        Stage("Tray lifecycle: external quit loop");
        TrayApplication app(store, config, loop);
        Check(app.Initialize(), "initialize for external WM_QUIT test");
        TrayEvent(WM_LBUTTONDBLCLK);
        PostQuitMessage(0);
        Check(app.Run() == 0 && Settings() == nullptr && !IsWindow(notification.hWnd)
            && deletes == 1 && keyboard_removals == 1, "external quit follows the same cleanup path");
    }
    Check(tracking_removals == 1, "external quit releases restore tracking");
    Pump();
    _Module.RemoveMessageLoop();
    _Module.Term();
    CoUninitialize();
    Check(SetThreadDesktop(original) != FALSE && CloseDesktop(isolated) != FALSE, "release isolated UI desktop");
    std::puts("Native WTL frame/tray/core lifecycle, modal delivery and startup failure tests passed.");
    return 0;
}
