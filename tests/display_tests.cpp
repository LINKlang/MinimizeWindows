// Model and real WTL controls on a never-activated desktop. No global input.
#include "../src/settings_window.h"
#include <imm.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <shellapi.h>
#include <shlobj.h>

WTL::CAppModule _Module;

namespace display_test {

MonitorSnapshot fixture;
DWORD query_error = ERROR_SUCCESS;
int query_count = 0;
bool use_real = false;
std::wstring opened_url;
bool browser_fails = false;
int browser_errors = 0;
bool legacy_dpi_apis = false;
bool control_pressed = false;
std::wstring startup_folder, startup_message;
HRESULT startup_folder_error = S_OK;
int startup_successes = 0, startup_errors = 0;
std::wstring explorer_application, explorer_command;
bool explorer_fails = false;
HANDLE explorer_process = nullptr, explorer_thread = nullptr;

BOOL WINAPI LaunchExplorer(LPCWSTR application, LPWSTR command, LPSECURITY_ATTRIBUTES process_attributes,
    LPSECURITY_ATTRIBUTES thread_attributes, BOOL inherit, DWORD flags, LPVOID environment, LPCWSTR directory,
    LPSTARTUPINFOW startup, LPPROCESS_INFORMATION process)
{
    if (application == nullptr || command == nullptr || process_attributes != nullptr || thread_attributes != nullptr
        || inherit || flags != 0 || environment != nullptr || directory != nullptr
        || startup->cb != sizeof(STARTUPINFOW) || startup->dwFlags != STARTF_USESHOWWINDOW
        || startup->wShowWindow != SW_SHOWNORMAL) { std::abort(); }
    explorer_application = application;
    explorer_command = command;
    if (explorer_fails) { SetLastError(ERROR_FILE_NOT_FOUND); return FALSE; }
    process->hProcess = explorer_process = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    process->hThread = explorer_thread = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (explorer_process == nullptr || explorer_thread == nullptr) { std::abort(); }
    return TRUE;
}

HRESULT WINAPI StartupFolder(REFKNOWNFOLDERID id, DWORD flags, HANDLE token, PWSTR* path)
{
    if (id != FOLDERID_Startup || flags != KF_FLAG_CREATE || token != nullptr || startup_folder.empty()) { std::abort(); }
    *path = nullptr;
    if (FAILED(startup_folder_error)) { return startup_folder_error; }
    const size_t bytes = (startup_folder.size() + 1) * sizeof(wchar_t);
    *path = static_cast<PWSTR>(CoTaskMemAlloc(bytes));
    if (*path == nullptr) { return E_OUTOFMEMORY; }
    std::memcpy(*path, startup_folder.c_str(), bytes);
    return S_OK;
}

int WINAPI StartupMessage(HWND, LPCWSTR text, LPCWSTR title, UINT type)
{
    if (std::wcscmp(title, L"MinimizeWindows") != 0) { std::abort(); }
    startup_message = text;
    if ((type & MB_ICONMASK) == MB_ICONINFORMATION) { ++startup_successes; }
    else if ((type & MB_ICONMASK) == MB_ICONERROR) { ++startup_errors; }
    else { std::abort(); }
    return IDOK;
}

HINSTANCE WINAPI OpenUrl(HWND, LPCWSTR operation, LPCWSTR url, LPCWSTR, LPCWSTR, INT)
{
    opened_url = url;
    if (std::wcscmp(operation, L"open") != 0) { std::abort(); }
    return reinterpret_cast<HINSTANCE>(static_cast<INT_PTR>(browser_fails ? 2 : 33));
}

int WINAPI LinkError(HWND, LPCWSTR text, LPCWSTR title, UINT)
{
    if (std::wcscmp(title, L"MinimizeWindows") != 0 || std::wcsstr(text, L"Unable to open") == nullptr) { std::abort(); }
    ++browser_errors;
    return IDOK;
}

FARPROC WINAPI UiProcAddress(HMODULE module, LPCSTR name)
{
    if (legacy_dpi_apis && (std::strcmp(name, "GetDpiForWindow") == 0
        || std::strcmp(name, "AdjustWindowRectExForDpi") == 0)) { return nullptr; }
    return GetProcAddress(module, name);
}

HMODULE WINAPI UiLoadLibrary(LPCWSTR name, HANDLE file, DWORD flags)
{
    return legacy_dpi_apis ? nullptr : LoadLibraryExW(name, file, flags);
}
SHORT WINAPI UiKeyState(int key) { return key == VK_CONTROL && control_pressed ? static_cast<SHORT>(0x8000) : 0; }

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
#define GetProcAddress display_test::UiProcAddress
#define LoadLibraryExW display_test::UiLoadLibrary
#include "../src/display_page.cpp"
#undef LoadLibraryExW
#undef GetProcAddress
#undef MonitorEnumerator

#define ShellExecuteW display_test::OpenUrl
#define MessageBoxW display_test::LinkError
#define GetKeyState display_test::UiKeyState
#include "../src/licenses_dialog.cpp"
#include "../src/about_page.cpp"
#undef GetKeyState
#undef MessageBoxW
#undef ShellExecuteW

#define ShellExecuteW display_test::OpenUrl
#define MessageBoxW display_test::StartupMessage
#define SHGetKnownFolderPath display_test::StartupFolder
#define CreateProcessW display_test::LaunchExplorer
#include "../src/settings_page.cpp"
#undef CreateProcessW
#undef SHGetKnownFolderPath
#undef MessageBoxW
#undef ShellExecuteW

namespace display_test {

std::wstring WindowText(HWND window)
{
    std::wstring text(GetWindowTextLengthW(window) + 1, L'\0');
    const int length = GetWindowTextW(window, &text[0], static_cast<int>(text.size()));
    text.resize(length);
    return text;
}

std::wstring CheckLicenseResource(UINT id, const wchar_t* path)
{
    const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "open original license for comparison");
    const DWORD size = GetFileSize(file, nullptr);
    std::string original(size, '\0');
    DWORD read = 0;
    Check(ReadFile(file, &original[0], size, &read, nullptr) && read == size, "read original license");
    CloseHandle(file);
    const HRSRC resource = FindResourceW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(id), RT_RCDATA);
    Check(resource != nullptr && SizeofResource(GetModuleHandleW(nullptr), resource) == size,
        "embedded license has original byte length");
    const void* embedded = LockResource(LoadResource(GetModuleHandleW(nullptr), resource));
    Check(embedded != nullptr && std::memcmp(embedded, original.data(), size) == 0,
        "embedded license preserves original bytes");
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, original.data(), size, nullptr, 0);
    std::wstring decoded(length, L'\0');
    Check(length != 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, original.data(), size, &decoded[0], length),
        "decode expected license");
    std::wstring expected;
    for (size_t i = 0; i < decoded.size(); ++i) {
        if (decoded[i] == L'\n' && (i == 0 || decoded[i - 1] != L'\r')) { expected += L'\r'; }
        expected += decoded[i];
    }
    return expected;
}

