# Native xxwidgets front end. The scan engine remains in xxfclib; the widget
# library only needs its public type headers and does not link the engine.
if(NOT TARGET xxwidgets::xxwidgets)
    set(_CDIE_DEFAULT_XXWIDGETS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../dep/xxwidgets")
    if(NOT EXISTS "${_CDIE_DEFAULT_XXWIDGETS_DIR}/CMakeLists.txt")
        set(_CDIE_DEFAULT_XXWIDGETS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../_mylibs/xxwidgets")
    endif()
    set(CDIE_XXWIDGETS_DIR "${_CDIE_DEFAULT_XXWIDGETS_DIR}"
        CACHE PATH "Location of the xxwidgets source tree")
    if(NOT EXISTS "${CDIE_XXWIDGETS_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "cdie_gui needs xxwidgets. Set CDIE_XXWIDGETS_DIR to its source tree.")
    endif()
    add_subdirectory("${CDIE_XXWIDGETS_DIR}" "${CMAKE_BINARY_DIR}/xxwidgets" EXCLUDE_FROM_ALL)
endif()

# WIN32 only affects Windows builds; it is ignored elsewhere.
add_executable(cdie_gui WIN32
    ${CDIE_LIB_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/core/gui_backend.c
    ${CMAKE_CURRENT_LIST_DIR}/core/gui_entry.c
)

if(WIN32)
    target_sources(cdie_gui PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/main_gui.c
        ${CMAKE_CURRENT_LIST_DIR}/core/tray.c
        ${CMAKE_CURRENT_LIST_DIR}/core/config.c
        ${CMAKE_CURRENT_LIST_DIR}/core/dlwatch.c
        ${CMAKE_CURRENT_LIST_DIR}/core/dbupdater.c
        ${CMAKE_CURRENT_LIST_DIR}/core/appupdate.c
        ${CMAKE_CURRENT_LIST_DIR}/core/i18n.c
        ${CMAKE_CURRENT_LIST_DIR}/panels/header.c
        ${CMAKE_CURRENT_LIST_DIR}/panels/infobar.c
        ${CMAKE_CURRENT_LIST_DIR}/panels/options.c
        ${CMAKE_CURRENT_LIST_DIR}/panels/entropy.c
        ${CMAKE_CURRENT_LIST_DIR}/panels/signatures.c
        ${CMAKE_CURRENT_LIST_DIR}/panels/strings.c
        ${CMAKE_CURRENT_LIST_DIR}/panels/ntview.c)
else()
    target_sources(cdie_gui PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/main_gui_gtk.c)
endif()

# Platform-neutral controls (they call into the pl_* layer).
target_sources(cdie_gui PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/controls/sidebar.c
    ${CMAKE_CURRENT_LIST_DIR}/controls/tabbar.c
    ${CMAKE_CURRENT_LIST_DIR}/controls/bottombar.c
    ${CMAKE_CURRENT_LIST_DIR}/controls/treeview.c
    ${CMAKE_CURRENT_LIST_DIR}/controls/hexviewer.c
    ${CMAKE_CURRENT_LIST_DIR}/controls/disasmview.c
    ${CMAKE_CURRENT_LIST_DIR}/controls/palette.c
    ${CMAKE_CURRENT_LIST_DIR}/core/disasm_backend.c
    ${CMAKE_CURRENT_LIST_DIR}/core/virustotal.c
    ${CMAKE_CURRENT_LIST_DIR}/panels/sectionsview.c)

target_include_directories(cdie_gui PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/..
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/resource)

target_link_libraries(cdie_gui PRIVATE xxfclib::die_engine xxwidgets::xxwidgets cdisasm)

if(WIN32)
    if(CDIE_STORE_BUILD)
        target_compile_definitions(cdie_gui PRIVATE CDIE_STORE_BUILD)
    endif()
    target_compile_definitions(cdie_gui PRIVATE CDISASM_STATIC)
    target_sources(cdie_gui PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/platform/platform_win32.c
        ${CMAKE_CURRENT_LIST_DIR}/resource/cdie_gui.rc)
    target_link_libraries(cdie_gui PRIVATE
        user32 gdi32 comdlg32 comctl32 shell32 ole32 advapi32 dwmapi msimg32)
elseif(APPLE)
    enable_language(OBJC)
    target_sources(cdie_gui PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/platform/platform_cocoa.m)
    target_compile_options(cdie_gui PRIVATE -fobjc-arc)
    target_link_libraries(cdie_gui PRIVATE "-framework Cocoa")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
    target_sources(cdie_gui PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/platform/platform_gtk.c)
    target_link_libraries(cdie_gui PRIVATE PkgConfig::GTK3)
endif()

if(MSVC)
    foreach(_t die_engine cdisasm xxwidgets)
    if(TARGET ${_t})
        target_compile_options(${_t} PRIVATE
            $<$<CONFIG:Release>:/O1>
            $<$<CONFIG:Release>:/Oi>
            $<$<CONFIG:Release>:/Gy>
            $<$<CONFIG:Release>:/GF>)
    endif()
endforeach()

    target_link_options(cdie_gui PRIVATE /MANIFEST:NO)
    target_compile_options(cdie_gui PRIVATE /utf-8
        $<$<CONFIG:Release>:/O1>
        $<$<CONFIG:Release>:/Gy>)
    target_link_options(cdie_gui PRIVATE
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>)

    if(CDIE_NO_CRT AND NOT BUILD_SHARED_LIBS)
        set_property(TARGET cdie_gui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        if(TARGET xxwidgets)
            set_property(TARGET xxwidgets PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        endif()
        message(STATUS "cdie_gui: xxwidgets frontend with static C runtime")
    endif()
endif()

# Add headers so they appear in Solution Explorer (they are not compiled).
file(GLOB_RECURSE _cdie_gui_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_SOURCE_DIR}/src/app/*.h"
    "${CMAKE_SOURCE_DIR}/src/global.h")
target_sources(cdie_gui PRIVATE ${_cdie_gui_headers})

# Group files in Solution Explorer: separate Source/Header/Resource roots,
# each mirroring the folder layout under src\.
get_target_property(_cdie_gui_all cdie_gui SOURCES)
list(FILTER _cdie_gui_all EXCLUDE REGEX "^\\$<")

set(_cdie_gui_abs "")
foreach(_f IN LISTS _cdie_gui_all)
    get_filename_component(_abs "${_f}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    list(APPEND _cdie_gui_abs "${_abs}")
endforeach()
# Only files under src\ can be grouped by TREE.
list(FILTER _cdie_gui_abs INCLUDE REGEX "^${CMAKE_SOURCE_DIR}/src/")

set(_cdie_gui_grp_h ${_cdie_gui_abs})
list(FILTER _cdie_gui_grp_h INCLUDE REGEX "\\.(h|hpp)$")

set(_cdie_gui_grp_rc ${_cdie_gui_abs})
list(FILTER _cdie_gui_grp_rc INCLUDE REGEX "\\.(rc|ico|manifest)$")

set(_cdie_gui_grp_c ${_cdie_gui_abs})
list(FILTER _cdie_gui_grp_c INCLUDE REGEX "\\.(c|cpp|m)$")

source_group(TREE "${CMAKE_SOURCE_DIR}/src" PREFIX "Source Files"   FILES ${_cdie_gui_grp_c})
source_group(TREE "${CMAKE_SOURCE_DIR}/src" PREFIX "Header Files"   FILES ${_cdie_gui_grp_h})
source_group(TREE "${CMAKE_SOURCE_DIR}/src" PREFIX "Resource Files" FILES ${_cdie_gui_grp_rc})
install(TARGETS cdie_gui RUNTIME DESTINATION ".")