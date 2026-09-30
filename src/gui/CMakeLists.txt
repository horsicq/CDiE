# Native xxwidgets front end. The scan engine remains in xxfclib; the widget
# library only needs its public type headers and does not link the engine.
if(NOT TARGET xxwidgets::xxwidgets)
    set(CDIE_XXWIDGETS_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../_mylibs/xxwidgets"
        CACHE PATH "Location of the xxwidgets source tree")
    if(NOT EXISTS "${CDIE_XXWIDGETS_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "cdie_gui needs xxwidgets. Set CDIE_XXWIDGETS_DIR to its source tree.")
    endif()
    add_subdirectory("${CDIE_XXWIDGETS_DIR}" "${CMAKE_BINARY_DIR}/xxwidgets" EXCLUDE_FROM_ALL)
endif()

add_executable(cdie_gui WIN32
    ${CDIE_LIB_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/gui_backend.c
    ${CMAKE_CURRENT_LIST_DIR}/main_gui.c
    ${CMAKE_CURRENT_LIST_DIR}/gui_entry.c
    ${CMAKE_CURRENT_LIST_DIR}/cdie_gui.rc
)
target_include_directories(cdie_gui PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(cdie_gui PRIVATE xxfclib::xxfclib xxwidgets::xxwidgets
    user32 gdi32 comdlg32 comctl32 shell32 ole32)

if(MSVC)
    # The application resource owns the common-controls and DPI manifest.
    target_link_options(cdie_gui PRIVATE /MANIFEST:NO)
    target_compile_options(cdie_gui PRIVATE /utf-8)
    if(CDIE_NO_CRT AND NOT BUILD_SHARED_LIBS)
        # CDIE_NO_CRT continues to govern the console and engine archive.
        # xxwidgets uses the hosted runtime. Link it statically in this shape
        # to match the archive and avoid adding a runtime DLL to the package.
        set_property(TARGET cdie_gui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        if(TARGET xxwidgets)
            set_property(TARGET xxwidgets PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        endif()
        message(STATUS "cdie_gui: xxwidgets frontend with static C runtime")
    endif()
endif()

install(TARGETS cdie_gui RUNTIME DESTINATION ".")
