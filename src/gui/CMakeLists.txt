# Native xxwidgets front end. The scan engine remains in xxfclib; the widget
# library only needs its public type headers and does not link the engine.
if(APPLE)
    enable_language(OBJC)
endif()

if(APPLE)
    add_executable(cdie_gui MACOSX_BUNDLE
        ${CDIE_LIB_SOURCES} gui_backend.c main_gui_macos.m)
    set_source_files_properties(main_gui_macos.m PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
    find_library(CDIE_APPKIT_FRAMEWORK AppKit REQUIRED)
    target_link_libraries(cdie_gui PRIVATE "${CDIE_APPKIT_FRAMEWORK}")
    # Info.plist LSMinimumSystemVersion: the macOS the compiler targets.
    # build_macos.sh always passes CMAKE_OSX_DEPLOYMENT_TARGET, and CMake seeds
    # it from MACOSX_DEPLOYMENT_TARGET. Without either, use build_macos.sh's
    # defaults: 11.0 for arm64 alone, 10.14 for x86_64 or a universal build.
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CDIE_MACOS_MINIMUM_VERSION "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    else()
        set(_cdie_macos_archs "${CMAKE_OSX_ARCHITECTURES}")
        if(NOT _cdie_macos_archs)
            set(_cdie_macos_archs "${CMAKE_SYSTEM_PROCESSOR}")
        endif()
        if(_cdie_macos_archs MATCHES "^(arm64|aarch64)$")
            set(CDIE_MACOS_MINIMUM_VERSION "11.0")
        else()
            set(CDIE_MACOS_MINIMUM_VERSION "10.14")
        endif()
    endif()
    set_target_properties(cdie_gui PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "die"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.ntinfo.cdie"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${CDIE_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${CDIE_VERSION}")
    set_property(TARGET cdie_gui APPEND PROPERTY LINK_DEPENDS ${CDIE_TRANSLATION_CATALOGS})
    add_custom_command(TARGET cdie_gui POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DDATABASE_DIR=${CDIE_DATABASE_DIR}"
            "-DINFO_DIR=${CDIE_INFO_DIR}"
            "-DRESOURCES_DIR=$<TARGET_BUNDLE_CONTENT_DIR:cdie_gui>/Resources"
            "-DCDIE_SOURCE_DIR=${CMAKE_CURRENT_LIST_DIR}/../.."
            "-DCDISASM_DIR=${XXFC_CDISASM_DIR}"
            -P "${CMAKE_CURRENT_LIST_DIR}/bundle_resources.cmake"
        VERBATIM)
elseif(WIN32)
    # rc.exe does not report embedded icon/manifest files as dependencies.
    set_property(SOURCE cdie_gui.rc APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/images/main.ico"
        "${CMAKE_CURRENT_LIST_DIR}/cdie_gui.manifest")
    add_executable(cdie_gui WIN32
        ${CDIE_LIB_SOURCES} gui_backend.c main_gui.c gui_entry.c cdie_gui.rc)
    cdie_stage_frontend_resources(cdie_gui)
    target_link_libraries(cdie_gui PRIVATE user32 gdi32 comdlg32 comctl32 shell32 ole32)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CDIE_GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0>=3.16)
    add_executable(cdie_gui ${CDIE_LIB_SOURCES} gui_backend.c main_gui_linux.c)
    target_link_libraries(cdie_gui PRIVATE PkgConfig::CDIE_GTK3 m)
    cdie_stage_frontend_resources(cdie_gui)
endif()
set_target_properties(cdie_gui PROPERTIES OUTPUT_NAME "die")

# The About picture, embedded for every platform.
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/cdie_about_image.c"
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CMAKE_CURRENT_LIST_DIR}/images/about.png"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/cdie_about_image.c" -DNAME=cdie_about_png
        -P "${CMAKE_CURRENT_LIST_DIR}/embed_file.cmake"
    DEPENDS "${CMAKE_CURRENT_LIST_DIR}/images/about.png" "${CMAKE_CURRENT_LIST_DIR}/embed_file.cmake"
    VERBATIM)
