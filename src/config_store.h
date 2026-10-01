#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <utility>

struct AppConfig {
    // Empty selects the primary monitor at startup; otherwise use a GDI device name.
    std::wstring monitor_device;
};

class ConfigStore {
public:
    // Resolves %APPDATA%\MinimizeWindows\config.json without creating files.
    static bool UserFilePath(std::wstring& path, std::wstring& error);
    explicit ConfigStore(std::wstring file_path) : file_path_(std::move(file_path)) { }

    // Missing files are created with defaults. Invalid/unreadable files are preserved.
    bool LoadOrCreate(AppConfig& config, std::wstring& error) const;
    // Writes UTF-8 through a temporary file in the same directory before replacing.
    bool Save(const AppConfig& config, std::wstring& error) const;
    const std::wstring& Path() const { return file_path_; }

private:
    bool Write(const AppConfig& config, bool replace, std::wstring& error) const;
    std::wstring file_path_;
};
