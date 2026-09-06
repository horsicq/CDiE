cmake_minimum_required(VERSION 3.16)

# Let per-target MSVC_RUNTIME_LIBRARY choose the CRT (the library targets pick
# the dynamic CRT so they link cleanly against default-built consumers).
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/release_version.txt CDIE_VERSION)

project(cdie VERSION ${CDIE_VERSION} LANGUAGES C)

set(X_COMPANYNAME "NTInfo")
set(X_DESCRIPTION "Detect It Easy console engine (C port)")
set(X_HOMEPAGE    "http://ntinfo.biz")
set(X_MAINTAINER  "horsicq@gmail.com")

# ------------------------------------------------------- language + warnings
#
# These live here rather than in src/ because lib/ and tests/ are siblings of
# src/, so a directory-scoped setting there would leave the library and test
# targets compiling the same engine sources as GNU C17/C23 with no warning
# flags at all -- meaning a -Wall/-Wextra defect in an engine TU would be
# diagnosed only on the cdie target, and a C23-only construct would build
# clean in die_static while failing in cdie.

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CDIE_WERROR "Treat compiler warnings as errors" OFF)

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
# Off by default: the tests are a developer tool, not part of the package, and
# tests/ is not published to the git mirror at all -- hence the EXISTS guard,
# so -DCDIE_BUILD_TESTS=ON on a released tree warns instead of failing to
# configure.

option(CDIE_BUILD_TESTS "Build the runtime test executable" OFF)

if(CDIE_BUILD_TESTS)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/tests/CMakeLists.txt")
        enable_testing()
        add_subdirectory(tests)
    else()
        message(WARNING "cdie: CDIE_BUILD_TESTS is ON but tests/ is not present in this tree")
    endif()
endif()

# ---------------------------------------------------------------- documents

install(FILES
    "${CMAKE_CURRENT_LIST_DIR}/README.md"
    "${CMAKE_CURRENT_LIST_DIR}/LICENSE"
    "${CMAKE_CURRENT_LIST_DIR}/changelog.txt"
    DESTINATION . OPTIONAL)

install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/docs" DESTINATION . OPTIONAL)

# ------------------------------------------------- signature database (opt)
#
# cdie is useless without a signature database, so a portable package bundles
# one when it can find it. Point CDIE_DATABASE_DIR at the directory holding
# db/, db_extra/ and db_custom/, or set it to NONE to package the binary
# alone.

set(CDIE_DATABASE_DIR "" CACHE PATH "Directory holding db, db_extra and db_custom")

set(CDIE_DATABASE_IMPLICIT OFF)

if(NOT CDIE_DATABASE_DIR)
    if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../_mylibs/Detect-It-Easy/db")
        get_filename_component(CDIE_DATABASE_DIR "${CMAKE_CURRENT_LIST_DIR}/../_mylibs/Detect-It-Easy" ABSOLUTE)
        set(CDIE_DATABASE_IMPLICIT ON)
    endif()
endif()

# main_console.c resolve_database_path() looks for db/ next to the executable
# and nowhere else, so the database has to land in the same directory the
# console binary does. On Linux that is <prefix>/bin (src/console installs
# there); everywhere else both are flat at the prefix root.

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    set(CDIE_DATABASE_DESTINATION "${CMAKE_INSTALL_BINDIR}")
else()
    set(CDIE_DATABASE_DESTINATION ".")
endif()

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
    install(DIRECTORY "${CDIE_DATABASE_DIR}/db/" DESTINATION "${CDIE_DATABASE_DESTINATION}/db"
        PATTERN ".vscode" EXCLUDE
        PATTERN "_ai_provenance" EXCLUDE
        PATTERN ".git*" EXCLUDE)

    foreach(_extra db_extra db_custom)
        if(EXISTS "${CDIE_DATABASE_DIR}/${_extra}")
            install(DIRECTORY "${CDIE_DATABASE_DIR}/${_extra}/" DESTINATION "${CDIE_DATABASE_DESTINATION}/${_extra}"
                PATTERN ".vscode" EXCLUDE
                PATTERN "_ai_provenance" EXCLUDE
                PATTERN ".git*" EXCLUDE)
        endif()
    endforeach()
else()
    message(STATUS "cdie: no signature database packaged (set CDIE_DATABASE_DIR to bundle one)")
endif()

# -------------------------------------------------------------------- CPack

if(WIN32)
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
    if(CMAKE_OSX_ARCHITECTURES)
        list(GET CMAKE_OSX_ARCHITECTURES 0 CDIE_PROCESSOR)
    else()
        set(CDIE_PROCESSOR "${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    string(TOLOWER "${CDIE_PROCESSOR}" CDIE_PROCESSOR)

    if(CDIE_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(CDIE_ARCH "x64")
    elseif(CDIE_PROCESSOR MATCHES "^(aarch64|arm64)$")
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
set(CPACK_PACKAGE_FILE_NAME         "cdie_${CDIE_OSNAME}_${CDIE_ARCH}_portable_${PROJECT_VERSION}")
set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
set(CPACK_COMPONENTS_ALL_IN_ONE_PACKAGE ON)

include(CPack)
