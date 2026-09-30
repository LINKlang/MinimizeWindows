#include "monitor_enumerator.h"

#include <algorithm>
#include <climits>
#include <new>
#include <utility>

namespace {

template<size_t N>
std::wstring ReadString(const wchar_t (&text)[N])
{
    return std::wstring(text, std::find(text, text + N, L'\0'));
}

bool SameAdapter(const LUID& left, const LUID& right)
{
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

bool SameRect(const RECT& left, const RECT& right)
{
    return left.left == right.left && left.top == right.top
        && left.right == right.right && left.bottom == right.bottom;
}

bool SetBounds(MonitorInfo& monitor, LONG x, LONG y, UINT32 width, UINT32 height)
{
    const long long right = static_cast<long long>(x) + width;
    const long long bottom = static_cast<long long>(y) + height;
    if (width == 0 || height == 0 || width > LONG_MAX || height > LONG_MAX
        || right > LONG_MAX || bottom > LONG_MAX) {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    monitor.bounds_px = {x, y, static_cast<LONG>(right), static_cast<LONG>(bottom)};
    monitor.desktop_size_px = {static_cast<LONG>(width), static_cast<LONG>(height)};
    monitor.primary = x == 0 && y == 0;
    return true;
}

class DpiScope {
public:
    DpiScope()
    {
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        set_context_ = reinterpret_cast<SetContext>(
            GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
        if (set_context_ != nullptr) {
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE, dynamically used on Win10+.
            previous_ = set_context_(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-3)));
        }
        physical_coordinates = previous_ != nullptr || IsProcessDPIAware() != FALSE;
    }

    ~DpiScope()
    {
        if (previous_ != nullptr) {
            set_context_(previous_);
        }
    }

    bool physical_coordinates = false;

private:
    using SetContext = HANDLE (WINAPI*)(HANDLE);
    SetContext set_context_ = nullptr;
    HANDLE previous_ = nullptr;
};

class ScaleQuery {
public:
    ScaleQuery()
    {
        // An unsupported search flag on old Win7 also makes this optional.
        module_ = LoadLibraryExW(L"Shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (module_ != nullptr) {
            get_scale_ = reinterpret_cast<GetScale>(
                GetProcAddress(module_, "GetScaleFactorForMonitor"));
        }
    }

    ~ScaleQuery()
    {
        if (module_ != nullptr) {
            FreeLibrary(module_);
        }
    }

    void Read(MonitorInfo& monitor) const
    {
        int scale = 0;
        if (get_scale_ != nullptr && monitor.handle != nullptr
            && get_scale_(monitor.handle, &scale) == S_OK && scale > 0) {
            monitor.scale_percent = {true, static_cast<unsigned int>(scale)};
        }
    }

private:
    using GetScale = HRESULT (WINAPI*)(HMONITOR, int*);
    HMODULE module_ = nullptr;
    GetScale get_scale_ = nullptr;
};

struct RuntimeMonitor {
    HMONITOR handle;
    MONITORINFOEXW info;
};

struct RuntimeCollection {
    std::vector<RuntimeMonitor> monitors;
    DWORD error = ERROR_SUCCESS;
};

BOOL CALLBACK CollectRuntimeMonitor(HMONITOR handle, HDC, LPRECT, LPARAM data)
{
    auto& collection = *reinterpret_cast<RuntimeCollection*>(data);
    RuntimeMonitor monitor{};
    monitor.handle = handle;
    monitor.info.cbSize = sizeof(monitor.info);
    if (!GetMonitorInfoW(handle, &monitor.info)) {
        collection.error = GetLastError();
        if (collection.error == ERROR_SUCCESS) {
            collection.error = ERROR_GEN_FAILURE;
        }
        return FALSE;
    }
    try {
        collection.monitors.push_back(monitor);
    }
    catch (const std::bad_alloc&) {
        collection.error = ERROR_NOT_ENOUGH_MEMORY;
        return FALSE;
    }
    return TRUE;
}

void AddRuntimeData(MonitorInfo& monitor, const std::vector<RuntimeMonitor>& runtime,
    bool physical_coordinates)
{
    for (const auto& item : runtime) {
        if (monitor.device_name != ReadString(item.info.szDevice)) {
            continue;
        }
        monitor.handle = item.handle;
        monitor.primary = (item.info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        const RECT& work = item.info.rcWork;
        if (physical_coordinates && SameRect(item.info.rcMonitor, monitor.bounds_px)
            && work.left >= monitor.bounds_px.left && work.top >= monitor.bounds_px.top
            && work.right <= monitor.bounds_px.right && work.bottom <= monitor.bounds_px.bottom
            && work.right > work.left && work.bottom > work.top) {
            monitor.work_area_px = {true, work};
        }
        break;
    }
}

bool IsMode(const DISPLAYCONFIG_MODE_INFO& mode, DISPLAYCONFIG_MODE_INFO_TYPE type,
    const LUID& adapter, UINT32 id)
{
    return mode.infoType == type && SameAdapter(mode.adapterId, adapter) && mode.id == id;
}

bool SetSourceBounds(MonitorInfo& monitor, const DISPLAYCONFIG_SOURCE_MODE& source,
    const std::vector<MonitorInfo>& previous)
{
    // Cloned outputs share one desktop view, even if their target rotations differ.
    for (const auto& item : previous) {
        if (SameAdapter(item.source_adapter_id, monitor.source_adapter_id)
            && item.source_id == monitor.source_id) {
            if (item.bounds_px.left != source.position.x || item.bounds_px.top != source.position.y) {
                SetLastError(ERROR_INVALID_DATA);
                return false;
            }
            return SetBounds(monitor, item.bounds_px.left, item.bounds_px.top,
                item.desktop_size_px.cx, item.desktop_size_px.cy);
        }
    }

    // Current GDI view dimensions are already rotated and are DPI-independent.
    // They also identify the shared desktop view of differently rotated clones.
    DEVMODEW current{};
    current.dmSize = sizeof(current);
    const DWORD required = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
    if (monitor.device_name.empty()
        || !EnumDisplaySettingsExW(monitor.device_name.c_str(), ENUM_CURRENT_SETTINGS, &current, 0)) {
        SetLastError(ERROR_GEN_FAILURE);
        return false;
    }
    if ((current.dmFields & required) != required) {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    if (current.dmPosition.x != source.position.x || current.dmPosition.y != source.position.y) {
        SetLastError(ERROR_RETRY);
        return false;
    }
    return SetBounds(monitor, source.position.x, source.position.y,
        current.dmPelsWidth, current.dmPelsHeight);
}

enum class CcdResult { Success, Unavailable, InvalidLayout };

CcdResult ReadDisplayConfig(MonitorSnapshot& snapshot,
    const std::vector<RuntimeMonitor>& runtime, bool physical_coordinates)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        UINT32 path_count = 0;
        UINT32 mode_count = 0;
        LONG result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count);
        if (result != ERROR_SUCCESS) {
            return CcdResult::Unavailable;
        }

        // QueryDisplayConfig requires non-null buffers even when their counts are zero.
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count == 0 ? 1 : path_count);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count == 0 ? 1 : mode_count);
        result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(),
            &mode_count, modes.data(), nullptr);
        if (result == ERROR_INSUFFICIENT_BUFFER) {
            continue;
        }
        if (result != ERROR_SUCCESS) {
            return CcdResult::Unavailable;
        }
        if (path_count > paths.size() || mode_count > modes.size()) {
            SetLastError(ERROR_INVALID_DATA);
            return CcdResult::InvalidLayout;
        }

        snapshot.query_source = MonitorQuerySource::DisplayConfig;
        for (UINT32 index = 0; index < path_count; ++index) {
            const auto& path = paths[index];
            if ((path.flags & DISPLAYCONFIG_PATH_ACTIVE) == 0) {
                continue;
            }
            const auto& source = path.sourceInfo;
            const auto& target = path.targetInfo;
            if (source.modeInfoIdx >= mode_count
                || !IsMode(modes[source.modeInfoIdx], DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE,
                    source.adapterId, source.id)) {
                SetLastError(ERROR_INVALID_DATA);
                return CcdResult::InvalidLayout;
            }
            const auto& mode = modes[source.modeInfoIdx].sourceMode;
            MonitorInfo monitor;
            monitor.ccd_identifiers_available = true;
            monitor.source_adapter_id = source.adapterId;
            monitor.target_adapter_id = target.adapterId;
            monitor.source_id = source.id;
            monitor.target_id = target.id;

            DISPLAYCONFIG_SOURCE_DEVICE_NAME source_name{};
            source_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source_name.header.size = sizeof(source_name);
            source_name.header.adapterId = source.adapterId;
            source_name.header.id = source.id;
            if (DisplayConfigGetDeviceInfo(&source_name.header) == ERROR_SUCCESS) {
                monitor.device_name = ReadString(source_name.viewGdiDeviceName);
            }
            if (mode.width == 0 || mode.height == 0) {
                SetLastError(ERROR_INVALID_DATA);
                return CcdResult::InvalidLayout;
            }
            if (!SetSourceBounds(monitor, mode, snapshot.monitors)) {
                return CcdResult::InvalidLayout;
            }

            DISPLAYCONFIG_TARGET_DEVICE_NAME target_name{};
            target_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            target_name.header.size = sizeof(target_name);
            target_name.header.adapterId = target.adapterId;
            target_name.header.id = target.id;
            if (DisplayConfigGetDeviceInfo(&target_name.header) == ERROR_SUCCESS) {
                monitor.device_path = ReadString(target_name.monitorDevicePath);
                monitor.friendly_name = ReadString(target_name.monitorFriendlyDeviceName);
            }
            if (monitor.friendly_name.empty()) {
                monitor.friendly_name = monitor.device_name;
            }

            switch (target.rotation) {
            case DISPLAYCONFIG_ROTATION_IDENTITY: monitor.rotation_degrees = {true, 0}; break;
            case DISPLAYCONFIG_ROTATION_ROTATE90: monitor.rotation_degrees = {true, 90}; break;
            case DISPLAYCONFIG_ROTATION_ROTATE180: monitor.rotation_degrees = {true, 180}; break;
            case DISPLAYCONFIG_ROTATION_ROTATE270: monitor.rotation_degrees = {true, 270}; break;
            default: break;
            }

            DISPLAYCONFIG_RATIONAL refresh = target.refreshRate;
            if (target.modeInfoIdx < mode_count
                && IsMode(modes[target.modeInfoIdx], DISPLAYCONFIG_MODE_INFO_TYPE_TARGET,
                    target.adapterId, target.id)) {
                const auto& signal = modes[target.modeInfoIdx].targetMode.targetVideoSignalInfo.vSyncFreq;
                if (signal.Numerator != 0 && signal.Denominator != 0) {
                    refresh = signal;
                }
            }
            if (refresh.Numerator != 0 && refresh.Denominator != 0) {
                monitor.refresh_rate = {true, {refresh.Numerator, refresh.Denominator}};
            }

            AddRuntimeData(monitor, runtime, physical_coordinates);
            // Do not deduplicate clones: distinct targets can share one source rectangle.
            snapshot.monitors.push_back(std::move(monitor));
        }
        return CcdResult::Success;
    }
    return CcdResult::Unavailable;
}

