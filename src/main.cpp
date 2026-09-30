#include "keyboard_hook.h"

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
    KeyboardHook keyboard_hook;
    if (!keyboard_hook.Install()) {
        MessageBoxW(nullptr, L"Keyboard hook installation failed.",
            L"MinimizeWindows", MB_OK | MB_ICONERROR);
        return 1;
    }

    MSG message;
    int result;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (message.hwnd == nullptr && message.message == KeyboardHook::WinDMessage) {
            // Keep the dialog outside KeyboardProc so the hook returns promptly.
            MessageBoxW(nullptr, L"Win+D detected.", L"MinimizeWindows",
                MB_OK | MB_ICONINFORMATION);
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
