#pragma once

#include "StringUtil.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace om4t {

struct Config {
    std::filesystem::path em4_path;
    std::map<std::wstring, bool> tweak_enabled;
    std::map<std::wstring, std::map<std::wstring, std::wstring>> sections;
    bool borderless_enabled = true;
    int borderless_x = 0;
    int borderless_y = 0;
    int borderless_width = 1920;
    int borderless_height = 1080;
    bool keep_visible_on_focus_loss = true;
};

inline std::map<std::wstring, std::wstring> read_ini(const std::filesystem::path& path) {
    std::map<std::wstring, std::wstring> values;
    std::wifstream input(path);
    std::wstring line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';' || line[0] == L'[') {
            continue;
        }
        const auto equals = line.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }
        values[trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
    }
    return values;
}

inline std::map<std::wstring, std::map<std::wstring, std::wstring>> read_ini_sections(const std::filesystem::path& path) {
    std::map<std::wstring, std::map<std::wstring, std::wstring>> sections;
    std::wifstream input(path);
    std::wstring line;
    std::wstring section;

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';') {
            continue;
        }

        if (line.front() == L'[' && line.back() == L']') {
            section = trim(line.substr(1, line.size() - 2));
            sections[section];
            continue;
        }

        const auto equals = line.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }

        sections[section][trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
    }

    return sections;
}

inline std::map<std::wstring, std::wstring> read_ini_section(const std::filesystem::path& path, const std::wstring& section_name) {
    std::map<std::wstring, std::wstring> values;
    std::wifstream input(path);
    std::wstring line;
    bool in_section = false;

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';') {
            continue;
        }

        if (line.front() == L'[' && line.back() == L']') {
            in_section = trim(line.substr(1, line.size() - 2)) == section_name;
            continue;
        }

        if (!in_section) {
            continue;
        }

        const auto equals = line.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }

        values[trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
    }

    return values;
}

inline Config load_config(const std::filesystem::path& path, const std::filesystem::path& launcher_dir) {
    Config config;
    config.sections = read_ini_sections(path);
    const auto game_it = config.sections.find(L"Game");
    const auto tweak_it = config.sections.find(L"Tweaks");
    const auto borderless_it = config.sections.find(L"BorderlessWindow");
    const auto& values = game_it != config.sections.end() ? game_it->second : config.sections[L"Game"];
    const auto& tweak_values = tweak_it != config.sections.end() ? tweak_it->second : config.sections[L"Tweaks"];
    const auto& borderless_values = borderless_it != config.sections.end() ? borderless_it->second : config.sections[L"BorderlessWindow"];

    if (auto it = values.find(L"em4_path"); it != values.end() && !it->second.empty()) {
        config.em4_path = it->second;
    } else if (std::filesystem::is_regular_file(launcher_dir / L"em4.exe")) {
        config.em4_path = launcher_dir / L"em4.exe";
    }

    for (const auto& [key, value] : tweak_values) {
        config.tweak_enabled[key] = parse_bool(value, false);
    }

    if (auto it = tweak_values.find(L"borderless_window"); it != tweak_values.end()) {
        config.borderless_enabled = parse_bool(it->second, config.borderless_enabled);
    }
    config.tweak_enabled[L"borderless_window"] = config.borderless_enabled;
    if (auto it = borderless_values.find(L"x"); it != borderless_values.end()) {
        config.borderless_x = parse_int(it->second, config.borderless_x);
    }
    if (auto it = borderless_values.find(L"y"); it != borderless_values.end()) {
        config.borderless_y = parse_int(it->second, config.borderless_y);
    }
    if (auto it = borderless_values.find(L"width"); it != borderless_values.end()) {
        config.borderless_width = parse_int(it->second, config.borderless_width);
    }
    if (auto it = borderless_values.find(L"height"); it != borderless_values.end()) {
        config.borderless_height = parse_int(it->second, config.borderless_height);
    }
    if (auto it = borderless_values.find(L"keep_visible_on_focus_loss"); it != borderless_values.end()) {
        config.keep_visible_on_focus_loss = parse_bool(it->second, config.keep_visible_on_focus_loss);
    }

    return config;
}

inline void sync_legacy_fields_from_sections(Config& config) {
    const auto borderless_it = config.sections.find(L"BorderlessWindow");
    if (borderless_it == config.sections.end()) {
        return;
    }

    const auto& values = borderless_it->second;
    if (auto it = values.find(L"x"); it != values.end()) {
        config.borderless_x = parse_int(it->second, config.borderless_x);
    }
    if (auto it = values.find(L"y"); it != values.end()) {
        config.borderless_y = parse_int(it->second, config.borderless_y);
    }
    if (auto it = values.find(L"width"); it != values.end()) {
        config.borderless_width = parse_int(it->second, config.borderless_width);
    }
    if (auto it = values.find(L"height"); it != values.end()) {
        config.borderless_height = parse_int(it->second, config.borderless_height);
    }
    if (auto it = values.find(L"keep_visible_on_focus_loss"); it != values.end()) {
        config.keep_visible_on_focus_loss = parse_bool(it->second, config.keep_visible_on_focus_loss);
    }
}

inline void write_config(const std::filesystem::path& path, const Config& config) {
    std::wofstream output(path, std::ios::trunc);
    output << L"[Game]\n";
    output << L"em4_path=" << config.em4_path.wstring() << L"\n\n";
    output << L"[Tweaks]\n";
    bool wrote_borderless = false;
    for (const auto& [key, enabled] : config.tweak_enabled) {
        output << key << L"=" << (enabled ? L"1" : L"0") << L"\n";
        if (key == L"borderless_window") {
            wrote_borderless = true;
        }
    }
    if (!wrote_borderless) {
        output << L"borderless_window=" << (config.borderless_enabled ? L"1" : L"0") << L"\n";
    }
    output << L"\n";
    output << L"[BorderlessWindow]\n";
    output << L"x=" << config.borderless_x << L"\n";
    output << L"y=" << config.borderless_y << L"\n";
    output << L"width=" << config.borderless_width << L"\n";
    output << L"height=" << config.borderless_height << L"\n";
    output << L"keep_visible_on_focus_loss=" << (config.keep_visible_on_focus_loss ? L"1" : L"0") << L"\n";

    for (const auto& [section, values] : config.sections) {
        if (section.empty() || section == L"Game" || section == L"Tweaks" || section == L"BorderlessWindow") {
            continue;
        }

        output << L"\n[" << section << L"]\n";
        for (const auto& [key, value] : values) {
            output << key << L"=" << value << L"\n";
        }
    }
}

} // namespace om4t
