#pragma once

#include "../shared/Config.h"
#include "../shared/Logger.h"

#include <filesystem>
#include <string>
#include <vector>

namespace om4t::launcher {

struct TweakPackage {
    std::wstring id;
    std::wstring name;
    std::wstring description;
    std::wstring version;
    std::wstring config_key;
    std::wstring dll_name;
    std::wstring log_name;
    std::filesystem::path package_dir;
    std::filesystem::path dll_path;
    bool default_enabled = false;
    bool enabled = false;
};

std::vector<TweakPackage> discover_tweak_packages(const std::filesystem::path& root, Logger& log);
void apply_config_to_packages(const Config& config, std::vector<TweakPackage>& packages);
void sync_config_from_packages(const std::vector<TweakPackage>& packages, Config& config);
std::vector<std::filesystem::path> enabled_hook_paths(const std::vector<TweakPackage>& packages);

} // namespace om4t::launcher
