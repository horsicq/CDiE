# cdie - C port of the Detect It Easy console scanner.
#
# The engine now comes from xxfclib: the JavaScript interpreter, the binary
# format parsers and the signature engine all live in this source tree.

# The C standard and the warning flags are set in the top-level CMakeLists so
# that lib/ and tests/ -- which are siblings of this directory, not children --
# are held to the same bar. Do not re-add them here.

set(X_COMPANYNAME       "NTInfo")
set(X_PROJECTNAME       "cdie")
set(X_MAINTAINER        "horsicq@gmail.com")
set(X_DESCRIPTION       "Detect It Easy console engine (C port)")
set(X_HOMEPAGE          "http://ntinfo.biz")
set(X_ORIGINAL_FILENAME "cdie")

# What is left of cdie's own sources. The scan engine, the format parsers, the
# JavaScript interpreter and the CRT-compatible runtime all live in xxfclib now
# (xxfclib::die_engine, added at the top level) and are no longer built here.
#
# utils_entry.c stays: it is the CRT-free entry point, and an entry point is the
# one thing a library cannot supply for the program that links it. It also
# supplies memset/memcpy/memmove for the /NODEFAULTLIB link -- xxfclib provides
# those only in its own shared configuration, never in the static archive cdie
# consumes, so there is no duplicate definition.
set(CDIE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/global.h

    ${CMAKE_CURRENT_LIST_DIR}/core/utils_entry.c

    ${CMAKE_CURRENT_LIST_DIR}/app/cdie_app.h
    ${CMAKE_CURRENT_LIST_DIR}/app/cdie_app.c
    ${CMAKE_CURRENT_LIST_DIR}/app/cdie_paths.h
    ${CMAKE_CURRENT_LIST_DIR}/app/cdie_paths.c
)

# The library targets (see lib/) reuse the whole engine but supply their own
# entry points, so they exclude core/utils_entry.c (which defines main / the
# CRT-free startup). Export both lists to the parent scope.
set(CDIE_LIB_SOURCES ${CDIE_SOURCES})
list(REMOVE_ITEM CDIE_LIB_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/core/utils_entry.c)

set(CDIE_SOURCES "${CDIE_SOURCES}" PARENT_SCOPE)
set(CDIE_LIB_SOURCES "${CDIE_LIB_SOURCES}" PARENT_SCOPE)

