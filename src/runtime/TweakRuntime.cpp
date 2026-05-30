#include "TweakRuntimeApi.h"

#include <windows.h>
#include <MinHook.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr const wchar_t* kDataDir = L"OMoutaEM4Tweaks";
constexpr const wchar_t* kLogsDir = L"Logs";
constexpr const wchar_t* kLogName = L"TweakRuntime.log";

using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using NtCreateFile_t = LONG(NTAPI*)(
    PHANDLE,
    ACCESS_MASK,
    PVOID,
    PVOID,
    PLARGE_INTEGER,
    ULONG,
    ULONG,
    ULONG,
    ULONG,
    PVOID,
    ULONG);

struct UnicodeString {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
};

struct ObjectAttributes {
    ULONG Length;
    HANDLE RootDirectory;
    UnicodeString* ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
};

struct FileOpenSubscriber {
    int priority;
    om4t::runtime::FileOpenCallback callback;
    void* user_data;
};

struct FileOpenedSubscriber {
    int priority;
    om4t::runtime::FileOpenedCallback callback;
    void* user_data;
};

std::wofstream g_log;
std::mutex g_log_mutex;
std::mutex g_subscriber_mutex;
std::vector<FileOpenSubscriber> g_file_open_subscribers;
std::vector<FileOpenedSubscriber> g_file_opened_subscribers;
CreateFileA_t g_create_file_a = nullptr;
CreateFileW_t g_create_file_w = nullptr;
CreateFileA_t g_kernelbase_create_file_a = nullptr;
CreateFileW_t g_kernelbase_create_file_w = nullptr;
NtCreateFile_t g_nt_create_file = nullptr;
thread_local bool g_inside_runtime = false;

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

std::wstring pad(WORD value) {
    return value < 10 ? L"0" + std::to_wstring(value) : std::to_wstring(value);
}

void initialize_log() {
    std::filesystem::create_directories(log_directory());
    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log.open(log_directory() / kLogName, std::ios::app);
}

void log_line(const std::wstring& message) {
    if (g_inside_runtime) {
        return;
    }

    g_inside_runtime = true;
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log.is_open()) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        g_log << L"[" << st.wYear << L"-" << pad(st.wMonth) << L"-" << pad(st.wDay)
              << L" " << pad(st.wHour) << L":" << pad(st.wMinute) << L":" << pad(st.wSecond)
              << L"] [TweakRuntime] " << message << L"\n";
        g_log.flush();
    }
    g_inside_runtime = false;
}

om4t::runtime::FileOpenAction dispatch_file_open(const om4t::runtime::FileOpenRequest& request) {
    if (g_inside_runtime) {
        return om4t::runtime::FileOpenAction::Continue;
    }

    std::vector<FileOpenSubscriber> subscribers;
    {
        std::lock_guard<std::mutex> lock(g_subscriber_mutex);
        subscribers = g_file_open_subscribers;
    }

    for (const auto& subscriber : subscribers) {
        if (!subscriber.callback) {
            continue;
        }

        const auto action = subscriber.callback(&request, subscriber.user_data);
        if (action != om4t::runtime::FileOpenAction::Continue) {
            return action;
        }
    }

    return om4t::runtime::FileOpenAction::Continue;
}

void dispatch_file_opened(const om4t::runtime::FileOpenRequest& request, HANDLE handle, DWORD last_error) {
    if (g_inside_runtime) {
        return;
    }

    std::vector<FileOpenedSubscriber> subscribers;
    {
        std::lock_guard<std::mutex> lock(g_subscriber_mutex);
        subscribers = g_file_opened_subscribers;
    }

    for (const auto& subscriber : subscribers) {
        if (subscriber.callback) {
            subscriber.callback(&request, handle, last_error, subscriber.user_data);
        }
    }
}

