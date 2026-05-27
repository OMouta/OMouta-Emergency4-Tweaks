#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

namespace {

constexpr int kClientWidth = 1920;
constexpr int kClientHeight = 1080;
constexpr int kWindowX = 0;
constexpr int kWindowY = 0;
constexpr DWORD kWatchdogDelayMs = 750;
constexpr const wchar_t* kDataDir = L"OMoutaEM4Tweaks";
constexpr const wchar_t* kLogsDir = L"Logs";
constexpr const wchar_t* kConfigName = L"config.ini";
constexpr const wchar_t* kLogName = L"BorderlessWindowFix.log";

std::wofstream g_log;
std::mutex g_log_mutex;
std::atomic<bool> g_hooks_ready{false};
std::atomic<bool> g_in_apply{false};

using ChangeDisplaySettingsA_t = LONG(WINAPI*)(DEVMODEA*, DWORD);
using ChangeDisplaySettingsW_t = LONG(WINAPI*)(DEVMODEW*, DWORD);
using ChangeDisplaySettingsExA_t = LONG(WINAPI*)(LPCSTR, DEVMODEA*, HWND, DWORD, LPVOID);
using ChangeDisplaySettingsExW_t = LONG(WINAPI*)(LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID);
using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using DirectInputCreateDeviceA_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8A*, REFGUID, LPDIRECTINPUTDEVICE8A*, LPUNKNOWN);
using DirectInputCreateDeviceW_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8W*, REFGUID, LPDIRECTINPUTDEVICE8W*, LPUNKNOWN);
using DirectInputSetCooperativeLevelA_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*, HWND, DWORD);
using DirectInputSetCooperativeLevelW_t = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8W*, HWND, DWORD);
using CreateWindowExA_t = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using CreateWindowExW_t = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using SetWindowLongA_t = LONG(WINAPI*)(HWND, int, LONG);
using SetWindowLongW_t = LONG(WINAPI*)(HWND, int, LONG);
using SetWindowLongPtrA_t = LONG_PTR(WINAPI*)(HWND, int, LONG_PTR);
using SetWindowLongPtrW_t = LONG_PTR(WINAPI*)(HWND, int, LONG_PTR);
using SetWindowPos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
using MoveWindow_t = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
using ShowWindow_t = BOOL(WINAPI*)(HWND, int);
using ClipCursor_t = BOOL(WINAPI*)(const RECT*);

ChangeDisplaySettingsA_t g_change_display_settings_a = nullptr;
ChangeDisplaySettingsW_t g_change_display_settings_w = nullptr;
ChangeDisplaySettingsExA_t g_change_display_settings_ex_a = nullptr;
ChangeDisplaySettingsExW_t g_change_display_settings_ex_w = nullptr;
DirectInput8Create_t g_direct_input_8_create = nullptr;
DirectInputCreateDeviceA_t g_direct_input_create_device_a = nullptr;
DirectInputCreateDeviceW_t g_direct_input_create_device_w = nullptr;
DirectInputSetCooperativeLevelA_t g_direct_input_set_cooperative_level_a = nullptr;
DirectInputSetCooperativeLevelW_t g_direct_input_set_cooperative_level_w = nullptr;
CreateWindowExA_t g_create_window_ex_a = nullptr;
CreateWindowExW_t g_create_window_ex_w = nullptr;
SetWindowLongA_t g_set_window_long_a = nullptr;
SetWindowLongW_t g_set_window_long_w = nullptr;
SetWindowLongPtrA_t g_set_window_long_ptr_a = nullptr;
SetWindowLongPtrW_t g_set_window_long_ptr_w = nullptr;
SetWindowPos_t g_set_window_pos = nullptr;
MoveWindow_t g_move_window = nullptr;
ShowWindow_t g_show_window = nullptr;
ClipCursor_t g_clip_cursor = nullptr;
std::atomic<HWND> g_main_window{nullptr};
std::atomic<bool> g_direct_input_create_hooked{false};
std::atomic<bool> g_direct_input_device_a_hooked{false};
std::atomic<bool> g_direct_input_device_w_hooked{false};
int g_borderless_x = kWindowX;
int g_borderless_y = kWindowY;
int g_borderless_width = kClientWidth;
int g_borderless_height = kClientHeight;
bool g_keep_visible_on_focus_loss = true;

