// Model and real WTL controls on a never-activated desktop. No global input.
#include "../src/settings_window.h"
#include <imm.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

WTL::CAppModule _Module;

namespace display_test {

MonitorSnapshot fixture;
DWORD query_error = ERROR_SUCCESS;
int query_count = 0;
bool use_real = false;

void Check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", message, GetLastError()); std::exit(1); }
}

MonitorInfo Monitor(const wchar_t* device, const wchar_t* path, RECT bounds, bool primary, UINT32 id)
{
    MonitorInfo result;
    result.device_name = device;
    result.device_path = path;
    result.friendly_name = primary ? L"主显示器 · 中文名称" : L"竖屏显示器";
    result.primary = primary;
    result.bounds_px = bounds;
    result.desktop_size_px = {bounds.right - bounds.left, bounds.bottom - bounds.top};
    result.ccd_identifiers_available = true;
    result.source_adapter_id = {1, 0};
    result.target_adapter_id = {1, 0};
    result.source_id = result.target_id = id;
    result.rotation_degrees = {true, primary ? 0u : 90u};
    result.scale_percent = {true, primary ? 150u : 100u};
    result.refresh_rate = {true, {60000, 1001}};
    return result;
}

MonitorSnapshot Dual()
{
    MonitorSnapshot snapshot;
    snapshot.monitors = {
        Monitor(L"\\\\.\\DISPLAY1", L"path1", {0, 0, 1920, 1080}, true, 1),
        Monitor(L"\\\\.\\DISPLAY2", L"path2", {-1080, -400, 0, 1520}, false, 2)
    };
    snapshot.virtual_bounds_px = {-1080, -400, 1920, 1520};
    snapshot.query_source = MonitorQuerySource::DisplayConfig;
    return snapshot;
}

void Pump()
{
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        Check(message.message != WM_QUIT, "settings closure does not quit");
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

RECT Bounds(HWND window) { RECT bounds{}; GetClientRect(window, &bounds); return bounds; }

void RenderClient(HWND window, HDC dc)
{
    SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND);
    for (HWND child = GetWindow(window, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT)) {
        if ((GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) == 0) { continue; }
        RECT bounds{};
        GetWindowRect(child, &bounds);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&bounds), 2);
        const int state = SaveDC(dc);
        OffsetViewportOrgEx(dc, bounds.left, bounds.top, nullptr);
        IntersectClipRect(dc, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top);
        RenderClient(child, dc);
        RestoreDC(dc, state);
    }
}

void SaveClient(HWND window, const wchar_t* path)
{
    const RECT bounds = Bounds(window);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bounds.right;
    info.bmiHeader.biHeight = -bounds.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    const HDC destination = GetDC(window);
    const HDC memory = CreateCompatibleDC(destination);
    const HBITMAP bitmap = CreateDIBSection(destination, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    Check(memory != nullptr && bitmap != nullptr && pixels != nullptr, "allocate screenshot");
    const HGDIOBJ old = SelectObject(memory, bitmap);
    RenderClient(window, memory);
    BITMAPFILEHEADER header{};
    header.bfType = 0x4d42;
    header.bfOffBits = sizeof(header) + sizeof(info.bmiHeader);
    const DWORD size = bounds.right * bounds.bottom * 4;
    header.bfSize = header.bfOffBits + size;
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "create screenshot");
    DWORD written = 0;
    Check(WriteFile(file, &header, sizeof(header), &written, nullptr) && written == sizeof(header), "write bitmap header");
    Check(WriteFile(file, &info.bmiHeader, sizeof(info.bmiHeader), &written, nullptr), "write bitmap information");
    Check(WriteFile(file, pixels, size, &written, nullptr) && written == size, "write screenshot pixels");
    CloseHandle(file);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(window, destination);
}

