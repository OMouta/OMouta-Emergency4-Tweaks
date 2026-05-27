#pragma once

#include "TweakPackage.h"

#include "../shared/Config.h"

#include <filesystem>
#include <vector>

namespace om4t::launcher {

enum class GuiResult {
    Launch,
    Cancel
};

GuiResult show_launcher_window(
    Config& config,
    std::vector<TweakPackage>& packages,
    const std::filesystem::path& config_path);

} // namespace om4t::launcher