HANDLE block_file_open() {
    SetLastError(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
}

HANDLE WINAPI hook_create_file_a(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE template_file) {
    const std::wstring wide_path = widen(path);
    const om4t::runtime::FileOpenRequest request{wide_path.c_str(), access, share, creation, flags, "CreateFileA"};
    if (dispatch_file_open(request) == om4t::runtime::FileOpenAction::BlockNotFound) {
        return block_file_open();
    }

    HANDLE handle = g_create_file_a(path, access, share, security, creation, flags, template_file);
    const DWORD last_error = handle == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    dispatch_file_opened(request, handle, last_error);
    if (last_error != 0) {
        SetLastError(last_error);
    }
    return handle;
}

HANDLE WINAPI hook_create_file_w(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE template_file) {
    const std::wstring wide_path = path ? path : L"";
    const om4t::runtime::FileOpenRequest request{wide_path.c_str(), access, share, creation, flags, "CreateFileW"};
    if (dispatch_file_open(request) == om4t::runtime::FileOpenAction::BlockNotFound) {
        return block_file_open();
    }

    HANDLE handle = g_create_file_w(path, access, share, security, creation, flags, template_file);
    const DWORD last_error = handle == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    dispatch_file_opened(request, handle, last_error);
    if (last_error != 0) {
        SetLastError(last_error);
    }
    return handle;
}

HANDLE WINAPI hook_kernelbase_create_file_a(LPCSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE template_file) {
    const std::wstring wide_path = widen(path);
    const om4t::runtime::FileOpenRequest request{wide_path.c_str(), access, share, creation, flags, "KERNELBASE.CreateFileA"};
    if (dispatch_file_open(request) == om4t::runtime::FileOpenAction::BlockNotFound) {
        return block_file_open();
    }

    HANDLE handle = g_kernelbase_create_file_a(path, access, share, security, creation, flags, template_file);
    const DWORD last_error = handle == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    dispatch_file_opened(request, handle, last_error);
    if (last_error != 0) {
        SetLastError(last_error);
    }
    return handle;
}

HANDLE WINAPI hook_kernelbase_create_file_w(LPCWSTR path, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security, DWORD creation, DWORD flags, HANDLE template_file) {
    const std::wstring wide_path = path ? path : L"";
    const om4t::runtime::FileOpenRequest request{wide_path.c_str(), access, share, creation, flags, "KERNELBASE.CreateFileW"};
    if (dispatch_file_open(request) == om4t::runtime::FileOpenAction::BlockNotFound) {
        return block_file_open();
    }

    HANDLE handle = g_kernelbase_create_file_w(path, access, share, security, creation, flags, template_file);
    const DWORD last_error = handle == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    dispatch_file_opened(request, handle, last_error);
    if (last_error != 0) {
        SetLastError(last_error);
    }
    return handle;
}

std::wstring path_from_object_attributes(PVOID object_attributes) {
    auto* attrs = reinterpret_cast<ObjectAttributes*>(object_attributes);
    if (!attrs || !attrs->ObjectName || !attrs->ObjectName->Buffer || attrs->ObjectName->Length == 0) {
        return L"";
    }

    return std::wstring(attrs->ObjectName->Buffer, attrs->ObjectName->Length / sizeof(wchar_t));
}

LONG NTAPI hook_nt_create_file(
    PHANDLE file_handle,
    ACCESS_MASK desired_access,
    PVOID object_attributes,
    PVOID io_status_block,
    PLARGE_INTEGER allocation_size,
    ULONG file_attributes,
    ULONG share_access,
    ULONG create_disposition,
    ULONG create_options,
    PVOID ea_buffer,
    ULONG ea_length) {
    const std::wstring path = path_from_object_attributes(object_attributes);
    const om4t::runtime::FileOpenRequest request{path.c_str(), desired_access, share_access, create_disposition, file_attributes | create_options, "NtCreateFile"};
    if (!path.empty() && dispatch_file_open(request) == om4t::runtime::FileOpenAction::BlockNotFound) {
        return static_cast<LONG>(0xC0000034); // STATUS_OBJECT_NAME_NOT_FOUND
    }

    const LONG status = g_nt_create_file(
        file_handle,
        desired_access,
        object_attributes,
        io_status_block,
        allocation_size,
        file_attributes,
        share_access,
        create_disposition,
        create_options,
        ea_buffer,
        ea_length);

    if (!path.empty()) {
        const bool ok = status >= 0 && file_handle && *file_handle;
        dispatch_file_opened(request, ok ? *file_handle : INVALID_HANDLE_VALUE, ok ? 0 : static_cast<DWORD>(status));
    }

    return status;
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

template <typename T>
void create_hook_if_unique(const wchar_t* module, const char* name, LPVOID hook, T* original) {
    HMODULE module_handle = GetModuleHandleW(module);
    if (!module_handle) {
        log_line(L"Hook skipped; module not loaded: " + std::wstring(module));
        return;
    }

    FARPROC target = GetProcAddress(module_handle, name);
    if (!target) {
        log_line(L"Hook skipped; function missing: " + widen(name));
        return;
    }

    const MH_STATUS status = MH_CreateHook(reinterpret_cast<LPVOID>(target), hook, reinterpret_cast<LPVOID*>(original));
    log_hook_result(name, status);
}

DWORD WINAPI worker_thread(LPVOID) {
    initialize_log();
    log_line(L"DLL loaded");

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) {
        log_line(L"MinHook initialization failed status=" + std::to_wstring(status));
        return 1;
    }

    create_hook(L"kernel32.dll", "CreateFileA", reinterpret_cast<LPVOID>(&hook_create_file_a), &g_create_file_a);
    create_hook(L"kernel32.dll", "CreateFileW", reinterpret_cast<LPVOID>(&hook_create_file_w), &g_create_file_w);
    create_hook_if_unique(L"KERNELBASE.dll", "CreateFileA", reinterpret_cast<LPVOID>(&hook_kernelbase_create_file_a), &g_kernelbase_create_file_a);
    create_hook_if_unique(L"KERNELBASE.dll", "CreateFileW", reinterpret_cast<LPVOID>(&hook_kernelbase_create_file_w), &g_kernelbase_create_file_w);
    create_hook_if_unique(L"ntdll.dll", "NtCreateFile", reinterpret_cast<LPVOID>(&hook_nt_create_file), &g_nt_create_file);

    status = MH_EnableHook(MH_ALL_HOOKS);
    if (status != MH_OK) {
        log_line(L"MH_EnableHook failed status=" + std::to_wstring(status));
        return 2;
    }

    log_line(L"Runtime hooks enabled");
    return 0;
}

} // namespace

