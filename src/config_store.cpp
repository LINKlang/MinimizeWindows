#include "config_store.h"

#include <shlobj.h>
#include <nlohmann/json.hpp>
#include <climits>
#include <new>

namespace {

struct File {
    HANDLE handle;
    explicit File(HANDLE value) : handle(value) { }
    ~File() { if (handle != INVALID_HANDLE_VALUE) { CloseHandle(handle); } }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};

struct TemporaryFile {
    std::wstring path;
    bool moved = false;
    ~TemporaryFile() { if (!moved && !path.empty()) { DeleteFileW(path.c_str()); } }
};

bool Fail(const std::wstring& path, std::wstring& error, const std::wstring& reason,
    DWORD code = ERROR_INVALID_DATA)
{
    error = L"Configuration \"" + path + L"\": " + reason;
    SetLastError(code);
    return false;
}

bool IoError(const std::wstring& path, std::wstring& error, const wchar_t* operation, DWORD code)
{
    return Fail(path, error, std::wstring(operation) + L" (Win32 error " + std::to_wstring(code) + L").", code);
}

bool FromUtf8(const std::string& input, std::wstring& output)
{
    if (input.empty()) { output.clear(); return true; }
    if (input.size() > INT_MAX) { return false; }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
        static_cast<int>(input.size()), nullptr, 0);
    if (size == 0) { return false; }
    output.resize(size);
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
        static_cast<int>(input.size()), &output[0], size) == size;
}

bool ToUtf8(const std::wstring& input, std::string& output)
{
    if (input.empty()) { output.clear(); return true; }
    if (input.size() > INT_MAX || input.find(L'\0') != std::wstring::npos) { return false; }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
        static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    if (size == 0) { return false; }
    output.resize(size);
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input.data(),
        static_cast<int>(input.size()), &output[0], size, nullptr, nullptr) == size;
}

} // namespace

bool ConfigStore::UserFilePath(std::wstring& path, std::wstring& error)
{
    path.clear();
    error.clear();
    PWSTR folder = nullptr;
    const HRESULT result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &folder);
    if (SUCCEEDED(result) && folder != nullptr) { path = std::wstring(folder) + L"\\MinimizeWindows\\config.json"; }
    CoTaskMemFree(folder);
    if (FAILED(result) || path.empty()) {
        return Fail(L"AppData", error, L"Cannot locate the current user's roaming AppData folder.",
            HRESULT_FACILITY(result) == FACILITY_WIN32 ? HRESULT_CODE(result) : ERROR_PATH_NOT_FOUND);
    }
    return true;
}

bool ConfigStore::LoadOrCreate(AppConfig& config, std::wstring& error) const
{
    config = {};
    error.clear();
    try {
        File file(CreateFileW(file_path_.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.handle == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            if (code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
                return IoError(file_path_, error, L"Cannot open the file", code);
            }
            if (Write(config, false, error)) { return true; }
            // Another instance may have created the file after our initial check.
            const DWORD write_error = GetLastError();
            if (write_error == ERROR_ALREADY_EXISTS || write_error == ERROR_FILE_EXISTS) {
                return LoadOrCreate(config, error);
            }
            return false;
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.handle, &size)) {
            return IoError(file_path_, error, L"Cannot read the file size", GetLastError());
        }
        if (size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) > MAXDWORD) {
            return Fail(file_path_, error, L"The file is too large.", ERROR_FILE_TOO_LARGE);
        }
        std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
        DWORD count = 0;
        if (!ReadFile(file.handle, bytes.empty() ? nullptr : &bytes[0], static_cast<DWORD>(bytes.size()), &count, nullptr)) {
            return IoError(file_path_, error, L"Cannot read the file", GetLastError());
        }
        if (count != bytes.size()) {
            return Fail(file_path_, error, L"The file changed while being read.", ERROR_READ_FAULT);
        }
        const auto document = nlohmann::json::parse(bytes);
        if (!document.is_object()) { return Fail(file_path_, error, L"The JSON root must be an object."); }
        const auto version = document.find("version");
        if (version != document.end() && (!version->is_number_integer() || *version != 1)) {
            return Fail(file_path_, error, L"Unsupported configuration version; expected version 1.");
        }
        const auto device = document.find("monitor_device");
        if (device == document.end()) { return true; }
        if (!device->is_string()) { return Fail(file_path_, error, L"monitor_device must be a string."); }
        AppConfig loaded;
        if (!FromUtf8(device->get<std::string>(), loaded.monitor_device)
            || loaded.monitor_device.find(L'\0') != std::wstring::npos) {
            return Fail(file_path_, error, L"monitor_device must contain valid Unicode without null characters.");
        }
        config = std::move(loaded);
        return true;
    }
    catch (const nlohmann::json::exception& exception) {
        std::wstring detail;
        FromUtf8(exception.what(), detail);
        return Fail(file_path_, error, L"Invalid JSON. " + detail);
    }
    catch (const std::bad_alloc&) {
        return Fail(file_path_, error, L"Not enough memory to read the file.", ERROR_NOT_ENOUGH_MEMORY);
    }
}

