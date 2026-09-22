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
# (xxfclib::xxfclib, added at the top level) and are no longer built here.
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
)

# The library targets (see lib/) reuse the whole engine but supply their own
# entry points, so they exclude core/utils_entry.c (which defines main / the
# CRT-free startup). Export both lists to the parent scope.
set(CDIE_LIB_SOURCES ${CDIE_SOURCES})
list(REMOVE_ITEM CDIE_LIB_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/core/utils_entry.c)

set(CDIE_SOURCES "${CDIE_SOURCES}" PARENT_SCOPE)
set(CDIE_LIB_SOURCES "${CDIE_LIB_SOURCES}" PARENT_SCOPE)

add_subdirectory(console)

# Windows-only native GUI front end (pure WinAPI). Reuses CDIE_LIB_SOURCES,
# which is in scope here (defined above), so no PARENT_SCOPE plumbing is needed.
if(WIN32)
    add_subdirectory(gui)
endif()
