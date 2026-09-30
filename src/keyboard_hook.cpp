#include "keyboard_hook.h"

KeyboardHook* KeyboardHook::active_hook_ = nullptr;

KeyboardHook::~KeyboardHook()
{
    Uninstall();
}

bool KeyboardHook::Install()
{
    if (hook_ != nullptr) {
        return true;
    }

    if (active_hook_ != nullptr) {
        SetLastError(ERROR_ALREADY_EXISTS);
        return false;
    }

    // PostThreadMessage requires an existing message queue.
    MSG message;
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    thread_id_ = GetCurrentThreadId();
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, nullptr, 0);
    if (hook_ == nullptr) {
        thread_id_ = 0;
        return false;
    }

    d_down_ = (GetAsyncKeyState('D') & 0x8000) != 0;
    active_hook_ = this;
    return true;
}

void KeyboardHook::Uninstall()
{
    if (hook_ != nullptr) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }

    if (active_hook_ == this) {
        active_hook_ = nullptr;
    }
    thread_id_ = 0;
    d_down_ = false;
}

LRESULT CALLBACK KeyboardHook::KeyboardProc(int code, WPARAM message, LPARAM data)
{
    KeyboardHook* self = active_hook_;
    if (code != HC_ACTION || self == nullptr) {
        return CallNextHookEx(nullptr, code, message, data);
    }

    const auto* keyboard = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
    if (keyboard->vkCode != 'D') {
        return CallNextHookEx(self->hook_, code, message, data);
    }

    if (message == WM_KEYUP || message == WM_SYSKEYUP) {
        self->d_down_ = false;
    }
    else if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
        const bool win_down = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0
            || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
        const bool extra_modifier = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0
            || (GetAsyncKeyState(VK_MENU) & 0x8000) != 0
            || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

        if (!self->d_down_ && win_down && !extra_modifier) {
            PostThreadMessageW(self->thread_id_, WinDMessage, 0, 0);
        }
        self->d_down_ = true;
    }

    return CallNextHookEx(self->hook_, code, message, data);
}
