#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

struct MonitorTarget {
    std::wstring device_name;
};

struct WindowRecord {
    HWND hwnd;
    DWORD processId;
    DWORD threadId;
    std::wstring device_name;
};

struct WindowBatch {
    std::vector<WindowRecord> windows;
};

class DesktopManager {
public:
    DesktopManager() = default;
    ~DesktopManager();
    DesktopManager(const DesktopManager&) = delete;
    DesktopManager& operator=(const DesktopManager&) = delete;

    // Start, toggle and destroy on the same thread, which must pump messages.
    bool StartTracking();
    void ToggleDesktop(const std::vector<MonitorTarget>& targets);
    // Called after a configuration commit; does not change any window state.
    void DiscardUnselectedRecords(const std::vector<MonitorTarget>& targets);

private:
    static void CALLBACK WindowEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd,
        LONG object, LONG child, DWORD thread, DWORD time);
    static DesktopManager* active_manager_;

    HWINEVENTHOOK event_hook_ = nullptr;
    DWORD tracking_thread_ = 0;
    WindowBatch candidateBatch_;
    WindowBatch restoreBatch_;
};
