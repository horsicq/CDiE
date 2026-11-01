# Console executable.

add_executable(cdie
    ${CDIE_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/main_console.c
    ${CMAKE_CURRENT_LIST_DIR}/../app/cdie_file_info.c
    ${CMAKE_CURRENT_LIST_DIR}/../app/cdie_entropy.c
    ${CMAKE_CURRENT_LIST_DIR}/../app/cdie_hash.c
    ${CMAKE_CURRENT_LIST_DIR}/../app/cdie_strings.c
    # Runs die-update for --checkupdate/--update; diec only.
    ${CMAKE_CURRENT_LIST_DIR}/../app/cdie_spawn.h
    ${CMAKE_CURRENT_LIST_DIR}/../app/cdie_spawn.c
)

set_target_properties(cdie PROPERTIES OUTPUT_NAME "diec")
target_compile_definitions(cdie PRIVATE X_APPLICATIONNAME=\"diec\")

target_include_directories(cdie PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)

target_link_libraries(cdie PRIVATE xxfclib::die_engine)
cdie_stage_frontend_resources(cdie)

# ---------------------------------------------------------------- no CRT ---
#
# With CDIE_NO_CRT the binary links against KERNEL32 alone: no msvcrt, no
# vcruntime, no api-ms-win-crt-*. The entry point becomes x_entry_point in
# core/utils_entry.c, which builds argv from GetCommandLineW and calls
# x_main().
#
# x86 additionally needs the compiler's 64-bit arithmetic helpers (_alldiv,
# _allshr and friends) which live in libcmt, so the option defaults to ON
# only for MSVC x64 and ARM64 builds.

# CDIE_NO_CRT is declared at the top level, where it also decides how the
# die_engine archive is compiled. Here it is only consumed.

if(CDIE_NO_CRT AND MSVC)
    target_compile_definitions(cdie PRIVATE CDIE_NO_CRT)

    target_compile_options(cdie PRIVATE
        /GS-  # no stack security cookie (__security_check_cookie)
        /sdl- # keep SDL from re-enabling the security-cookie instrumentation
        /GL-  # keep whole program optimisation from pulling in helpers
    )

    if(POLICY CMP0184)
        set_property(TARGET cdie PROPERTY MSVC_RUNTIME_CHECKS "")
    endif()
    target_link_options(cdie PRIVATE
        /NODEFAULTLIB
        /ENTRY:x_entry_point
        /SUBSYSTEM:CONSOLE
        /OPT:REF
        /OPT:ICF
    )

    target_link_libraries(cdie PRIVATE kernel32.lib)
    if(TARGET xxfclib::no_crt_runtime)
        target_link_libraries(cdie PRIVATE xxfclib::no_crt_runtime)
    endif()

    message(STATUS "cdie: CRT-free build, KERNEL32 import only")
endif()

if(UNIX)
    find_library(M_LIB m)

    if(M_LIB)
        target_link_libraries(cdie PRIVATE ${M_LIB})
    endif()
endif()

# bin/ on Linux (lib/cdie in the FHS layout), the prefix root elsewhere; see
# CDIE_BINARY_DESTINATION in the top-level CMakeLists.txt.
install(TARGETS cdie RUNTIME DESTINATION "${CDIE_BINARY_DESTINATION}")
