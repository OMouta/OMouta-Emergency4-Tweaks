#pragma once

#include <windows.h>

namespace om4t::runtime {

constexpr int kApiVersion = 1;

enum class FileOpenAction : int {
    Continue = 0,
    BlockNotFound = 1,
};

struct FileOpenRequest {
    const wchar_t* path;
    DWORD desired_access;
    DWORD share_mode;
    DWORD creation_disposition;
    DWORD flags_and_attributes;
    const char* api_name;
};

using FileOpenCallback = FileOpenAction(__stdcall*)(const FileOpenRequest* request, void* user_data);
using FileOpenedCallback = void(__stdcall*)(const FileOpenRequest* request, HANDLE handle, DWORD last_error, void* user_data);
using RegisterFileOpenCallbackFn = BOOL(__stdcall*)(int priority, FileOpenCallback callback, void* user_data);
using RegisterFileOpenedCallbackFn = BOOL(__stdcall*)(int priority, FileOpenedCallback callback, void* user_data);

constexpr const char* kRegisterFileOpenCallbackName = "OM4T_RegisterFileOpenCallback";
constexpr const char* kRegisterFileOpenedCallbackName = "OM4T_RegisterFileOpenedCallback";

} // namespace om4t::runtime
