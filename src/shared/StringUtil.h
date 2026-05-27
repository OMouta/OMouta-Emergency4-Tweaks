#pragma once

#include <string>

namespace om4t {

inline std::wstring trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return L"";
    }
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

inline bool parse_bool(const std::wstring& value, bool fallback) {
    const auto v = trim(value);
    if (v == L"1" || v == L"true" || v == L"True" || v == L"TRUE" || v == L"yes") {
        return true;
    }
    if (v == L"0" || v == L"false" || v == L"False" || v == L"FALSE" || v == L"no") {
        return false;
    }
    return fallback;
}

inline int parse_int(const std::wstring& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

} // namespace om4t
