add_library(StartupMovieSkip SHARED
    HookEntry.cpp
    StartupMovieSkip.cpp
)

omouta_configure_hook_package(StartupMovieSkip StartupMovieSkip "${CMAKE_CURRENT_LIST_DIR}/tweak.ini")

target_link_libraries(StartupMovieSkip PRIVATE
    kernel32
)
