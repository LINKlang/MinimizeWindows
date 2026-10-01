#include "../src/config_store.h"
#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>

namespace {

void Check(bool condition, const char* message)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s (Win32 %lu)\n", message, GetLastError()); std::exit(1); }
}

std::string Read(const std::wstring& path)
{
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "open fixture for reading");
    const DWORD size = GetFileSize(file, nullptr);
    Check(size != INVALID_FILE_SIZE, "read fixture length");
    std::string bytes(size, '\0');
    DWORD count = 0;
    Check(ReadFile(file, bytes.empty() ? nullptr : &bytes[0], size, &count, nullptr) && count == size, "read fixture");
    CloseHandle(file);
    return bytes;
}

void Write(const std::wstring& path, const std::string& bytes)
{
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "open fixture for writing");
    DWORD count = 0;
    Check(WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) && count == bytes.size(), "write fixture");
    CloseHandle(file);
}

} // namespace

int main()
{
    std::wstring error, user_path;
    Check(ConfigStore::UserFilePath(user_path, error), "resolve user configuration path without COM initialization");
    PWSTR roaming = nullptr;
    Check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)), "query native roaming folder");
    Check(user_path == std::wstring(roaming) + L"\\MinimizeWindows\\config.json", "configuration uses current user's Roaming AppData");
    CoTaskMemFree(roaming);

    wchar_t absolute[MAX_PATH]{};
    Check(GetFullPathNameW(L"build\\tests", ARRAYSIZE(absolute), absolute, nullptr) > 0, "resolve isolated test directory");
    const std::wstring directory = std::wstring(absolute) + L"\\配置-" + std::to_wstring(GetCurrentProcessId())
        + L"-" + std::to_wstring(GetTickCount64());
    const std::wstring path = directory + L"\\config.json";
    const ConfigStore store(path);
    AppConfig config;
    config.monitor_device = L"stale";
    Check(store.LoadOrCreate(config, error) && config.monitor_device.empty(), "first load creates default configuration");
    const std::string defaults = Read(path);
    const auto default_document = nlohmann::json::parse(defaults);
    Check(default_document["version"] == 1 && default_document["monitor_device"] == "",
        "default JSON contains version and startup-primary selection");
    Check(defaults.compare(0, 3, "\xEF\xBB\xBF") != 0, "saved configuration is UTF-8 without BOM");

    config.monitor_device = L"\\\\.\\DISPLAY2";
    Check(store.Save(config, error), "save an explicit target");
    AppConfig loaded;
    Check(store.LoadOrCreate(loaded, error) && loaded.monitor_device == config.monitor_device, "explicit target survives reload");
    Check(nlohmann::json::parse(Read(path))["monitor_device"] == "\\\\.\\DISPLAY2", "GDI backslashes are JSON-escaped correctly");

    config.monitor_device = L"显示设备 \U0001F5A5";
    Check(store.Save(config, error) && store.LoadOrCreate(loaded, error)
        && loaded.monitor_device == config.monitor_device, "Unicode values and Unicode file paths round-trip");
    const std::string unicode = Read(path);
    Check(unicode.find("\xE6\x98\xBE") != std::string::npos, "Unicode data is stored as UTF-8");
    config.monitor_device.assign(1, static_cast<wchar_t>(0xd800));
    Check(!store.Save(config, error) && Read(path) == unicode, "invalid UTF-16 cannot damage saved configuration");
    config.monitor_device = L"\\\\.\\DISPLAY3";
    const HANDLE blocker = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(blocker != INVALID_HANDLE_VALUE, "lock original against replacement");
    Check(!store.Save(config, error) && Read(path) == unicode, "failed replacement preserves original file");
    CloseHandle(blocker);
    Check(store.Save(config, error) && store.LoadOrCreate(loaded, error)
        && loaded.monitor_device == config.monitor_device, "save succeeds after replacement lock is released");

    const char* invalid[] = {
        "{broken", "[]", "{\"monitor_device\":42}", "{\"version\":2}",
        "{\"monitor_device\":\"\\\\\\\\.\\\\DISPLAY1\\u0000ignored\"}", ""
    };
    for (const auto bytes : invalid) {
        Write(path, bytes);
        loaded.monitor_device = L"stale";
        Check(!store.LoadOrCreate(loaded, error) && loaded.monitor_device.empty()
            && error.find(L"config.json") != std::wstring::npos && Read(path) == bytes,
            "invalid configuration reports path and preserves original bytes");
    }
    Write(path, "{}");
    Check(store.LoadOrCreate(loaded, error) && loaded.monitor_device.empty(), "missing settings use defaults without rewriting");
    Check(Read(path) == "{}", "loading a valid configuration does not rewrite it");
    Write(path, "\xEF\xBB\xBF{\"version\":1,\"monitor_device\":\"\"}");
    Check(store.LoadOrCreate(loaded, error), "UTF-8 BOM from a user's editor can be read");

    const std::wstring blocking_path = directory + L"\\blocked";
    Write(blocking_path, "keep");
    const ConfigStore blocked(blocking_path + L"\\config.json");
    Check(!blocked.Save(config, error) && Read(blocking_path) == "keep", "a file cannot be treated as a configuration directory");
    Check(DeleteFileW(blocking_path.c_str()) && DeleteFileW(path.c_str()) && RemoveDirectoryW(directory.c_str()),
        "remove isolated fixtures; no temporary configuration files remain");
    std::puts("AppData path, JSON defaults, target persistence, UTF-8, invalid files and failed-save preservation tests passed.");
    return 0;
}
