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

include(FetchContent)

FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.4
)
FetchContent_MakeAvailable(minhook)

add_executable(OMoutaEM4Tweaks
    src/launcher/main.cpp
    src/launcher/ConsoleMenu.cpp
    src/launcher/GuiLauncher.cpp
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
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/OMoutaEM4Tweaks/Hooks/BorderlessWindowFix"
)

add_custom_command(TARGET BorderlessWindowFix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:OMoutaEM4Tweaks>/$<TARGET_FILE_NAME:BorderlessWindowFix>"
    COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:OMoutaEM4Tweaks>/OMoutaEM4Tweaks/Hooks/$<TARGET_FILE_NAME:BorderlessWindowFix>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/src/hooks/borderless/tweak.ini" "$<TARGET_FILE_DIR:BorderlessWindowFix>/tweak.ini"
)