void Models()
{
    const MonitorTarget target{L"\\\\.\\display2"};
    DisplayModel model;
    model.Update(Dual(), target);
    Check(model.Selected()->device_path == L"path2", "initial selection uses case-insensitive startup target");
    const RECT viewport{0, 0, 640, 320};
    auto tiles = model.Layout(viewport, 24);
    Check(tiles.size() == 2, "two extended outputs produce two tiles");
    const RECT& a = tiles[0].bounds;
    const RECT& b = tiles[1].bounds;
    Check(b.right == a.left && b.top < a.top, "left/upper negative position and touching edges are retained");
    const double ratio = static_cast<double>(a.right - a.left) / (a.bottom - a.top);
    Check(std::abs(ratio - 1920.0 / 1080.0) < 0.02, "physical aspect ratio is preserved");
    Check(b.left >= 24 && b.top >= 24 && a.right <= 616 && b.bottom <= 296, "topology fits padded viewport");
    auto changed_scale = Dual();
    changed_scale.monitors[0].scale_percent.value = 225;
    model.Update(changed_scale, target);
    const auto same = model.Layout(viewport, 24);
    Check(EqualRect(&same[0].bounds, &a) && EqualRect(&same[1].bounds, &b),
        "system scale does not resize physical desktop topology");

    auto moved = Dual();
    moved.monitors[1].device_name = L"\\\\.\\DISPLAY8";
    moved.monitors[1].target_id = 88;
    model.Update(moved, {});
    Check(model.Selected()->device_path == L"path2", "device path preserves selection despite changed CCD and GDI IDs");
    moved.monitors[1].device_path.clear();
    model.Update(moved, {});
    Check(model.Selected()->target_id == 88, "CCD target identity is second matching choice");
    moved.monitors[1].ccd_identifiers_available = false;
    model.Update(moved, {});
    Check(model.Selected()->device_name == L"\\\\.\\DISPLAY8", "GDI name is the fallback matching choice");
    moved.monitors.pop_back();
    moved.virtual_bounds_px = moved.monitors[0].bounds_px;
    model.Update(moved, target);
    Check(model.Selected()->primary, "disconnected selection falls back to primary");

    auto clones = Dual();
    auto mirror = clones.monitors[0];
    mirror.device_path = L"mirror";
    mirror.target_id = 3;
    mirror.friendly_name = L"镜像输出";
    clones.monitors.push_back(mirror);
    model.Update(clones, {});
    tiles = model.Layout(viewport, 24);
    Check(tiles.size() == 2 && tiles[0].outputs.size() == 2 && model.Snapshot().monitors.size() == 3,
        "shared CCD source groups a tile without dropping physical output records");
    Check(tiles[0].label.find(L"Mirrored") != std::wstring::npos, "clone tile is labeled");
    auto unrelated = clones;
    unrelated.monitors.back().source_id = 99;
    model.Update(unrelated, {});
    Check(model.Layout(viewport, 24).size() == 3, "equal rectangles alone do not establish a clone");
    for (auto& monitor : unrelated.monitors) { monitor.ccd_identifiers_available = false; }
    model.Update(unrelated, {});
    Check(model.Layout(viewport, 24).size() == 3, "GDI fallback does not invent CCD clone grouping");

    MonitorInfo optional;
    optional.device_name = L"\\\\.\\DISPLAY4";
    optional.bounds_px = {-100, -200, 1500, 700};
    optional.desktop_size_px = {1600, 900};
    const auto unknown = DisplayInformation(optional);
    Check(unknown[0].second == optional.device_name && unknown[5].second == L"Unknown"
        && unknown[6].second == L"Unknown" && unknown[7].second == L"Unknown" && unknown[8].second == L"Unknown",
        "optional fields stay unknown and name falls back to device");
    optional.refresh_rate = {true, {60000, 1001}};
    Check(DisplayInformation(optional)[8].second == L"59.94 Hz", "rational refresh is formatted as Hz");
    model.Fail(ERROR_ACCESS_DENIED);
    Check(model.Selected() == nullptr && model.Snapshot().monitors.empty() && model.Layout(viewport, 24).empty(),
        "failure clears selection and topology");
    model.Update({}, {});
    Check(model.Error() == ERROR_SUCCESS && model.Selected() == nullptr, "empty successful snapshot is an empty state");
}

} // namespace display_test

class FixtureMonitorEnumerator {
public:
    bool Enumerate(MonitorSnapshot& snapshot) const
    {
        ++display_test::query_count;
        if (display_test::use_real) { return MonitorEnumerator().Enumerate(snapshot); }
        if (display_test::query_error != ERROR_SUCCESS) {
            snapshot = {};
            SetLastError(display_test::query_error);
            return false;
        }
        snapshot = display_test::fixture;
        return true;
    }
};

#define MonitorEnumerator FixtureMonitorEnumerator
#include "../src/display_page.cpp"
#undef MonitorEnumerator