HRESULT STDMETHODCALLTYPE hook_direct_input_create_device_a(IDirectInput8A* self, REFGUID guid, LPDIRECTINPUTDEVICE8A* device, LPUNKNOWN outer);
HRESULT STDMETHODCALLTYPE hook_direct_input_create_device_w(IDirectInput8W* self, REFGUID guid, LPDIRECTINPUTDEVICE8W* device, LPUNKNOWN outer);
HRESULT STDMETHODCALLTYPE hook_direct_input_set_cooperative_level_a(IDirectInputDevice8A* self, HWND hwnd, DWORD flags);
HRESULT STDMETHODCALLTYPE hook_direct_input_set_cooperative_level_w(IDirectInputDevice8W* self, HWND hwnd, DWORD flags);
bool is_known_game_window(HWND hwnd);

std::wstring pad(WORD value) {
    return value < 10 ? L"0" + std::to_wstring(value) : std::to_wstring(value);
}

void log_line(const std::wstring& message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (!g_log.is_open()) {
        return;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    g_log << L"[" << st.wYear << L"-" << pad(st.wMonth) << L"-" << pad(st.wDay)
          << L" " << pad(st.wHour) << L":" << pad(st.wMinute) << L":" << pad(st.wSecond)
          << L"] [BorderlessWindowFix] " << message << L"\n";
    g_log.flush();
}

void log_last_error(const std::wstring& context) {
    log_line(context + L" GetLastError=" + std::to_wstring(GetLastError()));
}

std::wstring hex_value(uintptr_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
}

std::wstring module_directory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring result(path);
    const auto slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : result.substr(0, slash);
}

std::filesystem::path process_directory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

void initialize_log() {
    const auto root = process_directory() / kDataDir;
    std::filesystem::create_directories(root / kLogsDir);
    const auto path = root / kLogsDir / kLogName;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log.open(path, std::ios::app);
}

std::wstring trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return L"";
    }
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::map<std::wstring, std::wstring> read_ini(const std::filesystem::path& path) {
    std::map<std::wstring, std::wstring> values;
    std::wifstream input(path);
    std::wstring line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == L'#' || line[0] == L';' || line[0] == L'[') {
            continue;
        }
        const auto equals = line.find(L'=');
        if (equals == std::wstring::npos) {
            continue;
        }
        values[trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
    }
    return values;
}

int parse_int(const std::wstring& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
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

void load_config() {
    const auto config_path = process_directory() / kDataDir / kConfigName;
    const auto values = read_ini(config_path);
    if (auto it = values.find(L"x"); it != values.end()) {
        g_borderless_x = parse_int(it->second, g_borderless_x);
    }
    if (auto it = values.find(L"y"); it != values.end()) {
        g_borderless_y = parse_int(it->second, g_borderless_y);
    }
    if (auto it = values.find(L"width"); it != values.end()) {
        g_borderless_width = parse_int(it->second, g_borderless_width);
    }
    if (auto it = values.find(L"height"); it != values.end()) {
        g_borderless_height = parse_int(it->second, g_borderless_height);
    }
    if (auto it = values.find(L"keep_visible_on_focus_loss"); it != values.end()) {
        g_keep_visible_on_focus_loss = parse_bool(it->second, g_keep_visible_on_focus_loss);
    }
}

bool is_eligible_window(HWND hwnd) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    return pid == GetCurrentProcessId()
        && GetAncestor(hwnd, GA_ROOT) == hwnd
        && IsWindowVisible(hwnd) == TRUE;
}

bool is_correctable_window(HWND hwnd) {
    return is_eligible_window(hwnd) || is_known_game_window(hwnd);
}

DWORD forced_style(DWORD style) {
    return (style & ~(WS_OVERLAPPEDWINDOW | WS_MINIMIZE | WS_MAXIMIZE)) | WS_POPUP | WS_VISIBLE;
}

DWORD forced_ex_style(DWORD ex_style) {
    return (ex_style & ~WS_EX_TOPMOST) | WS_EX_APPWINDOW;
}

BOOL call_original_set_window_pos(HWND hwnd, HWND insert_after, int x, int y, int cx, int cy, UINT flags) {
    if (g_set_window_pos) {
        return g_set_window_pos(hwnd, insert_after, x, y, cx, cy, flags);
    }

    return SetWindowPos(hwnd, insert_after, x, y, cx, cy, flags);
}

