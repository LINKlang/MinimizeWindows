#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <deque>
#include <string>
#include <vector>

namespace fixture {

void Check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

struct View {
    UINT32 id;
    std::wstring name;
    RECT bounds;
    bool primary;
    bool mirror = false;
    bool mode_available = true;
    DWORD mode_fields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT
        | DM_DISPLAYORIENTATION | DM_DISPLAYFREQUENCY;
    DWORD orientation = DMDO_DEFAULT;
    int scale = 100;
    HRESULT scale_result = S_OK;
};

std::deque<View> views;
std::vector<DISPLAYCONFIG_PATH_INFO> paths;
std::vector<DISPLAYCONFIG_MODE_INFO> modes;
LONG ccd_error = ERROR_SUCCESS;
int insufficient_queries = 0;
int query_calls = 0;
bool runtime_available = true;
bool target_names_available = true;
bool thread_dpi_available = true;
bool shcore_available = true;
HANDLE original_context = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-1));
HANDLE current_context = original_context;
int context_changes = 0;
int library_loads = 0;
int library_frees = 0;

void Reset()
{
    views.clear(); paths.clear(); modes.clear();
    ccd_error = ERROR_SUCCESS; insufficient_queries = 0; query_calls = 0;
    runtime_available = true; target_names_available = true;
    thread_dpi_available = true; shcore_available = true;
    current_context = original_context;
    context_changes = 0; library_loads = 0; library_frees = 0;
}

View& AddView(const wchar_t* name, RECT bounds, bool primary)
{
    views.push_back({static_cast<UINT32>(views.size()), name, bounds, primary});
    return views.back();
}

void AddPath(const View& view, UINT32 target_id, DISPLAYCONFIG_ROTATION rotation)
{
    UINT32 source_index = static_cast<UINT32>(modes.size());
    for (UINT32 index = 0; index < modes.size(); ++index) {
        if (modes[index].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE && modes[index].id == view.id) {
            source_index = index;
            break;
        }
    }
    DISPLAYCONFIG_MODE_INFO source{};
    source.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
    source.adapterId.LowPart = 16;
    source.id = view.id;
    source.sourceMode.position = {view.bounds.left, view.bounds.top};
    source.sourceMode.width = view.bounds.right - view.bounds.left;
    source.sourceMode.height = view.bounds.bottom - view.bounds.top;
    if (rotation == DISPLAYCONFIG_ROTATION_ROTATE90 || rotation == DISPLAYCONFIG_ROTATION_ROTATE270) {
        const UINT32 width = source.sourceMode.width;
        source.sourceMode.width = source.sourceMode.height;
        source.sourceMode.height = width;
    }
    if (source_index == modes.size()) { modes.push_back(source); }
    const UINT32 target_index = static_cast<UINT32>(modes.size());
    DISPLAYCONFIG_MODE_INFO target{};
    target.infoType = DISPLAYCONFIG_MODE_INFO_TYPE_TARGET;
    target.adapterId.LowPart = 16;
    target.id = target_id;
    target.targetMode.targetVideoSignalInfo.vSyncFreq = {60000, 1001};
    modes.push_back(target);

    DISPLAYCONFIG_PATH_INFO path{};
    path.flags = DISPLAYCONFIG_PATH_ACTIVE;
    path.sourceInfo.adapterId = source.adapterId;
    path.sourceInfo.id = view.id;
    path.sourceInfo.modeInfoIdx = source_index;
    path.targetInfo.adapterId = target.adapterId;
    path.targetInfo.id = target_id;
    path.targetInfo.modeInfoIdx = target_index;
    path.targetInfo.rotation = rotation;
    path.targetInfo.refreshRate = {60, 1};
    paths.push_back(path);
}

template<size_t N>
void Copy(wchar_t (&destination)[N], const std::wstring& value)
{
    Check(value.size() < N, "test string fits API buffer");
    std::wmemcpy(destination, value.c_str(), value.size() + 1);
}

BOOL WINAPI EnumDisplayMonitors(HDC, LPCRECT, MONITORENUMPROC callback, LPARAM data)
{
    if (!runtime_available) { return FALSE; }
    for (auto& view : views) {
        if (!callback(reinterpret_cast<HMONITOR>(&view), nullptr, &view.bounds, data)) {
            return FALSE;
        }
    }
    return TRUE;
}

