#include "display_model.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace {

bool SameText(const std::wstring& a, const std::wstring& b)
{
    return !a.empty() && !b.empty() && _wcsicmp(a.c_str(), b.c_str()) == 0;
}

bool SameAdapter(const LUID& a, const LUID& b)
{
    return a.LowPart == b.LowPart && a.HighPart == b.HighPart;
}

bool SameBounds(const RECT& a, const RECT& b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

bool Clones(const MonitorInfo& a, const MonitorInfo& b)
{
    return a.ccd_identifiers_available && b.ccd_identifiers_available
        && SameAdapter(a.source_adapter_id, b.source_adapter_id) && a.source_id == b.source_id
        && SameBounds(a.bounds_px, b.bounds_px);
}

std::wstring Position(LONG x, LONG y)
{
    return L"(" + std::to_wstring(x) + L", " + std::to_wstring(y) + L")";
}

} // namespace

void DisplayModel::Update(MonitorSnapshot snapshot, const MonitorTarget& initial_target)
{
    MonitorInfo previous;
    const bool had_selection = Selected() != nullptr;
    if (had_selection) { previous = *Selected(); }
    std::stable_sort(snapshot.monitors.begin(), snapshot.monitors.end(), [](const MonitorInfo& a, const MonitorInfo& b) {
        if (a.primary != b.primary) { return a.primary; }
        return _wcsicmp(a.device_name.c_str(), b.device_name.c_str()) < 0;
    });
    snapshot_ = std::move(snapshot);
    error_ = ERROR_SUCCESS;
    selected_ = NoSelection;
    const auto choose = [&](const auto& predicate) {
        for (size_t i = 0; i < snapshot_.monitors.size(); ++i) {
            if (predicate(snapshot_.monitors[i])) { selected_ = i; return true; }
        }
        return false;
    };
    if (had_selection) {
        if (choose([&](const MonitorInfo& m) { return SameText(previous.device_path, m.device_path); })) { return; }
        if (previous.ccd_identifiers_available && choose([&](const MonitorInfo& m) {
            return m.ccd_identifiers_available && SameAdapter(previous.target_adapter_id, m.target_adapter_id)
                && previous.target_id == m.target_id;
        })) { return; }
        if (choose([&](const MonitorInfo& m) { return SameText(previous.device_name, m.device_name); })) { return; }
    }
    if (choose([&](const MonitorInfo& m) { return SameText(initial_target.device_name, m.device_name); })) { return; }
    if (choose([](const MonitorInfo& m) { return m.primary; })) { return; }
    if (!snapshot_.monitors.empty()) { selected_ = 0; }
}

void DisplayModel::Fail(DWORD error)
{
    snapshot_ = {};
    selected_ = NoSelection;
    error_ = error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
}

bool DisplayModel::Select(size_t index)
{
    if (index >= snapshot_.monitors.size()) { return false; }
    selected_ = index;
    return true;
}

const MonitorInfo* DisplayModel::Selected() const
{
    return selected_ < snapshot_.monitors.size() ? &snapshot_.monitors[selected_] : nullptr;
}

std::wstring DisplayDeviceLabel(const std::wstring& name)
{
    const size_t separator = name.find_last_of(L"\\/");
    return name.empty() ? L"Unknown device" : name.substr(separator == std::wstring::npos ? 0 : separator + 1);
}

std::vector<DisplayTile> DisplayModel::Layout(const RECT& viewport, int padding) const
{
    std::vector<DisplayTile> result;
    const RECT& virtual_bounds = snapshot_.virtual_bounds_px;
    const double desktop_width = static_cast<double>(virtual_bounds.right) - virtual_bounds.left;
    const double desktop_height = static_cast<double>(virtual_bounds.bottom) - virtual_bounds.top;
    const double width = static_cast<double>(viewport.right) - viewport.left - 2 * padding;
    const double height = static_cast<double>(viewport.bottom) - viewport.top - 2 * padding;
    if (desktop_width <= 0 || desktop_height <= 0 || width <= 0 || height <= 0) { return result; }
    const double scale = (std::min)(width / desktop_width, height / desktop_height);
    const double origin_x = viewport.left + (viewport.right - viewport.left - desktop_width * scale) / 2;
    const double origin_y = viewport.top + (viewport.bottom - viewport.top - desktop_height * scale) / 2;
    for (size_t i = 0; i < snapshot_.monitors.size(); ++i) {
        const auto& monitor = snapshot_.monitors[i];
        auto group = std::find_if(result.begin(), result.end(), [&](const DisplayTile& tile) {
            return Clones(monitor, snapshot_.monitors[tile.outputs.front()]);
        });
        if (group != result.end()) {
            group->outputs.push_back(i);
            if (group->outputs.size() == 2) { group->label += L"\nMirrored"; }
            continue;
        }
        const auto map_x = [&](LONG x) {
            return static_cast<LONG>(std::lround(origin_x + (static_cast<double>(x) - virtual_bounds.left) * scale));
        };
        const auto map_y = [&](LONG y) {
            return static_cast<LONG>(std::lround(origin_y + (static_cast<double>(y) - virtual_bounds.top) * scale));
        };
        RECT bounds{map_x(monitor.bounds_px.left), map_y(monitor.bounds_px.top),
            map_x(monitor.bounds_px.right), map_y(monitor.bounds_px.bottom)};
        bounds.right = (std::max)(bounds.right, bounds.left + 1);
        bounds.bottom = (std::max)(bounds.bottom, bounds.top + 1);
        result.push_back({bounds, {i}, DisplayDeviceLabel(monitor.device_name)});
    }
    return result;
}

std::vector<std::pair<std::wstring, std::wstring>> DisplayInformation(const MonitorInfo& monitor)
{
    const std::wstring unknown = L"Unknown";
    std::wstring work = unknown;
    if (monitor.work_area_px.available) {
        const RECT& r = monitor.work_area_px.value;
        work = Position(r.left, r.top) + L" → " + Position(r.right, r.bottom);
    }
    std::wstring refresh = unknown;
    if (monitor.refresh_rate.available && monitor.refresh_rate.value.denominator != 0) {
        wchar_t text[64];
        swprintf_s(text, L"%.2f Hz", static_cast<double>(monitor.refresh_rate.value.numerator)
            / monitor.refresh_rate.value.denominator);
        refresh = text;
    }
    return {
        {L"Name", monitor.friendly_name.empty() ? monitor.device_name : monitor.friendly_name},
        {L"Device name", monitor.device_name.empty() ? unknown : monitor.device_name},
        {L"Primary", monitor.primary ? L"Yes" : L"No"},
        {L"Position", Position(monitor.bounds_px.left, monitor.bounds_px.top)},
        {L"Resolution", std::to_wstring(monitor.desktop_size_px.cx) + L" × " + std::to_wstring(monitor.desktop_size_px.cy)},
        {L"Work area", work},
        {L"Orientation", monitor.rotation_degrees.available ? std::to_wstring(monitor.rotation_degrees.value) + L"°" : unknown},
        {L"System scale", monitor.scale_percent.available ? std::to_wstring(monitor.scale_percent.value) + L"%" : unknown},
        {L"Refresh rate", refresh},
        {L"Device path", monitor.device_path.empty() ? unknown : monitor.device_path}
    };
}
