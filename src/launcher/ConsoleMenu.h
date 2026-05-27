#pragma once

#include "../shared/Config.h"

#include <filesystem>

namespace om4t::launcher {

enum class MenuResult {
    Launch,
    Cancel
};

MenuResult countdown_menu(Config& config, const std::filesystem::path& config_path);

} // namespace om4t::launcher