BOOL WINAPI GetMonitorInfoW(HMONITOR handle, LPMONITORINFO info)
{
    auto& extended = *static_cast<MONITORINFOEXW*>(info);
    const auto& view = *reinterpret_cast<View*>(handle);
    extended.rcMonitor = view.bounds;
    extended.rcWork = view.bounds;
    extended.rcWork.bottom -= 40;
    extended.dwFlags = view.primary ? MONITORINFOF_PRIMARY : 0;
    Copy(extended.szDevice, view.name);
    return TRUE;
}

LONG WINAPI GetDisplayConfigBufferSizes(UINT32 flags, UINT32* path_count, UINT32* mode_count)
{
    Check(flags == QDC_ONLY_ACTIVE_PATHS, "only active CCD paths queried");
    *path_count = static_cast<UINT32>(paths.size());
    *mode_count = static_cast<UINT32>(modes.size());
    return ccd_error;
}

LONG WINAPI QueryDisplayConfig(UINT32 flags, UINT32* path_count, DISPLAYCONFIG_PATH_INFO* output_paths,
    UINT32* mode_count, DISPLAYCONFIG_MODE_INFO* output_modes, DISPLAYCONFIG_TOPOLOGY_ID* topology)
{
    Check(flags == QDC_ONLY_ACTIVE_PATHS && topology == nullptr
        && output_paths != nullptr && output_modes != nullptr, "CCD query parameters");
    ++query_calls;
    if (query_calls <= insufficient_queries) { return ERROR_INSUFFICIENT_BUFFER; }
    Check(*path_count >= paths.size() && *mode_count >= modes.size(), "CCD allocated buffer sizes");
    std::copy(paths.begin(), paths.end(), output_paths);
    std::copy(modes.begin(), modes.end(), output_modes);
    return ERROR_SUCCESS;
}

LONG WINAPI DisplayConfigGetDeviceInfo(DISPLAYCONFIG_DEVICE_INFO_HEADER* header)
{
    if (header->type == DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME) {
        auto& name = *reinterpret_cast<DISPLAYCONFIG_SOURCE_DEVICE_NAME*>(header);
        for (const auto& view : views) {
            if (view.id == header->id) { Copy(name.viewGdiDeviceName, view.name); return ERROR_SUCCESS; }
        }
    }
    else if (header->type == DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME && target_names_available) {
        auto& name = *reinterpret_cast<DISPLAYCONFIG_TARGET_DEVICE_NAME*>(header);
        Copy(name.monitorFriendlyDeviceName, L"\u663E\u793A\u5668");
        Copy(name.monitorDevicePath, L"test-device-" + std::to_wstring(header->id));
        return ERROR_SUCCESS;
    }
    return ERROR_NOT_FOUND;
}

