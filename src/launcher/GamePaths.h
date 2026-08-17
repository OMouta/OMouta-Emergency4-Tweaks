#pragma once

#include <filesystem>

namespace om4t::launcher {

// Resolves the configured em4.exe path against the launcher folder, accepting a
// directory as well as a file path.
std::filesystem::path resolve_em4_path(std::filesystem::path configured_path, const std::filesystem::path& root);

} // namespace om4t::launcher
