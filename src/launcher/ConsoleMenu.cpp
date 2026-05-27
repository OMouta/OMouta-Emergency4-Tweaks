#include "ConsoleMenu.h"

#include "../shared/AppPaths.h"
#include "../shared/StringUtil.h"

#include <windows.h>
#include <conio.h>

#include <iostream>

namespace om4t::launcher {

namespace {

void show_settings(Config& config, const std::filesystem::path& config_path) {
    while (true) {
        std::wcout << L"\nSettings\n";
        std::wcout << L"1. Borderless Window: " << (config.borderless_enabled ? L"Enabled" : L"Disabled") << L"\n";
        std::wcout << L"2. Keep visible on focus loss: " << (config.keep_visible_on_focus_loss ? L"Enabled" : L"Disabled") << L"\n";
        std::wcout << L"3. Window rectangle: " << config.borderless_x << L"," << config.borderless_y << L" "
                   << config.borderless_width << L"x" << config.borderless_height << L"\n";
        std::wcout << L"4. EM4 path: " << config.em4_path.wstring() << L"\n";
        std::wcout << L"S. Save and return\n";
        std::wcout << L"Choice: ";

        std::wstring choice;
        std::getline(std::wcin, choice);
        choice = trim(choice);

        if (choice == L"1") {
            config.borderless_enabled = !config.borderless_enabled;
        } else if (choice == L"2") {
            config.keep_visible_on_focus_loss = !config.keep_visible_on_focus_loss;
        } else if (choice == L"3") {
            std::wcout << L"Enter x y width height: ";
            std::wcin >> config.borderless_x >> config.borderless_y >> config.borderless_width >> config.borderless_height;
            std::wcin.ignore(32767, L'\n');
        } else if (choice == L"4") {
            std::wcout << L"Enter full path to em4.exe: ";
            std::wstring path;
            std::getline(std::wcin, path);
            config.em4_path = trim(path);
        } else if (choice == L"S" || choice == L"s") {
            write_config(config_path, config);
            return;
        }
    }
}

} // namespace

MenuResult countdown_menu(Config& config, const std::filesystem::path& config_path) {
    for (int remaining = 5; remaining > 0; --remaining) {
        system("cls");
        std::wcout << kBrand << L"\n";
        std::wcout << L"Launching EM4 in " << remaining << L" seconds...\n\n";
        std::wcout << L"Borderless Window: " << (config.borderless_enabled ? L"Enabled" : L"Disabled") << L"\n";
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
                    show_settings(config, config_path);
                    return countdown_menu(config, config_path);
                }
            }
            Sleep(100);
        }
    }
    return MenuResult::Launch;
}

} // namespace om4t::launcher
