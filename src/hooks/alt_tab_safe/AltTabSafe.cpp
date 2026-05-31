#include "AltTabSafe.h"

#include <windows.h>
#include <MinHook.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>

namespace {

constexpr const wchar_t* kDataDir = L"OMoutaEM4Tweaks";
constexpr const wchar_t* kLogsDir = L"Logs";
constexpr const wchar_t* kConfigName = L"config.ini";
constexpr const wchar_t* kLogName = L"AltTabSafe.log";

using ChangeDisplaySettingsA_t = LONG(WINAPI*)(DEVMODEA*, DWORD);
using ChangeDisplaySettingsW_t = LONG(WINAPI*)(DEVMODEW*, DWORD);
using ChangeDisplaySettingsExA_t = LONG(WINAPI*)(LPCSTR, DEVMODEA*, HWND, DWORD, LPVOID);
using ChangeDisplaySettingsExW_t = LONG(WINAPI*)(LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID);
using CreateWindowExA_t = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using CreateWindowExW_t = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using SetWindowLongA_t = LONG(WINAPI*)(HWND, int, LONG);
using SetWindowLongW_t = LONG(WINAPI*)(HWND, int, LONG);
using SetWindowLongPtrA_t = LONG_PTR(WINAPI*)(HWND, int, LONG_PTR);
using SetWindowLongPtrW_t = LONG_PTR(WINAPI*)(HWND, int, LONG_PTR);
using SetWindowPos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);

std::wofstream g_log;
std::mutex g_log_mutex;
std::mutex g_window_mutex;
std::atomic<bool> g_force_borderless{true};
std::atomic<bool> g_block_display_changes{true};
HWND g_game_window = nullptr;
thread_local bool g_inside_hook = false;

ChangeDisplaySettingsA_t g_change_display_settings_a = nullptr;
ChangeDisplaySettingsW_t g_change_display_settings_w = nullptr;
ChangeDisplaySettingsExA_t g_change_display_settings_ex_a = nullptr;
ChangeDisplaySettingsExW_t g_change_display_settings_ex_w = nullptr;
CreateWindowExA_t g_create_window_ex_a = nullptr;
CreateWindowExW_t g_create_window_ex_w = nullptr;
SetWindowLongA_t g_set_window_long_a = nullptr;
SetWindowLongW_t g_set_window_long_w = nullptr;
SetWindowLongPtrA_t g_set_window_long_ptr_a = nullptr;
SetWindowLongPtrW_t g_set_window_long_ptr_w = nullptr;
SetWindowPos_t g_set_window_pos = nullptr;

std::wstring pad(WORD value) {
    return value < 10 ? L"0" + std::to_wstring(value) : std::to_wstring(value);
}

std::filesystem::path process_directory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

std::filesystem::path log_directory() {
    return process_directory() / kDataDir / kLogsDir;
}

std::wstring widen(const char* value) {
    if (!value) {
        return L"";
    }

    const int chars = MultiByteToWideChar(CP_ACP, 0, value, -1, nullptr, 0);
    if (chars <= 1) {
        return L"";
    }

    std::wstring result(static_cast<size_t>(chars - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, value, -1, result.data(), chars);
    return result;
}

std::wstring trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return L"";
    }
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parse_bool(const std::wstring& value, bool fallback) {
    const auto v = trim(value);
    if (v == L"1" || v == L"true" || v == L"True" || v == L"TRUE") {
        return true;
    }
    if (v == L"0" || v == L"false" || v == L"False" || v == L"FALSE") {
        return false;
    }
    return fallback;
}

std::map<std::wstring, std::map<std::wstring, std::wstring>> read_ini(const std::filesystem::path& path) {
    std::map<std::wstring, std::map<std::wstring, std::wstring>> sections;
    std::wifstream input(path);
    std::wstring section;
    std::wstring line;

    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';') {
            continue;
        }
        if (line.front() == L'[' && line.back() == L']') {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }
        const auto equals = line.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }
        sections[section][trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
    }

    return sections;
}

void initialize_log() {
    std::filesystem::create_directories(log_directory());
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log.open(log_directory() / kLogName, std::ios::app);
}

