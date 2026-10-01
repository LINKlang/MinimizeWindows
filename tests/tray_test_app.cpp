// Run the production app on a private desktop without publishing a real tray icon.
#include <windows.h>
#include <shellapi.h>
#include "../src/config_store.h"

// Core/window integration uses defaults and never reads or writes the user's AppData.
bool ConfigStore::UserFilePath(std::wstring& path, std::wstring& error)
{
    path = L"isolated-config.json";
    error.clear();
    return true;
}

bool ConfigStore::LoadOrCreate(AppConfig& config, std::wstring& error) const
{
    config = {};
    error.clear();
    return true;
}

bool ConfigStore::Save(const AppConfig&, std::wstring& error) const
{
    error.clear();
    return true;
}

namespace isolated_tray {
BOOL WINAPI Notify(DWORD, PNOTIFYICONDATAW) { return TRUE; }
}

#define Shell_NotifyIconW isolated_tray::Notify
#include "../src/tray_application.cpp"
#undef Shell_NotifyIconW
