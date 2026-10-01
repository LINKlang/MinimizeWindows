#include "keyboard_hook.h"
#include "desktop_manager.h"
#include "monitor_enumerator.h"
#include "tray_application.h"

#include <shellapi.h>

#include <climits>
#include <cwchar>
#include <iomanip>
#include <locale>
#include <sstream>

namespace {

const wchar_t* const UsageText =
    L"Usage:\n"
    L"  MinimizeWindows.exe                        Use the primary monitor at startup\n"
    L"  MinimizeWindows.exe --monitor \"\\\\.\\DISPLAY2\"  Use the specified GDI device\n"
    L"  MinimizeWindows.exe --list-monitors        List active displays and exit\n"
    L"  MinimizeWindows.exe --help                 Show this help and exit\n"
    L"\nChoose a GDI device name from --list-monitors, not a display number.\n"
    L"The selected device stays fixed until the program exits.\n";

struct CommandLineOptions {
    enum class Mode { Run, ListMonitors, Help };
    Mode mode = Mode::Run;
    std::wstring monitor_device;
};

bool ParseOptions(int argument_count, const wchar_t* const* arguments,
    CommandLineOptions& options, std::wstring& error)
{
    options = {};
    error.clear();
    bool monitor_seen = false;
    bool mode_seen = false;
    for (int index = 1; index < argument_count; ++index) {
        const wchar_t* argument = arguments[index];
        if (std::wcscmp(argument, L"--monitor") == 0) {
            if (monitor_seen || mode_seen) {
                error = L"--monitor must appear once and cannot be combined with --help or --list-monitors.";
                return false;
            }
            if (index + 1 >= argument_count || arguments[index + 1][0] == L'\0'
                || std::wcsncmp(arguments[index + 1], L"--", 2) == 0) {
                error = L"--monitor requires a GDI device name.";
                return false;
            }
            monitor_seen = true;
            options.monitor_device = arguments[++index];
        }
        else if (std::wcscmp(argument, L"--list-monitors") == 0
            || std::wcscmp(argument, L"--help") == 0) {
            if (mode_seen || monitor_seen) {
                error = L"--help and --list-monitors must be used alone.";
                return false;
            }
            mode_seen = true;
            options.mode = std::wcscmp(argument, L"--help") == 0
                ? CommandLineOptions::Mode::Help : CommandLineOptions::Mode::ListMonitors;
        }
        else {
            error = std::wstring(L"Unknown argument: ") + argument;
            return false;
        }
    }
    return true;
}

bool SelectMonitorTarget(const MonitorSnapshot& snapshot, const std::wstring& requested_device,
    MonitorTarget& target, std::wstring& error)
{
    target = {};
    error.clear();
    for (const auto& monitor : snapshot.monitors) {
        const bool selected = requested_device.empty() ? monitor.primary
            : _wcsicmp(monitor.device_name.c_str(), requested_device.c_str()) == 0;
        if (selected && !monitor.device_name.empty() && monitor.handle != nullptr) {
            // Default and explicit selection both produce the same device value.
            target.device_name = monitor.device_name;
            return true;
        }
    }
    error = requested_device.empty() ? L"No active primary monitor is available."
        : L"Monitor \"" + requested_device + L"\" is not available. Use --list-monitors to find an active GDI device.";
    return false;
}

bool UsableOutput(HANDLE handle)
{
    return handle != nullptr && handle != INVALID_HANDLE_VALUE
        && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

struct ConsoleOutput {
    HANDLE output = nullptr;
    HANDLE error = nullptr;

    bool Initialize()
    {
        const HANDLE previous_output = GetStdHandle(STD_OUTPUT_HANDLE);
        const HANDLE previous_error = GetStdHandle(STD_ERROR_HANDLE);
        const bool keep_output = UsableOutput(previous_output);
        const bool keep_error = UsableOutput(previous_error);
        const bool console_handle = (keep_output && GetFileType(previous_output) == FILE_TYPE_CHAR)
            || (keep_error && GetFileType(previous_error) == FILE_TYPE_CHAR);

        if ((!keep_output || !keep_error || console_handle) && GetConsoleWindow() == nullptr) {
            if (!AttachConsole(ATTACH_PARENT_PROCESS) && GetLastError() != ERROR_ACCESS_DENIED) {
                if ((!keep_output || !keep_error) && !AllocConsole()) {
                    return false;
                }
            }
        }
        // Attaching a console must not replace inherited file/pipe destinations.
        output = keep_output ? previous_output : GetStdHandle(STD_OUTPUT_HANDLE);
        error = keep_error ? previous_error : GetStdHandle(STD_ERROR_HANDLE);
        return UsableOutput(output) && UsableOutput(error);
    }
};

bool WriteText(HANDLE destination, const std::wstring& text)
{
    if (text.empty()) {
        return true;
    }
    if (text.size() > INT_MAX) {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    DWORD mode = 0;
    if (GetConsoleMode(destination, &mode)) {
        size_t offset = 0;
        while (offset < text.size()) {
            const size_t remaining = text.size() - offset;
            DWORD count = static_cast<DWORD>(remaining > 32768 ? 32768 : remaining);
            // Keep a surrogate pair together if a long console write is split.
            if (offset + count < text.size() && text[offset + count - 1] >= 0xD800
                && text[offset + count - 1] <= 0xDBFF) {
                --count;
            }
            DWORD written = 0;
            if (!WriteConsoleW(destination, text.data() + offset, count, &written, nullptr)) {
                return false;
            }
            if (written == 0) { SetLastError(ERROR_WRITE_FAULT); return false; }
            offset += written;
        }
        return true;
    }

    const int length = static_cast<int>(text.size());
    const int byte_count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        text.data(), length, nullptr, 0, nullptr, nullptr);
    if (byte_count == 0) {
        return false;
    }
    std::string bytes(byte_count, '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
            &bytes[0], byte_count, nullptr, nullptr) == 0) {
        return false;
    }
    size_t offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        if (!WriteFile(destination, bytes.data() + offset,
                static_cast<DWORD>(bytes.size() - offset), &written, nullptr)) {
            return false;
        }
        if (written == 0) { SetLastError(ERROR_WRITE_FAULT); return false; }
        offset += written;
    }
    return true;
}