target_sources(cdie_gui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cdie_about_image.c" gui_about.c gui_update.c gui_options.c gui_settings.c gui_preferences.c gui_options_extra.c gui_frontend_preferences.c gui_png.c gui_tree.c gui_result_tree.c gui_scan_options.c gui_file_information.c gui_entropy.c gui_hash.c gui_strings.c gui_strings_source.c gui_extractor.c gui_extractor_source.c gui_hex_view.c gui_disasm.c gui_memory_map.c gui_imports.c gui_exports.c gui_resources.c gui_symbols.c ../app/cdie_symbols.c ../app/cdie_resources.c ../app/cdie_exports.c ../app/cdie_imports.c ../app/cdie_memory_map.c ../app/cdie_disasm.c ../app/cdie_strings.c ../app/cdie_hash.c ../app/cdie_entropy.c ../app/cdie_file_info.c)
target_include_directories(cdie_gui PRIVATE ${CMAKE_CURRENT_LIST_DIR}/..)
target_link_libraries(cdie_gui PRIVATE xxfclib::die_engine xxwidgets::xxwidgets cdie_ui_translation cdie_settings cdisasm::cdisasm)
# The "Update database" button runs the updater in-process (CDIE_HAVE_UPDATER).
if(TARGET cdie_updater)
    target_link_libraries(cdie_gui PRIVATE cdie_updater)
    if(APPLE)
        # macOS ships no console programs; die-update lives inside die.app.
        # A target of its own, run on every build after both programs: a
        # POST_BUILD step of cdie_gui would miss a relink of die-update alone.
        add_custom_target(cdie_bundle_updater ALL
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:cdie_update>" "$<TARGET_BUNDLE_CONTENT_DIR:cdie_gui>/MacOS/die-update"
            COMMENT "Copying die-update into die.app"
            VERBATIM)
        add_dependencies(cdie_bundle_updater cdie_gui cdie_update)
    endif()
endif()

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
        if(TARGET xxwidgets_scan_colors)
            set_property(TARGET xxwidgets_scan_colors PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
        endif()
        message(STATUS "cdie_gui: xxwidgets frontend with static C runtime")
    endif()
endif()

if(UNIX AND NOT APPLE)
    # bin/, or lib/cdie in the FHS layout (top-level CDIE_BINARY_DESTINATION).
    install(TARGETS cdie_gui RUNTIME DESTINATION "${CDIE_BINARY_DESTINATION}")
else()
    install(TARGETS cdie_gui BUNDLE DESTINATION "." RUNTIME DESTINATION ".")
endif()

if(CDIE_BUILD_TESTS AND UNIX AND NOT APPLE)
    # The GTK probes include main_gui_linux.c and drive its real dialogs:
    # gui_linux_settings.c the database folders and fonts in Options,
    # gui_linux_dialogs.c every other dialog. The rest of the GUI is built
    # once for both.
    get_target_property(_cdie_gui_test_sources cdie_gui SOURCES)
    list(REMOVE_ITEM _cdie_gui_test_sources main_gui_linux.c)
    add_library(cdie_gtk_probe_objects OBJECT ${_cdie_gui_test_sources})
    target_include_directories(cdie_gtk_probe_objects PUBLIC ${CMAKE_CURRENT_LIST_DIR}/..)
    target_link_libraries(cdie_gtk_probe_objects PUBLIC xxfclib::die_engine xxwidgets::xxwidgets cdie_ui_translation cdie_settings cdisasm::cdisasm PkgConfig::CDIE_GTK3 m)
    if(TARGET cdie_updater)
        target_link_libraries(cdie_gtk_probe_objects PUBLIC cdie_updater)
    endif()
    foreach(_cdie_gtk_probe IN ITEMS linux_settings linux_dialogs)
        add_executable(cdie_test_${_cdie_gtk_probe} ../../tests/gui_${_cdie_gtk_probe}.c)
        target_link_libraries(cdie_test_${_cdie_gtk_probe} PRIVATE cdie_gtk_probe_objects)
    endforeach()
endif()