void ClickButton(HWND parent, UINT id)
{
    SendMessageW(parent, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(parent, id)));
}

void CheckStartupShortcut(const std::wstring& path)
{
    ATL::CComPtr<IShellLinkW> link;
    Check(SUCCEEDED(link.CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER)), "create shortcut reader");
    ATL::CComPtr<IPersistFile> file;
    Check(SUCCEEDED(link.QueryInterface(&file)) && SUCCEEDED(file->Load(path.c_str(), STGM_READ)), "read actual startup shortcut");
    wchar_t target[MAX_PATH]{}, working_directory[MAX_PATH]{}, arguments[MAX_PATH]{}, executable[MAX_PATH]{};
    Check(GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable)) != 0
        && SUCCEEDED(link->GetPath(target, ARRAYSIZE(target), nullptr, SLGP_RAWPATH))
        && _wcsicmp(target, executable) == 0, "shortcut targets the current executable");
    const std::wstring expected_directory = std::wstring(executable).substr(0, std::wstring(executable).find_last_of(L"\\/"));
    Check(SUCCEEDED(link->GetWorkingDirectory(working_directory, ARRAYSIZE(working_directory)))
        && _wcsicmp(working_directory, expected_directory.c_str()) == 0, "shortcut working directory is the executable directory");
    Check(SUCCEEDED(link->GetArguments(arguments, ARRAYSIZE(arguments))) && arguments[0] == L'\0', "startup shortcut has no arguments");
}