bool IsMirrorOrDetached(const std::wstring& name)
{
    DISPLAY_DEVICEW device{};
    for (DWORD index = 0; ; ++index) {
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(nullptr, index, &device, 0)) {
            return false;
        }
        if (name == ReadString(device.DeviceName)) {
            return (device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0
                || (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0;
        }
    }
}

bool ReadGdi(MonitorSnapshot& snapshot, const std::vector<RuntimeMonitor>& runtime,
    bool physical_coordinates)
{
    for (const auto& item : runtime) {
        MonitorInfo monitor;
        monitor.device_name = ReadString(item.info.szDevice);
        if (IsMirrorOrDetached(monitor.device_name)) {
            continue;
        }
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsExW(monitor.device_name.c_str(), ENUM_CURRENT_SETTINGS, &mode, 0)) {
            SetLastError(ERROR_GEN_FAILURE);
            return false;
        }
        const DWORD required = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
        if ((mode.dmFields & required) != required) {
            SetLastError(ERROR_INVALID_DATA);
            return false;
        }
        if (!SetBounds(monitor, mode.dmPosition.x, mode.dmPosition.y,
                mode.dmPelsWidth, mode.dmPelsHeight)) {
            return false;
        }
        monitor.friendly_name = monitor.device_name;
        if ((mode.dmFields & DM_DISPLAYORIENTATION) != 0 && mode.dmDisplayOrientation <= DMDO_270) {
            // GDI names the device orientation; CCD names its compensating image
            // transform. Corresponding Windows orientation labels share the index.
            monitor.rotation_degrees = {true, mode.dmDisplayOrientation * 90};
        }
        if ((mode.dmFields & DM_DISPLAYFREQUENCY) != 0 && mode.dmDisplayFrequency > 1) {
            monitor.refresh_rate = {true, {mode.dmDisplayFrequency, 1}};
        }
        AddRuntimeData(monitor, runtime, physical_coordinates);
        snapshot.monitors.push_back(std::move(monitor));
    }
    return true;
}

