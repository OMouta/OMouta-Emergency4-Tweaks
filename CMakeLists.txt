cmake_minimum_required(VERSION 3.21)

project(OMoutaEM4Tweaks LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    message(FATAL_ERROR "This project must be built with MSVC.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build with the Win32/x86 platform. Do not build x64.")
endif()

add_executable(OMoutaEM4Tweaks
    src/launcher/main.cpp
    src/launcher/ConsoleMenu.cpp
    src/launcher/GuiLauncher.cpp
    src/launcher/PePatch.cpp
    src/launcher/ProcessLauncher.cpp
    src/launcher/TweakPackage.cpp
    resources/windows/OMoutaEM4Tweaks.rc
)

target_compile_definitions(OMoutaEM4Tweaks PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

target_link_libraries(OMoutaEM4Tweaks PRIVATE
    comdlg32
    comctl32
    gdiplus
    kernel32
    shell32
    user32
)

target_include_directories(OMoutaEM4Tweaks PRIVATE
    src
)

set_target_properties(OMoutaEM4Tweaks PROPERTIES
    WIN32_EXECUTABLE TRUE
)

add_subdirectory(src/hooks)
add_subdirectory(src/runtime)
