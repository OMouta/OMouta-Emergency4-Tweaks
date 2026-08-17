#include "GamePaths.h"

#include "../shared/AppPaths.h"

namespace om4t::launcher {

std::filesystem::path resolve_em4_path(std::filesystem::path configured_path, const std::filesystem::path& root) {
    if (configured_path.empty()) {
        configured_path = L"em4.exe";
    }

    if (configured_path.is_relative()) {
        configured_path = root / configured_path;
    }

    if (std::filesystem::is_directory(configured_path)) {
        const auto upper = configured_path / L"Em4.exe";
        if (file_exists(upper)) {
            return std::filesystem::absolute(upper);
        }

        const auto lower = configured_path / L"em4.exe";
        if (file_exists(lower)) {
            return std::filesystem::absolute(lower);
        }
    }

    return std::filesystem::absolute(configured_path);
}

} // namespace om4t::launcher
