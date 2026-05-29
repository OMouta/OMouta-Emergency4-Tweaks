#include "EngineProbe.h"

#include <windows.h>
#include <tlhelp32.h>
#include <gl/GL.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr const wchar_t* kDataDir = L"OMoutaEM4Tweaks";
constexpr const wchar_t* kLogsDir = L"Logs";
constexpr const wchar_t* kConfigName = L"config.ini";
constexpr const wchar_t* kLogName = L"EngineProbe.log";
constexpr const wchar_t* kModuleName = L"EngineProbe.modules.txt";
constexpr const wchar_t* kModuleAfterOpenGlName = L"EngineProbe.modules.after_opengl.txt";
constexpr const wchar_t* kFileIoName = L"EngineProbe.fileio.csv";

using CreateWindowExA_t = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using CreateWindowExW_t = HWND(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using ReadFile_t = BOOL(WINAPI*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
using CloseHandle_t = BOOL(WINAPI*)(HANDLE);
using wglCreateContext_t = HGLRC(WINAPI*)(HDC);
using wglMakeCurrent_t = BOOL(WINAPI*)(HDC, HGLRC);

std::wofstream g_log;
std::ofstream g_fileio;
std::mutex g_log_mutex;
std::mutex g_file_mutex;
std::mutex g_handle_mutex;
std::map<HANDLE, std::wstring> g_handle_paths;
std::atomic<bool> g_trace_file_io{true};
std::atomic<bool> g_trace_game_root_only{true};
std::atomic<bool> g_trace_game_logfile{false};
std::atomic<bool> g_trace_windows{true};
std::atomic<bool> g_trace_opengl{true};
std::atomic<bool> g_opengl_logged{false};
std::atomic<bool> g_opengl_hooks_attempted{false};
std::atomic<uint32_t> g_exception_logs{0};
thread_local bool g_inside_probe = false;

CreateWindowExA_t g_create_window_ex_a = nullptr;
CreateWindowExW_t g_create_window_ex_w = nullptr;
CreateFileA_t g_create_file_a = nullptr;
CreateFileW_t g_create_file_w = nullptr;
ReadFile_t g_read_file = nullptr;
CloseHandle_t g_close_handle = nullptr;
wglCreateContext_t g_wgl_create_context = nullptr;
wglMakeCurrent_t g_wgl_make_current = nullptr;

std::wstring pad(WORD value) {
    return value < 10 ? L"0" + std::to_wstring(value) : std::to_wstring(value);
}

std::filesystem::path process_directory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

std::wstring normalized_path(std::wstring path) {
    for (auto& ch : path) {
        if (ch == L'/') {
            ch = L'\\';
        }
    }
    return path;
}

std::wstring lower_copy(std::wstring value) {
    for (auto& ch : value) {
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return value;
}

std::filesystem::path log_directory() {
    return process_directory() / kDataDir / kLogsDir;
}

std::wstring trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return L"";
    }
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string narrow(const std::wstring& value) {
    if (value.empty()) {
        return {};
    }

    const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        return {};
    }

    std::string result(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), bytes, nullptr, nullptr);
    return result;
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

std::wstring hex_value(uintptr_t value) {
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
}

void log_line(const std::wstring& message) {
    if (g_inside_probe) {
        return;
    }

    g_inside_probe = true;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log.is_open()) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        g_log << L"[" << st.wYear << L"-" << pad(st.wMonth) << L"-" << pad(st.wDay)
              << L" " << pad(st.wHour) << L":" << pad(st.wMinute) << L":" << pad(st.wSecond)
              << L"] [EngineProbe] " << message << L"\n";
        g_log.flush();
    }
    g_inside_probe = false;
}