void log_line(const std::wstring& message) {
    if (g_inside_hook) {
        return;
    }

    g_inside_hook = true;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log.is_open()) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        g_log << L"[" << st.wYear << L"-" << pad(st.wMonth) << L"-" << pad(st.wDay)
              << L" " << pad(st.wHour) << L":" << pad(st.wMinute) << L":" << pad(st.wSecond)
              << L"] [AltTabSafe] " << message << L"\n";
        g_log.flush();
    }
    g_inside_hook = false;
}

void load_config() {
    const auto sections = read_ini(process_directory() / kDataDir / kConfigName);
    if (auto section = sections.find(L"AltTabSafe"); section != sections.end()) {
        if (auto it = section->second.find(L"force_borderless"); it != section->second.end()) {
            g_force_borderless = parse_bool(it->second, true);
        }
        if (auto it = section->second.find(L"block_display_changes"); it != section->second.end()) {
            g_block_display_changes = parse_bool(it->second, true);
        }
    }
}

std::wstring hex_value(uintptr_t value) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"0x%Ix", value);
    return buffer;
}

RECT desktop_rect_for_window_rect(int x, int y, int width, int height) {
    RECT requested{x, y, x + width, y + height};
    HMONITOR monitor = MonitorFromRect(&requested, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        return info.rcMonitor;
    }
    return RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}

bool looks_like_game_window(HWND hwnd) {
    if (!hwnd || GetParent(hwnd)) {
        return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId()) {
        return false;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        return false;
    }

    return (rect.right - rect.left) >= 320 && (rect.bottom - rect.top) >= 240;
}

bool should_adjust_create_window(HWND parent, DWORD style, int width, int height) {
    if (parent || (style & WS_CHILD) || !(style & WS_POPUP)) {
        return false;
    }

    return width >= 320 && height >= 240;
}

bool is_tracked_game_window(HWND hwnd) {
    std::lock_guard<std::mutex> lock(g_window_mutex);
    return hwnd && hwnd == g_game_window;
}

void remember_game_window(HWND hwnd, const wchar_t* source) {
    if (!g_force_borderless.load() || !looks_like_game_window(hwnd)) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_window_mutex);
    if (!g_game_window) {
        g_game_window = hwnd;
        log_line(std::wstring(L"Tracking game window from ") + source
            + L" hwnd=" + hex_value(reinterpret_cast<uintptr_t>(hwnd)));
    }
}

LONG_PTR borderless_style(LONG_PTR style) {
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    style |= WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_VISIBLE;
    return style;
}

LONG_PTR borderless_ex_style(LONG_PTR ex_style) {
    ex_style &= ~(WS_EX_APPWINDOW | WS_EX_CLIENTEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE);
    return ex_style;
}

void apply_borderless(HWND hwnd, const RECT& rect, const wchar_t* reason) {
    if (!g_force_borderless.load() || !hwnd) {
        return;
    }

    g_inside_hook = true;
    SetWindowLongPtrW(hwnd, GWL_STYLE, borderless_style(GetWindowLongPtrW(hwnd, GWL_STYLE)));
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, borderless_ex_style(GetWindowLongPtrW(hwnd, GWL_EXSTYLE)));
    SetWindowPos(
        hwnd,
        HWND_NOTOPMOST,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    g_inside_hook = false;

    log_line(std::wstring(L"Applied borderless window from ") + reason
        + L" rect=" + std::to_wstring(rect.left) + L"," + std::to_wstring(rect.top)
        + L"," + std::to_wstring(rect.right) + L"," + std::to_wstring(rect.bottom));
}

void adjust_create_params(DWORD& ex_style, DWORD& style, int& x, int& y, int& width, int& height) {
    if (!g_force_borderless.load()) {
        return;
    }

    const RECT rect = desktop_rect_for_window_rect(x, y, width, height);
    ex_style = static_cast<DWORD>(borderless_ex_style(ex_style));
    style = static_cast<DWORD>(borderless_style(style));
    x = rect.left;
    y = rect.top;
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
}

LONG WINAPI hook_change_display_settings_a(DEVMODEA* devmode, DWORD flags) {
    if (g_block_display_changes.load() && devmode) {
        log_line(L"Suppressed ChangeDisplaySettingsA flags=" + std::to_wstring(flags));
        return DISP_CHANGE_SUCCESSFUL;
    }
    return g_change_display_settings_a(devmode, flags);
}

