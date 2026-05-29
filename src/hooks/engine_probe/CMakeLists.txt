add_library(EngineProbe SHARED
    HookEntry.cpp
    EngineProbe.cpp
)

omouta_configure_hook_package(EngineProbe EngineProbe "${CMAKE_CURRENT_LIST_DIR}/tweak.ini" USE_MINHOOK)

target_link_libraries(EngineProbe PRIVATE
    kernel32
    user32
    opengl32
)
