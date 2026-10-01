#include "keyboard_hook.h"

KeyboardHook* KeyboardHook::active_hook_ = nullptr;

KeyboardHook::~KeyboardHook()
{
    Uninstall();
}

bool KeyboardHook::Install(HWND notification_window)
{
    if (hook_ != nullptr) {
        if (notification_window_ != notification_window) {
            SetLastError(ERROR_ALREADY_EXISTS);
            return false;
        }
        return true;
    }

    if (active_hook_ != nullptr) {
        SetLastError(ERROR_ALREADY_EXISTS);
        return false;
    }

    const DWORD thread = GetCurrentThreadId();
    if (notification_window != nullptr && GetWindowThreadProcessId(notification_window, nullptr) != thread) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return false;
    }

    // The legacy thread notification path requires an existing message queue.
    MSG message;
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    thread_id_ = thread;
    notification_window_ = notification_window;
    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, nullptr, 0);
    if (hook_ == nullptr) {
        thread_id_ = 0;
        notification_window_ = nullptr;
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
    notification_window_ = nullptr;
    d_down_ = false;
    d_intercepted_ = false;
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
        const bool intercepted = self->d_intercepted_;
        self->d_down_ = false;
        self->d_intercepted_ = false;
        if (intercepted) {
            return 1;
        }
    }
    else if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
        if (self->d_down_) {
            return self->d_intercepted_
                ? 1 : CallNextHookEx(self->hook_, code, message, data);
        }
        self->d_down_ = true;

        const bool win_down = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0
            || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
        const bool extra_modifier = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0
            || (GetAsyncKeyState(VK_MENU) & 0x8000) != 0
            || (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

        const bool notified = self->enabled_ && win_down && !extra_modifier
            && (self->notification_window_ != nullptr
                ? PostMessageW(self->notification_window_, WinDMessage, 0, 0)
                : PostThreadMessageW(self->thread_id_, WinDMessage, 0, 0));
        if (notified) {
            self->d_intercepted_ = true;

            // Mark Win as used so releasing it does not open the Start menu.
            // 0xFF is the unused key employed for this purpose by PowerToys.
            INPUT dummy_keys[2]{};
            for (auto& key : dummy_keys) {
                key.type = INPUT_KEYBOARD;
                key.ki.wVk = 0xFF;
            }
            dummy_keys[1].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(ARRAYSIZE(dummy_keys), dummy_keys, sizeof(INPUT));
            return 1;
        }
    }

    return CallNextHookEx(self->hook_, code, message, data);
}
