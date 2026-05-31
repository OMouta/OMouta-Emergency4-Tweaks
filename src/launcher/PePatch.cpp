#include "PePatch.h"

#include <windows.h>

#include <cstdint>
#include <fstream>
#include <system_error>
#include <vector>

namespace om4t::launcher {

namespace {

constexpr uint16_t kDosSignature = 0x5A4D;
constexpr uint32_t kPeSignature = 0x00004550;
constexpr uint16_t kLargeAddressAware = 0x0020;

struct PeLocation {
    uint64_t characteristics_offset = 0;
    uint16_t characteristics = 0;
};

bool read_location(const std::filesystem::path& exe_path, PeLocation& location, std::wstring& error) {
    std::ifstream input(exe_path, std::ios::binary);
    if (!input) {
        error = L"Could not open executable.";
        return false;
    }

    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0x40) {
        error = L"Executable is too small to be a valid PE file.";
        return false;
    }
    input.seekg(0, std::ios::beg);

    uint16_t dos_signature = 0;
    input.read(reinterpret_cast<char*>(&dos_signature), sizeof(dos_signature));
    if (dos_signature != kDosSignature) {
        error = L"Executable is missing the MZ header.";
        return false;
    }

    input.seekg(0x3C, std::ios::beg);
    int32_t pe_offset = 0;
    input.read(reinterpret_cast<char*>(&pe_offset), sizeof(pe_offset));
    if (pe_offset <= 0 || static_cast<uint64_t>(pe_offset) + 24 > static_cast<uint64_t>(size)) {
        error = L"Executable has an invalid PE header offset.";
        return false;
    }

    input.seekg(pe_offset, std::ios::beg);
    uint32_t pe_signature = 0;
    input.read(reinterpret_cast<char*>(&pe_signature), sizeof(pe_signature));
    if (pe_signature != kPeSignature) {
        error = L"Executable is missing the PE header.";
        return false;
    }

    constexpr uint64_t characteristics_from_pe = 4 + 18;
    location.characteristics_offset = static_cast<uint64_t>(pe_offset) + characteristics_from_pe;
    input.seekg(static_cast<std::streamoff>(location.characteristics_offset), std::ios::beg);
    input.read(reinterpret_cast<char*>(&location.characteristics), sizeof(location.characteristics));
    if (!input) {
        error = L"Could not read PE characteristics.";
        return false;
    }

    return true;
}

bool write_characteristics(const std::filesystem::path& exe_path, uint64_t offset, uint16_t value, std::wstring& error) {
    std::fstream file(exe_path, std::ios::binary | std::ios::in | std::ios::out);
    if (!file) {
        error = L"Could not open executable for writing. Close the game and try running the launcher as administrator.";
        return false;
    }

    file.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    file.write(reinterpret_cast<const char*>(&value), sizeof(value));
    if (!file) {
        error = L"Could not write PE characteristics.";
        return false;
    }

    return true;
}

} // namespace

std::filesystem::path large_address_backup_path(const std::filesystem::path& exe_path) {
    return exe_path.wstring() + L".omouta-4gb-backup";
}

LargeAddressAwareInfo inspect_large_address_aware(const std::filesystem::path& exe_path) {
    LargeAddressAwareInfo info;
    std::error_code ec;
    info.backup_exists = std::filesystem::is_regular_file(large_address_backup_path(exe_path), ec);

    if (!std::filesystem::is_regular_file(exe_path, ec)) {
        info.state = LargeAddressAwareState::Missing;
        info.message = L"em4.exe was not found.";
        return info;
    }

    PeLocation location;
    std::wstring error;
    if (!read_location(exe_path, location, error)) {
        info.state = LargeAddressAwareState::Invalid;
        info.message = error;
        return info;
    }

    const bool enabled = (location.characteristics & kLargeAddressAware) != 0;
    info.state = enabled ? LargeAddressAwareState::Enabled : LargeAddressAwareState::Disabled;
    info.message = enabled ? L"4 GB patch is applied." : L"4 GB patch is not applied.";
    return info;
}

bool patch_large_address_aware(const std::filesystem::path& exe_path, std::wstring& error) {
    PeLocation location;
    if (!read_location(exe_path, location, error)) {
        return false;
    }

    const auto backup_path = large_address_backup_path(exe_path);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(backup_path, ec)) {
        std::filesystem::copy_file(exe_path, backup_path, std::filesystem::copy_options::none, ec);
        if (ec) {
            error = L"Could not create backup: " + backup_path.wstring();
            return false;
        }
    }

    const uint16_t patched = location.characteristics | kLargeAddressAware;
    return write_characteristics(exe_path, location.characteristics_offset, patched, error);
}

bool restore_large_address_backup(const std::filesystem::path& exe_path, std::wstring& error) {
    const auto backup_path = large_address_backup_path(exe_path);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(backup_path, ec)) {
        error = L"No backup exists for this executable.";
        return false;
    }

    std::filesystem::copy_file(backup_path, exe_path, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        error = L"Could not restore backup. Close the game and try running the launcher as administrator.";
        return false;
    }

    return true;
}

} // namespace om4t::launcher