SIZE forced_outer_size(DWORD style, DWORD ex_style) {
    UNREFERENCED_PARAMETER(style);
    UNREFERENCED_PARAMETER(ex_style);
    return SIZE{g_borderless_width, g_borderless_height};
}

void apply_window_fix(HWND hwnd, const wchar_t* reason, bool allow_initial_position) {
    if (!hwnd || g_in_apply.exchange(true)) {
        return;
    }

    if (!is_correctable_window(hwnd)) {
        g_in_apply = false;
        return;
    }

    if (IsIconic(hwnd)) {
        log_line(L"Known EM4 window was minimized; restoring for borderless mode");
        if (g_keep_visible_on_focus_loss) {
            ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        }
    }

    log_line(std::wstring(L"Window handle detected: ") + std::to_wstring(reinterpret_cast<uintptr_t>(hwnd)) + L" reason=" + reason);
    g_main_window.store(hwnd);

    const LONG_PTR original_style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR original_ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    log_line(L"Original window style: " + hex_value(static_cast<uintptr_t>(original_style)));
    log_line(L"Original extended window style: " + hex_value(static_cast<uintptr_t>(original_ex_style)));

    const LONG_PTR new_style = forced_style(static_cast<DWORD>(original_style));
    const LONG_PTR new_ex_style = forced_ex_style(static_cast<DWORD>(original_ex_style));

    if (new_style != original_style && SetWindowLongPtrW(hwnd, GWL_STYLE, new_style) == 0) {
        const DWORD err = GetLastError();
        if (err != 0) {
            log_line(L"SetWindowLongPtrW(GWL_STYLE) failed. GetLastError=" + std::to_wstring(err));
        }
    }

    if (new_ex_style != original_ex_style && SetWindowLongPtrW(hwnd, GWL_EXSTYLE, new_ex_style) == 0) {
        const DWORD err = GetLastError();
        if (err != 0) {
            log_line(L"SetWindowLongPtrW(GWL_EXSTYLE) failed. GetLastError=" + std::to_wstring(err));
        }
    }

    log_line(L"Modified window style: " + hex_value(static_cast<uintptr_t>(new_style)));
    log_line(L"Modified extended window style: " + hex_value(static_cast<uintptr_t>(new_ex_style)));

    const SIZE size = forced_outer_size(static_cast<DWORD>(new_style), static_cast<DWORD>(new_ex_style));
    RECT current_rect{};
    GetWindowRect(hwnd, &current_rect);

    const bool size_changed = (current_rect.right - current_rect.left) != size.cx
        || (current_rect.bottom - current_rect.top) != size.cy;
    const bool style_changed = new_style != original_style || new_ex_style != original_ex_style;
    const UINT move_flag = allow_initial_position ? 0 : SWP_NOMOVE;
    const int x = allow_initial_position ? g_borderless_x : current_rect.left;
    const int y = allow_initial_position ? g_borderless_y : current_rect.top;

    if (!style_changed && !size_changed && !allow_initial_position) {
        g_in_apply = false;
        return;
    }

    UINT set_pos_flags = SWP_FRAMECHANGED | move_flag;
    if (allow_initial_position) {
        set_pos_flags |= SWP_SHOWWINDOW;
    } else {
        set_pos_flags |= SWP_NOACTIVATE;
    }

    const BOOL positioned = call_original_set_window_pos(
        hwnd,
        HWND_NOTOPMOST,
        x,
        y,
        size.cx,
        size.cy,
        set_pos_flags);

    log_line(L"SetWindowPos applied: " + std::to_wstring(positioned));
    if (!positioned) {
        log_last_error(L"SetWindowPos failed while applying window fix.");
    }

    g_in_apply = false;
}

bool is_known_game_window(HWND hwnd) {
    return hwnd && hwnd == g_main_window.load();
}

BOOL CALLBACK enum_windows_proc(HWND hwnd, LPARAM) {
    if (is_correctable_window(hwnd)) {
        apply_window_fix(hwnd, L"watchdog", false);
    }
    return TRUE;
}

LONG WINAPI hook_change_display_settings_a(DEVMODEA*, DWORD) {
    log_line(L"ChangeDisplaySettings call blocked: A");
    return DISP_CHANGE_SUCCESSFUL;
}

