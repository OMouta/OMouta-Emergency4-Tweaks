#pragma once

#include "TweakPackage.h"

#include "../shared/Config.h"

#include <filesystem>
#include <vector>

namespace om4t::launcher {

enum class GuiResult {
    PlayGame,
    OpenEditor,
    Cancel
};

// Shows the splash screen. Returns what the user picked; `config` and `packages`
// reflect any changes saved from the settings screen.
GuiResult show_launcher_window(
    Config& config,
    std::vector<TweakPackage>& packages,
    const std::filesystem::path& config_path);

} // namespace om4t::launcher