# The desktop frontend uses the shared widget library. Its terminal backend
# remains available to developer probes, but no TUI application is built.
if(CDIE_BUILD_GUI)
    if(CMAKE_VERSION VERSION_LESS 3.21)
        message(FATAL_ERROR "cdie GUI/TUI builds require CMake 3.21 or newer")
    endif()
    if(NOT TARGET xxwidgets::xxwidgets)
        set(_CDIE_DEFAULT_XXWIDGETS_DIR "${CMAKE_CURRENT_LIST_DIR}/../dep/xxwidgets")
        if(NOT EXISTS "${_CDIE_DEFAULT_XXWIDGETS_DIR}/CMakeLists.txt")
            set(_CDIE_DEFAULT_XXWIDGETS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../_mylibs/xxwidgets")
        endif()
        set(CDIE_XXWIDGETS_DIR "${_CDIE_DEFAULT_XXWIDGETS_DIR}"
            CACHE PATH "Location of the xxwidgets source tree")
        if(NOT EXISTS "${CDIE_XXWIDGETS_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "cdie GUI/TUI need xxwidgets. Set CDIE_XXWIDGETS_DIR to its source tree.")
        endif()
        function(cdie_add_widgets)
            set(CMAKE_POLICY_DEFAULT_CMP0126 NEW)
            set(XXWIDGETS_BUILD_EXAMPLES OFF)
            set(XXWIDGETS_BUILD_TESTS OFF)
            if(XXFCLIB_DIR)
                set(XXWIDGETS_XXFCLIB_DIR "${XXFCLIB_DIR}")
            endif()
            add_subdirectory("${CDIE_XXWIDGETS_DIR}" "${CMAKE_BINARY_DIR}/xxwidgets" EXCLUDE_FROM_ALL)
        endfunction()
        cdie_add_widgets()
    endif()
endif()

# Persistent Options for the GUI and die-update's settings: the xxfclib
# settings store. Its runtime (xx_rt, memory, strings) already comes with
# die_engine.
if(CDIE_BUILD_GUI OR CDIE_BUILD_UPDATER)
    set(_cdie_settings_src "${xxfclib_SOURCE_DIR}/src")
    add_library(cdie_settings STATIC
        "${_cdie_settings_src}/settings/xx_settings.c"
        "${_cdie_settings_src}/settings/xx_settings_ini.c"
        "${_cdie_settings_src}/settings/xx_settings_value.c"
        "${_cdie_settings_src}/global/xx_settings_global.c")
    if(WIN32)
        target_sources(cdie_settings PRIVATE
            "${_cdie_settings_src}/settings/platforms/xx_settings_file_windows.c"
            "${_cdie_settings_src}/settings/platforms/xx_settings_windows.c")
    else()
        target_sources(cdie_settings PRIVATE "${_cdie_settings_src}/settings/platforms/xx_settings_file_posix.c")
        target_compile_definitions(cdie_settings PRIVATE _POSIX_C_SOURCE=200809L)
        if(APPLE)
            target_sources(cdie_settings PRIVATE "${_cdie_settings_src}/settings/platforms/xx_settings_macos.c")
            target_compile_definitions(cdie_settings PRIVATE _DARWIN_C_SOURCE)
            target_link_libraries(cdie_settings PRIVATE "-framework CoreFoundation")
        else()
            target_sources(cdie_settings PRIVATE "${_cdie_settings_src}/settings/platforms/xx_settings_posix.c")
        endif()
    endif()
    target_link_libraries(cdie_settings PUBLIC xxfclib::die_engine)
    if(MSVC AND CDIE_NO_CRT AND NOT BUILD_SHARED_LIBS)
        set_property(TARGET cdie_settings PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
    endif()
endif()

if(CDIE_BUILD_GUI)
    add_library(cdie_ui_translation STATIC app/cdie_translation.c)
    target_link_libraries(cdie_ui_translation PUBLIC xxwidgets::xxwidgets)
    target_include_directories(cdie_ui_translation PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
    if(MSVC)
        target_compile_options(cdie_ui_translation PRIVATE /utf-8)
    endif()
    if(MSVC AND CDIE_NO_CRT AND NOT BUILD_SHARED_LIBS)
        # Every embedded widget object must match the hosted frontend's CRT.
        foreach(_widget_target cdie_ui_translation cdie_settings xxwidgets xxwidgets_io xxwidgets_history xxwidgets_translation
                xxwidgets_threads xxwidgets_codepages xxwidgets_scan_colors)
            if(TARGET ${_widget_target})
                set_property(TARGET ${_widget_target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
            endif()
        endforeach()
    endif()
endif()

# Place signature databases and notices beside standalone frontends, using
# the same resource staging rules as the native macOS app bundle.
set(_CDIE_RESOURCES_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/gui/bundle_resources.cmake")
set(_CDIE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
function(cdie_stage_frontend_resources target)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${CDIE_TRANSLATION_CATALOGS})
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DDATABASE_DIR=${CDIE_DATABASE_DIR}"
            "-DINFO_DIR=${CDIE_INFO_DIR}"
            "-DRESOURCES_DIR=$<TARGET_FILE_DIR:${target}>"
            "-DCDIE_SOURCE_DIR=${_CDIE_SOURCE_DIR}"
            "-DCDISASM_DIR=${XXFC_CDISASM_DIR}"
            -P "${_CDIE_RESOURCES_SCRIPT}"
        VERBATIM)
endfunction()

if(CDIE_BUILD_CONSOLE)
    add_subdirectory(console)
endif()
# After the console (it copies die-update beside diec) and before the GUI
# (which links cdie_updater).
if(CDIE_BUILD_UPDATER)
    add_subdirectory(updater)
endif()
if(CDIE_BUILD_GUI)
    add_subdirectory(gui)
endif()
