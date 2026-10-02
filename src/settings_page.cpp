#include "settings_page.h"
#include "display_page.h"
#include "licenses_dialog.h"

#include <shlobj.h>

namespace {

HRESULT CreateStartupShortcut()
{
    PWSTR folder = nullptr;
    HRESULT result = SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_CREATE, nullptr, &folder);
    std::wstring shortcut;
    if (SUCCEEDED(result) && folder != nullptr) { shortcut = std::wstring(folder) + L"\\MinimizeWindows.lnk"; }
    CoTaskMemFree(folder);
    if (FAILED(result)) { return result; }
    if (shortcut.empty()) { return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND); }

    std::wstring executable(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, &executable[0], static_cast<DWORD>(executable.size()));
        if (length == 0) { return HRESULT_FROM_WIN32(GetLastError()); }
        if (length < executable.size()) { executable.resize(length); break; }
        executable.resize(executable.size() * 2);
    }

    ATL::CComPtr<IShellLinkW> link;
    result = link.CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER);
    if (FAILED(result)) { return result; }
    result = link->SetPath(executable.c_str());
    if (FAILED(result)) { return result; }
    result = link->SetWorkingDirectory(executable.substr(0, executable.find_last_of(L"\\/")).c_str());
    if (FAILED(result)) { return result; }
    result = link->SetArguments(L"");
    if (FAILED(result)) { return result; }
    ATL::CComPtr<IPersistFile> file;
    result = link.QueryInterface(&file);
    if (FAILED(result)) { return result; }
    return file->Save(shortcut.c_str(), TRUE);
}

} // namespace

bool SettingsPage::CreatePage(HWND parent, const RECT& bounds)
{
    RECT rectangle = bounds;
    return Create(parent, &rectangle, nullptr, WS_CHILD | WS_CLIPCHILDREN, WS_EX_CONTROLPARENT) != nullptr;
}

LRESULT SettingsPage::OnCreate(UINT, WPARAM, LPARAM, BOOL&)
{
    dpi_ = DisplayWindowDpi(m_hWnd);
    const wchar_t* captions[] = {L"Open Startup Folder", L"Create Startup Shortcut"};
    for (UINT i = 0; i < ARRAYSIZE(captions); ++i) {
        if (CreateWindowExW(0, L"BUTTON", captions[i], WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 0, 0, m_hWnd, reinterpret_cast<HMENU>(static_cast<UINT_PTR>(OpenStartupId + i)),
                _Module.GetModuleInstance(), nullptr) == nullptr) { return -1; }
    }
    Layout();
    return 0;
}

void SettingsPage::SetDpi(UINT dpi)
{
    dpi_ = dpi;
    if (IsWindow()) { Layout(); }
}

void SettingsPage::Layout()
{
    if (body_font_ == nullptr || font_dpi_ != dpi_) {
        if (body_font_ != nullptr) { DeleteObject(body_font_); }
        if (title_font_ != nullptr) { DeleteObject(title_font_); }
        body_font_ = CreateFontW(-Px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        title_font_ = CreateFontW(-Px(26), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        font_dpi_ = dpi_;
    }
    for (UINT i = 0; i < 2; ++i) {
        const UINT id = OpenStartupId + i;
        ::SetWindowPos(GetDlgItem(id), nullptr, Px(24), Px(124 + i * 44), Px(240), Px(32),
            SWP_NOZORDER | SWP_NOACTIVATE);
        SendDlgItemMessage(id, WM_SETFONT, reinterpret_cast<WPARAM>(body_font_), TRUE);
    }
    Invalidate(FALSE);
}

void SettingsPage::Paint(HDC dc)
{
    RECT client{};
    GetClientRect(&client);
    SetDCBrushColor(dc, RGB(32, 32, 32));
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(245, 245, 245));
    const HGDIOBJ old = SelectObject(dc, title_font_ != nullptr ? title_font_ : GetStockObject(DEFAULT_GUI_FONT));
    RECT title{Px(24), Px(18), client.right - Px(24), Px(50)};
    DrawTextW(dc, L"Startup", -1, &title, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, body_font_ != nullptr ? body_font_ : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(dc, RGB(173, 173, 173));
    RECT text{Px(24), Px(62), client.right - Px(24), Px(106)};
    DrawTextW(dc, L"MinimizeWindows can be started automatically by\nplacing a shortcut in the Windows Startup folder.",
        -1, &text, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
}

LRESULT SettingsPage::OnSize(UINT, WPARAM, LPARAM, BOOL&) { Layout(); return 0; }
LRESULT SettingsPage::OnPaint(UINT, WPARAM, LPARAM, BOOL&)
{
    PAINTSTRUCT paint{};
    const HDC dc = BeginPaint(&paint);
    Paint(dc);
    EndPaint(&paint);
    return 0;
}
LRESULT SettingsPage::OnPrint(UINT, WPARAM dc, LPARAM, BOOL&) { Paint(reinterpret_cast<HDC>(dc)); return 0; }
LRESULT SettingsPage::OnDrawItem(UINT, WPARAM, LPARAM parameter, BOOL&)
{
    DrawDarkButton(*reinterpret_cast<const DRAWITEMSTRUCT*>(parameter), body_font_);
    return TRUE;
}
LRESULT SettingsPage::OnOpenStartup(WORD, WORD, HWND, BOOL&)
{
    std::wstring windows(MAX_PATH, L'\0');
    UINT length = GetWindowsDirectoryW(&windows[0], static_cast<UINT>(windows.size()));
    if (length >= windows.size()) {
        windows.resize(length + 1);
        length = GetWindowsDirectoryW(&windows[0], static_cast<UINT>(windows.size()));
    }
    BOOL opened = FALSE;
    if (length != 0 && length < windows.size()) {
        windows.resize(length);
        const std::wstring explorer = windows + L"\\explorer.exe";
        std::wstring command = L"\"" + explorer + L"\" shell:startup";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_SHOWNORMAL;
        PROCESS_INFORMATION process{};
        // Keep folder associations and Shell extensions out of the UI process.
        opened = CreateProcessW(explorer.c_str(), &command[0], nullptr, nullptr, FALSE, 0,
            nullptr, nullptr, &startup, &process);
        if (opened) {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }
    if (!opened) {
        ::MessageBoxW(m_hWnd, L"Unable to open the Startup folder.", L"MinimizeWindows", MB_OK | MB_ICONERROR);
    }
    return 0;
}
LRESULT SettingsPage::OnCreateStartup(WORD, WORD, HWND, BOOL&)
{
    const HRESULT result = CreateStartupShortcut();
    ::MessageBoxW(m_hWnd, SUCCEEDED(result) ? L"Startup shortcut created or updated." : L"Unable to create the startup shortcut.",
        L"MinimizeWindows", MB_OK | (SUCCEEDED(result) ? MB_ICONINFORMATION : MB_ICONERROR));
    return 0;
}
LRESULT SettingsPage::OnDestroy(UINT, WPARAM, LPARAM, BOOL&)
{
    if (body_font_ != nullptr) { DeleteObject(body_font_); body_font_ = nullptr; }
    if (title_font_ != nullptr) { DeleteObject(title_font_); title_font_ = nullptr; }
    return 0;
}
