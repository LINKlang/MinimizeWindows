#include "keyboard_hook.h"
#include "desktop_manager.h"

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    DesktopManager desktop_manager;
    KeyboardHook keyboard_hook;
    if (!keyboard_hook.Install()) {
        OutputDebugStringW(L"MinimizeWindows: keyboard hook installation failed.\n");
        return 1;
    }

    MSG message;
    int result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (message.hwnd == nullptr && message.message == KeyboardHook::WinDMessage) {
            desktop_manager.ToggleDesktop();
        }
        else {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    if (result == -1) {
        return 1;
    }
    return 0;
}