void StartupTests(SettingsWindow& frame, HICON icon, const std::vector<std::wstring>& devices, SaveMonitorSelection save)
{
    const HWND display = GetDlgItem(frame, SettingsWindow::DisplayId);
    const HWND settings = GetDlgItem(frame, SettingsWindow::SettingsId);
    const HWND about = GetDlgItem(frame, SettingsWindow::AboutId);
    Check(settings != nullptr && !IsWindowVisible(settings) && IsWindowVisible(display), "Settings starts hidden");
    ClickButton(frame, SettingsWindow::SettingsTabId);
    Check(IsWindowVisible(settings) && !IsWindowVisible(display) && !IsWindowVisible(about)
        && GetFocus() == GetDlgItem(settings, SettingsPage::OpenStartupId), "Settings tab selects only its page and moves focus");
    Check(frame.Show(icon, devices, save) && IsWindowVisible(settings), "re-activation retains Settings tab");
    Check(WindowText(GetDlgItem(settings, SettingsPage::OpenStartupId)) == L"Open Startup Folder"
        && WindowText(GetDlgItem(settings, SettingsPage::CreateStartupId)) == L"Create Startup Shortcut", "Startup button captions");
    MSG tab{};
    tab.hwnd = GetDlgItem(settings, SettingsPage::OpenStartupId);
    tab.message = WM_KEYDOWN;
    tab.wParam = VK_TAB;
    SetFocus(tab.hwnd);
    Check(frame.PreTranslateMessage(&tab) && GetFocus() == GetDlgItem(settings, SettingsPage::CreateStartupId),
        "keyboard navigation reaches Create Startup Shortcut");
    SaveClient(frame, L"build\\tests\\settings-minimum.bmp");
    opened_url.clear();
    ClickButton(settings, SettingsPage::OpenStartupId);
    wchar_t system_windows[MAX_PATH]{};
    Check(GetWindowsDirectoryW(system_windows, ARRAYSIZE(system_windows)) != 0, "locate system Explorer");
    const std::wstring explorer = std::wstring(system_windows) + L"\\explorer.exe";
    Check(explorer_application == explorer && explorer_command == L"\"" + explorer + L"\" shell:startup"
        && opened_url.empty() && startup_errors == 0, "open Startup launches system Explorer without in-process Shell execution");
    DWORD handle_flags = 0;
    Check(!GetHandleInformation(explorer_process, &handle_flags) && GetLastError() == ERROR_INVALID_HANDLE
        && !GetHandleInformation(explorer_thread, &handle_flags) && GetLastError() == ERROR_INVALID_HANDLE,
        "Explorer process and thread handles are released without waiting");
    explorer_fails = true;
    ClickButton(settings, SettingsPage::OpenStartupId);
    Check(startup_errors == 1 && startup_message == L"Unable to open the Startup folder.", "opening failure is reported");
    explorer_fails = false;

    wchar_t root[MAX_PATH]{};
    Check(GetFullPathNameW(L"build\\tests", ARRAYSIZE(root), root, nullptr) != 0, "resolve isolated Startup test directory");
    startup_folder = std::wstring(root) + L"\\Startup 中文 " + std::to_wstring(GetCurrentProcessId());
    Check(GetFileAttributesW(startup_folder.c_str()) == INVALID_FILE_ATTRIBUTES, "opening Settings has not created a startup shortcut");
    Check(CreateDirectoryW(startup_folder.c_str(), nullptr), "create isolated Unicode Startup directory");
    const std::wstring shortcut = startup_folder + L"\\MinimizeWindows.lnk";
    ClickButton(settings, SettingsPage::CreateStartupId);
    Check(startup_successes == 1 && startup_message == L"Startup shortcut created or updated.", "creation reports success");
    CheckStartupShortcut(shortcut);

    // Simulate a shortcut left by a different executable location and launch arguments.
    {
        ATL::CComPtr<IShellLinkW> link;
        ATL::CComPtr<IPersistFile> file;
        wchar_t windows[MAX_PATH]{};
        Check(GetWindowsDirectoryW(windows, ARRAYSIZE(windows)) != 0
            && SUCCEEDED(link.CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER))
            && SUCCEEDED(link.QueryInterface(&file)) && SUCCEEDED(file->Load(shortcut.c_str(), STGM_READ))
            && SUCCEEDED(link->SetPath((std::wstring(windows) + L"\\explorer.exe").c_str()))
            && SUCCEEDED(link->SetWorkingDirectory(windows)) && SUCCEEDED(link->SetArguments(L"--old"))
            && SUCCEEDED(file->Save(shortcut.c_str(), TRUE)), "seed an outdated startup shortcut");
    }
    ClickButton(settings, SettingsPage::CreateStartupId);
    Check(startup_successes == 2, "repeated creation reports success");
    CheckStartupShortcut(shortcut);
    WIN32_FIND_DATAW entry{};
    const HANDLE search = FindFirstFileW((startup_folder + L"\\*.lnk").c_str(), &entry);
    Check(search != INVALID_HANDLE_VALUE && std::wcscmp(entry.cFileName, L"MinimizeWindows.lnk") == 0
        && !FindNextFileW(search, &entry) && GetLastError() == ERROR_NO_MORE_FILES, "repeated creation keeps only one shortcut");
    FindClose(search);

    startup_folder_error = E_ACCESSDENIED;
    ClickButton(settings, SettingsPage::CreateStartupId);
    Check(startup_errors == 2 && startup_successes == 2 && startup_message == L"Unable to create the startup shortcut.",
        "Startup folder lookup failure is reported");
    startup_folder_error = S_OK;
    CheckStartupShortcut(shortcut);
    Check(DeleteFileW(shortcut.c_str()) && CreateDirectoryW(shortcut.c_str(), nullptr), "block shortcut save with a directory");
    ClickButton(settings, SettingsPage::CreateStartupId);
    Check(startup_errors == 3 && startup_successes == 2 && startup_message == L"Unable to create the startup shortcut.",
        "actual shortcut save failure is reported");
    Check(RemoveDirectoryW(shortcut.c_str()) && RemoveDirectoryW(startup_folder.c_str()), "remove isolated Startup fixtures");
    startup_folder.clear();
    ClickButton(frame, SettingsWindow::AboutTabId);
    Check(IsWindowVisible(about) && !IsWindowVisible(settings) && !IsWindowVisible(display), "About hides Settings");
    ClickButton(frame, SettingsWindow::DisplayTabId);
    Check(IsWindowVisible(display) && !IsWindowVisible(settings) && !IsWindowVisible(about)
        && GetFocus() == GetDlgItem(display, DisplayPage::TopologyId), "Display restores its page and focus");
}

