#include "ConsoleMenu.h"
#include "ProcessLauncher.h"

#include "../shared/AppPaths.h"
#include "../shared/Config.h"
#include "../shared/Logger.h"

#include <windows.h>

#include <iostream>
#include <vector>

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

    const auto legacy_hook = root / kBorderlessDll;
    const auto managed_hook = hooks_dir(root) / kBorderlessDll;
    if (file_exists(legacy_hook) && !file_exists(managed_hook)) {
        std::filesystem::copy_file(legacy_hook, managed_hook, std::filesystem::copy_options::overwrite_existing);
    }

    if (launcher::countdown_menu(config, cfg_path) == launcher::MenuResult::Cancel) {
        log.write(L"Launch cancelled by user");
        return 0;
    }
    write_config(cfg_path, config);

    if (config.em4_path.empty() || !file_exists(config.em4_path)) {
        return fail(log, L"Could not find em4.exe. Edit settings and set the correct path.");
    }

    std::vector<std::filesystem::path> enabled_hooks;
    if (config.borderless_enabled) {
        if (!file_exists(managed_hook)) {
            return fail(log, L"Borderless hook DLL not found: " + managed_hook.wstring(), 2);
        }
        enabled_hooks.push_back(managed_hook);
    }

    if (!launcher::launch_game_with_hooks(config.em4_path, enabled_hooks, log)) {
        return fail(log, L"Failed to launch EM4 with enabled hooks.", 3);
    }

    return 0;
}