LONG WINAPI hook_change_display_settings_w(DEVMODEW*, DWORD) {
    log_line(L"ChangeDisplaySettings call blocked: W");
    return DISP_CHANGE_SUCCESSFUL;
}

LONG WINAPI hook_change_display_settings_ex_a(LPCSTR, DEVMODEA*, HWND, DWORD, LPVOID) {
    log_line(L"ChangeDisplaySettings call blocked: ExA");
    return DISP_CHANGE_SUCCESSFUL;
}

LONG WINAPI hook_change_display_settings_ex_w(LPCWSTR, DEVMODEW*, HWND, DWORD, LPVOID) {
    log_line(L"ChangeDisplaySettings call blocked: ExW");
    return DISP_CHANGE_SUCCESSFUL;
}

HWND WINAPI hook_create_window_ex_a(DWORD ex_style, LPCSTR class_name, LPCSTR window_name, DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    log_line(L"CreateWindowEx call intercepted: A");
    HWND hwnd = g_create_window_ex_a(ex_style, class_name, window_name, style, x, y, width, height, parent, menu, instance, param);
    if (is_correctable_window(hwnd)) {
        apply_window_fix(hwnd, L"CreateWindowExA", true);
    }
    return hwnd;
}

HWND WINAPI hook_create_window_ex_w(DWORD ex_style, LPCWSTR class_name, LPCWSTR window_name, DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    log_line(L"CreateWindowEx call intercepted: W");
    HWND hwnd = g_create_window_ex_w(ex_style, class_name, window_name, style, x, y, width, height, parent, menu, instance, param);
    if (is_correctable_window(hwnd)) {
        apply_window_fix(hwnd, L"CreateWindowExW", true);
    }
    return hwnd;
}

LONG WINAPI hook_set_window_long_a(HWND hwnd, int index, LONG value) {
    if (is_known_game_window(hwnd) && index == GWL_STYLE) {
        value = static_cast<LONG>(forced_style(static_cast<DWORD>(value)));
        log_line(L"SetWindowLongA style value forced before original call");
    } else if (is_known_game_window(hwnd) && index == GWL_EXSTYLE) {
        value = static_cast<LONG>(forced_ex_style(static_cast<DWORD>(value)));
        log_line(L"SetWindowLongA ex-style value forced before original call");
    }

    LONG result = g_set_window_long_a(hwnd, index, value);
    if (index == GWL_STYLE || index == GWL_EXSTYLE) {
        apply_window_fix(hwnd, L"SetWindowLongA", false);
    }
    return result;
}

LONG WINAPI hook_set_window_long_w(HWND hwnd, int index, LONG value) {
    if (is_known_game_window(hwnd) && index == GWL_STYLE) {
        value = static_cast<LONG>(forced_style(static_cast<DWORD>(value)));
        log_line(L"SetWindowLongW style value forced before original call");
    } else if (is_known_game_window(hwnd) && index == GWL_EXSTYLE) {
        value = static_cast<LONG>(forced_ex_style(static_cast<DWORD>(value)));
        log_line(L"SetWindowLongW ex-style value forced before original call");
    }

    LONG result = g_set_window_long_w(hwnd, index, value);
    if (index == GWL_STYLE || index == GWL_EXSTYLE) {
        apply_window_fix(hwnd, L"SetWindowLongW", false);
    }
    return result;
}

LONG_PTR WINAPI hook_set_window_long_ptr_a(HWND hwnd, int index, LONG_PTR value) {
    if (is_known_game_window(hwnd) && index == GWL_STYLE) {
        value = static_cast<LONG_PTR>(forced_style(static_cast<DWORD>(value)));
        log_line(L"SetWindowLongPtrA style value forced before original call");
    } else if (is_known_game_window(hwnd) && index == GWL_EXSTYLE) {
        value = static_cast<LONG_PTR>(forced_ex_style(static_cast<DWORD>(value)));
        log_line(L"SetWindowLongPtrA ex-style value forced before original call");
    }

    LONG_PTR result = g_set_window_long_ptr_a(hwnd, index, value);
    if (index == GWL_STYLE || index == GWL_EXSTYLE) {
        apply_window_fix(hwnd, L"SetWindowLongPtrA", false);
    }
    return result;
}