HWND AboutTests(SettingsWindow& frame, HICON icon, const std::vector<std::wstring>& devices, SaveMonitorSelection save)
{
    const std::wstring app_license = CheckLicenseResource(IDR_APP_LICENSE, L"LICENSE");
    const std::wstring wtl_license = CheckLicenseResource(IDR_WTL_LICENSE, L"third_party\\wtl\\MS-PL.txt");
    const std::wstring json_license = CheckLicenseResource(IDR_JSON_LICENSE, L"third_party\\nlohmann\\LICENSE.MIT");
    const HWND display = GetDlgItem(frame, SettingsWindow::DisplayId);
    HWND about = GetDlgItem(frame, SettingsWindow::AboutId);
    Check(about != nullptr && !IsWindowVisible(about) && IsWindowVisible(display), "Display is the initial visible page");
    ClickButton(frame, SettingsWindow::AboutTabId);
    Check(IsWindowVisible(about) && !IsWindowVisible(display) && GetFocus() == GetDlgItem(about, AboutPage::GitHubId),
        "About tab hides Display and moves keyboard focus");
    Check(frame.Show(icon, devices, save) && IsWindowVisible(about), "re-activation retains About tab");
    SaveClient(frame, L"build\\tests\\about-minimum.bmp");
    ClickButton(about, AboutPage::GitHubId);
    Check(opened_url == L"https://github.com/LINKlang/MinimizeWindows", "GitHub button opens our repository");
    browser_fails = true;
    ClickButton(about, AboutPage::OriginalProjectId);
    Check(opened_url == L"https://github.com/deadem/minimize-windows" && browser_errors == 1,
        "original project button reports browser failure");
    browser_fails = false;

    // Production loading remains independent of the executable's current directory.
    wchar_t original_directory[MAX_PATH];
    Check(GetCurrentDirectoryW(ARRAYSIZE(original_directory), original_directory) != 0 && SetCurrentDirectoryW(L"build\\tests"),
        "change working directory away from source license files");
    ClickButton(about, AboutPage::LicenseId);
    Check(SetCurrentDirectoryW(original_directory), "restore working directory");
    HWND dialog = FindWindowW(L"#32770", L"MinimizeWindows License");
    Check(dialog != nullptr && GetWindow(dialog, GW_OWNER) == frame && IsWindowEnabled(frame),
        "app license is a non-modal dialog owned by Settings");
    const HWND license_text = GetDlgItem(dialog, LicensesDialog::TextId);
    Check(WindowText(license_text) == app_license && (GetWindowLongW(license_text, GWL_STYLE) & ES_READONLY) != 0,
        "app license text is complete and read-only");
    MSG select_all{};
    select_all.hwnd = license_text;
    select_all.message = WM_KEYDOWN;
    select_all.wParam = 'A';
    control_pressed = true;
    Check(frame.PreTranslateMessage(&select_all), "Ctrl+A selects license text for copying");
    control_pressed = false;
    DWORD selection_start = 0, selection_end = 0;
    SendMessageW(license_text, EM_GETSEL, reinterpret_cast<WPARAM>(&selection_start), reinterpret_cast<LPARAM>(&selection_end));
    Check(selection_start == 0 && selection_end == app_license.size(), "all license text can be selected");
    ClickButton(about, AboutPage::LicenseId);
    Check(FindWindowW(L"#32770", L"MinimizeWindows License") == dialog, "repeated license opening reuses dialog");
    ClickButton(dialog, LicensesDialog::HomepageId);
    Check(opened_url == L"https://github.com/LINKlang/MinimizeWindows", "app license homepage opens our repository");
    ClickButton(about, AboutPage::ThirdPartyId);
    Check(FindWindowW(L"#32770", L"Third-party software licenses") == dialog
        && WindowText(license_text) == wtl_license, "third-party entry reuses viewer and defaults to WTL");
    Check(WindowText(GetDlgItem(dialog, LicensesDialog::MetadataId)).find(L"Microsoft Corporation, WTL Team") != std::wstring::npos,
        "WTL original attribution is displayed");
    ClickButton(dialog, LicensesDialog::HomepageId);
    Check(opened_url == L"https://sourceforge.net/projects/wtl/", "WTL homepage opens upstream");
    SaveClient(dialog, L"build\\tests\\licenses-wtl.bmp");
    SendMessageW(GetDlgItem(dialog, LicensesDialog::ComponentId), CB_SETCURSEL, 1, 0);
    SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(LicensesDialog::ComponentId, CBN_SELCHANGE),
        reinterpret_cast<LPARAM>(GetDlgItem(dialog, LicensesDialog::ComponentId)));
    Check(WindowText(license_text) == json_license, "json selection loads its exact complete MIT text");
    const std::wstring metadata = WindowText(GetDlgItem(dialog, LicensesDialog::MetadataId));
    Check(metadata.find(L"3.12.0") != std::wstring::npos && metadata.find(L"Niels Lohmann") != std::wstring::npos
        && metadata.find(L"Evan Nemerson") != std::wstring::npos && metadata.find(L"Abseil Authors") != std::wstring::npos
        && metadata.find(L"Hoehrmann") != std::wstring::npos && metadata.find(L"Florian Loitsch") != std::wstring::npos,
        "json version and bundled attribution notices are retained");
    ClickButton(dialog, LicensesDialog::HomepageId);
    Check(opened_url == L"https://github.com/nlohmann/json", "json homepage opens upstream");
    SaveClient(dialog, L"build\\tests\\licenses-json.bmp");
    ClickButton(frame, SettingsWindow::DisplayTabId);
    Check(IsWindow(dialog) && IsWindowVisible(display) && IsWindowEnabled(GetDlgItem(display, DisplayPage::SaveId)),
        "switching tabs keeps license window and Display draft");
    MSG tab{};
    tab.hwnd = GetDlgItem(dialog, LicensesDialog::ComponentId);
    tab.message = WM_KEYDOWN;
    tab.wParam = VK_TAB;
    SetFocus(tab.hwnd);
    Check(frame.PreTranslateMessage(&tab) && GetFocus() == GetDlgItem(dialog, LicensesDialog::MetadataId),
        "message filter routes keyboard navigation to owned license dialog");
    MSG escape{};
    escape.hwnd = license_text;
    escape.message = WM_KEYDOWN;
    escape.wParam = VK_ESCAPE;
    Check(frame.PreTranslateMessage(&escape) && !IsWindow(dialog), "Escape destroys modeless dialog");
    Pump();

    ClickButton(frame, SettingsWindow::AboutTabId);
    ClickButton(about, AboutPage::ThirdPartyId);
    dialog = FindWindowW(L"#32770", L"Third-party software licenses");
    Check(dialog != nullptr && WindowText(GetDlgItem(dialog, LicensesDialog::TextId)) == wtl_license,
        "closed license viewer can reopen");
    SendMessageW(dialog, WM_SYSCOMMAND, SC_CLOSE, 0);
    Check(!IsWindow(dialog), "Alt+F4 closes license viewer without ending application");
    Pump();
    ClickButton(about, AboutPage::ThirdPartyId);
    dialog = FindWindowW(L"#32770", L"Third-party software licenses");
    Check(dialog != nullptr, "reopen license viewer for DPI tests");
    return dialog;
}

