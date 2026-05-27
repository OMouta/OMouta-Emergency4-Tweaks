#pragma once

#include "../shared/Logger.h"

#include <filesystem>
#include <vector>

namespace om4t::launcher {

bool launch_game_with_hooks(
    const std::filesystem::path& em4_path,
    const std::vector<std::filesystem::path>& enabled_hooks,
    Logger& log);

} // namespace om4t::launcher