bool ConfigStore::Write(const AppConfig& config, bool replace, std::wstring& error) const
{
    error.clear();
    try {
        std::string device;
        if (!ToUtf8(config.monitor_device, device)) {
            return Fail(file_path_, error, L"monitor_device must contain valid Unicode without null characters.");
        }
        const nlohmann::json document{{"version", 1}, {"monitor_device", device}};
        const std::string bytes = document.dump(2) + "\n";
        const size_t separator = file_path_.find_last_of(L"\\/");
        if (separator == std::wstring::npos) {
            return Fail(file_path_, error, L"A configuration directory is required.", ERROR_BAD_PATHNAME);
        }
        const std::wstring directory = file_path_.substr(0, separator);
        if (!CreateDirectoryW(directory.c_str(), nullptr)) {
            const DWORD code = GetLastError();
            if (code != ERROR_ALREADY_EXISTS) { return IoError(file_path_, error, L"Cannot create the directory", code); }
            const DWORD attributes = GetFileAttributesW(directory.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                return Fail(file_path_, error, L"The configuration directory is not accessible.", ERROR_DIRECTORY);
            }
        }
        wchar_t temporary_path[MAX_PATH]{};
        if (!GetTempFileNameW(directory.c_str(), L"mwc", 0, temporary_path)) {
            return IoError(file_path_, error, L"Cannot create a temporary file", GetLastError());
        }
        TemporaryFile temporary{temporary_path};
        {
            File file(CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (file.handle == INVALID_HANDLE_VALUE) {
                return IoError(file_path_, error, L"Cannot open the temporary file", GetLastError());
            }
            DWORD count = 0;
            if (!WriteFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr)) {
                return IoError(file_path_, error, L"Cannot write the temporary file", GetLastError());
            }
            if (count != bytes.size()) {
                return Fail(file_path_, error, L"Could not write the complete configuration.", ERROR_WRITE_FAULT);
            }
            if (!FlushFileBuffers(file.handle)) {
                return IoError(file_path_, error, L"Cannot flush the temporary file", GetLastError());
            }
        }
        const DWORD flags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
        if (!MoveFileExW(temporary.path.c_str(), file_path_.c_str(), flags)) {
            const DWORD code = GetLastError();
            // Restore the error after cleanup so LoadOrCreate can handle creation races.
            DeleteFileW(temporary.path.c_str());
            temporary.moved = true;
            return IoError(file_path_, error, L"Cannot replace the configuration file", code);
        }
        temporary.moved = true;
        return true;
    }
    catch (const nlohmann::json::exception&) {
        return Fail(file_path_, error, L"Could not encode the configuration as UTF-8 JSON.");
    }
    catch (const std::bad_alloc&) {
        return Fail(file_path_, error, L"Not enough memory to save the file.", ERROR_NOT_ENOUGH_MEMORY);
    }
}

bool ConfigStore::Save(const AppConfig& config, std::wstring& error) const
{
    return Write(config, true, error);
}
