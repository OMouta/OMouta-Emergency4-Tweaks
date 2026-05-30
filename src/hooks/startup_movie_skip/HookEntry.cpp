#include "StartupMovieSkip.h"

#include <windows.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        om4t::hooks::startup_movie_skip::start_startup_movie_skip(module);
    }

    return TRUE;
}
