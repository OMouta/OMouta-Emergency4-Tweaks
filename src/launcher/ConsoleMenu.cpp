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

void show_settings(Config& config, std::vector<TweakPackage>& packages, const std::filesystem::path& config_path) {
    while (true) {
        std::wcout << L"\nSettings\n";
        show_tweaks(packages);
        std::wcout << L"T. Toggle a tweak\n";
        std::wcout << L"F. Keep visible on focus loss: " << (config.keep_visible_on_focus_loss ? L"Enabled" : L"Disabled") << L"\n";
        std::wcout << L"R. Window rectangle: " << config.borderless_x << L"," << config.borderless_y << L" "
                   << config.borderless_width << L"x" << config.borderless_height << L"\n";
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
        } else if (choice == L"F" || choice == L"f") {
            config.keep_visible_on_focus_loss = !config.keep_visible_on_focus_loss;
        } else if (choice == L"R" || choice == L"r") {
            std::wcout << L"Enter x y width height: ";
            std::wcin >> config.borderless_x >> config.borderless_y >> config.borderless_width >> config.borderless_height;
            std::wcin.ignore(32767, L'\n');
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
