#include "AltTabSafe.h"

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        om4t::hooks::alt_tab_safe::start_alt_tab_safe(module);
    }

    return TRUE;
}