void log_last_error(const std::wstring& context) {
    log_line(context + L" GetLastError=" + std::to_wstring(GetLastError()));
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

void load_config() {
    const auto config_path = process_directory() / kDataDir / kConfigName;
    const auto sections = read_ini(config_path);
    if (auto section = sections.find(L"EngineProbe"); section != sections.end()) {
        if (auto it = section->second.find(L"trace_file_io"); it != section->second.end()) {
            g_trace_file_io = parse_bool(it->second, true);
        }
        if (auto it = section->second.find(L"trace_game_root_only"); it != section->second.end()) {
            g_trace_game_root_only = parse_bool(it->second, true);
        }
        if (auto it = section->second.find(L"trace_game_logfile"); it != section->second.end()) {
            g_trace_game_logfile = parse_bool(it->second, false);
        }
        if (auto it = section->second.find(L"trace_windows"); it != section->second.end()) {
            g_trace_windows = parse_bool(it->second, true);
        }
        if (auto it = section->second.find(L"trace_opengl"); it != section->second.end()) {
            g_trace_opengl = parse_bool(it->second, true);
        }
    }
}

bool starts_with_i(const std::wstring& value, const std::wstring& prefix) {
    const auto lower_value = lower_copy(value);
    const auto lower_prefix = lower_copy(prefix);
    return lower_value.rfind(lower_prefix, 0) == 0;
}

bool ends_with_i(const std::wstring& value, const std::wstring& suffix) {
    const auto lower_value = lower_copy(value);
    const auto lower_suffix = lower_copy(suffix);
    return lower_value.size() >= lower_suffix.size()
        && lower_value.compare(lower_value.size() - lower_suffix.size(), lower_suffix.size(), lower_suffix) == 0;
}

bool should_trace_path(const std::wstring& path) {
    if (!g_trace_file_io.load() || path.empty()) {
        return false;
    }

    const auto normalized = normalized_path(path);
    const auto root = normalized_path(process_directory().wstring());
    if (g_trace_game_root_only.load() && !starts_with_i(normalized, root)) {
        return false;
    }

    if (!g_trace_game_logfile.load() && ends_with_i(normalized, L"\\logfile.txt")) {
        return false;
    }

    return true;
}

void initialize_logs() {
    std::filesystem::create_directories(log_directory());

    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        g_log.open(log_directory() / kLogName, std::ios::app);
    }

    {
        std::lock_guard<std::mutex> lock(g_file_mutex);
        g_fileio.open(log_directory() / kFileIoName, std::ios::trunc);
        g_fileio << "event,tick,handle,path,bytes,ok,last_error\n";
        g_fileio.flush();
    }
}

void fileio_line(const std::wstring& event, HANDLE handle, const std::wstring& path, DWORD bytes, BOOL ok, DWORD error) {
    if (g_inside_probe || !g_trace_file_io.load()) {
        return;
    }

    g_inside_probe = true;
    std::lock_guard<std::mutex> lock(g_file_mutex);
    if (g_fileio.is_open()) {
        g_fileio << narrow(event) << ','
                 << GetTickCount64() << ','
                 << reinterpret_cast<uintptr_t>(handle) << ",\""
                 << narrow(normalized_path(path)) << "\","
                 << bytes << ','
                 << (ok ? 1 : 0) << ','
                 << error << '\n';
        g_fileio.flush();
    }
    g_inside_probe = false;
}

void write_module_snapshot(const wchar_t* file_name) {
    std::wofstream output(log_directory() / file_name, std::ios::trunc);
    if (!output.is_open()) {
        log_last_error(L"Could not open module snapshot.");
        return;
    }

    wchar_t exe_path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    WIN32_FILE_ATTRIBUTE_DATA exe_data{};
    GetFileAttributesExW(exe_path, GetFileExInfoStandard, &exe_data);

    output << L"process=" << exe_path << L"\n";
    output << L"pid=" << GetCurrentProcessId() << L"\n";
    output << L"exe_size=" << (static_cast<uint64_t>(exe_data.nFileSizeHigh) << 32 | exe_data.nFileSizeLow) << L"\n\n";

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) {
        output << L"CreateToolhelp32Snapshot failed: " << GetLastError() << L"\n";
        return;
    }

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            output << hex_value(reinterpret_cast<uintptr_t>(entry.modBaseAddr))
                   << L" size=" << entry.modBaseSize
                   << L" module=" << entry.szModule
                   << L" path=" << entry.szExePath << L"\n";
        } while (Module32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
}

std::wstring window_text(HWND hwnd) {
    wchar_t text[256]{};
    GetWindowTextW(hwnd, text, static_cast<int>(std::size(text)));
    return text;
}

std::wstring window_class(HWND hwnd) {
    wchar_t text[256]{};
    GetClassNameW(hwnd, text, static_cast<int>(std::size(text)));
    return text;
}

void log_window(HWND hwnd, const wchar_t* reason) {
    if (!g_trace_windows.load() || !hwnd) {
        return;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != GetCurrentProcessId()) {
        return;
    }

    RECT rect{};
    GetWindowRect(hwnd, &rect);
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

    log_line(std::wstring(L"Window ") + reason
        + L" hwnd=" + hex_value(reinterpret_cast<uintptr_t>(hwnd))
        + L" class=\"" + window_class(hwnd)
        + L"\" title=\"" + window_text(hwnd)
        + L"\" rect=" + std::to_wstring(rect.left) + L"," + std::to_wstring(rect.top)
        + L"," + std::to_wstring(rect.right) + L"," + std::to_wstring(rect.bottom)
        + L" style=" + hex_value(static_cast<uintptr_t>(style))
        + L" ex_style=" + hex_value(static_cast<uintptr_t>(ex_style)));
}

