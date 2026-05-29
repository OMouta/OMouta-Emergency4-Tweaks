#include "EngineProbe.h"

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        om4t::hooks::engine_probe::start_engine_probe(module);
    }

    return TRUE;
}
