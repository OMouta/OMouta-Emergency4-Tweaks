#pragma once

#include <windows.h>

#include <filesystem>

namespace om4t {

inline constexpr const wchar_t* kBrand = L"OMouta's EM4 Tweaks";
inline constexpr const wchar_t* kDataDir = L"OMoutaEM4Tweaks";
inline constexpr const wchar_t* kHooksDir = L"Hooks";
inline constexpr const wchar_t* kLogsDir = L"Logs";
inline constexpr const wchar_t* kConfigName = L"config.ini";
inline constexpr const wchar_t* kLauncherLogName = L"Launcher.log";

inline std::filesystem::path module_dir() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

inline bool file_exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

inline void ensure_app_layout(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / kDataDir / kHooksDir);
    std::filesystem::create_directories(root / kDataDir / kLogsDir);
}

inline std::filesystem::path config_path(const std::filesystem::path& root) {
    return root / kDataDir / kConfigName;
}

inline std::filesystem::path log_path(const std::filesystem::path& root, const wchar_t* file_name) {
    return root / kDataDir / kLogsDir / file_name;
}

inline std::filesystem::path hooks_dir(const std::filesystem::path& root) {
    return root / kDataDir / kHooksDir;
}

} // namespace om4t