void PrintRect(std::wostream& output, const RECT& rect)
{
    output << L"[" << rect.left << L", " << rect.top << L", "
        << rect.right << L", " << rect.bottom << L"]";
}

void PrintAdapter(std::wostream& output, const LUID& adapter)
{
    output << L"0x" << std::hex << std::setfill(L'0') << std::setw(8)
        << static_cast<DWORD>(adapter.HighPart) << std::setw(8) << adapter.LowPart
        << std::dec << std::setfill(L' ');
}

const std::wstring& KnownName(const std::wstring& name)
{
    static const std::wstring unknown = L"unknown";
    return name.empty() ? unknown : name;
}

std::wstring FormatSnapshot(const MonitorSnapshot& snapshot)
{
    std::wostringstream output;
    output.imbue(std::locale::classic());
    output << L"Query source: "
        << (snapshot.query_source == MonitorQuerySource::DisplayConfig
            ? L"QueryDisplayConfig (active output targets)" : L"GDI fallback (logical desktop monitors)")
        << L"\nActive displays: " << snapshot.monitors.size()
        << L"\nCoordinates: signed physical desktop pixels\nVirtual desktop: ";
    PrintRect(output, snapshot.virtual_bounds_px);
    output << L" size="
        << static_cast<long long>(snapshot.virtual_bounds_px.right) - snapshot.virtual_bounds_px.left
        << L"x" << static_cast<long long>(snapshot.virtual_bounds_px.bottom) - snapshot.virtual_bounds_px.top
        << L"\nDebug indices are not Windows Settings display numbers.\n";

    size_t index = 0;
    for (const auto& monitor : snapshot.monitors) {
        output << L"\nDisplay #" << ++index
            << L"\n  Name: " << KnownName(monitor.friendly_name)
            << L"\n  GDI device: " << KnownName(monitor.device_name)
            << L"\n  Device path: " << KnownName(monitor.device_path)
            << L"\n  Runtime monitor handle: ";
        if (monitor.handle != nullptr) {
            output << L"0x" << std::hex << reinterpret_cast<ULONG_PTR>(monitor.handle) << std::dec;
        }
        else { output << L"unknown"; }
        output << L"\n  Primary: " << (monitor.primary ? L"yes" : L"no") << L"\n  Bounds px: ";
        PrintRect(output, monitor.bounds_px);
        output << L"\n  Position px: (" << monitor.bounds_px.left << L", " << monitor.bounds_px.top
            << L")\n  Desktop size px: " << monitor.desktop_size_px.cx << L"x" << monitor.desktop_size_px.cy
            << L"\n  Work area px: ";
        if (monitor.work_area_px.available) { PrintRect(output, monitor.work_area_px.value); }
        else { output << L"unknown"; }
        output << L"\n  Orientation: ";
        if (monitor.rotation_degrees.available) { output << monitor.rotation_degrees.value << L" degrees"; }
        else { output << L"unknown"; }
        output << L"\n  Scale (system step): ";
        if (monitor.scale_percent.available) { output << monitor.scale_percent.value << L"%"; }
        else { output << L"unknown"; }
        output << L"\n  Refresh: ";
        if (monitor.refresh_rate.available) {
            const auto& refresh = monitor.refresh_rate.value;
            output << refresh.numerator << L"/" << refresh.denominator << L" = "
                << std::fixed << std::setprecision(3)
                << static_cast<double>(refresh.numerator) / refresh.denominator << L" Hz";
        }
        else { output << L"unknown"; }
        output << L"\n  Source adapter/id: ";
        if (monitor.ccd_identifiers_available) {
            PrintAdapter(output, monitor.source_adapter_id);
            output << L"/" << monitor.source_id << L"\n  Target adapter/id: ";
            PrintAdapter(output, monitor.target_adapter_id);
            output << L"/" << monitor.target_id;
        }
        else { output << L"unknown\n  Target adapter/id: unknown"; }
        output << L"\n";
    }
    return output.str();
}