LONG WINAPI hook_change_display_settings_w(DEVMODEW* devmode, DWORD flags) {
    if (g_block_display_changes.load() && devmode) {
        log_line(L"Suppressed ChangeDisplaySettingsW flags=" + std::to_wstring(flags));
        return DISP_CHANGE_SUCCESSFUL;
    }
    return g_change_display_settings_w(devmode, flags);
}

LONG WINAPI hook_change_display_settings_ex_a(LPCSTR device, DEVMODEA* devmode, HWND hwnd, DWORD flags, LPVOID param) {
    if (g_block_display_changes.load() && devmode) {
        log_line(L"Suppressed ChangeDisplaySettingsExA device=\"" + widen(device)
            + L"\" flags=" + std::to_wstring(flags));
        return DISP_CHANGE_SUCCESSFUL;
    }
    return g_change_display_settings_ex_a(device, devmode, hwnd, flags, param);
}

LONG WINAPI hook_change_display_settings_ex_w(LPCWSTR device, DEVMODEW* devmode, HWND hwnd, DWORD flags, LPVOID param) {
    if (g_block_display_changes.load() && devmode) {
        log_line(L"Suppressed ChangeDisplaySettingsExW device=\"" + std::wstring(device ? device : L"")
            + L"\" flags=" + std::to_wstring(flags));
        return DISP_CHANGE_SUCCESSFUL;
    }
    return g_change_display_settings_ex_w(device, devmode, hwnd, flags, param);
}

HWND WINAPI hook_create_window_ex_a(DWORD ex_style, LPCSTR class_name, LPCSTR window_name, DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    const bool should_adjust = should_adjust_create_window(parent, style, width, height);
    if (should_adjust) {
        adjust_create_params(ex_style, style, x, y, width, height);
    }

    HWND hwnd = g_create_window_ex_a(ex_style, class_name, window_name, style, x, y, width, height, parent, menu, instance, param);
    if (should_adjust) {
        remember_game_window(hwnd, L"CreateWindowExA");
        apply_borderless(hwnd, desktop_rect_for_window_rect(x, y, width, height), L"CreateWindowExA");
    }
    return hwnd;
}

HWND WINAPI hook_create_window_ex_w(DWORD ex_style, LPCWSTR class_name, LPCWSTR window_name, DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    const bool should_adjust = should_adjust_create_window(parent, style, width, height);
    if (should_adjust) {
        adjust_create_params(ex_style, style, x, y, width, height);
    }

    HWND hwnd = g_create_window_ex_w(ex_style, class_name, window_name, style, x, y, width, height, parent, menu, instance, param);
    if (should_adjust) {
        remember_game_window(hwnd, L"CreateWindowExW");
        apply_borderless(hwnd, desktop_rect_for_window_rect(x, y, width, height), L"CreateWindowExW");
    }
    return hwnd;
}

LONG WINAPI hook_set_window_long_a(HWND hwnd, int index, LONG value) {
    if (!g_inside_hook && is_tracked_game_window(hwnd)) {
        if (index == GWL_STYLE) {
            value = static_cast<LONG>(borderless_style(value));
        } else if (index == GWL_EXSTYLE) {
            value = static_cast<LONG>(borderless_ex_style(value));
        }
    }
    return g_set_window_long_a(hwnd, index, value);
}

LONG WINAPI hook_set_window_long_w(HWND hwnd, int index, LONG value) {
    if (!g_inside_hook && is_tracked_game_window(hwnd)) {
        if (index == GWL_STYLE) {
            value = static_cast<LONG>(borderless_style(value));
        } else if (index == GWL_EXSTYLE) {
            value = static_cast<LONG>(borderless_ex_style(value));
        }
    }
    return g_set_window_long_w(hwnd, index, value);
}

LONG_PTR WINAPI hook_set_window_long_ptr_a(HWND hwnd, int index, LONG_PTR value) {
    if (!g_inside_hook && is_tracked_game_window(hwnd)) {
        if (index == GWL_STYLE) {
            value = borderless_style(value);
        } else if (index == GWL_EXSTYLE) {
            value = borderless_ex_style(value);
        }
    }
    return g_set_window_long_ptr_a(hwnd, index, value);
}

