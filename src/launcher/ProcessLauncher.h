#pragma once

#include "../shared/Logger.h"

#include <filesystem>
#include <vector>

namespace om4t::launcher {

bool launch_game_with_hooks(
    const std::filesystem::path& em4_path,
    const std::vector<std::filesystem::path>& enabled_hooks,
    Logger& log);

// Starts the EM4 editor, which is the game executable run with "-editor". No hooks
// are injected: the tweak packages target the game, not the editor.
bool launch_editor(const std::filesystem::path& em4_path, Logger& log);

} // namespace om4t::launcher
