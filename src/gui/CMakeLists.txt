# Windows-only native GUI front end (pure WinAPI, no Qt, no toolkit).
#
# It reuses the whole engine (CDIE_LIB_SOURCES = every engine .c minus the
# console entry point, inherited from src/CMakeLists.txt) and adds its own
# frontend + entry point. Two build shapes, chosen by the same CDIE_NO_CRT
# cache option the console defines:
#
#   CDIE_NO_CRT ON  (default on 64-bit MSVC): no C runtime. Entry point is
#                    x_gui_entry_point, subsystem WINDOWS, imports KERNEL32 plus
#                    the GUI system DLLs only - matching the console's ethos.
#   CDIE_NO_CRT OFF (32-bit, or opted out): a normal WinMain links the CRT.

add_executable(cdie_gui
    ${CDIE_LIB_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/gui_backend.c
    ${CMAKE_CURRENT_LIST_DIR}/main_gui.c
    ${CMAKE_CURRENT_LIST_DIR}/gui_entry.c
    ${CMAKE_CURRENT_LIST_DIR}/cdie_gui.rc
)

target_include_directories(cdie_gui PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)

target_link_libraries(cdie_gui PRIVATE xxfclib::xxfclib)

# The manifest (common-controls v6 + DPI) is embedded from cdie_gui.rc. Stop the
# MSVC toolchain from also generating and embedding its own manifest, which would
# collide as a duplicate RT_MANIFEST resource (CVT1100).
if(MSVC)
    target_link_options(cdie_gui PRIVATE /MANIFEST:NO)
endif()

set(CDIE_GUI_SYSLIBS user32.lib gdi32.lib comdlg32.lib comctl32.lib shell32.lib ole32.lib)

if(CDIE_NO_CRT AND MSVC)
    target_compile_definitions(cdie_gui PRIVATE CDIE_NO_CRT)

    target_compile_options(cdie_gui PRIVATE
        /GS-  # no stack security cookie (__security_check_cookie)
        /GL-  # keep whole program optimisation from pulling in helpers
    )

    target_link_options(cdie_gui PRIVATE
        /NODEFAULTLIB
        /ENTRY:x_gui_entry_point
        /SUBSYSTEM:WINDOWS
        /OPT:REF
        /OPT:ICF
    )

    target_link_libraries(cdie_gui PRIVATE kernel32.lib ${CDIE_GUI_SYSLIBS})

    message(STATUS "cdie_gui: CRT-free build, system DLLs only")
else()
    # Hosted build: WinMain + the CRT startup. WIN32_EXECUTABLE selects the
    # WINDOWS subsystem and the WinMainCRTStartup entry.
    set_target_properties(cdie_gui PROPERTIES WIN32_EXECUTABLE ON)
    target_link_libraries(cdie_gui PRIVATE ${CDIE_GUI_SYSLIBS})
endif()

install(TARGETS cdie_gui RUNTIME DESTINATION ".")
