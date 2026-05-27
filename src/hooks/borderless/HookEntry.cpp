#include "BorderlessTweak.h"

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        om4t::hooks::borderless::start_borderless_window_fix(module);
    }

    return TRUE;
}