int main()
{
    using namespace display_test;
    Models();
    const HDESK original = GetThreadDesktop(GetCurrentThreadId());
    wchar_t desktop_name[64];
    swprintf_s(desktop_name, L"MinimizeWindowsDisplayTests_%lu", GetCurrentProcessId());
    const HDESK isolated = CreateDesktopW(desktop_name, nullptr, nullptr, 0,
        DESKTOP_CREATEWINDOW | DESKTOP_CREATEMENU | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS, nullptr);
    Check(isolated != nullptr && SetThreadDesktop(isolated), "attach to isolated display desktop");
    ImmDisableIME(0);
    Check(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize COM");
    Check(SUCCEEDED(_Module.Init(nullptr, GetModuleHandleW(nullptr))), "initialize WTL");
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    Check(InitCommonControlsEx(&controls), "initialize common controls");
    WTL::CMessageLoop loop;
    Check(_Module.AddMessageLoop(&loop), "register message loop");

    fixture = Dual();
    const MonitorTarget target{L"\\\\.\\DISPLAY1"};
    const HICON icon = LoadIconW(nullptr, IDI_APPLICATION);
    SettingsWindow frame;
    Check(frame.Show(icon, target), "create settings with Display tab");
    Check(Bounds(frame).right == 800 && Bounds(frame).bottom == 640, "initial client dimensions");
    const HWND integrated = GetDlgItem(frame, SettingsWindow::DisplayId);
    Check(integrated != nullptr && GetDlgItem(frame, SettingsWindow::DisplayTabId) != nullptr,
        "settings contains Display tab and page");
    const int opened_queries = query_count;
    Check(frame.Show(icon, target) && query_count == opened_queries + 1, "re-activation refreshes snapshot");
    SendMessageW(frame, WM_DISPLAYCHANGE, 0, 0);
    Check(query_count == opened_queries + 2, "display changes refresh snapshot");
    const HWND integrated_graph = GetDlgItem(integrated, DisplayPage::TopologyId);
    SetFocus(integrated_graph);
    MSG tab{};
    tab.hwnd = integrated_graph;
    tab.message = WM_KEYDOWN;
    tab.wParam = VK_TAB;
    Check(frame.PreTranslateMessage(&tab) && GetFocus() == GetDlgItem(integrated, DisplayPage::ListId),
        "Tab moves between focusable surfaces");
    SaveClient(frame, L"build\\tests\\display-fixture.bmp");

    const HWND parent = CreateWindowExW(0, L"STATIC", L"Private display fixture", WS_POPUP,
        0, 0, 800, 584, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(parent != nullptr, "create isolated page parent");
    DisplayPage page;
    Check(page.CreatePage(parent, {0, 0, 800, 584}, target), "create independent Display controls");
    const HDC measure = GetDC(parent);
    const std::wstring unbroken(240, L'A');
    std::wstring wrapped = WrapValue(measure, unbroken, 100);
    ReleaseDC(parent, measure);
    Check(std::count(wrapped.begin(), wrapped.end(), L'\n') > 5, "unbroken device values wrap to the information width");
    wrapped.erase(std::remove(wrapped.begin(), wrapped.end(), L'\n'), wrapped.end());
    Check(wrapped == unbroken, "wrapping retains the complete device value");
    page.Refresh();
    const HWND graph = GetDlgItem(page, DisplayPage::TopologyId), list = GetDlgItem(page, DisplayPage::ListId);
    auto tiles = page.Model().Layout(Bounds(graph), 24);
    const RECT secondary = tiles[1].bounds;
    SendMessageW(graph, WM_LBUTTONDOWN, 0, MAKELPARAM((secondary.left + secondary.right) / 2,
        (secondary.top + secondary.bottom) / 2));
    Check(page.SelectedMonitor()->device_path == L"path2", "clicking topology selects matching output");
    SendMessageW(list, WM_KEYDOWN, VK_HOME, 0);
    Check(page.SelectedMonitor()->primary, "list keyboard selection updates selected output");
    SendMessageW(list, WM_LBUTTONDOWN, 0, MAKELPARAM(30, 48 + 62 + 10));
    Check(page.SelectedMonitor()->device_path == L"path2", "list mouse selection updates selected output");
    const int before_paint = query_count;
    SaveClient(page, L"build\\tests\\display-page.bmp");
    Check(query_count == before_paint, "painting never re-enumerates monitors");
    SendMessageW(GetDlgItem(page, DisplayPage::InformationId), WM_KEYDOWN, VK_END, 0);
    SaveClient(page, L"build\\tests\\display-details.bmp");

    fixture.monitors[1].device_name = L"\\\\.\\DISPLAY7";
    SendMessageW(page, WM_COMMAND, MAKEWPARAM(DisplayPage::RefreshId, BN_CLICKED),
        reinterpret_cast<LPARAM>(GetDlgItem(page, DisplayPage::RefreshId)));
    Check(page.SelectedMonitor()->device_path == L"path2", "refresh button retains output identity");
    fixture.monitors.pop_back();
    fixture.virtual_bounds_px = fixture.monitors[0].bounds_px;
    page.Refresh();
    Check(page.SelectedMonitor()->primary, "refresh handles disconnected output");
    query_error = ERROR_ACCESS_DENIED;
    page.Refresh();
    Check(page.SelectedMonitor() == nullptr && page.Model().Error() == ERROR_ACCESS_DENIED
        && page.Model().Snapshot().monitors.empty(), "query failure clears stale UI data");
    SaveClient(page, L"build\\tests\\display-error.bmp");
    query_error = ERROR_SUCCESS;
    fixture = {};
    page.Refresh();
    Check(page.SelectedMonitor() == nullptr && page.Model().Error() == ERROR_SUCCESS, "empty UI state");
    fixture = Dual();
    auto mirror = fixture.monitors[0];
    mirror.target_id = 3;
    mirror.device_path = L"mirror";
    fixture.monitors.push_back(mirror);
    page.Refresh();
    SendMessageW(list, WM_KEYDOWN, VK_DOWN, 0);
    Check(page.SelectedMonitor()->device_path == L"mirror", "clone outputs can be selected separately");
    tiles = page.Model().Layout(Bounds(graph), 24);
    SendMessageW(graph, WM_LBUTTONDOWN, 0, MAKELPARAM((tiles[0].bounds.left + tiles[0].bounds.right) / 2,
        (tiles[0].bounds.top + tiles[0].bounds.bottom) / 2));
    Check(page.SelectedMonitor()->device_path == L"mirror", "clicking clone tile preserves a selection in that group");
    SaveClient(page, L"build\\tests\\display-clones.bmp");
    fixture = Dual();
    fixture.monitors[0].friendly_name = L"非常长的中文显示器名称，用于检查信息换行和列表省略显示 — "
        L"Very long monitor friendly name with Unicode text and additional descriptive content";
    fixture.monitors[0].device_path = L"\\\\?\\DISPLAY#LONG_DEVICE_PATH#" + std::wstring(240, L'A');
    page.Refresh();
    SendMessageW(list, WM_KEYDOWN, VK_HOME, 0);
    SaveClient(page, L"build\\tests\\display-long-name.bmp");
    SendMessageW(GetDlgItem(page, DisplayPage::InformationId), WM_KEYDOWN, VK_END, 0);
    SaveClient(page, L"build\\tests\\display-long-details.bmp");
    use_real = true;
    page.Refresh();
    Check(page.Model().Error() == ERROR_SUCCESS && !page.Model().Snapshot().monitors.empty(), "real hardware enumeration");
    SaveClient(page, L"build\\tests\\display-native.bmp");
    std::printf("Native display page contains %zu output(s).\n", page.Model().Snapshot().monitors.size());

    page.DestroyWindow();
    DestroyWindow(parent);
    MINMAXINFO limits{};
    SendMessageW(frame, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
    RECT minimum{0, 0, 640, 520};
    AdjustWindowRectEx(&minimum, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW);
    Check(limits.ptMinTrackSize.x == minimum.right - minimum.left, "minimum frame width contains 640px client");
    frame.SetWindowPos(nullptr, 0, 0, limits.ptMinTrackSize.x, limits.ptMinTrackSize.y, SWP_NOMOVE | SWP_NOZORDER);
    SaveClient(frame, L"build\\tests\\display-minimum.bmp");
    frame.DestroyWindow();
    Pump();
    Check(frame.Show(icon, target), "closed settings can reopen with Display controls");
    frame.DestroyWindow();
    Pump();
    _Module.RemoveMessageLoop();
    _Module.Term();
    CoUninitialize();
    Check(SetThreadDesktop(original) && CloseDesktop(isolated), "release isolated display desktop");
    std::puts("Display topology, identities, selection, keyboard, refresh, unknown/error, clones and native render tests passed.");
    return 0;
}
