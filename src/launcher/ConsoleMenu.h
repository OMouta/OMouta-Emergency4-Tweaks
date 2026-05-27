#pragma once

#include "../shared/Config.h"
#include "TweakPackage.h"

#include <filesystem>
#include <vector>

namespace om4t::launcher {

enum class MenuResult {
    Launch,
    Cancel
};

MenuResult countdown_menu(Config& config, std::vector<TweakPackage>& packages, const std::filesystem::path& config_path);

} // namespace om4t::launcher
