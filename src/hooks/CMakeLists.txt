option(OMOUTA_BUILD_HOOKS "Build tweak hook DLL packages." ON)

function(omouta_ensure_minhook)
    get_property(minhook_ready GLOBAL PROPERTY OMOUTA_MINHOOK_READY)
    if(minhook_ready)
        return()
    endif()

    include(FetchContent)
    FetchContent_Declare(
        minhook
        GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
        GIT_TAG v1.3.4
    )
    FetchContent_MakeAvailable(minhook)

    set_property(GLOBAL PROPERTY OMOUTA_MINHOOK_READY TRUE)
endfunction()

function(omouta_configure_hook_package target package_name manifest_path)
    set(options USE_MINHOOK)
    cmake_parse_arguments(HOOK "${options}" "" "" ${ARGN})

    target_compile_definitions(${target} PRIVATE
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
    )

    target_include_directories(${target} PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
    )

    if(HOOK_USE_MINHOOK)
        omouta_ensure_minhook()
        target_link_libraries(${target} PRIVATE minhook)
    endif()

    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${target}"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>/OMoutaEM4Tweaks/Hooks/${package_name}"
    )

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${manifest_path}"
            "$<TARGET_FILE_DIR:${target}>/tweak.ini"
    )
endfunction()

if(OMOUTA_BUILD_HOOKS)
    file(GLOB hook_cmake_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/*/CMakeLists.txt")
    foreach(hook_cmake_file IN LISTS hook_cmake_files)
        get_filename_component(hook_dir "${hook_cmake_file}" DIRECTORY)
        add_subdirectory("${hook_dir}")
    endforeach()
endif()