RECT ChildBounds(HWND window)
{
    RECT bounds{};
    GetWindowRect(window, &bounds);
    MapWindowPoints(nullptr, GetParent(window), reinterpret_cast<POINT*>(&bounds), 2);
    return bounds;
}

void CheckFont(HWND window, UINT dpi)
{
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
    LOGFONTW description{};
    Check(font != nullptr && GetObjectW(font, sizeof(description), &description) == sizeof(description)
        && description.lfHeight == -MulDiv(14, dpi, 96), "control font matches current DPI");
}

void CheckDpiLayout(SettingsWindow& frame, HWND dialog, UINT dpi, UINT dialog_dpi)
{
    const HWND display = GetDlgItem(frame, SettingsWindow::DisplayId);
    const HWND about = GetDlgItem(frame, SettingsWindow::AboutId);
    const HWND settings = GetDlgItem(frame, SettingsWindow::SettingsId);
    const RECT tab = ChildBounds(GetDlgItem(frame, SettingsWindow::DisplayTabId));
    Check(tab.left == MulDiv(20, dpi, 96) && tab.top == MulDiv(10, dpi, 96)
        && tab.right - tab.left == MulDiv(112, dpi, 96)
        && tab.bottom - tab.top == MulDiv(38, dpi, 96), "tab geometry matches current DPI");
    const RECT settings_tab = ChildBounds(GetDlgItem(frame, SettingsWindow::SettingsTabId));
    const RECT about_tab = ChildBounds(GetDlgItem(frame, SettingsWindow::AboutTabId));
    Check(settings_tab.left == MulDiv(140, dpi, 96) && about_tab.left == MulDiv(260, dpi, 96),
        "navigation order is Display, Settings, About at current DPI");
    const RECT open_startup = ChildBounds(GetDlgItem(settings, SettingsPage::OpenStartupId));
    const RECT create_startup = ChildBounds(GetDlgItem(settings, SettingsPage::CreateStartupId));
    Check(open_startup.left == MulDiv(24, dpi, 96) && open_startup.top == MulDiv(124, dpi, 96)
        && open_startup.right - open_startup.left == MulDiv(240, dpi, 96)
        && open_startup.bottom - open_startup.top == MulDiv(32, dpi, 96)
        && create_startup.top == MulDiv(168, dpi, 96) && create_startup.right <= Bounds(settings).right,
        "Startup buttons scale vertically without clipping even while hidden");
    CheckFont(GetDlgItem(settings, SettingsPage::OpenStartupId), dpi);
    CheckFont(GetDlgItem(settings, SettingsPage::CreateStartupId), dpi);
    const RECT configure = ChildBounds(GetDlgItem(display, DisplayPage::ConfigureId));
    Check(configure.left == Bounds(display).right - MulDiv(20, dpi, 96) - MulDiv(268, dpi, 96)
        && configure.top == MulDiv(16, dpi, 96)
        && configure.right - configure.left == MulDiv(100, dpi, 96), "Display buttons match current DPI");
    const RECT github = ChildBounds(GetDlgItem(about, AboutPage::GitHubId));
    Check(github.left == MulDiv(24, dpi, 96) && github.top == MulDiv(180, dpi, 96)
        && github.right - github.left == MulDiv(100, dpi, 96), "About layout matches current DPI even when hidden");
    CheckFont(GetDlgItem(about, AboutPage::GitHubId), dpi);
    CheckFont(GetDlgItem(dialog, LicensesDialog::TextId), dialog_dpi);
    const RECT metadata = ChildBounds(GetDlgItem(dialog, LicensesDialog::MetadataId));
    Check(metadata.left == MulDiv(20, dialog_dpi, 96)
        && metadata.top == MulDiv(80, dialog_dpi, 96), "license controls use one DPI scale");
    MINMAXINFO minimum{};
    SendMessageW(frame, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&minimum));
    RECT expected{0, 0, MulDiv(640, dpi, 96), MulDiv(520, dpi, 96)};
    DisplayAdjustWindowRectForDpi(&expected, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW, dpi);
    Check(minimum.ptMinTrackSize.x == expected.right - expected.left
        && minimum.ptMinTrackSize.y == expected.bottom - expected.top, "frame minimum uses current DPI");
    SendMessageW(dialog, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&minimum));
    expected = {0, 0, MulDiv(600, dialog_dpi, 96), MulDiv(440, dialog_dpi, 96)};
    DisplayAdjustWindowRectForDpi(&expected, GetWindowLongW(dialog, GWL_STYLE), FALSE,
        GetWindowLongW(dialog, GWL_EXSTYLE), dialog_dpi);
    Check(minimum.ptMinTrackSize.x == expected.right - expected.left
        && minimum.ptMinTrackSize.y == expected.bottom - expected.top, "license minimum uses current DPI");
}

