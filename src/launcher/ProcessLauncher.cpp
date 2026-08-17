#include "ProcessLauncher.h"

#include <windows.h>

namespace om4t::launcher {

namespace {

std::wstring quote(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

bool inject_dll(HANDLE process, const std::filesystem::path& dll_path, Logger& log) {
    log.write(L"Injecting hook DLL: " + dll_path.wstring());
    const std::wstring dll_string = dll_path.wstring();
    const SIZE_T bytes = (dll_string.size() + 1) * sizeof(wchar_t);

    LPVOID remote_memory = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    log.write(L"VirtualAllocEx result: " + std::to_wstring(reinterpret_cast<uintptr_t>(remote_memory)));
    if (!remote_memory) {
        log.last_error(L"VirtualAllocEx failed.");
        return false;
    }

    SIZE_T written = 0;
    const BOOL wrote = WriteProcessMemory(process, remote_memory, dll_string.c_str(), bytes, &written);
    log.write(L"WriteProcessMemory result: " + std::to_wstring(wrote) + L", bytes=" + std::to_wstring(written));
    if (!wrote || written != bytes) {
        log.last_error(L"WriteProcessMemory failed.");
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"));
    if (!load_library) {
        log.last_error(L"GetProcAddress(LoadLibraryW) failed.");
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        return false;
    }

    HANDLE remote_thread = CreateRemoteThread(process, nullptr, 0, load_library, remote_memory, 0, nullptr);
    log.write(L"CreateRemoteThread result: " + std::to_wstring(reinterpret_cast<uintptr_t>(remote_thread)));
    if (!remote_thread) {
        log.last_error(L"CreateRemoteThread failed.");
        VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(remote_thread, INFINITE);
    DWORD remote_exit = 0;
    GetExitCodeThread(remote_thread, &remote_exit);
    log.write(L"LoadLibrary remote thread exit code: " + std::to_wstring(remote_exit));
    CloseHandle(remote_thread);
    VirtualFreeEx(process, remote_memory, 0, MEM_RELEASE);
    return remote_exit != 0;
}

} // namespace

bool launch_editor(const std::filesystem::path& em4_path, Logger& log) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const auto absolute_path = std::filesystem::absolute(em4_path);
    std::wstring command_line = quote(absolute_path) + L" -editor";
    const auto working_dir = absolute_path.parent_path().wstring();

    log.write(L"Starting editor: " + command_line);
    const BOOL created = CreateProcessW(
        absolute_path.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        working_dir.c_str(),
        &si,
        &pi);

    if (!created) {
        log.last_error(L"CreateProcess failed.");
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool launch_game_with_hooks(
    const std::filesystem::path& em4_path,
    const std::vector<std::filesystem::path>& enabled_hooks,
    Logger& log) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const auto absolute_em4_path = std::filesystem::absolute(em4_path);
    std::wstring command_line = quote(absolute_em4_path);
    const auto working_dir = absolute_em4_path.parent_path().wstring();

    const BOOL created = CreateProcessW(
        absolute_em4_path.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_SUSPENDED,
        nullptr,
        working_dir.c_str(),
        &si,
        &pi);

    log.write(L"CreateProcess result: " + std::to_wstring(created));
    if (!created) {
        log.last_error(L"CreateProcess failed.");
        return false;
    }

    const auto runtime_path = absolute_em4_path.parent_path() / L"OMoutaEM4Tweaks" / L"Runtime" / L"TweakRuntime.dll";
    if (std::filesystem::exists(runtime_path)) {
        if (!inject_dll(pi.hProcess, runtime_path, log)) {
            TerminateProcess(pi.hProcess, 6);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return false;
        }
    } else {
        log.write(L"No tweak runtime found: " + runtime_path.wstring());
    }

    for (const auto& hook : enabled_hooks) {
        if (!inject_dll(pi.hProcess, std::filesystem::absolute(hook), log)) {
            TerminateProcess(pi.hProcess, 4);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return false;
        }
    }

    const DWORD resume_result = ResumeThread(pi.hThread);
    log.write(L"ResumeThread result: " + std::to_wstring(resume_result));
    if (resume_result == static_cast<DWORD>(-1)) {
        log.last_error(L"ResumeThread failed.");
        TerminateProcess(pi.hProcess, 5);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    log.write(L"Game launched");
    return true;
}

} // namespace om4t::launcher
