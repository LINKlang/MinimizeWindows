// Run the production app on a private desktop without publishing a real tray icon.
#include <windows.h>
#include <shellapi.h>

namespace isolated_tray {
BOOL WINAPI Notify(DWORD, PNOTIFYICONDATAW) { return TRUE; }
}

#define Shell_NotifyIconW isolated_tray::Notify
#include "../src/tray_application.cpp"
#undef Shell_NotifyIconW