std::wstring FormatError(const wchar_t* operation, DWORD error)
{
    LPWSTR description = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, 0,
        reinterpret_cast<LPWSTR>(&description), 0, nullptr);
    std::wstring message = std::wstring(operation) + L" failed (Win32 error "
        + std::to_wstring(error) + L"). ";
    if (description != nullptr) {
        message += description;
        LocalFree(description);
    }
    message += L"\n";
    return message;
}

int ReportStartupError(const std::wstring& error)
{
    const std::wstring message = L"MinimizeWindows: " + error + L"\n\n" + UsageText;
    ConsoleOutput console;
    if (console.Initialize()) { WriteText(console.error, message); }
    else { OutputDebugStringW(message.c_str()); }
    return 1;
}

int PrintHelp()
{
    ConsoleOutput console;
    if (!console.Initialize()) {
        return ReportStartupError(FormatError(L"Console output initialization", GetLastError()));
    }
    if (!WriteText(console.output, UsageText)) {
        const DWORD error = GetLastError();
        WriteText(console.error, FormatError(L"Help output", error));
        return 1;
    }
    return 0;
}

int ListMonitors()
{
    ConsoleOutput console;
    if (!console.Initialize()) {
        const std::wstring message = FormatError(L"Console output initialization", GetLastError());
        const HANDLE error_output = GetStdHandle(STD_ERROR_HANDLE);
        if (UsableOutput(error_output)) { WriteText(error_output, message); }
        else { OutputDebugStringW(message.c_str()); }
        return 1;
    }
    MonitorSnapshot snapshot;
    if (!MonitorEnumerator{}.Enumerate(snapshot)) {
        const DWORD error = GetLastError();
        WriteText(console.error, FormatError(L"Monitor enumeration", error));
        return 1;
    }
    if (!WriteText(console.output, FormatSnapshot(snapshot))) {
        WriteText(console.error, FormatError(L"Monitor output", GetLastError()));
        return 1;
    }
    return 0;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    int argument_count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr) {
        return ReportStartupError(FormatError(L"Command line parsing", GetLastError()));
    }
    CommandLineOptions options;
    std::wstring error;
    const bool parsed = ParseOptions(argument_count, arguments, options, error);
    LocalFree(arguments);
    if (!parsed) {
        return ReportStartupError(error);
    }
    if (options.mode == CommandLineOptions::Mode::ListMonitors) {
        return ListMonitors();
    }
    if (options.mode == CommandLineOptions::Mode::Help) {
        return PrintHelp();
    }

    MonitorTarget target;
    {
        MonitorSnapshot snapshot;
        if (!MonitorEnumerator{}.Enumerate(snapshot)) {
            return ReportStartupError(FormatError(L"Monitor enumeration", GetLastError()));
        }
        if (!SelectMonitorTarget(snapshot, options.monitor_device, target, error)) {
            return ReportStartupError(error);
        }
    }

    const int result = RunTrayApplication(instance, target);
    if (result != 0) { return ReportStartupError(FormatError(L"Tray application startup", GetLastError())); }
    return result;
}
