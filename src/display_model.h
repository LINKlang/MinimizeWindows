#pragma once

#include "desktop_manager.h"
#include "monitor_enumerator.h"

#include <utility>

struct DisplayTile {
    RECT bounds{};
    std::vector<size_t> outputs;
    std::wstring label;
};

// UI-only state: the latest snapshot and the output currently being inspected.
class DisplayModel {
public:
    static constexpr size_t NoSelection = static_cast<size_t>(-1);

    void Update(MonitorSnapshot snapshot, const MonitorTarget& initial_target);
    void Fail(DWORD error);
    bool Select(size_t index);
    const MonitorInfo* Selected() const;
    const MonitorSnapshot& Snapshot() const { return snapshot_; }
    size_t Selection() const { return selected_; }
    DWORD Error() const { return error_; }

    std::vector<DisplayTile> Layout(const RECT& viewport, int padding) const;

private:
    MonitorSnapshot snapshot_;
    size_t selected_ = NoSelection;
    DWORD error_ = ERROR_SUCCESS;
};

std::wstring DisplayDeviceLabel(const std::wstring& device_name);
std::vector<std::pair<std::wstring, std::wstring>> DisplayInformation(const MonitorInfo& monitor);