void FinishSnapshot(MonitorSnapshot& snapshot)
{
    std::stable_sort(snapshot.monitors.begin(), snapshot.monitors.end(),
        [](const MonitorInfo& left, const MonitorInfo& right) {
            if (left.primary != right.primary) { return left.primary; }
            if (left.device_name != right.device_name) { return left.device_name < right.device_name; }
            if (left.device_path != right.device_path) { return left.device_path < right.device_path; }
            return left.target_id < right.target_id;
        });
    if (snapshot.monitors.empty()) {
        return;
    }
    RECT bounds = snapshot.monitors.front().bounds_px;
    for (const auto& monitor : snapshot.monitors) {
        const RECT& rect = monitor.bounds_px;
        bounds.left = rect.left < bounds.left ? rect.left : bounds.left;
        bounds.top = rect.top < bounds.top ? rect.top : bounds.top;
        bounds.right = rect.right > bounds.right ? rect.right : bounds.right;
        bounds.bottom = rect.bottom > bounds.bottom ? rect.bottom : bounds.bottom;
    }
    snapshot.virtual_bounds_px = bounds;
}

} // namespace

bool MonitorEnumerator::Enumerate(MonitorSnapshot& snapshot) const
{
    snapshot = MonitorSnapshot{};
    DWORD error = ERROR_SUCCESS;
    try {
        MonitorSnapshot result;
        {
            DpiScope dpi;
            RuntimeCollection runtime;
            const bool runtime_available = EnumDisplayMonitors(nullptr, nullptr,
                CollectRuntimeMonitor, reinterpret_cast<LPARAM>(&runtime)) != FALSE;
            if (!runtime_available) {
                if (runtime.error == ERROR_SUCCESS) {
                    runtime.error = ERROR_GEN_FAILURE;
                }
                runtime.monitors.clear();
            }

            const CcdResult ccd = ReadDisplayConfig(result, runtime.monitors,
                dpi.physical_coordinates);
            if (ccd == CcdResult::InvalidLayout) {
                error = GetLastError();
            }
            else if (ccd == CcdResult::Unavailable
                || (result.monitors.empty() && !runtime.monitors.empty())) {
                result = MonitorSnapshot{};
                if (!runtime_available) {
                    error = runtime.error;
                }
                else if (!ReadGdi(result, runtime.monitors, dpi.physical_coordinates)) {
                    error = GetLastError();
                }
            }
        }
        if (error == ERROR_SUCCESS) {
            ScaleQuery scale;
            for (auto& monitor : result.monitors) {
                scale.Read(monitor);
            }
            FinishSnapshot(result);
            snapshot = std::move(result);
        }
    }
    catch (const std::bad_alloc&) {
        error = ERROR_NOT_ENOUGH_MEMORY;
    }
    SetLastError(error);
    return error == ERROR_SUCCESS;
}
