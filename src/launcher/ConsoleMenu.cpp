#include "ConsoleMenu.h"

#include "../shared/AppPaths.h"
#include "../shared/StringUtil.h"

#include <windows.h>
#include <conio.h>

#include <iostream>

namespace om4t::launcher {

namespace {

void show_tweaks(const std::vector<TweakPackage>& packages) {
    for (size_t i = 0; i < packages.size(); ++i) {
        const auto& package = packages[i];
        std::wcout << (i + 1) << L". " << package.name << L": " << (package.enabled ? L"Enabled" : L"Disabled") << L"\n";
        if (!package.description.empty()) {
            std::wcout << L"   " << package.description << L"\n";
        }
    }
}

std::wstring setting_value(const Config& config, const TweakSetting& setting) {
    if (auto section = config.sections.find(setting.section); section != config.sections.end()) {
        if (auto value = section->second.find(setting.key); value != section->second.end()) {
            return value->second;
        }
    }
    return setting.default_value;
}

void show_settings(Config& config, std::vector<TweakPackage>& packages, const std::filesystem::path& config_path) {
    while (true) {
        std::wcout << L"\nSettings\n";
        show_tweaks(packages);
        std::wcout << L"T. Toggle a tweak\n";
        std::wcout << L"G. Change a tweak setting\n";
        std::wcout << L"P. EM4 path: " << config.em4_path.wstring() << L"\n";
        std::wcout << L"S. Save and return\n";
        std::wcout << L"Choice: ";

        std::wstring choice;
        std::getline(std::wcin, choice);
        choice = trim(choice);

        if (choice == L"T" || choice == L"t") {
            std::wcout << L"Enter tweak number: ";
            std::wstring tweak_choice;
            std::getline(std::wcin, tweak_choice);
            const int tweak_number = parse_int(trim(tweak_choice), 0);
            if (tweak_number >= 1 && static_cast<size_t>(tweak_number) <= packages.size()) {
                auto& package = packages[static_cast<size_t>(tweak_number - 1)];
                package.enabled = !package.enabled;
                sync_config_from_packages(packages, config);
            }
        } else if (choice == L"G" || choice == L"g") {
            for (size_t i = 0; i < packages.size(); ++i) {
                for (size_t j = 0; j < packages[i].settings.size(); ++j) {
                    const auto& setting = packages[i].settings[j];
                    std::wcout << (i + 1) << L"." << (j + 1) << L" " << packages[i].name
                               << L" - " << setting.label << L": " << setting_value(config, setting) << L"\n";
                }
            }
            std::wcout << L"Enter tweak number and setting number: ";
            size_t package_number = 0;
            size_t setting_number = 0;
            std::wcin >> package_number >> setting_number;
            std::wcin.ignore(32767, L'\n');
            if (package_number >= 1 && package_number <= packages.size()
                && setting_number >= 1 && setting_number <= packages[package_number - 1].settings.size()) {
                const auto& setting = packages[package_number - 1].settings[setting_number - 1];
                std::wcout << L"Enter value: ";
                std::wstring value;
                std::getline(std::wcin, value);
                config.sections[setting.section][setting.key] = trim(value);
                sync_legacy_fields_from_sections(config);
            }
        } else if (choice == L"P" || choice == L"p") {
            std::wcout << L"Enter full path to em4.exe: ";
            std::wstring path;
            std::getline(std::wcin, path);
            config.em4_path = trim(path);
        } else if (choice == L"S" || choice == L"s") {
            sync_config_from_packages(packages, config);
            write_config(config_path, config);
            return;
        }
    }
}

} // namespace

MenuResult countdown_menu(Config& config, std::vector<TweakPackage>& packages, const std::filesystem::path& config_path) {
    for (int remaining = 5; remaining > 0; --remaining) {
        system("cls");
        std::wcout << kBrand << L"\n";
        std::wcout << L"Launching EM4 in " << remaining << L" seconds...\n\n";
        if (packages.empty()) {
            std::wcout << L"Tweaks: none found\n";
        } else {
            std::wcout << L"Tweaks:\n";
            show_tweaks(packages);
        }
        std::wcout << L"EM4: " << config.em4_path.wstring() << L"\n\n";
        std::wcout << L"[L] Launch now   [E] Edit settings   [C] Cancel\n";

        for (int tick = 0; tick < 10; ++tick) {
            if (_kbhit()) {
                const int key = _getwch();
                if (key == L'L' || key == L'l') {
                    return MenuResult::Launch;
                }
                if (key == L'C' || key == L'c' || key == 27) {
                    return MenuResult::Cancel;
                }
                if (key == L'E' || key == L'e') {
                    show_settings(config, packages, config_path);
                    return countdown_menu(config, packages, config_path);
                }
            }
            Sleep(100);
        }
    }
    return MenuResult::Launch;
}

} // namespace om4t::launcher
