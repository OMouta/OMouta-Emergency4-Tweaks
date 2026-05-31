add_library(AltTabSafe SHARED
    AltTabSafe.cpp
    HookEntry.cpp
)

omouta_configure_hook_package(AltTabSafe AltTabSafe "${CMAKE_CURRENT_LIST_DIR}/tweak.ini" USE_MINHOOK)

target_link_libraries(AltTabSafe PRIVATE
    user32
    kernel32
)
