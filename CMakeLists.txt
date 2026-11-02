cmake_minimum_required(VERSION 3.16)

# Let per-target MSVC_RUNTIME_LIBRARY choose the CRT (the library targets pick
# the dynamic CRT so they link cleanly against default-built consumers).
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
if(POLICY CMP0184)
    # Select per-target runtime checks before C is first enabled. CRT-free
    # targets clear this property instead of inheriting Debug's /RTC flags.
    cmake_policy(SET CMP0184 NEW)
endif()
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/release_version.txt CDIE_VERSION)

# macOS: the binaries and Info.plist's LSMinimumSystemVersion (src/gui) must
# name the same macOS. Without a deployment target the compiler targets the
# SDK's macOS, newer than the plist claims, and an older macOS fails to load
# the app instead of naming the version it needs. It must be set before
# project(); the defaults are build_macos.sh's (11.0 for arm64 alone, 10.14
# otherwise), and MACOSX_DEPLOYMENT_TARGET or -DCMAKE_OSX_DEPLOYMENT_TARGET win.
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET AND NOT DEFINED ENV{MACOSX_DEPLOYMENT_TARGET})
    set(_cdie_macos_archs "${CMAKE_OSX_ARCHITECTURES}")
    if(NOT _cdie_macos_archs)
        execute_process(COMMAND uname -m OUTPUT_VARIABLE _cdie_macos_archs OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    if(_cdie_macos_archs MATCHES "^(arm64|aarch64)$")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    else()
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.14" CACHE STRING "Minimum macOS version")
    endif()
endif()

project(cdie VERSION ${CDIE_VERSION} LANGUAGES C)

set(X_COMPANYNAME "NTInfo")
set(X_DESCRIPTION "Detect It Easy console engine (C port)")
set(X_HOMEPAGE    "http://ntinfo.biz")
set(X_MAINTAINER  "horsicq@gmail.com")

# ---------------------------------------------------------- language + options
#
# The C standard and the warning policy live here rather than in src/ because
# lib/ and tests/ are siblings of src/, so a directory-scoped setting there
# would leave the library and test targets compiling cdie's remaining sources
# as GNU C17/C23 with no warning flags at all.
#
# The warning flags themselves are applied further down, after xxfclib has been
# added -- see the note there.

# Database creation needs plain TAR writing; other archive formats are read-only.
set(XXFC_DIE_ENGINE_TAR_ONLY_WRITING ON)

# Omit large East Asian double-byte codepage tables (~490 KiB) not needed by cdie.
set(XXFC_CODEPAGE_JAPANESE OFF CACHE BOOL "Include Japanese CP932" FORCE)
set(XXFC_CODEPAGE_CHINESE OFF CACHE BOOL "Include Chinese CP936 and CP950" FORCE)
set(XXFC_CODEPAGE_KOREAN OFF CACHE BOOL "Include Korean CP949" FORCE)

# Compile only the optional format streams used by this application.
if(NOT TARGET xxfclib::xxfclib)
    set(XXFC_ENABLE_STREAM_DATASTRUCTS ON)
    set(XXFC_ENABLE_STREAM_IMPORT ON)
    set(XXFC_ENABLE_STREAM_EXPORT ON)
    # Resource expansion uses the native PE/ELF resource stream readers.
    set(XXFC_ENABLE_STREAM_RESOURCES ON)
    set(XXFC_ENABLE_STREAM_METADATA OFF)
    # ELF imports/exports enumerate symbols through the native cursor.
    set(XXFC_ENABLE_STREAM_SYMBOLS ON)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CDIE_WERROR "Treat compiler warnings as errors" OFF)

# Supported frontend matrix and migration of cached legacy TUI settings.
include("${CMAKE_CURRENT_LIST_DIR}/cmake/cdie_frontends.cmake")
if(CDIE_BUILD_GUI AND NOT (WIN32 OR APPLE OR (UNIX AND NOT APPLE)))
    message(FATAL_ERROR "The cdie native GUI requires Windows, macOS, or a GTK desktop")
endif()

# The comprehensive private Radare2 gate covers each local library with a
# compatible upstream corpus: decoding/assembly, bytecode, demangling,
# emulation and file formats. The older ASM-only switch remains available for
# callers that use the focused dependency check.
set(XXFC_CDISASM_ENABLE_MIPS OFF)
# Signature scripts expect architecture-wide decoding. The base-only cdisasm
# profile rejects system and SIMD instructions that Qt DIE can disassemble.
set(XXFC_CDISASM_ENABLE_EXTRA ON)
option(CDIE_BUILD_PRIVATE_RADARE2_TESTS
    "Build all private Radare2-derived compatibility tests" OFF)
option(CDIE_BUILD_PRIVATE_ASM_TESTS
    "Build only the private ARM/x86 asm corpus test" OFF)

if(CDIE_BUILD_PRIVATE_ASM_TESTS OR CDIE_BUILD_PRIVATE_RADARE2_TESTS)
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/private_tests/CMakeLists.txt")
        message(FATAL_ERROR
            "Private Radare2 tests need private_tests/CMakeLists.txt")
    endif()
    enable_testing()
    set(XXFC_CDISASM_ENABLE_ARM ON)
    set(XXFC_CDISASM_ENABLE_EXTRA ON)
