#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

class KeyboardHook {
public:
    static constexpr UINT WinDMessage = WM_APP + 1;

    KeyboardHook() = default;
    ~KeyboardHook();

    KeyboardHook(const KeyboardHook&) = delete;
    KeyboardHook& operator=(const KeyboardHook&) = delete;

    // Install/uninstall on the same thread, which must pump a message loop.
    // Successfully posting WinDMessage consumes that D press through key-up.
    // A notification window must belong to the installing thread.
    bool Install(HWND notification_window = nullptr);
    void Uninstall();
    void SetEnabled(bool enabled) { enabled_ = enabled; }

private:
    static LRESULT CALLBACK KeyboardProc(int code, WPARAM message, LPARAM data);
    static KeyboardHook* active_hook_;

    HHOOK hook_ = nullptr;
    DWORD thread_id_ = 0;
    HWND notification_window_ = nullptr;
    bool d_down_ = false;
    bool d_intercepted_ = false;
    bool enabled_ = true;
};