void DpiTransitions(SettingsWindow& frame, HWND dialog)
{
    ClickButton(frame, SettingsWindow::DisplayTabId);
    const HWND display = GetDlgItem(frame, SettingsWindow::DisplayId);
    const std::wstring content = WindowText(GetDlgItem(dialog, LicensesDialog::TextId));
    const RECT suggested{0, 0, 1600, 1280};
    // Keep the suggested rectangle unchanged between DPI messages. DPI updates
    // must work even when SetWindowPos does not generate a new WM_SIZE.
    for (const UINT dpi : {96u, 144u, 192u, 96u}) {
        SendMessageW(frame, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
        SendMessageW(dialog, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
        RECT actual{};
        GetWindowRect(frame, &actual);
        Check(EqualRect(&actual, &suggested), "settings applies the suggested DPI rectangle");
        GetWindowRect(dialog, &actual);
        Check(EqualRect(&actual, &suggested), "license viewer applies the suggested DPI rectangle");
        CheckDpiLayout(frame, dialog, dpi, dpi);
        Check(WindowText(GetDlgItem(display, DisplayPage::ConfigureId)) == L"Cancel"
            && IsWindowEnabled(GetDlgItem(display, DisplayPage::SaveId)), "DPI transitions retain the editing session");
        Check(!IsWindowVisible(GetDlgItem(frame, SettingsWindow::AboutId))
            && !IsWindowVisible(GetDlgItem(frame, SettingsWindow::SettingsId))
            && WindowText(GetDlgItem(dialog, LicensesDialog::TextId)) == content, "DPI transitions retain hidden tab and license content");
        if (dpi == 144) {
            SaveClient(frame, L"build\\tests\\display-150-percent.bmp");
            SaveClient(GetDlgItem(frame, SettingsWindow::AboutId), L"build\\tests\\about-150-percent.bmp");
            SaveClient(dialog, L"build\\tests\\licenses-150-percent.bmp");
        }
        if (dpi == 192) { SaveClient(frame, L"build\\tests\\display-200-percent.bmp"); }
        ClickButton(frame, SettingsWindow::SettingsTabId);
        Check(IsWindowVisible(GetDlgItem(frame, SettingsWindow::SettingsId)) && !IsWindowVisible(display),
            "Settings remains selectable after DPI transitions");
        const std::wstring screenshot = L"build\\tests\\settings-" + std::to_wstring(MulDiv(dpi, 100, 96)) + L"-percent.bmp";
        SaveClient(frame, screenshot.c_str());
        ClickButton(frame, SettingsWindow::DisplayTabId);
    }
    legacy_dpi_apis = true;
    Check(DisplayWindowDpi(frame) == 96, "missing DPI APIs and Shcore use the GDI fallback");
    RECT adjusted{0, 0, 800, 640}, expected = adjusted;
    Check(AdjustWindowRectEx(&expected, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW)
        && DisplayAdjustWindowRectForDpi(&adjusted, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW, 96)
        && EqualRect(&adjusted, &expected), "missing DPI rectangle API uses the legacy calculation");
    legacy_dpi_apis = false;
}

void NativeDpiTests(HICON icon, const std::vector<std::wstring>& devices, SaveMonitorSelection save)
{
    using SetContext = HANDLE (WINAPI*)(HANDLE);
    const auto set_context = reinterpret_cast<SetContext>(GetProcAddress(GetModuleHandleW(L"user32.dll"),
        "SetThreadDpiAwarenessContext"));
    const HANDLE previous = set_context != nullptr
        ? set_context(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))) : nullptr;
    if (previous == nullptr) { std::puts("Native PMv2 tests skipped: PMv2 is unavailable."); return; }
    SettingsWindow frame;
    Check(frame.Show(icon, devices, save), "create native PMv2 settings");
    const UINT initial_dpi = DisplayWindowDpi(frame);
    Check(Bounds(frame).right == MulDiv(800, initial_dpi, 96)
        && Bounds(frame).bottom == MulDiv(640, initial_dpi, 96), "native PMv2 initial client dimensions");
    ClickButton(frame, SettingsWindow::AboutTabId);
    ClickButton(GetDlgItem(frame, SettingsWindow::AboutId), AboutPage::ThirdPartyId);
    const HWND dialog = FindWindowW(L"#32770", L"Third-party software licenses");
    Check(dialog != nullptr, "create native PMv2 license viewer");
    const UINT license_dpi = DisplayWindowDpi(dialog);
    Check(Bounds(dialog).right == MulDiv(720, license_dpi, 96)
        && Bounds(dialog).bottom == MulDiv(580, license_dpi, 96), "native license initial client dimensions");
    using GetBehavior = int (WINAPI*)(HWND);
    const auto get_behavior = reinterpret_cast<GetBehavior>(GetProcAddress(GetModuleHandleW(L"user32.dll"),
        "GetDialogDpiChangeBehavior"));
    Check(get_behavior != nullptr && (get_behavior(dialog) & 1) != 0, "native license automatic DPI layout is disabled");
    ClickButton(frame, SettingsWindow::DisplayTabId);
    CheckDpiLayout(frame, dialog, initial_dpi, license_dpi);
    SaveClient(frame, L"build\\tests\\display-native-dpi.bmp");
    SaveClient(GetDlgItem(frame, SettingsWindow::AboutId), L"build\\tests\\about-native-dpi.bmp");
    SaveClient(dialog, L"build\\tests\\licenses-native-dpi.bmp");
    MonitorSnapshot snapshot;
    Check(MonitorEnumerator().Enumerate(snapshot), "enumerate native DPI monitors");
    std::vector<UINT> observed_dpis;
    // Visit all physical displays and return in reverse order. No desktop is activated.
    for (size_t step = 0; step < snapshot.monitors.size() * 2; ++step) {
        const size_t index = step < snapshot.monitors.size() ? step : snapshot.monitors.size() * 2 - step - 1;
        const auto& monitor = snapshot.monitors[index];
        MONITORINFO info{sizeof(info)};
        Check(GetMonitorInfoW(monitor.handle, &info), "read native monitor work area");
        RECT outer{};
        GetWindowRect(frame, &outer);
        const int x = (info.rcWork.left + info.rcWork.right - (outer.right - outer.left)) / 2;
        const int y = (info.rcWork.top + info.rcWork.bottom - (outer.bottom - outer.top)) / 2;
        frame.SetWindowPos(nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        Pump();
        Check(MonitorFromWindow(frame, MONITOR_DEFAULTTONEAREST) == monitor.handle
            && MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST) == monitor.handle, "native DPI windows reach the target display");
        const UINT dpi = DisplayWindowDpi(frame), current_license_dpi = DisplayWindowDpi(dialog);
        CheckDpiLayout(frame, dialog, dpi, current_license_dpi);
        if (std::find(observed_dpis.begin(), observed_dpis.end(), dpi) == observed_dpis.end()) {
            observed_dpis.push_back(dpi);
            std::printf("Native PMv2 UI checked at %u DPI (%u%%).\n", dpi, MulDiv(dpi, 100, 96));
        }
    }
    if (std::find(observed_dpis.begin(), observed_dpis.end(), 144u) == observed_dpis.end()) {
        std::puts("Real 150% UI check unavailable: no tested display uses 144 DPI.");
    }
    if (observed_dpis.size() < 2) { std::puts("Mixed-DPI check unavailable: tested displays have the same DPI."); }
    frame.DestroyWindow();
    Pump();
    set_context(previous);
}

} // namespace display_test

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
    std::vector<std::wstring> saved_devices{target.device_name};
    bool save_fails = false;
    int saves = 0;
    const SaveMonitorSelection save = [&](const std::vector<std::wstring>& devices, std::wstring& error) {
        ++saves;
        if (save_fails) { error = L"Configuration file is locked."; return false; }
        saved_devices = devices;
        return true;
    };
    const HICON icon = LoadIconW(nullptr, IDI_APPLICATION);
    SettingsWindow frame;
    Check(frame.Show(icon, saved_devices, save), "create settings with Display tab");
    Check(Bounds(frame).right == 800 && Bounds(frame).bottom == 640, "initial client dimensions");
    const HWND integrated = GetDlgItem(frame, SettingsWindow::DisplayId);
    Check(integrated != nullptr && GetDlgItem(frame, SettingsWindow::DisplayTabId) != nullptr,
        "settings contains Display tab and page");
    const int opened_queries = query_count;
    Check(frame.Show(icon, saved_devices, save) && query_count == opened_queries + 1, "re-activation refreshes snapshot");
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
    Check(page.CreatePage(parent, {0, 0, 800, 584}, saved_devices, save), "create independent Display controls");
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

    const auto button = [&](UINT id) {
        SendMessageW(page, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), reinterpret_cast<LPARAM>(GetDlgItem(page, id)));
    };
    const auto click_secondary = [&] {
        SendMessageW(graph, WM_LBUTTONDOWN, 0, MAKELPARAM((secondary.left + secondary.right) / 2,
            (secondary.top + secondary.bottom) / 2));
    };
    Check(!page.Editing() && !IsWindowEnabled(GetDlgItem(page, DisplayPage::SaveId))
        && page.IsTargetDevice(target.device_name) && !page.IsTargetDevice(L"\\\\.\\DISPLAY2"),
        "view mode inspection does not change configured targets");
    button(DisplayPage::ConfigureId);
    Check(page.Editing() && IsWindowEnabled(GetDlgItem(page, DisplayPage::SaveId)), "Configure enables an editable draft and Save");
    click_secondary();
    Check(page.IsTargetDevice(L"\\\\.\\display2"), "topology click adds a case-insensitive draft target");
    click_secondary();
    Check(!page.IsTargetDevice(L"\\\\.\\DISPLAY2"), "repeated topology click removes the target");
    SendMessageW(list, WM_LBUTTONDOWN, 0, MAKELPARAM(30, 48 + 62 + 10));
    SendMessageW(list, WM_KEYDOWN, VK_HOME, 0);
    Check(page.IsTargetDevice(L"\\\\.\\DISPLAY2") && page.IsTargetDevice(target.device_name),
        "list click toggles and arrow navigation only changes inspection");
    SendMessageW(list, WM_KEYDOWN, VK_SPACE, 0);
    Check(!page.IsTargetDevice(target.device_name), "Space toggles the inspected target in edit mode");
    SendMessageW(list, WM_KEYDOWN, VK_SPACE, 0);
    page.Refresh();
    Check(page.Editing() && page.IsTargetDevice(L"\\\\.\\DISPLAY2") && saved_devices.size() == 1,
        "refresh retains a draft without applying it");
    SaveClient(page, L"build\\tests\\display-configure.bmp");
    save_fails = true;
    button(DisplayPage::SaveId);
    Check(page.Editing() && page.IsTargetDevice(L"\\\\.\\DISPLAY2") && saved_devices.size() == 1,
        "save failure retains the draft and prior committed selection");
    SaveClient(page, L"build\\tests\\display-save-error.bmp");
    save_fails = false;
    button(DisplayPage::SaveId);
    Check(!page.Editing() && saved_devices.size() == 2 && !IsWindowEnabled(GetDlgItem(page, DisplayPage::SaveId)),
        "successful Save commits all targets and leaves edit mode");
    click_secondary();
    Check(page.IsTargetDevice(L"\\\\.\\DISPLAY2"), "view mode click leaves committed targets unchanged");
    button(DisplayPage::ConfigureId);
    click_secondary();
    button(DisplayPage::ConfigureId);
    Check(!page.Editing() && page.IsTargetDevice(L"\\\\.\\DISPLAY2"), "Cancel discards draft changes");
    button(DisplayPage::ConfigureId);
    SendMessageW(list, WM_KEYDOWN, VK_HOME, 0);
    SendMessageW(list, WM_KEYDOWN, VK_SPACE, 0);
    SendMessageW(list, WM_KEYDOWN, VK_END, 0);
    SendMessageW(list, WM_KEYDOWN, VK_SPACE, 0);
    button(DisplayPage::SaveId);
    Check(saved_devices.empty() && !page.IsTargetDevice(target.device_name), "empty selection can be saved without a primary fallback");

    page.SetConfiguration({target.device_name, L"\\\\.\\DISPLAY99"});
    SendMessageW(list, WM_LBUTTONDOWN, 0, MAKELPARAM(30, 48 + 2 * 62 + 10));
    Check(page.SelectedMonitor() == nullptr, "unavailable configured target has an independent information entry");
    button(DisplayPage::ConfigureId);
    SendMessageW(list, WM_KEYDOWN, VK_SPACE, 0);
    page.Refresh();
    Check(!page.IsTargetDevice(L"\\\\.\\DISPLAY99"), "refresh preserves the removal of an unavailable target");
    button(DisplayPage::SaveId);
    Check(saved_devices == std::vector<std::wstring>{target.device_name} && page.SelectedMonitor() != nullptr,
        "unavailable targets can be removed and the information selection recovers");
    page.SetConfiguration({target.device_name, L"\\\\.\\DISPLAY2"});
    button(DisplayPage::ConfigureId);
    SendMessageW(list, WM_KEYDOWN, VK_HOME, 0);
    SendMessageW(list, WM_KEYDOWN, VK_SPACE, 0);
    fixture.monitors.pop_back();
    fixture.virtual_bounds_px = fixture.monitors[0].bounds_px;
    page.Refresh();
    SendMessageW(list, WM_KEYDOWN, VK_END, 0);
    Check(page.SelectedMonitor() == nullptr && page.IsTargetDevice(L"\\\\.\\DISPLAY2")
        && !page.IsTargetDevice(target.device_name), "disconnect preserves offline membership and unsaved edits");
    fixture = Dual();
    page.Refresh();
    Check(page.SelectedMonitor()->device_path == L"path2" && !page.IsTargetDevice(target.device_name),
        "reconnection recovers the inspected output without replacing the draft");
    button(DisplayPage::ConfigureId);
    page.SetConfiguration({target.device_name});
    SendMessageW(list, WM_KEYDOWN, VK_END, 0);

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
    button(DisplayPage::ConfigureId);
    SendMessageW(graph, WM_LBUTTONDOWN, 0, MAKELPARAM((tiles[0].bounds.left + tiles[0].bounds.right) / 2,
        (tiles[0].bounds.top + tiles[0].bounds.bottom) / 2));
    Check(!page.IsTargetDevice(fixture.monitors[0].device_name), "clone tile toggles its shared GDI target once");
    SendMessageW(list, WM_KEYDOWN, VK_SPACE, 0);
    Check(page.IsTargetDevice(fixture.monitors[2].device_name), "clone outputs share one participation state");
    button(DisplayPage::ConfigureId);
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
    const int before_close_saves = saves;
    SendMessageW(integrated, WM_COMMAND, MAKEWPARAM(DisplayPage::ConfigureId, BN_CLICKED),
        reinterpret_cast<LPARAM>(GetDlgItem(integrated, DisplayPage::ConfigureId)));
    SendMessageW(GetDlgItem(integrated, DisplayPage::ListId), WM_KEYDOWN, VK_HOME, 0);
    SendMessageW(GetDlgItem(integrated, DisplayPage::ListId), WM_KEYDOWN, VK_SPACE, 0);
    Check(frame.Show(icon, saved_devices, save)
        && IsWindowEnabled(GetDlgItem(integrated, DisplayPage::SaveId)), "re-activation retains an existing edit session");
    StartupTests(frame, icon, saved_devices, save);
    const HWND license_window = AboutTests(frame, icon, saved_devices, save);
    DpiTransitions(frame, license_window);
    frame.DestroyWindow();
    Check(!IsWindow(license_window), "closing Settings destroys its owned license viewer");
    Pump();
    Check(frame.Show(icon, saved_devices, save), "closed settings can reopen with Display controls");
    Check(IsWindowVisible(GetDlgItem(frame, SettingsWindow::DisplayId))
        && !IsWindowVisible(GetDlgItem(frame, SettingsWindow::SettingsId))
        && !IsWindowVisible(GetDlgItem(frame, SettingsWindow::AboutId)), "reopened Settings defaults to Display");
    Check(saves == before_close_saves && !IsWindowEnabled(GetDlgItem(GetDlgItem(frame, SettingsWindow::DisplayId), DisplayPage::SaveId)),
        "closing discards unsaved edits and reopening returns to view mode");
    frame.DestroyWindow();
    Pump();
    NativeDpiTests(icon, saved_devices, save);
    _Module.RemoveMessageLoop();
    _Module.Term();
    CoUninitialize();
    Check(SetThreadDesktop(original) && CloseDesktop(isolated), "release isolated display desktop");
    std::puts("Display topology, identities, selection, keyboard, refresh, unknown/error, clones and native render tests passed.");
    return 0;
}