LONG_PTR WINAPI hook_set_window_long_ptr_w(HWND hwnd, int index, LONG_PTR value) {
    if (is_known_game_window(hwnd) && index == GWL_STYLE) {
        value = static_cast<LONG_PTR>(forced_style(static_cast<DWORD>(value)));
        log_line(L"SetWindowLongPtrW style value forced before original call");
    } else if (is_known_game_window(hwnd) && index == GWL_EXSTYLE) {
        value = static_cast<LONG_PTR>(forced_ex_style(static_cast<DWORD>(value)));
        log_line(L"SetWindowLongPtrW ex-style value forced before original call");
    }

    LONG_PTR result = g_set_window_long_ptr_w(hwnd, index, value);
    if (index == GWL_STYLE || index == GWL_EXSTYLE) {
        apply_window_fix(hwnd, L"SetWindowLongPtrW", false);
    }
    return result;
}

BOOL WINAPI hook_set_window_pos(HWND hwnd, HWND insert_after, int x, int y, int cx, int cy, UINT flags) {
    if (insert_after == HWND_TOPMOST) {
        log_line(L"SetWindowPos HWND_TOPMOST replaced with HWND_NOTOPMOST");
        insert_after = HWND_NOTOPMOST;
    }

    if (g_keep_visible_on_focus_loss && is_known_game_window(hwnd)) {
        if ((flags & SWP_HIDEWINDOW) != 0) {
            log_line(L"SetWindowPos SWP_HIDEWINDOW blocked");
            flags &= ~SWP_HIDEWINDOW;
            flags |= SWP_SHOWWINDOW;
        }
    }

    BOOL result = g_set_window_pos(hwnd, insert_after, x, y, cx, cy, flags);
    if (is_known_game_window(hwnd) || is_eligible_window(hwnd)) {
        apply_window_fix(hwnd, L"SetWindowPos", false);
    }
    return result;
}

BOOL WINAPI hook_move_window(HWND hwnd, int x, int y, int width, int height, BOOL repaint) {
    log_line(L"MoveWindow intercepted");
    BOOL result = g_move_window(hwnd, x, y, width, height, repaint);
    apply_window_fix(hwnd, L"MoveWindow", false);
    return result;
}

BOOL WINAPI hook_show_window(HWND hwnd, int command) {
    if (g_keep_visible_on_focus_loss
        && is_known_game_window(hwnd)
        && (command == SW_HIDE
            || command == SW_MINIMIZE
            || command == SW_SHOWMINIMIZED
            || command == SW_FORCEMINIMIZE
            || command == SW_SHOWMINNOACTIVE)) {
        log_line(L"ShowWindow hide/minimize blocked for borderless mode");
        apply_window_fix(hwnd, L"ShowWindow hide/minimize block", false);
        return TRUE;
    }

    log_line(L"ShowWindow intercepted");
    BOOL result = g_show_window(hwnd, command);
    apply_window_fix(hwnd, L"ShowWindow", false);
    return result;
}

BOOL WINAPI hook_clip_cursor(const RECT* rect) {
    if (rect) {
        log_line(L"ClipCursor blocked");
        return TRUE;
    }

    return g_clip_cursor(nullptr);
}

void log_hook_result(const char* name, MH_STATUS status) {
    std::wstring wide_name;
    while (*name) {
        wide_name.push_back(static_cast<wchar_t>(*name++));
    }

    if (status == MH_OK) {
        log_line(L"Hook installed successfully: " + wide_name);
    } else if (status == MH_ERROR_ALREADY_CREATED) {
        log_line(L"Hook already covered: " + wide_name);
    } else if (status == MH_ERROR_FUNCTION_NOT_FOUND
        && (wide_name == L"SetWindowLongPtrA" || wide_name == L"SetWindowLongPtrW")
        && sizeof(void*) == 4) {
        log_line(L"Hook already covered by SetWindowLong on x86: " + wide_name);
    } else {
        log_line(L"Hook install failed: " + wide_name + L" status=" + std::to_wstring(status));
    }
}

template <typename T>
void create_hook(const wchar_t* module, const char* name, LPVOID hook, T* original) {
    const MH_STATUS status = MH_CreateHookApi(module, name, hook, reinterpret_cast<LPVOID*>(original));
    log_hook_result(name, status);
}