endif()

if(CDIE_BUILD_PRIVATE_RADARE2_TESTS)
    if(NOT DEFINED XXFC_RADARE2_TESTBINS OR
       NOT IS_DIRECTORY "${XXFC_RADARE2_TESTBINS}")
        message(FATAL_ERROR
            "CDIE_BUILD_PRIVATE_RADARE2_TESTS requires "
            "-DXXFC_RADARE2_TESTBINS=<pinned radare2-testbins checkout>")
    endif()

    get_filename_component(_CDIE_DEFAULT_PRIVATE_LIBS_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../_mylibs" ABSOLUTE)
    set(CDIE_PRIVATE_LIBS_DIR "${_CDIE_DEFAULT_PRIVATE_LIBS_DIR}"
        CACHE PATH "Directory containing the local libraries' private Radare2 tests")
    unset(_CDIE_DEFAULT_PRIVATE_LIBS_DIR)

    # Configure the format suite in xxfclib's own directory so its private
    # tests retain the correct project source directory for internal headers.
    set(XXFC_BUILD_RADARE2_PRIVATE_TESTS ON)
endif()

# Normal builds retain only base x86 and ARM/Thumb/AArch64 instructions.
# Extended opcode coverage is reserved for the opt-in private compatibility gate.
option(CDIE_DISASM_ARM "Include ARM/Thumb/AArch64 in the GUI disassembler" ON)
if(NOT (CDIE_BUILD_PRIVATE_ASM_TESTS OR CDIE_BUILD_PRIVATE_RADARE2_TESTS))
    set(XXFC_CDISASM_ENABLE_ARM ${CDIE_DISASM_ARM})
endif()

# ------------------------------------------------------------------ xxfclib --
#
# Added HERE, above the warning block, on purpose. add_compile_options() and
# add_compile_definitions() are directory properties: a subdirectory inherits a
# snapshot taken at the add_subdirectory() call, so anything set after this
# point cannot reach xxfclib. Added below the block instead, every xxfclib TU
# compiles as "/W3 /W4" (211 D9025 warnings) and, with CDIE_WERROR=ON, as
# "/W3 /WX /W4" -- cdie's warning policy would then govern a library cdie does
# not own, and on GCC the trailing -Wextra would undo cdie's
# -Wno-unused-parameter as well. xxfclib sets its own /W4 (or -Wall -Wextra
# -Wpedantic), its own C11, and its own _CRT_SECURE_NO_WARNINGS, so it loses
# nothing by being configured first.
#
# Whether the console links without the CRT also has to be settled BEFORE
# xxfclib is configured, because it decides how the archive itself must be
# compiled: a /NODEFAULTLIB program cannot link objects that reference
# __GSHandlerCheck or carry /DEFAULTLIB records. x86 additionally needs the
# compiler's 64-bit arithmetic helpers (_alldiv, _allshr and friends) from
# libcmt. The in-tree compiler helpers cover x64 and ARM64, so those are the
# two architectures where this defaults to ON.
set(CDIE_NO_CRT_SUPPORTED FALSE)
if(MSVC AND CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|ARM64)$")
    set(CDIE_NO_CRT_SUPPORTED TRUE)
    set(CDIE_NO_CRT_DEFAULT ON)
else()
    set(CDIE_NO_CRT_DEFAULT OFF)
endif()
option(CDIE_NO_CRT "Link without the C runtime (KERNEL32 import only)" ${CDIE_NO_CRT_DEFAULT})

if(CDIE_NO_CRT AND MSVC AND NOT CDIE_NO_CRT_SUPPORTED)
    message(FATAL_ERROR
        "CDIE_NO_CRT is supported only for MSVC x64 and ARM64 targets")
endif()

# Older CMake versions and previously configured build trees can carry MSVC's
# /RTC flags in the configuration flags. Strip them for CRT-free targets; with
# CMP0184 NEW, the per-target MSVC_RUNTIME_CHECKS property below prevents them
# from being added again.
if(CDIE_NO_CRT AND MSVC)
    foreach(_cdie_flags CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELWITHDEBINFO)
        string(REGEX REPLACE "[/-](RTC[1csu]|GZ)" ""
            ${_cdie_flags} "${${_cdie_flags}}")
    endforeach()
endif()

if(CDIE_NO_CRT AND MSVC)
    # xxfclib's matching switch compiles the static archive /GS- /GL- /Zl and
    # exposes the architecture's clean-room compiler helpers. It deliberately
    # does NOT define
    # XXFC_NO_CRT -- that would make xxfclib's own entry object emit
    # memset/memcpy/memmove/_fltused, which src/core/utils_entry.c already
    # owns here.
    set(XXFC_STATIC_NO_CRT ON CACHE BOOL "" FORCE)
else()
    # Forced both ways: without this, flipping CDIE_NO_CRT to OFF in an
    # existing build directory leaves XXFC_STATIC_NO_CRT=ON in the cache, and
    # the archive keeps building /GS- /Zl for a build that asked to be hosted.
    set(XXFC_STATIC_NO_CRT OFF CACHE BOOL "" FORCE)
endif()

# cdie's engine lives in xxfclib. The static archive is used so the console
# keeps shipping as a single self-contained executable, which is also what the
# CRT-free configuration needs.
#
# EXCLUDE_FROM_ALL suppresses the subdirectory's install() rules. Without it
# cdie's install tree -- and therefore the CPack portable archive -- gains 207
# extra entries: the whole include/xxfclib/ header tree, lib/xxfclib.lib, and
# xxfclib's own README.md/LICENSE/FIND_TEST_RESULTS.md at the package root,
# where the first two collide with cdie's.
if(NOT TARGET xxfclib AND NOT TARGET xxfclib_static)
    set(_CDIE_DEFAULT_XXFCLIB_DIR "${CMAKE_CURRENT_LIST_DIR}/dep/xxfclib")
    if(NOT EXISTS "${_CDIE_DEFAULT_XXFCLIB_DIR}/CMakeLists.txt")
        set(_CDIE_DEFAULT_XXFCLIB_DIR "${CMAKE_CURRENT_LIST_DIR}/dep/xxfclib")
    endif()
    set(XXFCLIB_DIR "${_CDIE_DEFAULT_XXFCLIB_DIR}"
        CACHE PATH "Location of the xxfclib source tree")
    if(NOT EXISTS "${XXFCLIB_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "cdie needs xxfclib, which supplies the scan engine. Looked in "
            "'${XXFCLIB_DIR}'. Set -DXXFCLIB_DIR=<path> to point at it.")
    endif()
    add_subdirectory("${XXFCLIB_DIR}" "${CMAKE_BINARY_DIR}/xxfclib" EXCLUDE_FROM_ALL)

    # File Information deliberately exposes native DataStructs for these three
    # executable families only. Keep the focused engine independent of the
    # generic schema catalog (which is not shipped by every public mirror) and
    # unrelated archive/.NET DataStructs providers. Other scan capabilities,
    # including archive and resource scanning, keep their core readers.
    if(TARGET die_engine)
        get_target_property(_cdie_engine_sources die_engine SOURCES)
        set(_cdie_info_sources)
        foreach(_source IN LISTS _cdie_engine_sources)
            if(_source MATCHES "/xx_.+_datastructs\\.c$" AND
               NOT _source MATCHES "/xx_(pe|elf|macho|stream)_datastructs\\.c$")
                continue()
            endif()
            list(APPEND _cdie_info_sources "${_source}")
        endforeach()
        set_property(TARGET die_engine PROPERTY SOURCES "${_cdie_info_sources}")
        get_target_property(_cdie_engine_definitions die_engine COMPILE_DEFINITIONS)
        set(_cdie_info_definitions)
        foreach(_definition IN LISTS _cdie_engine_definitions)
            if(_definition MATCHES "^XXFC_STREAM_HAS_(.+)=")
                set(_cdie_stream_name "${CMAKE_MATCH_1}")
                if(_cdie_stream_name MATCHES "^(PE|ELF|MACHO)$")
                    list(APPEND _cdie_info_definitions "XXFC_STREAM_HAS_${_cdie_stream_name}=1")
                else()
                    list(APPEND _cdie_info_definitions "XXFC_STREAM_HAS_${_cdie_stream_name}=0")
                endif()
            else()
                list(APPEND _cdie_info_definitions "${_definition}")
            endif()
        endforeach()
        set_property(TARGET die_engine PROPERTY COMPILE_DEFINITIONS "${_cdie_info_definitions}")
    endif()

    # The current xxfclib format readers need these POSIX compatibility
    # definitions when compiled as strict C11. Keep them on the dependency
    # target, as CXFileUnpacker does, so cdie's C99 flags stay independent.
    if(UNIX)
        foreach(_CDIE_XXFC_TARGET xxfclib xxfclib_static xxfclib_shared die_engine)
            if(TARGET ${_CDIE_XXFC_TARGET})
                target_compile_options(${_CDIE_XXFC_TARGET} PRIVATE
                    "SHELL:-include string.h" "SHELL:-include stdlib.h")
                target_compile_definitions(${_CDIE_XXFC_TARGET} PRIVATE
                    _POSIX_C_SOURCE=200809L WEP_UNIX=AF_UNIX)
                if(APPLE)
                    target_compile_definitions(${_CDIE_XXFC_TARGET} PRIVATE _DARWIN_C_SOURCE)
                endif()
            endif()
        endforeach()
    endif()
endif()

if(COMMAND xxfclib_require_streams AND TARGET xxfclib::die_engine)
    xxfclib_require_streams(xxfclib::die_engine DATASTRUCTS)
endif()

# ------------------------------------------------------ xxnetlib + xxgithub --
#
# die-update (src/updater) and the GUIs' database update download through
# xxgithub over xxnetlib. Like xxfclib, both are added above cdie's warning
# policy and EXCLUDE_FROM_ALL, so neither inherits cdie's flags nor adds its
# install rules to the package. xxnetlib is pointed at the die_engine archive
# cdie already builds instead of the full ~3400-object xxfclib, so no program
# links two archives compiled from the same sources. Without their sources
# (dep/ first, then ../_mylibs) the updater is left out with a warning.
option(CDIE_BUILD_UPDATER "Build die-update and the GUI database update" ON)
if(CDIE_BUILD_UPDATER AND NOT TARGET xxgithub::xxgithub)
    set(_cdie_updater_missing "")
    if(CMAKE_VERSION VERSION_LESS 3.20)
        list(APPEND _cdie_updater_missing "CMake 3.20 or newer")
    endif()
    if(NOT TARGET xxfclib::die_engine)
        list(APPEND _cdie_updater_missing "xxfclib's die_engine target")
    endif()
    foreach(_cdie_net_lib xxnetlib xxgithub)
        string(TOUPPER "${_cdie_net_lib}" _cdie_net_upper)
        set(_cdie_net_default "${CMAKE_CURRENT_LIST_DIR}/dep/${_cdie_net_lib}")
        if(NOT EXISTS "${_cdie_net_default}/CMakeLists.txt")
            set(_cdie_net_default "${CMAKE_CURRENT_LIST_DIR}/../_mylibs/${_cdie_net_lib}")
        endif()
        set(CDIE_${_cdie_net_upper}_DIR "${_cdie_net_default}" CACHE PATH "Location of the ${_cdie_net_lib} source tree")
        if(NOT EXISTS "${CDIE_${_cdie_net_upper}_DIR}/CMakeLists.txt")
            list(APPEND _cdie_updater_missing "${_cdie_net_lib} (set CDIE_${_cdie_net_upper}_DIR)")
        endif()
    endforeach()

    if(_cdie_updater_missing)
        string(REPLACE ";" ", " _cdie_updater_missing "${_cdie_updater_missing}")
        message(WARNING "cdie: die-update and the GUI database update are not built; missing: ${_cdie_updater_missing}")
        set(CDIE_BUILD_UPDATER OFF)
    else()
        set(XXNETLIB_XXFCLIB_TARGET xxfclib::die_engine CACHE STRING "xxfclib target that xxnetlib links" FORCE)
        set(XXNETLIB_BUILD_TESTS OFF)
        set(XXNETLIB_BUILD_CLI OFF)
        set(XXNETLIB_INSTALL OFF)
        set(XXNETLIB_ONLINE_TESTS OFF)
        # The local HTTP server the update tests run against.
        set(XXNETLIB_BUILD_TESTKIT ${CDIE_BUILD_TESTS})
        add_subdirectory("${CDIE_XXNETLIB_DIR}" "${CMAKE_BINARY_DIR}/xxnetlib" EXCLUDE_FROM_ALL)
        set(XXGITHUB_BUILD_TESTS OFF)
        set(XXGITHUB_BUILD_CLI OFF)
        set(XXGITHUB_INSTALL OFF)
        set(XXGITHUB_ONLINE_TESTS OFF)
        add_subdirectory("${CDIE_XXGITHUB_DIR}" "${CMAKE_BINARY_DIR}/xxgithub" EXCLUDE_FROM_ALL)

        # die_engine uses the static runtime when the console is CRT-free;
        # every hosted library linked beside it must match.
        if(MSVC AND CDIE_NO_CRT AND NOT BUILD_SHARED_LIBS)
            foreach(_cdie_net_target xxnetlib xxgithub xxnetlib_testkit)
                if(TARGET ${_cdie_net_target})
                    set_property(TARGET ${_cdie_net_target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
                endif()
            endforeach()
        endif()
        message(STATUS "cdie: die-update with xxnetlib ${CDIE_XXNETLIB_DIR} and xxgithub ${CDIE_XXGITHUB_DIR}")
    endif()
endif()

# ------------------------------------------------------- language + warnings
#
# cdie's own targets only. See the xxfclib note above for why this block has to
# stay below the add_subdirectory() call.

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
    add_compile_options(/W3)

    if(CDIE_WERROR)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)

    if(CDIE_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

# Resolve the database before the GUI so the macOS bundle can carry it.
set(CDIE_DATABASE_DIR "" CACHE PATH "Directory holding db and db_custom")

set(CDIE_DATABASE_IMPLICIT OFF)

if(NOT CDIE_DATABASE_DIR)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/dep/Detect-It-Easy/db")
        get_filename_component(CDIE_DATABASE_DIR "${CMAKE_CURRENT_LIST_DIR}/dep/Detect-It-Easy" ABSOLUTE)
    elseif(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../_mylibs/Detect-It-Easy/db")
        get_filename_component(CDIE_DATABASE_DIR "${CMAKE_CURRENT_LIST_DIR}/../_mylibs/Detect-It-Easy" ABSOLUTE)
        set(CDIE_DATABASE_IMPLICIT ON)
    endif()
endif()

# Descriptions the result tree's "?" button shows (XInfoDB info/html/<name>.html).
set(CDIE_INFO_DIR "" CACHE PATH "Directory holding html/ with detection descriptions (XInfoDB info)")
if(NOT CDIE_INFO_DIR)
    foreach(_info "${CMAKE_CURRENT_LIST_DIR}/dep/XInfoDB/info" "${CMAKE_CURRENT_LIST_DIR}/../_mylibs/XInfoDB/info")
        if(EXISTS "${_info}/html")
            get_filename_component(CDIE_INFO_DIR "${_info}" ABSOLUTE)
            break()
        endif()
    endforeach()
endif()

# Where the executables are installed. The frontends find their databases
# beside themselves, so CDIE_DATABASE_DESTINATION below follows this.
#
# CDIE_INSTALL_LAYOUT picks the Linux install tree. PORTABLE (the default) is
# the self-contained tree the portable packages have always had: programs and
# databases together in <prefix>/bin. FHS is for system packages (.deb,
# AppImage): die, diec, die-update and their databases share the private
# directory CDIE_INSTALL_PKGLIBDIR, and bin/ holds relative symlinks to them.
# cdie_app_dir() resolves /proc/self/exe, so the programs still find the
# databases beside their real path. Windows and macOS have only PORTABLE.
set(CDIE_INSTALL_LAYOUT "PORTABLE" CACHE STRING "Install layout: PORTABLE (self-contained tree) or FHS (Linux system packages)")
set_property(CACHE CDIE_INSTALL_LAYOUT PROPERTY STRINGS PORTABLE FHS)
if(NOT CDIE_INSTALL_LAYOUT MATCHES "^(PORTABLE|FHS)$")
    message(FATAL_ERROR "CDIE_INSTALL_LAYOUT must be PORTABLE or FHS, not '${CDIE_INSTALL_LAYOUT}'")
endif()
if(CDIE_INSTALL_LAYOUT STREQUAL "FHS" AND NOT (UNIX AND NOT APPLE))
    message(FATAL_ERROR "CDIE_INSTALL_LAYOUT=FHS is for Linux; Windows and macOS packages use PORTABLE")
endif()

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    if(CDIE_INSTALL_LAYOUT STREQUAL "FHS")
        # Not CMAKE_INSTALL_LIBDIR: for the /usr prefix on Debian that is the
        # multiarch library directory, and this one holds programs and data.
        set(CDIE_INSTALL_PKGLIBDIR "lib/cdie" CACHE STRING
            "FHS layout: directory for the programs and their databases, relative to the prefix")
        if(IS_ABSOLUTE "${CDIE_INSTALL_PKGLIBDIR}" OR IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}")
            message(FATAL_ERROR "CDIE_INSTALL_LAYOUT=FHS needs CDIE_INSTALL_PKGLIBDIR and CMAKE_INSTALL_BINDIR relative to the prefix")
        endif()
        set(CDIE_BINARY_DESTINATION "${CDIE_INSTALL_PKGLIBDIR}")
    else()
        set(CDIE_BINARY_DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()
else()
    set(CDIE_BINARY_DESTINATION ".")
endif()

file(GLOB CDIE_TRANSLATION_CATALOGS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/translations/*.po")
add_subdirectory(src)

# --------------------------------------------------------------- library ----
#
# The die_library-compatible shared + static libraries (lib/). On by default;
# turn off to build only the console scanner.

option(CDIE_BUILD_LIBRARY "Build the die_library-compatible shared and static libraries" ON)

if(CDIE_BUILD_LIBRARY)
    add_subdirectory(lib)
endif()

# ------------------------------------------------------------------- tests --
#
# Public, generated-fixture integration tests. Private corpora remain optional.
option(CDIE_BUILD_TESTS "Build the public compatibility tests" OFF)

if(CDIE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/private_tests/CMakeLists.txt")
    add_subdirectory(private_tests)
endif()

# ---------------------------------------------------------------- documents

# A portable tree keeps its documents at the root; the FHS layout uses the
# system documentation directory (share/doc/cdie).
if(CDIE_INSTALL_LAYOUT STREQUAL "FHS")
    set(CDIE_DOC_DESTINATION "${CMAKE_INSTALL_DOCDIR}")
    set(CDIE_LICENSES_DESTINATION "${CMAKE_INSTALL_DOCDIR}/licenses")
else()
    set(CDIE_DOC_DESTINATION ".")
    set(CDIE_LICENSES_DESTINATION "licenses")
endif()

install(FILES
    "${CMAKE_CURRENT_LIST_DIR}/README.md"
    "${CMAKE_CURRENT_LIST_DIR}/LICENSE"
    "${CMAKE_CURRENT_LIST_DIR}/changelog.txt"
    DESTINATION "${CDIE_DOC_DESTINATION}" OPTIONAL)

install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/docs" DESTINATION "${CDIE_DOC_DESTINATION}" OPTIONAL)

# xxfclib is excluded from the install tree, so carry the notices for its
# statically linked decoder in the CDiE package itself.
if(XXFC_CDISASM_DIR AND EXISTS "${XXFC_CDISASM_DIR}/LICENSE")
    install(FILES
        "${XXFC_CDISASM_DIR}/LICENSE"
        "${XXFC_CDISASM_DIR}/THIRD_PARTY_NOTICES.md"
        DESTINATION "${CDIE_LICENSES_DESTINATION}/cdisasm" OPTIONAL)
    install(DIRECTORY "${XXFC_CDISASM_DIR}/LICENSES/"
        DESTINATION "${CDIE_LICENSES_DESTINATION}/cdisasm/LICENSES" OPTIONAL)
endif()

# ------------------------------------------------- signature database (opt)
#
# cdie is useless without a signature database, so a portable package bundles
# one when it can find it. Point CDIE_DATABASE_DIR at the directory holding
# db/ and db_custom/, or set it to NONE to package the binary alone. cdie has
# no extra database: a db_extra/ folder there is never installed (its rules
# are part of db/, tagged extra).

# main_console.c resolve_database_path() looks for db/ next to the executable
# and nowhere else, so the database has to land in the same directory the
# console binary does: CDIE_BINARY_DESTINATION, which is <prefix>/bin on Linux
# (<prefix>/lib/cdie in the FHS layout) and the prefix root everywhere else.

set(CDIE_DATABASE_DESTINATION "${CDIE_BINARY_DESTINATION}")

if(CDIE_DATABASE_DIR AND (NOT CDIE_DATABASE_DIR STREQUAL "NONE") AND EXISTS "${CDIE_DATABASE_DIR}/db")
    message(STATUS "cdie: packaging the signature database from ${CDIE_DATABASE_DIR}")

    # The fallback points at an unversioned working copy, so two packages
    # built from the same cdie tag weeks apart can carry different signature
    # sets with nothing recording which. A packaging run should name the
    # directory (ideally a pinned checkout) explicitly.
    if(CDIE_DATABASE_IMPLICIT)
        message(WARNING "cdie: CDIE_DATABASE_DIR was not set; the database above was picked up implicitly "
                        "and its revision is not recorded. Pass -DCDIE_DATABASE_DIR=<path> for a package, "
                        "or -DCDIE_DATABASE_DIR=NONE to ship the binary alone.")
    endif()

    # The source is a live working copy, so editor and tooling directories sit
    # alongside the signatures; they must not be republished to end users.
    # macOS installs only die.app, which carries its own copy in
    # Contents/Resources (src/gui/bundle_resources.cmake); nothing beside the
    # bundle would read a second one.
    if(NOT APPLE)
    install(DIRECTORY "${CDIE_DATABASE_DIR}/db/" DESTINATION "${CDIE_DATABASE_DESTINATION}/db"
        PATTERN ".vscode" EXCLUDE
        PATTERN "_ai_provenance" EXCLUDE
        PATTERN ".git*" EXCLUDE)

    foreach(_extra db_custom)
        if(EXISTS "${CDIE_DATABASE_DIR}/${_extra}")
            install(DIRECTORY "${CDIE_DATABASE_DIR}/${_extra}/" DESTINATION "${CDIE_DATABASE_DESTINATION}/${_extra}"
                PATTERN ".vscode" EXCLUDE
                PATTERN "_ai_provenance" EXCLUDE
                PATTERN ".git*" EXCLUDE)
        endif()
    endforeach()
    endif()

    if(EXISTS "${CDIE_DATABASE_DIR}/db_extra")
        message(STATUS "cdie: ignoring ${CDIE_DATABASE_DIR}/db_extra (cdie uses only db and db_custom)")
    endif()
else()
    message(STATUS "cdie: no signature database packaged (set CDIE_DATABASE_DIR to bundle one)")
endif()

# The "?" descriptions sit beside the databases, where the frontends look
# (on macOS inside die.app only, as the databases).
if(NOT APPLE AND CDIE_INFO_DIR AND (NOT CDIE_INFO_DIR STREQUAL "NONE") AND EXISTS "${CDIE_INFO_DIR}/html")
    install(DIRECTORY "${CDIE_INFO_DIR}/html/" DESTINATION "${CDIE_DATABASE_DESTINATION}/info/html"
        FILES_MATCHING PATTERN "*.html")
endif()

# ------------------------------------------------ FHS desktop integration --
#
# bin/ gets relative symlinks to the programs in CDIE_INSTALL_PKGLIBDIR. They
# are made here in the build tree and installed as symlinks, so DESTDIR,
# CPack and an AppDir all receive ../lib/cdie/<name> and the links resolve
# inside whatever tree the install lands in. The desktop entry, icons and
# AppStream data come from packaging/linux/ (the PNGs are generated from
# src/gui/images/main.ico by packaging/linux/extract_icons.py).

if(CDIE_INSTALL_LAYOUT STREQUAL "FHS")
    set(_cdie_link_dir "${CMAKE_BINARY_DIR}/cdie_fhs_bin")
    file(REMOVE_RECURSE "${_cdie_link_dir}")
    file(MAKE_DIRECTORY "${_cdie_link_dir}")
    set(_cdie_links)
    foreach(_cdie_program cdie_gui=die cdie=diec cdie_update=die-update)
        string(REPLACE "=" ";" _cdie_program "${_cdie_program}")
        list(GET _cdie_program 0 _cdie_target)
        list(GET _cdie_program 1 _cdie_name)
        if(TARGET ${_cdie_target})
            file(RELATIVE_PATH _cdie_relative "/${CMAKE_INSTALL_BINDIR}" "/${CDIE_INSTALL_PKGLIBDIR}/${_cdie_name}")
            file(CREATE_LINK "${_cdie_relative}" "${_cdie_link_dir}/${_cdie_name}" SYMBOLIC)
            list(APPEND _cdie_links "${_cdie_link_dir}/${_cdie_name}")
        endif()
    endforeach()
    if(_cdie_links)
        install(FILES ${_cdie_links} DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()

    if(TARGET cdie_gui)
        set(_cdie_linux_dir "${CMAKE_CURRENT_LIST_DIR}/packaging/linux")
        install(FILES "${_cdie_linux_dir}/io.github.horsicq.cdie.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
        install(FILES "${_cdie_linux_dir}/io.github.horsicq.cdie.metainfo.xml"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
        foreach(_cdie_icon_size 48 128 256)
            install(FILES "${_cdie_linux_dir}/icons/${_cdie_icon_size}x${_cdie_icon_size}/io.github.horsicq.cdie.png"
                DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_cdie_icon_size}x${_cdie_icon_size}/apps")
        endforeach()
    endif()

    install(FILES "${CMAKE_CURRENT_LIST_DIR}/packaging/debian/copyright" DESTINATION "${CMAKE_INSTALL_DOCDIR}")
endif()

# -------------------------------------------------------------------- CPack

if(WIN32)
    # Windows releases are ready-to-run archives. The marker belongs beside
    # every frontend executable, so extraction immediately selects local INI.
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_BINARY_ZIP ON)
    set(CPACK_BINARY_NSIS OFF)
    install(FILES "${CMAKE_CURRENT_LIST_DIR}/packaging/windows/PORTABLE" DESTINATION ".")
    set(CDIE_OSNAME "win")
elseif(APPLE)
    set(CDIE_OSNAME "mac")
else()
    set(CDIE_OSNAME "linux")
endif()

if(CMAKE_GENERATOR_PLATFORM)
    string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" CDIE_ARCH)

    # "Win32" is the MSVC spelling of x86; normalise it so the package name
    # does not read cdie_win_win32_...
    if(CDIE_ARCH STREQUAL "win32")
        set(CDIE_ARCH "x86")
    endif()
else()
    # Only Visual Studio generators set CMAKE_GENERATOR_PLATFORM, so on Ninja
    # and Makefiles the instruction set has to come from the processor name.
    # Pointer size alone cannot tell x86_64 from aarch64, and labelling an
    # arm64 package "x64" makes it collide with the real x64 one in release/.
    # A macOS build for several architectures (arm64;x86_64) is "universal",
    # as packaging/macos/build_macos.sh names it.
    list(LENGTH CMAKE_OSX_ARCHITECTURES _cdie_osx_arch_count)
    if(_cdie_osx_arch_count GREATER 1)
        set(CDIE_PROCESSOR "universal")
    elseif(CMAKE_OSX_ARCHITECTURES)
        list(GET CMAKE_OSX_ARCHITECTURES 0 CDIE_PROCESSOR)
    else()
        set(CDIE_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    string(TOLOWER "${CDIE_PROCESSOR}" CDIE_PROCESSOR)

    if(CDIE_PROCESSOR STREQUAL "universal")
        set(CDIE_ARCH "universal")
    elseif(CDIE_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(CDIE_ARCH "x64")
    elseif(CDIE_PROCESSOR MATCHES "^(aarch64|arm64e?)$")
        set(CDIE_ARCH "arm64")
    elseif(CDIE_PROCESSOR MATCHES "^(i[3-6]86|x86)$")
        set(CDIE_ARCH "x86")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(CDIE_ARCH "x64")
    else()
        set(CDIE_ARCH "x86")
    endif()
endif()

set(CPACK_PACKAGE_NAME              "cdie")
set(CPACK_PACKAGE_VENDOR            "${X_COMPANYNAME}")
set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR     "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR     "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH     "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${X_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL      "${X_HOMEPAGE}")
set(CPACK_PACKAGE_CONTACT           "${X_MAINTAINER}")
set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_CURRENT_LIST_DIR}/LICENSE")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "cdie")
if(CDIE_INSTALL_LAYOUT STREQUAL "FHS")
    set(CPACK_PACKAGE_FILE_NAME     "cdie_${CDIE_OSNAME}_${CDIE_ARCH}_${PROJECT_VERSION}")
else()
    set(CPACK_PACKAGE_FILE_NAME     "cdie_${CDIE_OSNAME}_${CDIE_ARCH}_portable_${PROJECT_VERSION}")
endif()
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_COMPONENTS_ALL_IN_ONE_PACKAGE ON)

# Debian package: cpack -G DEB on an FHS tree (packaging/debian/build_deb.sh).
# Only the DEB generator reads CPACK_DEBIAN_*, so the ZIP packages are
# unaffected; packaging/cpack_project_config.cmake refuses a .deb made from a
# PORTABLE tree, which would scatter db/ and README.md over /usr.
set(CPACK_PROJECT_CONFIG_FILE       "${CMAKE_CURRENT_LIST_DIR}/packaging/cpack_project_config.cmake")
set(CPACK_CDIE_INSTALL_LAYOUT       "${CDIE_INSTALL_LAYOUT}")
set(CPACK_DEBIAN_PACKAGE_NAME       "cdie")
set(CPACK_DEBIAN_FILE_NAME          "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_SECTION    "devel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY   "optional")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "hors <horsicq@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE   "https://github.com/horsicq/Detect-It-Easy")
# The DEB generator takes the synopsis from CPACK_PACKAGE_DESCRIPTION_SUMMARY,
# which describes the console engine; the project config file swaps this in.
set(CPACK_CDIE_DEBIAN_SUMMARY       "Detect It Easy (C port): identifies compilers, packers and file types")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "cdie is a C port of Detect It Easy. It runs the Detect It Easy signature\n"
    "database to identify the file type, compiler, linker, packer, protector and\n"
    "installer of PE, ELF, Mach-O, DEX, APK and other files.\n"
    "\n"
    "The package contains the GTK desktop application (die), the command-line\n"
    "scanner (diec), the signature database updater (die-update) and a bundled\n"
    "signature database.")
string(REPLACE ";" "" CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_DEBIAN_PACKAGE_DESCRIPTION}")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
# dpkg-shlibdeps finds libc and GTK. xxnetlib dlopen()s libcurl, which it
# cannot see; without libcurl die-update and Update database cannot use https.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
if(TARGET cdie_update)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcurl4t64 | libcurl4 | libcurl3t64-gnutls | libcurl3-gnutls")
endif()
# The Qt Detect It Easy package owns /usr/bin/die and /usr/bin/diec as well.
set(CPACK_DEBIAN_PACKAGE_CONFLICTS  "detectiteasy")
set(CPACK_DEBIAN_PACKAGE_REPLACES   "detectiteasy")

include(CPack)

# GUI localization is independent of console and engine compatibility tests.
option(CDIE_BUILD_TRANSLATION_TESTS "Build interactive translation regressions" OFF)
if(CDIE_BUILD_TRANSLATION_TESTS)
    if(NOT TARGET cdie_ui_translation)
        message(FATAL_ERROR "Translation tests require CDIE_BUILD_GUI")
    endif()
    enable_testing()
    add_subdirectory(tests/translation)
endif()
find_program(CDIE_XGETTEXT_EXECUTABLE xgettext)
if(CDIE_XGETTEXT_EXECUTABLE AND TARGET cdie_ui_translation)
    file(GLOB_RECURSE _cdie_translation_inputs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/*.m"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/gui/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/src/tui/*.c")
    get_target_property(_cdie_widgets_source xxwidgets::xxwidgets SOURCE_DIR)
    file(GLOB_RECURSE _cdie_widget_inputs
        "${_cdie_widgets_source}/src/*.c" "${_cdie_widgets_source}/src/*.m")
    foreach(_source IN LISTS _cdie_widget_inputs)
        file(RELATIVE_PATH _relative "${CMAKE_CURRENT_SOURCE_DIR}" "${_source}")
        list(APPEND _cdie_translation_inputs "${_relative}")
    endforeach()
    add_custom_target(cdie_update_translations
        COMMAND "${CDIE_XGETTEXT_EXECUTABLE}" --from-code=UTF-8
            --keyword=CDIE_TR --keyword=CDIE_N_ --keyword=CDIE_FMT
            --keyword=cdie_ns_tr --keyword=cdie_ns_fmt --keyword=cdie_translation_ngettext:1,2
            --keyword=XXW_TR:2 --keyword=XXW_N_ --keyword=xxwidgets_app_gettext:2
            --keyword=xxwidgets_app_ngettext:2,3 --flag=CDIE_FMT:1:c-format --flag=cdie_translation_snprintf:3:c-format
            --flag=cdie_translation_swprintf:3:c-format --flag=CDIE_N_:1:pass-c-format
            --flag=cdie_translation_ngettext:1:c-format --flag=cdie_translation_ngettext:2:c-format
            --flag=cdie_ns_fmt:1:objc-format
            --sort-output --package-name=cdie --package-version=${CDIE_VERSION}
            --output=translations/cdie.pot ${_cdie_translation_inputs}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Extracting combined CDiE and xxwidgets translation catalog")
endif()
# The GUI loads the catalog named in Options; "Choose translation" starts in
# cdie_translations_dir() (src/app/cdie_paths.c), which finds each of these:
# translations/ beside die (Windows, the build tree, the FHS lib/cdie tree),
# share/cdie/translations beside bin/ (the Linux portable/install tree), and
# Contents/Resources/translations inside die.app (staged by the bundle build,
# so nothing is installed beside the bundle).
if(CDIE_INSTALL_LAYOUT STREQUAL "FHS")
    install(DIRECTORY translations/ DESTINATION "${CDIE_BINARY_DESTINATION}/translations"
        FILES_MATCHING PATTERN "*.po" PATTERN "*.pot")
elseif(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(DIRECTORY translations/ DESTINATION "${CMAKE_INSTALL_DATADIR}/cdie/translations"
        FILES_MATCHING PATTERN "*.po" PATTERN "*.pot")
elseif(NOT APPLE)
    install(DIRECTORY translations/ DESTINATION translations
        FILES_MATCHING PATTERN "*.po" PATTERN "*.pot")
endif()