BOOL WINAPI EnumDisplaySettingsExW(LPCWSTR name, DWORD setting, DEVMODEW* mode, DWORD flags)
{
    Check(setting == ENUM_CURRENT_SETTINGS && flags == 0, "only current display settings read");
    for (const auto& view : views) {
        if (view.name == name && view.mode_available) {
            mode->dmFields = view.mode_fields;
            mode->dmPosition = {view.bounds.left, view.bounds.top};
            mode->dmPelsWidth = view.bounds.right - view.bounds.left;
            mode->dmPelsHeight = view.bounds.bottom - view.bounds.top;
            mode->dmDisplayOrientation = view.orientation;
            mode->dmDisplayFrequency = 60;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL WINAPI EnumDisplayDevicesW(LPCWSTR, DWORD index, PDISPLAY_DEVICEW device, DWORD)
{
    if (index >= views.size()) { return FALSE; }
    Copy(device->DeviceName, views[index].name);
    device->StateFlags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP
        | (views[index].mirror ? DISPLAY_DEVICE_MIRRORING_DRIVER : 0);
    return TRUE;
}

HANDLE WINAPI SetContext(HANDLE context)
{
    HANDLE previous = current_context;
    current_context = context;
    ++context_changes;
    SetLastError(ERROR_ACCESS_DENIED); // Restore calls must not replace the result error.
    return previous;
}

HRESULT WINAPI GetScale(HMONITOR handle, int* scale)
{
    Check(current_context == original_context, "scale read after restoring thread DPI");
    const auto& view = *reinterpret_cast<View*>(handle);
    *scale = view.scale;
    return view.scale_result;
}

HMODULE WINAPI GetModuleHandleW(LPCWSTR) { return reinterpret_cast<HMODULE>(1); }
HMODULE WINAPI LoadLibraryExW(LPCWSTR name, HANDLE, DWORD flags)
{
    Check(std::wstring(name) == L"Shcore.dll" && flags == LOAD_LIBRARY_SEARCH_SYSTEM32,
        "optional DLL searched only in System32");
    if (!shcore_available) { return nullptr; }
    ++library_loads;
    return reinterpret_cast<HMODULE>(2);
}
BOOL WINAPI FreeLibrary(HMODULE) { ++library_frees; SetLastError(ERROR_ACCESS_DENIED); return TRUE; }
BOOL WINAPI IsProcessDPIAware() { return FALSE; }
FARPROC WINAPI GetProcAddress(HMODULE module, LPCSTR name)
{
    if (module == reinterpret_cast<HMODULE>(1) && std::strcmp(name, "SetThreadDpiAwarenessContext") == 0) {
        return thread_dpi_available ? reinterpret_cast<FARPROC>(SetContext) : nullptr;
    }
    if (module == reinterpret_cast<HMODULE>(2) && std::strcmp(name, "GetScaleFactorForMonitor") == 0) {
        return reinterpret_cast<FARPROC>(GetScale);
    }
    return nullptr;
}

} // namespace fixture

#define EnumDisplayMonitors fixture::EnumDisplayMonitors
#define GetMonitorInfoW fixture::GetMonitorInfoW
#define GetDisplayConfigBufferSizes fixture::GetDisplayConfigBufferSizes
#define QueryDisplayConfig fixture::QueryDisplayConfig
#define DisplayConfigGetDeviceInfo fixture::DisplayConfigGetDeviceInfo
#define EnumDisplaySettingsExW fixture::EnumDisplaySettingsExW
#define EnumDisplayDevicesW fixture::EnumDisplayDevicesW
#define GetModuleHandleW fixture::GetModuleHandleW
#define GetProcAddress fixture::GetProcAddress
#define LoadLibraryExW fixture::LoadLibraryExW
#define FreeLibrary fixture::FreeLibrary
#define IsProcessDPIAware fixture::IsProcessDPIAware
#include "../src/monitor_enumerator.cpp"

int main()
{
    using namespace fixture;
    MonitorEnumerator enumerator;
    MonitorSnapshot snapshot;
    Reset();
    auto& primary = AddView(L"\\\\.\\DISPLAY1", {0, 0, 2560, 1440}, true);
    primary.scale = 150;
    auto& portrait = AddView(L"\\\\.\\DISPLAY2", {-1080, -559, 0, 1361}, false);
    portrait.scale = 125;
    AddPath(primary, 10, DISPLAYCONFIG_ROTATION_IDENTITY);
    AddPath(portrait, 20, DISPLAYCONFIG_ROTATION_ROTATE90);
    Check(enumerator.Enumerate(snapshot) && snapshot.monitors.size() == 2, "two active outputs");
    Check(snapshot.monitors[1].desktop_size_px.cx == 1080 && snapshot.monitors[1].desktop_size_px.cy == 1920,
        "portrait source surface normalized to desktop view");
    Check(snapshot.virtual_bounds_px.left == -1080 && snapshot.virtual_bounds_px.top == -559
        && snapshot.virtual_bounds_px.right == 2560 && snapshot.virtual_bounds_px.bottom == 1440,
        "negative positions and virtual desktop union");
    Check(snapshot.monitors[0].scale_percent.value == 150 && snapshot.monitors[1].scale_percent.value == 125,
        "mixed per-monitor scale steps retained");
    Check(snapshot.monitors[0].work_area_px.available && snapshot.monitors[1].work_area_px.available,
        "physical work areas available under temporary DPI context");
    Check(snapshot.monitors[0].refresh_rate.value.denominator == 1001
        && snapshot.monitors[0].friendly_name == L"\u663E\u793A\u5668", "rational refresh and Unicode names");
    Check(current_context == original_context && context_changes == 2
        && library_loads == library_frees && GetLastError() == ERROR_SUCCESS, "DPI, library and error cleanup");

    AddPath(primary, 30, DISPLAYCONFIG_ROTATION_ROTATE270);
    Check(enumerator.Enumerate(snapshot) && snapshot.monitors.size() == 3, "cloned targets retained");
    Check(snapshot.monitors[0].source_id == snapshot.monitors[1].source_id
        && snapshot.monitors[0].target_id != snapshot.monitors[1].target_id
        && SameRect(snapshot.monitors[0].bounds_px, snapshot.monitors[1].bounds_px),
        "differently rotated clones share the real source desktop rectangle");

    target_names_available = false;
    primary.scale_result = E_FAIL;
    paths[0].targetInfo.rotation = DISPLAYCONFIG_ROTATION_FORCE_UINT32;
    modes[1].targetMode.targetVideoSignalInfo.vSyncFreq = {0, 0};
    paths[0].targetInfo.refreshRate = {0, 0};
    Check(enumerator.Enumerate(snapshot), "missing optional fields do not lose layouts");
    Check(snapshot.monitors[0].friendly_name == primary.name && snapshot.monitors[0].device_path.empty()
        && !snapshot.monitors[0].scale_percent.available && !snapshot.monitors[0].rotation_degrees.available
        && !snapshot.monitors[0].refresh_rate.available, "unknown optional data never becomes fake defaults");

    Reset();
    auto& fallback = AddView(L"\\\\.\\DISPLAY1", {-800, 0, 0, 600}, true);
    fallback.orientation = DMDO_90;
    ccd_error = ERROR_NOT_SUPPORTED;
    thread_dpi_available = false;
    shcore_available = false;
    auto& pseudo = AddView(L"mirror-driver", {0, 0, 800, 600}, false);
    pseudo.mirror = true;
    pseudo.mode_available = false;
    Check(enumerator.Enumerate(snapshot) && snapshot.monitors.size() == 1
        && snapshot.query_source == MonitorQuerySource::Gdi, "Win7 GDI fallback skips mirror pseudo-monitors");
    Check(!snapshot.monitors[0].ccd_identifiers_available && !snapshot.monitors[0].work_area_px.available
        && !snapshot.monitors[0].scale_percent.available && snapshot.monitors[0].rotation_degrees.value == 90,
        "legacy unknown fields and Windows orientation labels");

    Reset();
    auto& hotplug = AddView(L"\\\\.\\DISPLAY1", {0, 0, 800, 600}, true);
    AddPath(hotplug, 10, DISPLAYCONFIG_ROTATION_IDENTITY);
    insufficient_queries = 2;
    Check(enumerator.Enumerate(snapshot) && query_calls == 3
        && snapshot.query_source == MonitorQuerySource::DisplayConfig, "buffer size retry succeeds on third attempt");
    query_calls = 0;
    insufficient_queries = 99;
    Check(enumerator.Enumerate(snapshot) && query_calls == 3
        && snapshot.query_source == MonitorQuerySource::Gdi, "unstable CCD falls back after three attempts");

    query_calls = 0; insufficient_queries = 0;
    paths[0].sourceInfo.modeInfoIdx = 999;
    Check(!enumerator.Enumerate(snapshot) && snapshot.monitors.empty()
        && GetLastError() == ERROR_INVALID_DATA && current_context == original_context,
        "invalid source index clears stale snapshot and restores DPI");
    paths[0].sourceInfo.modeInfoIdx = 0;
    modes[0].id = 99;
    Check(!enumerator.Enumerate(snapshot) && GetLastError() == ERROR_INVALID_DATA, "source identity must match");
    modes[0].id = 0;
    modes[0].sourceMode.position.x = 10;
    Check(!enumerator.Enumerate(snapshot) && GetLastError() == ERROR_RETRY, "incoherent topology reports failure");
    modes[0].sourceMode.position.x = 0;
    hotplug.mode_available = false;
    Check(!enumerator.Enumerate(snapshot) && snapshot.monitors.empty(), "missing required layout is a failure");

    Reset();
    AddView(L"\\\\.\\DISPLAY1", {0, 0, 800, 600}, true);
    Check(enumerator.Enumerate(snapshot) && snapshot.query_source == MonitorQuerySource::Gdi
        && snapshot.monitors.size() == 1, "empty CCD with desktop monitors uses GDI");
    ccd_error = ERROR_ACCESS_DENIED;
    runtime_available = false;
    Check(!enumerator.Enumerate(snapshot) && snapshot.monitors.empty(), "both query paths unavailable clear output");
    Reset();
    Check(enumerator.Enumerate(snapshot) && snapshot.monitors.empty(), "genuinely empty topology is valid");
    std::puts("Monitor boundary tests passed.");
    return 0;
}
