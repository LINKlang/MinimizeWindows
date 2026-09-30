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
    // Win+D posts WinDMessage to that thread; keyboard input is never swallowed.
    bool Install();
    void Uninstall();

private:
    static LRESULT CALLBACK KeyboardProc(int code, WPARAM message, LPARAM data);
    static KeyboardHook* active_hook_;

    HHOOK hook_ = nullptr;
    DWORD thread_id_ = 0;
    bool d_down_ = false;
};
