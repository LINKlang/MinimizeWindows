#include "desktop_manager.h"

#include <dwmapi.h>

#include <algorithm>
#include <cwchar>
#include <new>
#include <utility>

namespace {

bool SameDevice(const std::wstring& first, const std::wstring& second)
{
    return _wcsicmp(first.c_str(), second.c_str()) == 0;
}

struct ResolvedMonitor {
    std::wstring device_name;
    HMONITOR monitor = nullptr;
};

using MonitorLookup = std::vector<ResolvedMonitor>;

const ResolvedMonitor* FindResolved(const MonitorLookup& targets, const std::wstring& device)
{
    const auto found = std::find_if(targets.begin(), targets.end(), [&](const ResolvedMonitor& target) {
        return SameDevice(target.device_name, device);
    });
    return found != targets.end() ? &*found : nullptr;
}

BOOL CALLBACK FindTargetMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data)
{
    auto& lookup = *reinterpret_cast<MonitorLookup*>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return FALSE;
    }
    for (auto& target : lookup) {
        if (_wcsicmp(info.szDevice, target.device_name.c_str()) == 0) { target.monitor = monitor; }
    }
    return TRUE;
}

bool TargetStillAvailable(HMONITOR monitor, const std::wstring& device_name)
{
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    return GetMonitorInfoW(monitor, &info)
        && _wcsicmp(info.szDevice, device_name.c_str()) == 0;
}

enum class WindowClassification { ShouldMinimize, Exempt, Ignore };

bool HasStandardMinimizeBox(HWND hwnd)
{
    return (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_MINIMIZEBOX) != 0;
}

bool IsCloaked(HWND hwnd)
{
    DWORD cloaked = 0;
    const HRESULT result = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    // Windows 7 does not support this attribute. Keep the basic filters there.
    return SUCCEEDED(result) && cloaked != 0;
}

WindowClassification ClassifyWindow(HWND hwnd)
{
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)
        || hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) {
        return WindowClassification::Ignore;
    }
    const LONG_PTR extended_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) != 0
        || (extended_style & WS_EX_TOOLWINDOW) != 0) {
        return WindowClassification::Ignore;
    }

    wchar_t class_name[256];
    if (GetClassNameW(hwnd, class_name, ARRAYSIZE(class_name)) == 0) {
        return WindowClassification::Ignore;
    }

    if (std::wcscmp(class_name, L"Progman") == 0
        || std::wcscmp(class_name, L"WorkerW") == 0
        || std::wcscmp(class_name, L"Shell_TrayWnd") == 0
        || std::wcscmp(class_name, L"Shell_SecondaryTrayWnd") == 0
        || IsCloaked(hwnd)) {
        return WindowClassification::Ignore;
    }

    if ((extended_style & WS_EX_TOPMOST) != 0 && !HasStandardMinimizeBox(hwnd)) {
        return WindowClassification::Exempt;
    }
    return WindowClassification::ShouldMinimize;
}

bool SameIdentity(const WindowRecord& record)
{
    if (!IsWindow(record.hwnd)) { return false; }
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(record.hwnd, &process);
    return record.processId != 0 && record.threadId != 0
        && record.processId == process && record.threadId == thread;
}

bool ValidateRecord(const WindowRecord& record, HMONITOR monitor)
{
    return SameIdentity(record) && IsIconic(record.hwnd)
        && ClassifyWindow(record.hwnd) == WindowClassification::ShouldMinimize
        && MonitorFromWindow(record.hwnd, MONITOR_DEFAULTTONEAREST) == monitor;
}

bool SameRecord(const WindowRecord& first, const WindowRecord& second)
{
    return first.hwnd == second.hwnd && first.processId == second.processId
        && first.threadId == second.threadId;
}

bool ContainsRecord(const WindowBatch& batch, const WindowRecord& record)
{
    return std::any_of(batch.windows.begin(), batch.windows.end(), [&](const WindowRecord& current) {
        return SameRecord(current, record);
    });
}

void EraseRecord(WindowBatch& batch, const WindowRecord& record)
{
    batch.windows.erase(std::remove_if(batch.windows.begin(), batch.windows.end(),
        [&](const WindowRecord& current) { return SameRecord(current, record); }), batch.windows.end());
}