extern "C" __declspec(dllexport) BOOL __stdcall OM4T_RegisterFileOpenCallback(
    int priority,
    om4t::runtime::FileOpenCallback callback,
    void* user_data) {
    if (!callback) {
        return FALSE;
    }

    std::lock_guard<std::mutex> lock(g_subscriber_mutex);
    g_file_open_subscribers.push_back(FileOpenSubscriber{priority, callback, user_data});
    std::sort(g_file_open_subscribers.begin(), g_file_open_subscribers.end(), [](const auto& left, const auto& right) {
        return left.priority < right.priority;
    });
    log_line(L"Registered file-open callback priority=" + std::to_wstring(priority)
        + L" count=" + std::to_wstring(g_file_open_subscribers.size()));
    return TRUE;
}

extern "C" __declspec(dllexport) BOOL __stdcall OM4T_RegisterFileOpenedCallback(
    int priority,
    om4t::runtime::FileOpenedCallback callback,
    void* user_data) {
    if (!callback) {
        return FALSE;
    }

    std::lock_guard<std::mutex> lock(g_subscriber_mutex);
    g_file_opened_subscribers.push_back(FileOpenedSubscriber{priority, callback, user_data});
    std::sort(g_file_opened_subscribers.begin(), g_file_opened_subscribers.end(), [](const auto& left, const auto& right) {
        return left.priority < right.priority;
    });
    log_line(L"Registered file-opened callback priority=" + std::to_wstring(priority)
        + L" count=" + std::to_wstring(g_file_opened_subscribers.size()));
    return TRUE;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, worker_thread, nullptr, 0, nullptr);
        if (thread) {
            CloseHandle(thread);
        }
    }

    return TRUE;
}
