#include "ConsoleMenu.h"
#include "ProcessLauncher.h"
#include "TweakPackage.h"

#include "../shared/AppPaths.h"
#include "../shared/Config.h"
#include "../shared/Logger.h"

#include <windows.h>

#include <fstream>
#include <iostream>

namespace {

std::filesystem::path resolve_em4_path(std::filesystem::path configured_path, const std::filesystem::path& root) {
    if (configured_path.empty()) {
        configured_path = L"em4.exe";
    }

    if (configured_path.is_relative()) {
        configured_path = root / configured_path;
    }

    if (std::filesystem::is_directory(configured_path)) {
        const auto upper = configured_path / L"Em4.exe";
        if (om4t::file_exists(upper)) {
            return std::filesystem::absolute(upper);
        }

        const auto lower = configured_path / L"em4.exe";
        if (om4t::file_exists(lower)) {
            return std::filesystem::absolute(lower);
        }
    }

    return std::filesystem::absolute(configured_path);
}

int fail(om4t::Logger& log, const std::wstring& message, DWORD code = 1) {
    log.write(message);
    std::wcerr << message << L"\n";
    system("pause");
    return static_cast<int>(code);
}

void ensure_builtin_borderless_package(const std::filesystem::path& root) {
    const auto package_dir = om4t::borderless_package_dir(root);
    const auto package_dll = package_dir / om4t::kBorderlessDll;
    const auto manifest = package_dir / om4t::kTweakManifestName;
    const auto flat_hook = om4t::hooks_dir(root) / om4t::kBorderlessDll;
    const auto legacy_root_hook = root / om4t::kBorderlessDll;

    std::filesystem::create_directories(package_dir);

    if (!om4t::file_exists(package_dll)) {
        if (om4t::file_exists(flat_hook)) {
            std::filesystem::copy_file(flat_hook, package_dll, std::filesystem::copy_options::overwrite_existing);
        } else if (om4t::file_exists(legacy_root_hook)) {
            std::filesystem::copy_file(legacy_root_hook, package_dll, std::filesystem::copy_options::overwrite_existing);
        }
    }

    if (om4t::file_exists(package_dll) && !om4t::file_exists(manifest)) {
        std::wofstream output(manifest, std::ios::trunc);
        output << L"[Tweak]\n";
        output << L"id=borderless_window\n";
        output << L"name=Borderless Window Fix\n";
        output << L"description=Runs EM4 in a borderless window and reduces fullscreen-style focus behavior.\n";
        output << L"version=1.0.0\n";
        output << L"dll=BorderlessWindowFix.dll\n";
        output << L"config_key=borderless_window\n";
        output << L"default_enabled=1\n";
        output << L"log=BorderlessWindowFix.log\n";
    }
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    using namespace om4t;

    SetConsoleTitleW(kBrand);
    const auto root = module_dir();
    ensure_app_layout(root);

    Logger log(log_path(root, kLauncherLogName), L"Launcher");
    log.write(L"Launcher started");

    const auto cfg_path = config_path(root);
    Config config = load_config(cfg_path, root);
    if (argc >= 2 && file_exists(argv[1])) {
        config.em4_path = std::filesystem::absolute(argv[1]);
    }
    config.em4_path = resolve_em4_path(config.em4_path, root);

    if (!file_exists(cfg_path)) {
        write_config(cfg_path, config);
    }

    ensure_builtin_borderless_package(root);
    auto packages = launcher::discover_tweak_packages(root, log);
    launcher::apply_config_to_packages(config, packages);
    launcher::sync_config_from_packages(packages, config);

    if (launcher::countdown_menu(config, packages, cfg_path) == launcher::MenuResult::Cancel) {
        log.write(L"Launch cancelled by user");
        return 0;
    }
    launcher::sync_config_from_packages(packages, config);
    write_config(cfg_path, config);

    if (config.em4_path.empty() || !file_exists(config.em4_path)) {
        return fail(log, L"Could not find em4.exe. Edit settings and set the correct path.");
    }

    const auto enabled_hooks = launcher::enabled_hook_paths(packages);

    if (!launcher::launch_game_with_hooks(config.em4_path, enabled_hooks, log)) {
        return fail(log, L"Failed to launch EM4 with enabled hooks.", 3);
    }

    return 0;
}
