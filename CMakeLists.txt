cmake_minimum_required(VERSION 3.21)

project(OMoutasEM4Tweaks LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    message(FATAL_ERROR "This project must be built with MSVC / Visual Studio 2022.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Build with the Win32/x86 platform. Do not build x64.")
endif()

include(FetchContent)

FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
)
FetchContent_MakeAvailable(minhook)

add_executable(OMoutasEM4Tweaks
    src/launcher/main.cpp
    src/launcher/ConsoleMenu.cpp
    src/launcher/ProcessLauncher.cpp
    resources/windows/OMoutasEM4Tweaks.rc
)

target_compile_definitions(OMoutasEM4Tweaks PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

target_link_libraries(OMoutasEM4Tweaks PRIVATE
    kernel32
)

target_include_directories(OMoutasEM4Tweaks PRIVATE
    src
)

add_library(BorderlessWindowFix SHARED
    src/hooks/borderless/HookEntry.cpp
    src/hooks/borderless/BorderlessTweak.cpp
)

target_compile_definitions(BorderlessWindowFix PRIVATE
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
)

target_link_libraries(BorderlessWindowFix PRIVATE
    minhook
    user32
    kernel32
)

target_include_directories(BorderlessWindowFix PRIVATE
    src
)

set_target_properties(BorderlessWindowFix PROPERTIES
    OUTPUT_NAME "BorderlessWindowFix"
)

add_custom_command(TARGET BorderlessWindowFix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:OMoutasEM4Tweaks>/OMoutasEM4Tweaks/Hooks"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:BorderlessWindowFix>" "$<TARGET_FILE_DIR:OMoutasEM4Tweaks>/OMoutasEM4Tweaks/Hooks/$<TARGET_FILE_NAME:BorderlessWindowFix>"
)