LONG_PTR WINAPI hook_set_window_long_ptr_w(HWND hwnd, int index, LONG_PTR value) {
    if (!g_inside_hook && is_tracked_game_window(hwnd)) {
        if (index == GWL_STYLE) {
            value = borderless_style(value);
        } else if (index == GWL_EXSTYLE) {
            value = borderless_ex_style(value);
        }
    }
    return g_set_window_long_ptr_w(hwnd, index, value);
}

BOOL WINAPI hook_set_window_pos(HWND hwnd, HWND insert_after, int x, int y, int cx, int cy, UINT flags) {
    if (!g_inside_hook && is_tracked_game_window(hwnd)) {
        RECT current{};
        GetWindowRect(hwnd, &current);
        const RECT rect = desktop_rect_for_window_rect(current.left, current.top, current.right - current.left, current.bottom - current.top);
        insert_after = HWND_NOTOPMOST;
        x = rect.left;
        y = rect.top;
        cx = rect.right - rect.left;
        cy = rect.bottom - rect.top;
        flags &= ~(SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        flags |= SWP_FRAMECHANGED | SWP_NOOWNERZORDER;
    }
    return g_set_window_pos(hwnd, insert_after, x, y, cx, cy, flags);
}

void log_hook_result(const char* name, MH_STATUS status) {
    if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) {
        log_line(L"Hook ready: " + widen(name));
    } else {
        log_line(L"Hook failed: " + widen(name) + L" status=" + std::to_wstring(status));
    }
}

template <typename T>
void create_hook(const wchar_t* module, const char* name, LPVOID hook, T* original) {
    const MH_STATUS status = MH_CreateHookApi(module, name, hook, reinterpret_cast<LPVOID*>(original));
    log_hook_result(name, status);
}

DWORD WINAPI worker_thread(LPVOID) {
    initialize_log();
    load_config();
    log_line(L"DLL loaded");
    log_line(L"Config force_borderless=" + std::to_wstring(g_force_borderless.load())
        + L" block_display_changes=" + std::to_wstring(g_block_display_changes.load()));

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        log_line(L"MinHook initialization failed status=" + std::to_wstring(status));
        return 1;
    }

    create_hook(L"user32.dll", "ChangeDisplaySettingsA", reinterpret_cast<LPVOID>(&hook_change_display_settings_a), &g_change_display_settings_a);
    create_hook(L"user32.dll", "ChangeDisplaySettingsW", reinterpret_cast<LPVOID>(&hook_change_display_settings_w), &g_change_display_settings_w);
    create_hook(L"user32.dll", "ChangeDisplaySettingsExA", reinterpret_cast<LPVOID>(&hook_change_display_settings_ex_a), &g_change_display_settings_ex_a);
    create_hook(L"user32.dll", "ChangeDisplaySettingsExW", reinterpret_cast<LPVOID>(&hook_change_display_settings_ex_w), &g_change_display_settings_ex_w);
    create_hook(L"user32.dll", "CreateWindowExA", reinterpret_cast<LPVOID>(&hook_create_window_ex_a), &g_create_window_ex_a);
    create_hook(L"user32.dll", "CreateWindowExW", reinterpret_cast<LPVOID>(&hook_create_window_ex_w), &g_create_window_ex_w);
    create_hook(L"user32.dll", "SetWindowLongA", reinterpret_cast<LPVOID>(&hook_set_window_long_a), &g_set_window_long_a);
    create_hook(L"user32.dll", "SetWindowLongW", reinterpret_cast<LPVOID>(&hook_set_window_long_w), &g_set_window_long_w);
    create_hook(L"user32.dll", "SetWindowLongPtrA", reinterpret_cast<LPVOID>(&hook_set_window_long_ptr_a), &g_set_window_long_ptr_a);
    create_hook(L"user32.dll", "SetWindowLongPtrW", reinterpret_cast<LPVOID>(&hook_set_window_long_ptr_w), &g_set_window_long_ptr_w);
    create_hook(L"user32.dll", "SetWindowPos", reinterpret_cast<LPVOID>(&hook_set_window_pos), &g_set_window_pos);

    status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        log_line(L"MH_EnableHook failed status=" + std::to_wstring(status));
        return 2;
    }

    log_line(L"Hooks enabled");
    return 0;
}

} // namespace

namespace om4t::hooks::alt_tab_safe {

void start_alt_tab_safe(HMODULE module) {
    DisableThreadLibraryCalls(module);
    HANDLE thread = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    }
}

} // namespace om4t::hooks::alt_tab_safe
