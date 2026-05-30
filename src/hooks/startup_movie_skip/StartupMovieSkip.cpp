#include "StartupMovieSkip.h"

#include "../../runtime/TweakRuntimeApi.h"

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>

namespace {

constexpr const wchar_t* kDataDir = L"OMoutaEM4Tweaks";
constexpr const wchar_t* kLogsDir = L"Logs";
constexpr const wchar_t* kConfigName = L"config.ini";
constexpr const wchar_t* kLogName = L"StartupMovieSkip.log";

std::wofstream g_log;
std::mutex g_log_mutex;
std::atomic<bool> g_skip_16t{true};
std::atomic<bool> g_skip_intro{true};
thread_local bool g_inside_hook = false;

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

std::wstring trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return L"";
    }
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::wstring normalized_path(std::wstring path) {
    if (path.rfind(L"\\??\\", 0) == 0) {
        path = path.substr(4);
    }
    if (path.rfind(L"\\\\?\\", 0) == 0) {
        path = path.substr(4);
    }
    for (auto& ch : path) {
        if (ch == L'/') {
            ch = L'\\';
        }
        ch = static_cast<wchar_t>(towlower(ch));
    }
    return path;
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
              << L"] [StartupMovieSkip] " << message << L"\n";
        g_log.flush();
    }
    g_inside_hook = false;
}

void load_config() {
    const auto sections = read_ini(process_directory() / kDataDir / kConfigName);
    if (auto section = sections.find(L"StartupMovieSkip"); section != sections.end()) {
        if (auto it = section->second.find(L"skip_16t"); it != section->second.end()) {
            g_skip_16t = parse_bool(it->second, true);
        }
        if (auto it = section->second.find(L"skip_intro"); it != section->second.end()) {
            g_skip_intro = parse_bool(it->second, true);
        }
    }
}

bool ends_with(const std::wstring& value, const wchar_t* suffix) {
    const std::wstring suffix_value = suffix;
    return value.size() >= suffix_value.size()
        && value.compare(value.size() - suffix_value.size(), suffix_value.size(), suffix_value) == 0;
}

om4t::runtime::FileOpenAction __stdcall on_file_open(const om4t::runtime::FileOpenRequest* request, void*) {
    if (!request || !request->path) {
        return om4t::runtime::FileOpenAction::Continue;
    }

    const auto path = normalized_path(request->path);
    const bool skip_16t = g_skip_16t.load() && ends_with(path, L"\\data\\video\\16t.mpg");
    const bool skip_intro = g_skip_intro.load() && ends_with(path, L"\\data\\video\\intro.mpg");
    if (!skip_16t && !skip_intro) {
        return om4t::runtime::FileOpenAction::Continue;
    }

    log_line(L"Blocking startup movie: " + std::wstring(request->path));
    return om4t::runtime::FileOpenAction::BlockNotFound;
}

bool register_with_runtime() {
    HMODULE runtime = GetModuleHandleW(L"TweakRuntime.dll");
    if (!runtime) {
        log_line(L"TweakRuntime.dll is not loaded; Startup Movie Skip is inactive");
        return false;
    }

    auto register_file_open = reinterpret_cast<om4t::runtime::RegisterFileOpenCallbackFn>(
        GetProcAddress(runtime, om4t::runtime::kRegisterFileOpenCallbackName));
    if (!register_file_open) {
        log_line(L"TweakRuntime file-open API was not found");
        return false;
    }

    if (!register_file_open(100, &on_file_open, nullptr)) {
        log_line(L"TweakRuntime rejected file-open callback registration");
        return false;
    }

    log_line(L"Registered with TweakRuntime");
    return true;
}

DWORD WINAPI worker_thread(LPVOID) {
    initialize_log();
    load_config();
    log_line(L"DLL loaded");
    log_line(L"Config skip_16t=" + std::to_wstring(g_skip_16t.load())
        + L" skip_intro=" + std::to_wstring(g_skip_intro.load()));
    register_with_runtime();
    return 0;
}

} // namespace

namespace om4t::hooks::startup_movie_skip {

void start_startup_movie_skip(HMODULE module) {
    DisableThreadLibraryCalls(module);
    HANDLE thread = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    }
}

} // namespace om4t::hooks::startup_movie_skip
