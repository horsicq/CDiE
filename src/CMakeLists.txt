# cdie - C port of the Detect It Easy console scanner.
#
# The project has no external dependencies: the JavaScript engine, the binary
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

set(CDIE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/global.h

    ${CMAKE_CURRENT_LIST_DIR}/core/utils.h
    ${CMAKE_CURRENT_LIST_DIR}/core/utils.c
    ${CMAKE_CURRENT_LIST_DIR}/core/utils_math.c
    ${CMAKE_CURRENT_LIST_DIR}/core/utils_fp.c
    ${CMAKE_CURRENT_LIST_DIR}/core/utils_entry.c
    ${CMAKE_CURRENT_LIST_DIR}/core/cd_common.h
    ${CMAKE_CURRENT_LIST_DIR}/core/cd_common.c
    ${CMAKE_CURRENT_LIST_DIR}/core/cd_fs.h
    ${CMAKE_CURRENT_LIST_DIR}/core/cd_fs.c

    ${CMAKE_CURRENT_LIST_DIR}/js/js.h
    ${CMAKE_CURRENT_LIST_DIR}/js/js_internal.h
    ${CMAKE_CURRENT_LIST_DIR}/js/js_ast.h
    ${CMAKE_CURRENT_LIST_DIR}/js/js_lex.h
    ${CMAKE_CURRENT_LIST_DIR}/js/js_value.c
    ${CMAKE_CURRENT_LIST_DIR}/js/js_lex.c
    ${CMAKE_CURRENT_LIST_DIR}/js/js_parse.c
    ${CMAKE_CURRENT_LIST_DIR}/js/js_interp.c
    ${CMAKE_CURRENT_LIST_DIR}/js/js_builtins.c
    ${CMAKE_CURRENT_LIST_DIR}/js/js_regexp.c
    ${CMAKE_CURRENT_LIST_DIR}/js/js_engine.c

    ${CMAKE_CURRENT_LIST_DIR}/format/xb.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xb.c
    ${CMAKE_CURRENT_LIST_DIR}/format/xpe.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xpe.c
    ${CMAKE_CURRENT_LIST_DIR}/format/xjpeg.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xjpeg.c
    ${CMAKE_CURRENT_LIST_DIR}/format/xpng.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xpng.c
    ${CMAKE_CURRENT_LIST_DIR}/format/inflate.h
    ${CMAKE_CURRENT_LIST_DIR}/format/inflate.c
    ${CMAKE_CURRENT_LIST_DIR}/format/xapk.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xapk.c
    ${CMAKE_CURRENT_LIST_DIR}/format/xpdf.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xpdf.c
    ${CMAKE_CURRENT_LIST_DIR}/format/xelf.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xelf.c
    ${CMAKE_CURRENT_LIST_DIR}/format/xdex.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xdex.c
    ${CMAKE_CURRENT_LIST_DIR}/format/xmach.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xmach.c
    ${CMAKE_CURRENT_LIST_DIR}/format/xpyc.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xpyc.c
    ${CMAKE_CURRENT_LIST_DIR}/format/xiso9660.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xiso9660.c
    ${CMAKE_CURRENT_LIST_DIR}/format/xzip.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xzip.c
    ${CMAKE_CURRENT_LIST_DIR}/format/xft.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xft.c
    ${CMAKE_CURRENT_LIST_DIR}/format/xdisasm.h
    ${CMAKE_CURRENT_LIST_DIR}/format/xdisasm.c

    ${CMAKE_CURRENT_LIST_DIR}/engine/cdie.h
    ${CMAKE_CURRENT_LIST_DIR}/engine/db.c
    ${CMAKE_CURRENT_LIST_DIR}/engine/api.c
    ${CMAKE_CURRENT_LIST_DIR}/engine/scan.c
    ${CMAKE_CURRENT_LIST_DIR}/engine/result.c
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
