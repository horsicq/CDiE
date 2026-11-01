# die-update: checks for and installs signature database updates.
#
# cdie_updater holds the logic and is linked by die-update and by the GUIs,
# which run it in-process. xxgithub/xxnetlib are linked PRIVATE so their C11
# compile feature stays on this library and does not reach the frontends; the
# header names xxfclib types only.

add_library(cdie_updater STATIC
    ${CMAKE_CURRENT_LIST_DIR}/cdie_updater.h
    ${CMAKE_CURRENT_LIST_DIR}/cdie_updater.c
)
target_include_directories(cdie_updater PUBLIC ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(cdie_updater PUBLIC xxfclib::die_engine cdie_settings PRIVATE xxgithub::xxgithub)
target_compile_definitions(cdie_updater PUBLIC CDIE_HAVE_UPDATER=1)
# In the FHS layout (.deb, AppImage, /usr or /usr/local) the package manager
# owns the database beside the programs. CDIE_SYSTEM_INSTALL makes the updater
# treat that default folder as read-only and install updates into the
# per-user folder, even when run as root, so it never rewrites package files.
if(CDIE_INSTALL_LAYOUT STREQUAL "FHS")
    target_compile_definitions(cdie_updater PRIVATE CDIE_SYSTEM_INSTALL=1)
endif()

# The executable reuses cdie's entry point (UTF-8 argv on Windows) and app
# helpers, hosted: it needs the C runtime for the network backends.
add_executable(cdie_update ${CDIE_SOURCES} ${CMAKE_CURRENT_LIST_DIR}/main_updater.c)
set_target_properties(cdie_update PROPERTIES OUTPUT_NAME "die-update")
target_compile_definitions(cdie_update PRIVATE X_APPLICATIONNAME=\"die-update\")
target_include_directories(cdie_update PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(cdie_update PRIVATE cdie_updater)

if(MSVC)
    target_compile_options(cdie_updater PRIVATE /utf-8)
    target_compile_options(cdie_update PRIVATE /utf-8)
    if(CDIE_NO_CRT AND NOT BUILD_SHARED_LIBS)
        # die_engine is built for the static runtime in this shape; every
        # hosted object linked with it has to match (see src/gui).
        foreach(_cdie_updater_target cdie_updater cdie_update)
            set_property(TARGET ${_cdie_updater_target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        endforeach()
    endif()
endif()

if(UNIX)
    find_library(CDIE_UPDATER_M_LIB m)
    if(CDIE_UPDATER_M_LIB)
        target_link_libraries(cdie_update PRIVATE ${CDIE_UPDATER_M_LIB})
    endif()
endif()

# diec launches die-update from its own folder (see main_console.c), so the
# build tree keeps a copy beside diec as every package does. The destination
# names the file: copying to a folder that does not exist yet (a per-config
# folder of the Visual Studio generator) would create a file in its place.
if(TARGET cdie)
    add_dependencies(cdie_update cdie)
    add_custom_command(TARGET cdie_update POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:cdie_update>" "$<TARGET_FILE_DIR:cdie>/$<TARGET_FILE_NAME:cdie_update>"
        VERBATIM)
endif()

# macOS carries die-update inside die.app (src/gui/CMakeLists.txt); elsewhere
# it sits beside die and diec.
if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS cdie_update RUNTIME DESTINATION "${CDIE_BINARY_DESTINATION}")
elseif(NOT APPLE)
    install(TARGETS cdie_update RUNTIME DESTINATION ".")
endif()