void log_opengl() {
    if (!g_trace_opengl.load() || g_opengl_logged.exchange(true)) {
        return;
    }

    const auto vendor = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
    const auto renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const auto extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));

    log_line(L"OpenGL vendor=\"" + widen(vendor)
        + L"\" renderer=\"" + widen(renderer)
        + L"\" version=\"" + widen(version) + L"\"");

    if (extensions) {
        const std::wstring wide_extensions = widen(extensions);
        log_line(L"OpenGL extensions length=" + std::to_wstring(wide_extensions.size())
            + L" has_GL_ARB_multitexture=" + std::to_wstring(wide_extensions.find(L"GL_ARB_multitexture") != std::wstring::npos)
            + L" has_GL_ARB_vertex_buffer_object=" + std::to_wstring(wide_extensions.find(L"GL_ARB_vertex_buffer_object") != std::wstring::npos));
    }

    write_module_snapshot(kModuleAfterOpenGlName);
}

LONG CALLBACK vectored_exception_handler(PEXCEPTION_POINTERS exception) {
    if (!exception || !exception->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const DWORD code = exception->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION
        && code != EXCEPTION_ILLEGAL_INSTRUCTION
        && code != EXCEPTION_IN_PAGE_ERROR
        && code != EXCEPTION_STACK_OVERFLOW) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if (g_exception_logs.fetch_add(1) >= 32) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const auto address = reinterpret_cast<uintptr_t>(exception->ExceptionRecord->ExceptionAddress);
    HMODULE module = nullptr;
    wchar_t module_name[MAX_PATH]{};
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(exception->ExceptionRecord->ExceptionAddress),
            &module)) {
        GetModuleFileNameW(module, module_name, MAX_PATH);
    }

    log_line(L"Exception code=" + hex_value(code)
        + L" address=" + hex_value(address)
        + L" module_base=" + hex_value(reinterpret_cast<uintptr_t>(module))
        + L" module=\"" + std::wstring(module_name) + L"\"");

    return EXCEPTION_CONTINUE_SEARCH;
}

HWND WINAPI hook_create_window_ex_a(DWORD ex_style, LPCSTR class_name, LPCSTR window_name, DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    HWND hwnd = g_create_window_ex_a(ex_style, class_name, window_name, style, x, y, width, height, parent, menu, instance, param);
    log_window(hwnd, L"CreateWindowExA");
    return hwnd;
}

HWND WINAPI hook_create_window_ex_w(DWORD ex_style, LPCWSTR class_name, LPCWSTR window_name, DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu, HINSTANCE instance, LPVOID param) {
    HWND hwnd = g_create_window_ex_w(ex_style, class_name, window_name, style, x, y, width, height, parent, menu, instance, param);
    log_window(hwnd, L"CreateWindowExW");
    return hwnd;
}

HANDLE WINAPI hook_create_file_a(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE template_file) {
    HANDLE handle = g_create_file_a(path, access, share, security, creation, flags, template_file);
    const std::wstring wide_path = widen(path);
    if (handle != INVALID_HANDLE_VALUE && !g_inside_probe && should_trace_path(wide_path)) {
        std::lock_guard<std::mutex> lock(g_handle_mutex);
        g_handle_paths[handle] = normalized_path(wide_path);
    }
    if (should_trace_path(wide_path)) {
        fileio_line(L"open", handle, wide_path, 0, handle != INVALID_HANDLE_VALUE, handle == INVALID_HANDLE_VALUE ? GetLastError() : 0);
    }
    return handle;
}

HANDLE WINAPI hook_create_file_w(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE template_file) {
    HANDLE handle = g_create_file_w(path, access, share, security, creation, flags, template_file);
    const std::wstring wide_path = path ? path : L"";
    if (handle != INVALID_HANDLE_VALUE && !g_inside_probe && should_trace_path(wide_path)) {
        std::lock_guard<std::mutex> lock(g_handle_mutex);
        g_handle_paths[handle] = normalized_path(wide_path);
    }
    if (should_trace_path(wide_path)) {
        fileio_line(L"open", handle, wide_path, 0, handle != INVALID_HANDLE_VALUE, handle == INVALID_HANDLE_VALUE ? GetLastError() : 0);
    }
    return handle;
}

BOOL WINAPI hook_read_file(HANDLE file, LPVOID buffer, DWORD bytes_to_read, LPDWORD bytes_read, LPOVERLAPPED overlapped) {
    BOOL ok = g_read_file(file, buffer, bytes_to_read, bytes_read, overlapped);
    std::wstring path;
    {
        std::lock_guard<std::mutex> lock(g_handle_mutex);
        if (auto it = g_handle_paths.find(file); it != g_handle_paths.end()) {
            path = it->second;
        }
    }
    if (!path.empty()) {
        fileio_line(L"read", file, path, bytes_read ? *bytes_read : bytes_to_read, ok, ok ? 0 : GetLastError());
    }
    return ok;
}

