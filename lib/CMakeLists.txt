# The Detect It Easy library — die_library-compatible C API over the cdie
# engine. Builds both a shared and a static library from the same sources.
#
#   die_static  ->  die_static.lib / libdie.a     (CRT-free)
#   die_shared  ->  die.dll        / libdie.so    (default CRT)
#
# The public header is lib/die.h; it is ABI-compatible with horsicq/die_library.

option(DIE_BUILD_STATIC "Build the static library" ON)
option(DIE_BUILD_SHARED "Build the shared library" ON)

set(DIE_LIB_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/die.h
    ${CMAKE_CURRENT_LIST_DIR}/die.c
    ${CDIE_LIB_SOURCES}
)

# ---------------------------------------------------------------- static ---

if(DIE_BUILD_STATIC)
    add_library(die_static STATIC ${DIE_LIB_SOURCES})
    # PUBLIC: an archive cannot resolve its own dependencies, so whoever links
    # die_static must also link xxfclib. This does change the documented
    # one-liner in docs/LIBRARY.md -- consumers now need xxfclib on the link
    # line too, or CMake's die_static target which carries it for them.
    target_link_libraries(die_static PUBLIC xxfclib::xxfclib)
    target_include_directories(die_static PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    target_compile_definitions(die_static PUBLIC DIE_STATIC)

    if(WIN32)
        set_target_properties(die_static PROPERTIES OUTPUT_NAME die_static)
    else()
        set_target_properties(die_static PROPERTIES OUTPUT_NAME die)
    endif()
endif()

# ---------------------------------------------------------------- shared ---

if(DIE_BUILD_SHARED)
    add_library(die_shared SHARED ${DIE_LIB_SOURCES})
    target_link_libraries(die_shared PRIVATE xxfclib::xxfclib)
    # C_VISIBILITY_PRESET below governs only the objects CMake compiles for
    # this target; the archive's objects were compiled separately and carry
    # default visibility. Without this every xxfclib global -- db_load,
    # xx_rt_malloc, js_new, several hundred more -- would land in libdie.so's
    # dynamic symbol table and be interposable, which is exactly what the
    # hidden preset exists to prevent.
    if(NOT WIN32 AND NOT APPLE)
        target_link_options(die_shared PRIVATE "LINKER:--exclude-libs,ALL")
    endif()
    target_include_directories(die_shared PUBLIC ${CMAKE_CURRENT_LIST_DIR})
    target_compile_definitions(die_shared PRIVATE DIE_BUILD_SHARED)

    # Hidden by default so only the DIE_* entry points (which die.h marks
    # visibility("default") when DIE_BUILD_SHARED is set) reach the dynamic
    # symbol table. Without it the engine's internals -- cd_malloc, db_load,
    # inflate_raw and ~300 more -- take part in ELF symbol interposition and a
    # host that happens to define one of those names silently replaces it.
    #
    # VERSION/SOVERSION give the library a real soname so a distribution can
    # carry two ABI revisions; they are ignored on Windows.
    set_target_properties(die_shared PROPERTIES
        OUTPUT_NAME die
        C_VISIBILITY_PRESET hidden
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})
endif()

# ------------------------------------------------------------------- libm --
#
# core/utils_math.c lowers to a real sqrt() call at -O0 and -Os, so both
# targets need libm. It is PUBLIC/INTERFACE so a consumer of the static
# archive inherits it too; without it "gcc simple.c -l:libdie.so" fails with
# an undefined reference to sqrt on any non-Release build.

if(UNIX)
    find_library(M_LIB m)

    if(M_LIB)
        if(DIE_BUILD_STATIC)
            target_link_libraries(die_static PUBLIC ${M_LIB})
        endif()

        if(DIE_BUILD_SHARED)
            target_link_libraries(die_shared PUBLIC ${M_LIB})
        endif()
    endif()
endif()

# ---------------------------------------------------------------- MSVC CRT‑free flags (STATIC ONLY)
#
# /Zl is what actually makes the archive CRT-free: it keeps cl from emitting
# the /DEFAULTLIB records, so a consumer picks its own runtime. Do not remove
# it. /NODEFAULTLIB belongs on the *consumer's* link line -- a STATIC library
# is produced by lib.exe, which never sees LINK_OPTIONS, so putting it here
# would be silently discarded. /GR-, /EH* and /kernel are C++-only or
# deprecated (D9035 on VS2019+) and have no place on a C-only target.

if (MSVC)
    if(DIE_BUILD_STATIC)
        target_compile_options(die_static PRIVATE
            /GS-
            /Zl
            /DNDEBUG
        )
    endif()
endif()

# ---------------------------------------------------------------- install --

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)

    if(DIE_BUILD_STATIC)
        install(TARGETS die_static
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()

    if(DIE_BUILD_SHARED)
        install(TARGETS die_shared
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
            LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
            ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    endif()

else()
    if(DIE_BUILD_STATIC)
        install(TARGETS die_static
            RUNTIME DESTINATION "."
            LIBRARY DESTINATION "."
            ARCHIVE DESTINATION ".")
    endif()

    if(DIE_BUILD_SHARED)
        install(TARGETS die_shared
            RUNTIME DESTINATION "."
            LIBRARY DESTINATION "."
            ARCHIVE DESTINATION ".")
    endif()
endif()

install(FILES ${CMAKE_CURRENT_LIST_DIR}/die.h DESTINATION "include" OPTIONAL)