void EraseWindow(WindowBatch& batch, HWND hwnd)
{
    batch.windows.erase(std::remove_if(batch.windows.begin(), batch.windows.end(),
        [hwnd](const WindowRecord& record) { return record.hwnd == hwnd; }), batch.windows.end());
}

struct WindowScan {
    const MonitorLookup& targets;
    bool blocking = false;
    std::vector<WindowRecord> windows;
};

BOOL CALLBACK CollectWindows(HWND hwnd, LPARAM data)
{
    auto& scan = *reinterpret_cast<WindowScan*>(data);
    if (ClassifyWindow(hwnd) != WindowClassification::ShouldMinimize || IsIconic(hwnd)) {
        return TRUE;
    }
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    const auto target = std::find_if(scan.targets.begin(), scan.targets.end(), [&](const ResolvedMonitor& current) {
        return current.monitor != nullptr && current.monitor == monitor;
    });
    if (target == scan.targets.end()) { return TRUE; }
    scan.blocking = true;
    WindowRecord record{hwnd, 0, 0, {}};
    record.threadId = GetWindowThreadProcessId(hwnd, &record.processId);
    if (record.processId == 0 || record.threadId == 0) { return TRUE; }
    try { record.device_name = target->device_name; scan.windows.push_back(std::move(record)); }
    catch (const std::bad_alloc&) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE; }
    return TRUE;
}

} // namespace

DesktopManager* DesktopManager::active_manager_ = nullptr;

DesktopManager::~DesktopManager()
{
    if (active_manager_ == this) { active_manager_ = nullptr; }
    if (event_hook_ != nullptr) { UnhookWinEvent(event_hook_); }
}