BOOL WINAPI hook_close_handle(HANDLE handle) {
    std::wstring path;
    {
        std::lock_guard<std::mutex> lock(g_handle_mutex);
        if (auto it = g_handle_paths.find(handle); it != g_handle_paths.end()) {
            path = it->second;
            g_handle_paths.erase(it);
        }
    }
    if (!path.empty()) {
        fileio_line(L"close", handle, path, 0, TRUE, 0);
    }
    return g_close_handle(handle);
}

HGLRC WINAPI hook_wgl_create_context(HDC dc) {
    HGLRC context = g_wgl_create_context(dc);
    log_line(L"wglCreateContext hdc=" + hex_value(reinterpret_cast<uintptr_t>(dc))
        + L" hglrc=" + hex_value(reinterpret_cast<uintptr_t>(context)));
    return context;
}

BOOL WINAPI hook_wgl_make_current(HDC dc, HGLRC context) {
    BOOL ok = g_wgl_make_current(dc, context);
    if (ok && context) {
        log_opengl();
    }
    return ok;
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

void install_opengl_hooks() {
    if (!g_trace_opengl.load() || g_opengl_hooks_attempted.exchange(true)) {
        return;
    }
    if (!GetModuleHandleW(L"opengl32.dll")) {
        g_opengl_hooks_attempted = false;
        return;
    }

    create_hook(L"opengl32.dll", "wglCreateContext", reinterpret_cast<LPVOID>(&hook_wgl_create_context), &g_wgl_create_context);
    create_hook(L"opengl32.dll", "wglMakeCurrent", reinterpret_cast<LPVOID>(&hook_wgl_make_current), &g_wgl_make_current);
    const MH_STATUS status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK && status != MH_ERROR_ENABLED) {
        log_line(L"MH_EnableHook after OpenGL hooks failed status=" + std::to_wstring(status));
    }
}

DWORD WINAPI watchdog_thread(LPVOID) {
    while (!g_opengl_logged.load()) {
        Sleep(500);
        install_opengl_hooks();
    }
    return 0;
}

DWORD WINAPI worker_thread(LPVOID) {
    initialize_logs();
    load_config();
    log_line(L"DLL loaded");
    log_line(L"Config trace_file_io=" + std::to_wstring(g_trace_file_io.load())
        + L" trace_game_root_only=" + std::to_wstring(g_trace_game_root_only.load())
        + L" trace_game_logfile=" + std::to_wstring(g_trace_game_logfile.load())
        + L" trace_windows=" + std::to_wstring(g_trace_windows.load())
        + L" trace_opengl=" + std::to_wstring(g_trace_opengl.load()));
    write_module_snapshot(kModuleName);
    AddVectoredExceptionHandler(1, vectored_exception_handler);

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        log_line(L"MinHook initialization failed status=" + std::to_wstring(status));
        return 1;
    }

    if (g_trace_windows.load()) {
        create_hook(L"user32.dll", "CreateWindowExA", reinterpret_cast<LPVOID>(&hook_create_window_ex_a), &g_create_window_ex_a);
        create_hook(L"user32.dll", "CreateWindowExW", reinterpret_cast<LPVOID>(&hook_create_window_ex_w), &g_create_window_ex_w);
    }

    if (g_trace_file_io.load()) {
        create_hook(L"kernel32.dll", "CreateFileA", reinterpret_cast<LPVOID>(&hook_create_file_a), &g_create_file_a);
        create_hook(L"kernel32.dll", "CreateFileW", reinterpret_cast<LPVOID>(&hook_create_file_w), &g_create_file_w);
        create_hook(L"kernel32.dll", "ReadFile", reinterpret_cast<LPVOID>(&hook_read_file), &g_read_file);
        create_hook(L"kernel32.dll", "CloseHandle", reinterpret_cast<LPVOID>(&hook_close_handle), &g_close_handle);
    }

    install_opengl_hooks();

    status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        log_line(L"MH_EnableHook failed status=" + std::to_wstring(status));
        return 2;
    }
    log_line(L"Initial hooks enabled");

    HANDLE watchdog = CreateThread(nullptr, 0, watchdog_thread, nullptr, 0, nullptr);
    if (watchdog) {
        CloseHandle(watchdog);
    } else {
        log_last_error(L"CreateThread(watchdog) failed.");
    }

    return 0;
}

} // namespace

namespace om4t::hooks::engine_probe {

void start_engine_probe(HMODULE module) {
    DisableThreadLibraryCalls(module);
    HANDLE thread = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    }
}

} // namespace om4t::hooks::engine_probe
