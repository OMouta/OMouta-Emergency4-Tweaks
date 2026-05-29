#include "TweakPackage.h"

#include "../shared/AppPaths.h"
#include "../shared/StringUtil.h"

#include <algorithm>
#include <array>

namespace om4t::launcher {

namespace {

constexpr const wchar_t* kManifestName = L"tweak.ini";

std::wstring value_or(const std::map<std::wstring, std::wstring>& values, const wchar_t* key, const wchar_t* fallback = L"") {
    if (auto it = values.find(key); it != values.end()) {
        return it->second;
    }
    return fallback;
}

bool manifest_to_package(const std::filesystem::path& manifest_path, TweakPackage& package, Logger& log) {
    const auto values = read_ini_section(manifest_path, L"Tweak");
    const auto settings = read_ini_section(manifest_path, L"Settings");

    package.id = value_or(values, L"id");
    package.name = value_or(values, L"name");
    package.description = value_or(values, L"description");
    package.version = value_or(values, L"version", L"1.0.0");
    package.config_key = value_or(values, L"config_key", package.id.c_str());
    package.dll_name = value_or(values, L"dll");
    package.log_name = value_or(values, L"log");
    package.default_enabled = parse_bool(value_or(values, L"default_enabled", L"0"), false);
    package.package_dir = manifest_path.parent_path();
    package.dll_path = package.package_dir / package.dll_name;

    if (package.id.empty() || package.name.empty() || package.config_key.empty() || package.dll_name.empty()) {
        log.write(L"Ignoring invalid tweak manifest: " + manifest_path.wstring());
        return false;
    }

    if (!file_exists(package.dll_path)) {
        log.write(L"Ignoring tweak with missing DLL: " + package.dll_path.wstring());
        return false;
    }

    for (const auto& [id, spec] : settings) {
        std::array<std::wstring, 5> parts{};
        size_t start = 0;
        size_t part = 0;
        while (part < parts.size()) {
            const auto separator = spec.find(L'|', start);
            parts[part++] = trim(spec.substr(start, separator == std::wstring::npos ? std::wstring::npos : separator - start));
            if (separator == std::wstring::npos) {
                break;
            }
            start = separator + 1;
        }

        if (parts[0].empty() || parts[1].empty() || parts[2].empty()) {
            log.write(L"Ignoring invalid setting '" + id + L"' in " + manifest_path.wstring());
            continue;
        }

        TweakSetting setting;
        setting.id = id;
        setting.section = parts[0];
        setting.key = parts[1];
        setting.label = parts[2];
        setting.type = parts[3].empty() ? L"text" : parts[3];
        setting.default_value = parts[4];
        package.settings.push_back(std::move(setting));
    }

    return true;
}

} // namespace

std::vector<TweakPackage> discover_tweak_packages(const std::filesystem::path& root, Logger& log) {
    std::vector<TweakPackage> packages;
    const auto hooks = hooks_dir(root);
    std::error_code ec;

    if (!std::filesystem::is_directory(hooks, ec)) {
        return packages;
    }

    for (const auto& entry : std::filesystem::directory_iterator(hooks, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }

        const auto manifest = entry.path() / kManifestName;
        if (!file_exists(manifest)) {
            continue;
        }

        TweakPackage package;
        if (manifest_to_package(manifest, package, log)) {
            packages.push_back(std::move(package));
        }
    }

    std::sort(packages.begin(), packages.end(), [](const TweakPackage& left, const TweakPackage& right) {
        return left.name < right.name;
    });

    log.write(L"Discovered tweak packages: " + std::to_wstring(packages.size()));
    for (const auto& package : packages) {
        log.write(L" - " + package.name + L" (" + package.dll_path.wstring() + L")");
    }

    return packages;
}

void apply_config_to_packages(const Config& config, std::vector<TweakPackage>& packages) {
    for (auto& package : packages) {
        package.enabled = package.default_enabled;
        if (auto it = config.tweak_enabled.find(package.config_key); it != config.tweak_enabled.end()) {
            package.enabled = it->second;
        }
    }
}

void sync_config_from_packages(const std::vector<TweakPackage>& packages, Config& config) {
    config.tweak_enabled.clear();
    config.sections[L"Tweaks"].clear();

    for (const auto& package : packages) {
        config.tweak_enabled[package.config_key] = package.enabled;
        config.sections[L"Tweaks"][package.config_key] = package.enabled ? L"1" : L"0";

        for (const auto& setting : package.settings) {
            if (setting.section.empty() || setting.key.empty()) {
                continue;
            }

            auto& section = config.sections[setting.section];
            if (auto value = section.find(setting.key); value == section.end()) {
                section[setting.key] = setting.default_value;
            }
        }
    }

    config.sections[L"Game"][L"em4_path"] = config.em4_path.wstring();
}

std::vector<std::filesystem::path> enabled_hook_paths(const std::vector<TweakPackage>& packages) {
    std::vector<std::filesystem::path> paths;
    for (const auto& package : packages) {
        if (package.enabled) {
            paths.push_back(package.dll_path);
        }
    }
    return paths;
}

} // namespace om4t::launcher