bool DesktopManager::StartTracking()
{
    if (event_hook_ != nullptr) {
        if (tracking_thread_ == GetCurrentThreadId()) { return true; }
        SetLastError(ERROR_INVALID_THREAD_ID);
        return false;
    }
    if (active_manager_ != nullptr) {
        SetLastError(ERROR_ALREADY_EXISTS);
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    event_hook_ = SetWinEventHook(EVENT_SYSTEM_MINIMIZEEND, EVENT_SYSTEM_MINIMIZEEND,
        nullptr, WindowEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (event_hook_ == nullptr) {
        if (GetLastError() == ERROR_SUCCESS) { SetLastError(ERROR_GEN_FAILURE); }
        return false;
    }
    tracking_thread_ = GetCurrentThreadId();
    active_manager_ = this;
    return true;
}

void CALLBACK DesktopManager::WindowEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
    LONG object, LONG child, DWORD, DWORD)
{
    DesktopManager* self = active_manager_;
    if (self == nullptr || hook != self->event_hook_ || event != EVENT_SYSTEM_MINIMIZEEND
        || hwnd == nullptr || object != OBJID_WINDOW || child != CHILDID_SELF) {
        return;
    }
    // This history cannot be inferred from IsIconic after a manual re-minimize.
    EraseWindow(self->candidateBatch_, hwnd);
    EraseWindow(self->restoreBatch_, hwnd);
}

void DesktopManager::DiscardUnselectedRecords(const std::vector<MonitorTarget>& targets)
{
    const auto discard = [&](WindowBatch& batch) {
        batch.windows.erase(std::remove_if(batch.windows.begin(), batch.windows.end(),
            [&](const WindowRecord& record) {
                return std::none_of(targets.begin(), targets.end(), [&](const MonitorTarget& target) {
                    return SameDevice(target.device_name, record.device_name);
                });
            }), batch.windows.end());
    };
    discard(candidateBatch_);
    discard(restoreBatch_);
}

void DesktopManager::ToggleDesktop(const std::vector<MonitorTarget>& targets)
{
    if (event_hook_ == nullptr || tracking_thread_ != GetCurrentThreadId() || targets.empty()) {
        return;
    }
    try {
        MonitorLookup lookup;
        lookup.reserve(targets.size());
        for (const auto& target : targets) {
            if (!target.device_name.empty() && FindResolved(lookup, target.device_name) == nullptr) {
                lookup.push_back({target.device_name, nullptr});
            }
        }
        if (!EnumDisplayMonitors(nullptr, nullptr, FindTargetMonitor,
                reinterpret_cast<LPARAM>(&lookup))) {
            return;
        }
        if (std::none_of(lookup.begin(), lookup.end(), [](const ResolvedMonitor& target) {
                return target.monitor != nullptr;
            })) { return; }
        WindowScan scan{lookup};
        if (!EnumWindows(CollectWindows, reinterpret_cast<LPARAM>(&scan))) { return; }
        for (const auto& target : lookup) {
            if (target.monitor != nullptr && !TargetStillAvailable(target.monitor, target.device_name)) { return; }
        }

        // WinEvent may erase records during an API call. Never retain batch iterators.
        WindowBatch promoted;
        const auto candidates = candidateBatch_.windows;
        for (const auto& record : candidates) {
            const auto target = FindResolved(lookup, record.device_name);
            if (target == nullptr || target->monitor == nullptr) { continue; }
            if (!TargetStillAvailable(target->monitor, record.device_name)) { return; }
            if (!ValidateRecord(record, target->monitor)) { EraseRecord(candidateBatch_, record); }
            else if (ContainsRecord(candidateBatch_, record)) { promoted.windows.push_back(record); }
        }
        if (!promoted.windows.empty()) {
            // Preserve pending records for selected offline devices; validate on use
            // after reconnection. A new successful online batch still replaces them.
            for (const auto& record : candidateBatch_.windows) {
                const auto target = FindResolved(lookup, record.device_name);
                if (target != nullptr && target->monitor == nullptr) { promoted.windows.push_back(record); }
            }
            // A later query may have delivered a restore event for an earlier record.
            promoted.windows.erase(std::remove_if(promoted.windows.begin(), promoted.windows.end(),
                [&](const WindowRecord& record) { return !ContainsRecord(candidateBatch_, record); }), promoted.windows.end());
            if (!promoted.windows.empty()) {
                restoreBatch_ = std::move(promoted);
                for (const auto& record : restoreBatch_.windows) { EraseRecord(candidateBatch_, record); }
            }
        }

        if (scan.blocking) {
            WindowBatch next;
            next.windows.reserve(scan.windows.size());
            candidateBatch_ = std::move(next);
            for (const auto& record : scan.windows) {
                const auto target = FindResolved(lookup, record.device_name);
                if (target == nullptr || target->monitor == nullptr
                    || !TargetStillAvailable(target->monitor, record.device_name)) { return; }
                if (!SameIdentity(record) || ClassifyWindow(record.hwnd) != WindowClassification::ShouldMinimize
                    || IsIconic(record.hwnd)
                    || MonitorFromWindow(record.hwnd, MONITOR_DEFAULTTONEAREST) != target->monitor) {
                    continue;
                }
                // Register before the API call so an intervening restore can revoke it.
                candidateBatch_.windows.push_back(record);
                if (!ShowWindowAsync(record.hwnd, SW_MINIMIZE)) { EraseRecord(candidateBatch_, record); }
                // TODO: emulate desktop occlusion for windows that accept a show-state
                // request but never become iconic. They continue to block the desktop.
            }
            return;
        }

        const auto records = restoreBatch_.windows;
        for (auto iterator = records.rbegin(); iterator != records.rend(); ++iterator) {
            const WindowRecord record = *iterator;
            if (!ContainsRecord(restoreBatch_, record)) { continue; }
            const auto target = FindResolved(lookup, record.device_name);
            if (target == nullptr || target->monitor == nullptr) { continue; }
            if (!TargetStillAvailable(target->monitor, record.device_name)) { return; }
            if (!ValidateRecord(record, target->monitor)) {
                EraseRecord(restoreBatch_, record);
                continue;
            }
            if (ContainsRecord(restoreBatch_, record) && ShowWindowAsync(record.hwnd, SW_RESTORE)) {
                EraseRecord(restoreBatch_, record);
            }
        }
    }
    catch (const std::bad_alloc&) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); }
}
