#include "GuiLauncher.h"
#include "ProcessLauncher.h"
#include "TweakPackage.h"

#include "../shared/AppPaths.h"
#include "../shared/Config.h"
#include "../shared/Logger.h"

#include <windows.h>
#include <shellapi.h>

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
    MessageBoxW(nullptr, message.c_str(), om4t::kBrand, MB_OK | MB_ICONERROR);
    return static_cast<int>(code);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using namespace om4t;

    const auto root = module_dir();
    ensure_app_layout(root);

    Logger log(log_path(root, kLauncherLogName), L"Launcher");
    log.write(L"Launcher started");

    const auto cfg_path = config_path(root);
    Config config = load_config(cfg_path, root);

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc >= 2 && file_exists(argv[1])) {
        config.em4_path = std::filesystem::absolute(argv[1]);
    }
    if (argv) {
        LocalFree(argv);
    }
    config.em4_path = resolve_em4_path(config.em4_path, root);

    auto packages = launcher::discover_tweak_packages(root, log);
    launcher::apply_config_to_packages(config, packages);
    launcher::sync_config_from_packages(packages, config);
    if (!file_exists(cfg_path)) {
        write_config(cfg_path, config);
    }

    if (launcher::show_launcher_window(config, packages, cfg_path) == launcher::GuiResult::Cancel) {
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
