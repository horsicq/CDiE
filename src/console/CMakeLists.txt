# Console executable.

add_executable(cdie
    ${CDIE_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/main_console.c
)

target_include_directories(cdie PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)

target_link_libraries(cdie PRIVATE xxfclib::xxfclib)

# ---------------------------------------------------------------- no CRT ---
#
# With CDIE_NO_CRT the binary links against KERNEL32 alone: no msvcrt, no
# vcruntime, no api-ms-win-crt-*. The entry point becomes x_entry_point in
# core/utils_entry.c, which builds argv from GetCommandLineA and calls
# x_main().
#
# x86 additionally needs the compiler's 64-bit arithmetic helpers (_alldiv,
# _allshr and friends) which live in libcmt, so the option defaults to ON
# only for 64 bit MSVC builds.

# CDIE_NO_CRT is declared at the top level, where it also decides how the
# xxfclib archive is compiled. Here it is only consumed.

if(CDIE_NO_CRT AND MSVC)
    target_compile_definitions(cdie PRIVATE CDIE_NO_CRT)

    target_compile_options(cdie PRIVATE
        /GS-  # no stack security cookie (__security_check_cookie)
        /GL-  # keep whole program optimisation from pulling in helpers
    )

    target_link_options(cdie PRIVATE
        /NODEFAULTLIB
        /ENTRY:x_entry_point
        /SUBSYSTEM:CONSOLE
        /OPT:REF
        /OPT:ICF
    )

    target_link_libraries(cdie PRIVATE kernel32.lib)

    message(STATUS "cdie: CRT-free build, KERNEL32 import only")
endif()

if(UNIX)
    find_library(M_LIB m)

    if(M_LIB)
        target_link_libraries(cdie PRIVATE ${M_LIB})
    endif()
endif()

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS cdie RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
else()
    install(TARGETS cdie RUNTIME DESTINATION ".")
endif()
