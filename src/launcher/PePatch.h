#pragma once

#include <filesystem>
#include <string>

namespace om4t::launcher {

enum class LargeAddressAwareState {
    Missing,
    Invalid,
    Disabled,
    Enabled,
};

struct LargeAddressAwareInfo {
    LargeAddressAwareState state = LargeAddressAwareState::Missing;
    bool backup_exists = false;
    std::wstring message;
};

std::filesystem::path large_address_backup_path(const std::filesystem::path& exe_path);
LargeAddressAwareInfo inspect_large_address_aware(const std::filesystem::path& exe_path);
bool patch_large_address_aware(const std::filesystem::path& exe_path, std::wstring& error);
bool restore_large_address_backup(const std::filesystem::path& exe_path, std::wstring& error);

} // namespace om4t::launcher
