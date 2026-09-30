#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

template<class T>
struct MonitorValue {
    bool available = false;
    T value{};
};

struct MonitorRefreshRate {
    UINT32 numerator = 0;
    UINT32 denominator = 0;
};

struct MonitorInfo {
    // Handles and CCD identifiers belong to this snapshot, not a persistent ID.
    HMONITOR handle = nullptr;
    std::wstring device_name;
    std::wstring device_path;
    std::wstring friendly_name;
    bool ccd_identifiers_available = false;
    LUID source_adapter_id{};
    LUID target_adapter_id{};
    UINT32 source_id = 0;
    UINT32 target_id = 0;

    // Signed physical desktop pixels. Clones share the same logical desktop view.
    RECT bounds_px{};
    SIZE desktop_size_px{};
    MonitorValue<RECT> work_area_px;
    bool primary = false;
    // Windows orientation labels (CCD target rotation), not a mounting angle.
    // Scale is the system-reported discrete scale step.
    MonitorValue<unsigned int> rotation_degrees;
    MonitorValue<unsigned int> scale_percent;
    MonitorValue<MonitorRefreshRate> refresh_rate;
};

enum class MonitorQuerySource {
    DisplayConfig,
    Gdi
};

struct MonitorSnapshot {
    std::vector<MonitorInfo> monitors;
    RECT virtual_bounds_px{};
    MonitorQuerySource query_source = MonitorQuerySource::Gdi;
};

class MonitorEnumerator {
public:
    // Failure clears snapshot and sets a Win32 error retrievable by GetLastError.
    bool Enumerate(MonitorSnapshot& snapshot) const;
};
