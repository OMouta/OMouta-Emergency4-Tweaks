add_library(TweakRuntime SHARED
    TweakRuntime.cpp
    TweakRuntime.def
)

target_compile_definitions(TweakRuntime PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

target_include_directories(TweakRuntime PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
)

omouta_ensure_minhook()

target_link_libraries(TweakRuntime PRIVATE
    minhook
    kernel32
)

set_target_properties(TweakRuntime PROPERTIES
    OUTPUT_NAME "TweakRuntime"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/OMoutaEM4Tweaks/Runtime"
)
