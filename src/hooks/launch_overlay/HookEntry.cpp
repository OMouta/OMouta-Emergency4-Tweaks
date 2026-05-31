#include "LaunchOverlay.h"

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        om4t::hooks::launch_overlay::start_launch_overlay(module);
    }

    return TRUE;
}