template <typename T>
void create_hook_address(LPVOID target, const wchar_t* name, LPVOID hook, T* original) {
    const MH_STATUS status = MH_CreateHook(target, hook, reinterpret_cast<LPVOID*>(original));
    std::string narrow;
    while (*name) {
        narrow.push_back(static_cast<char>(*name++));
    }
    log_hook_result(narrow.c_str(), status);
    if (status == MH_OK || status == MH_ERROR_ALREADY_CREATED) {
        const MH_STATUS enable_status = MH_EnableHook(target);
        if (enable_status != MH_OK && enable_status != MH_ERROR_ENABLED) {
            log_line(L"MH_EnableHook for DirectInput method failed status=" + std::to_wstring(enable_status));
        }
    }
}

void hook_direct_input_device_a(IDirectInputDevice8A* device) {
    if (!device || g_direct_input_device_a_hooked.exchange(true)) {
        return;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    create_hook_address(vtable[13], L"IDirectInputDevice8A::SetCooperativeLevel", reinterpret_cast<LPVOID>(&hook_direct_input_set_cooperative_level_a), &g_direct_input_set_cooperative_level_a);
}

void hook_direct_input_device_w(IDirectInputDevice8W* device) {
    if (!device || g_direct_input_device_w_hooked.exchange(true)) {
        return;
    }

    void** vtable = *reinterpret_cast<void***>(device);
    create_hook_address(vtable[13], L"IDirectInputDevice8W::SetCooperativeLevel", reinterpret_cast<LPVOID>(&hook_direct_input_set_cooperative_level_w), &g_direct_input_set_cooperative_level_w);
}

HRESULT STDMETHODCALLTYPE hook_direct_input_create_device_a(IDirectInput8A* self, REFGUID guid, LPDIRECTINPUTDEVICE8A* device, LPUNKNOWN outer) {
    HRESULT result = g_direct_input_create_device_a(self, guid, device, outer);
    if (SUCCEEDED(result) && device && *device) {
        log_line(L"IDirectInput8A::CreateDevice intercepted");
        hook_direct_input_device_a(*device);
    }
    return result;
}

HRESULT STDMETHODCALLTYPE hook_direct_input_create_device_w(IDirectInput8W* self, REFGUID guid, LPDIRECTINPUTDEVICE8W* device, LPUNKNOWN outer) {
    HRESULT result = g_direct_input_create_device_w(self, guid, device, outer);
    if (SUCCEEDED(result) && device && *device) {
        log_line(L"IDirectInput8W::CreateDevice intercepted");
        hook_direct_input_device_w(*device);
    }
    return result;
}

HRESULT STDMETHODCALLTYPE hook_direct_input_set_cooperative_level_a(IDirectInputDevice8A* self, HWND hwnd, DWORD flags) {
    const DWORD forced = DISCL_FOREGROUND | DISCL_NONEXCLUSIVE;
    log_line(L"IDirectInputDevice8A::SetCooperativeLevel forced from " + hex_value(flags) + L" to " + hex_value(forced));
    return g_direct_input_set_cooperative_level_a(self, hwnd, forced);
}

HRESULT STDMETHODCALLTYPE hook_direct_input_set_cooperative_level_w(IDirectInputDevice8W* self, HWND hwnd, DWORD flags) {
    const DWORD forced = DISCL_FOREGROUND | DISCL_NONEXCLUSIVE;
    log_line(L"IDirectInputDevice8W::SetCooperativeLevel forced from " + hex_value(flags) + L" to " + hex_value(forced));
    return g_direct_input_set_cooperative_level_w(self, hwnd, forced);
}

void hook_direct_input_interface(LPVOID interface_ptr) {
    if (!interface_ptr || g_direct_input_create_hooked.exchange(true)) {
        return;
    }

    void** vtable = *reinterpret_cast<void***>(interface_ptr);
    create_hook_address(vtable[3], L"IDirectInput8::CreateDevice", reinterpret_cast<LPVOID>(&hook_direct_input_create_device_a), &g_direct_input_create_device_a);
    g_direct_input_create_device_w = reinterpret_cast<DirectInputCreateDeviceW_t>(g_direct_input_create_device_a);
    log_line(L"DirectInput CreateDevice hook installed");
}

HRESULT WINAPI hook_direct_input_8_create(HINSTANCE instance, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer) {
    HRESULT result = g_direct_input_8_create(instance, version, riid, out, outer);
    if (SUCCEEDED(result) && out && *out) {
        log_line(L"DirectInput8Create intercepted");
        hook_direct_input_interface(*out);
    }
    return result;
}

DWORD WINAPI watchdog_thread(LPVOID) {
    while (true) {
        Sleep(kWatchdogDelayMs);
        if (g_hooks_ready.load()) {
            EnumWindows(enum_windows_proc, 0);
        }
    }
}

DWORD WINAPI worker_thread(LPVOID) {
    initialize_log();
    log_line(L"DLL loaded");
    load_config();
    log_line(L"Config loaded: x=" + std::to_wstring(g_borderless_x)
        + L" y=" + std::to_wstring(g_borderless_y)
        + L" width=" + std::to_wstring(g_borderless_width)
        + L" height=" + std::to_wstring(g_borderless_height)
        + L" keep_visible_on_focus_loss=" + std::to_wstring(g_keep_visible_on_focus_loss));
    log_line(L"Hook initialization started");

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        log_line(L"MinHook initialization failed status=" + std::to_wstring(status));
        return 1;
    }
    log_line(L"MinHook initialized");

    create_hook(L"user32.dll", "ChangeDisplaySettingsA", reinterpret_cast<LPVOID>(&hook_change_display_settings_a), &g_change_display_settings_a);
    create_hook(L"user32.dll", "ChangeDisplaySettingsW", reinterpret_cast<LPVOID>(&hook_change_display_settings_w), &g_change_display_settings_w);
    create_hook(L"user32.dll", "ChangeDisplaySettingsExA", reinterpret_cast<LPVOID>(&hook_change_display_settings_ex_a), &g_change_display_settings_ex_a);
    create_hook(L"user32.dll", "ChangeDisplaySettingsExW", reinterpret_cast<LPVOID>(&hook_change_display_settings_ex_w), &g_change_display_settings_ex_w);
    create_hook(L"dinput8.dll", "DirectInput8Create", reinterpret_cast<LPVOID>(&hook_direct_input_8_create), &g_direct_input_8_create);
    create_hook(L"user32.dll", "CreateWindowExA", reinterpret_cast<LPVOID>(&hook_create_window_ex_a), &g_create_window_ex_a);
    create_hook(L"user32.dll", "CreateWindowExW", reinterpret_cast<LPVOID>(&hook_create_window_ex_w), &g_create_window_ex_w);
    create_hook(L"user32.dll", "SetWindowLongA", reinterpret_cast<LPVOID>(&hook_set_window_long_a), &g_set_window_long_a);
    create_hook(L"user32.dll", "SetWindowLongW", reinterpret_cast<LPVOID>(&hook_set_window_long_w), &g_set_window_long_w);
    create_hook(L"user32.dll", "SetWindowLongPtrA", reinterpret_cast<LPVOID>(&hook_set_window_long_ptr_a), &g_set_window_long_ptr_a);
    create_hook(L"user32.dll", "SetWindowLongPtrW", reinterpret_cast<LPVOID>(&hook_set_window_long_ptr_w), &g_set_window_long_ptr_w);
    create_hook(L"user32.dll", "SetWindowPos", reinterpret_cast<LPVOID>(&hook_set_window_pos), &g_set_window_pos);
    create_hook(L"user32.dll", "MoveWindow", reinterpret_cast<LPVOID>(&hook_move_window), &g_move_window);
    create_hook(L"user32.dll", "ShowWindow", reinterpret_cast<LPVOID>(&hook_show_window), &g_show_window);
    create_hook(L"user32.dll", "ClipCursor", reinterpret_cast<LPVOID>(&hook_clip_cursor), &g_clip_cursor);

    status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        log_line(L"MH_EnableHook failed status=" + std::to_wstring(status));
        return 2;
    }

    g_hooks_ready = true;
    log_line(L"All hooks enabled");

    HANDLE watchdog = CreateThread(nullptr, 0, watchdog_thread, nullptr, 0, nullptr);
    if (!watchdog) {
        log_last_error(L"CreateThread(watchdog) failed.");
    } else {
        CloseHandle(watchdog);
    }

    return 0;
}

} // namespace

namespace om4t::hooks::borderless {

void start_borderless_window_fix(HMODULE module) {
    DisableThreadLibraryCalls(module);
    HANDLE thread = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    }
}

} // namespace om4t::hooks::borderless
