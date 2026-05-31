add_library(LaunchOverlay SHARED
    HookEntry.cpp
    LaunchOverlay.cpp
)

omouta_configure_hook_package(LaunchOverlay LaunchOverlay "${CMAKE_CURRENT_LIST_DIR}/tweak.ini")

target_link_libraries(LaunchOverlay PRIVATE
    gdiplus
    user32
    kernel32
)
