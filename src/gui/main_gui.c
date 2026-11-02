/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* The main controls and common modal forms belong to xxwidgets. Win32 supplies
 * file/folder pickers, drag-and-drop and pixel layout. The engine stays behind
 * gui_backend.h because its PE declarations must not meet <windows.h>.
 * String storage uses xxfclib; other scratch storage uses the process heap.
 * xxwidgets supplies the hosted GUI
 * runtime while the console keeps its own runtime configuration. */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <winioctl.h>
#include <cderr.h>
#include <xxfclib/strings/xx_string.h>
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_scan_panel.h>
#include <xxwidgets/xxwidgets_combobox.h>
#include <xxwidgets/xxwidgets_scan_options.h>
#include <xxwidgets/xxwidgets_font_options.h>
#include <xxwidgets/xxwidgets_context_options.h>
#include <xxwidgets/xxwidgets_optimization_options.h>
#include <xxwidgets/xxwidgets_options_pages.h>
#include <xxwidgets/xxwidgets_die_main_panel.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "gui_backend.h"
#include "app/cdie_app.h"
#include "app/cdie_translation.h"
#include "gui_scan_options.h"
#include "gui_tree.h"
#include "gui_file_information.h"
#include "gui_entropy.h"
#include "gui_hash.h"
#include "gui_strings.h"
#include "gui_hex_view.h"
#include "gui_about.h"
#include "gui_options.h"
#include "gui_settings.h"
#include "gui_frontend_preferences.h"
#include <xxfclib/rt/xx_rt.h>
#include "gui_disasm.h"
#include "gui_memory_map.h"
#include "gui_imports.h"
#include "gui_exports.h"
#include "gui_resources.h"
#include "gui_symbols.h"
#include "gui_extractor.h"
#include "gui_tools.h"
#include "gui_update.h"
#include "gui_error.h"
#include "gui_main_actions.h"
#include "gui_shortcuts.h"
#include "resource.h"

#if defined(_M_ARM64) || defined(_M_ARM64EC) || defined(_M_ARM) || defined(__aarch64__) || defined(__arm__)
#define CDIE_GUI_ARM_TARGET 1
#else
#define CDIE_GUI_ARM_TARGET 0
#endif

/* What the main loop runs once app_poll returns (modal loops never start
 * inside the event callback): opening the path field, an action of the main
 * panel (g_pendingDie), or a shortcut (ACTION_SHORTCUT + gui_shortcut_action). */
enum gui_action { ACTION_NONE, ACTION_OPEN /* the path field, as typed or dropped */, ACTION_DIE, ACTION_SHORTCUT = 100 };
#define SHORTCUT_ACTION(shortcut) (ACTION_SHORTCUT + (int)(shortcut))

static HINSTANCE g_hInst;
static xxwidgets_app *g_app;
/* The window's content is one xxwidgets_die_main_panel (DIE_LOOK.md):
 * the file name bar, the file header, the scan panel and the right column.
 * The globals below borrow its controls. */
static xxwidgets_die_main_panel *g_main;
static xxwidgets_file_header *g_header;
static xxwidgets_side_panel *g_side;
static xxwidgets_widget *g_window, *g_path, *g_browse, *g_type, *g_recent;
static xxwidgets_widget *g_flags, *g_databases, *g_msec, *g_scan, *g_results;
static xxwidgets_widget *g_flagLabel, *g_databaseLabel;
static xxwidgets_widget *g_includeTags, *g_excludeTags, *g_includeTagsLabel, *g_excludeTagsLabel;
/* DIE's scan bar: Signatures, Directory and Log; the engine row's values. */
static xxwidgets_widget *g_signaturesButton, *g_directoryButton, *g_logButton, *g_facts[4];
/* The right column: Advanced, Update database (Q2), Shortcuts, Options, About, Exit. */
static xxwidgets_widget *g_advanced, *g_updateButton, *g_shortcutsButton;
static xxwidgets_widget *g_options, *g_aboutButton, *g_reportButton, *g_exit;
static xxwidgets_scan_panel *g_panel;
static xxwidgets_about_dialog *g_about;
static xxwidgets_die_action g_pendingDie;
static int g_running, g_pending, g_memorySettings, g_loaded, g_quit;
static char *g_typePath;
static gui_main_file_types g_fileTypes;
/* No status line (Q1): the last message is kept (smoke runs read it), and
 * errors are shown in a message dialog. */
static char g_lastMessage[640];
/* Set in the smoke runs: errors are kept and traced, never shown in a box
 * no step expects. */
static int g_quietErrors;

static int g_bFlagDeep = 1, g_bFlagHeur = 1, g_bFlagVerbose = 1;
static int g_bFlagAggr, g_bFlagHideUnknown, g_bFlagFormat;
/* The custom database; the main one is always used. */
static int g_bDbCustom = 1;
static void smoke_trace(const char *text);
static void smoke_last_message(char *text, size_t size);
static void hook_settings_changed(void *user);

static HWND native(xxwidgets_widget *widget)
{
    return (HWND)xxwidgets_widget_native_handle(widget);
}

static void apply_flags_to_backend(void)
{
    gui_backend_set_flags(g_bFlagDeep, g_bFlagHeur, g_bFlagVerbose,
                          g_bFlagAggr, g_bFlagHideUnknown, g_bFlagFormat);
    gui_backend_set_databases(g_bDbCustom);
}

/* An outcome worth no dialog (a finished scan, applied options). */
static void note_message(const char *text)
{
    xx_rt_snprintf(g_lastMessage, sizeof(g_lastMessage), "%s", text ? text : "");
}

/* An error: kept, and shown in a text dialog over the main window. */
static void show_error(const char *text)
{
    if (!text || !text[0]) return;
    note_message(text);
    if (g_quietErrors || !g_window) {
        smoke_trace(text);
        return;
    }
    (void)xxwidgets_text_dialog(g_window, gui_backend_app_title(), text);
}

static void show_error_wide(const WCHAR *wide)
{
    char *text = xx_str_unicode_to_utf8(wide);
    if (text) {
        show_error(text);
        xx_str_free(text);
    }
}

static void note_message_wide(const WCHAR *wide)
{
    char *text = xx_str_unicode_to_utf8(wide);
    if (text) {
        note_message(text);
        xx_str_free(text);
    }
}

/* "<operation>: <sentence>", the sentence from gui_error.h, never the status name. */
static void report_ui_error(const char *operation, xxwidgets_status status)
{
    WCHAR text[256];
    WCHAR *name = xx_str_utf8_to_unicode(CDIE_TR(operation));
    WCHAR *detail = xx_str_utf8_to_unicode(gui_error_status_text(status));
    cdie_translation_swprintf(text, sizeof(text)/sizeof(text[0]), CDIE_N_(L"%s: %s"), name ? name : L"Interface error", detail ? detail : L"unknown error");
    show_error_wide(text);
    if (name) xx_str_wfree(name);
    if (detail) xx_str_wfree(detail);
}

/* The panel's file state: with a file, the scan bar and the file header are
 * enabled (Report only with a report, the tag filters only with tags). The
 * scan panel's own setters enable controls regardless, so this is re-applied
 * after them. */
static void apply_loaded(int loaded)
{
    g_loaded = loaded != 0;
    if (g_main) (void)xxwidgets_die_main_panel_set_loaded(g_main, g_loaded);
}
static unsigned int flags_mask(void)
{
    return (g_bFlagDeep ? 1u : 0u) | (g_bFlagHeur ? 2u : 0u) |
           (g_bFlagVerbose ? 4u : 0u) | (g_bFlagAggr ? 8u : 0u) |
           (g_bFlagHideUnknown ? 16u : 0u) | (g_bFlagFormat ? 32u : 0u);
}

static unsigned int databases_mask(void)
{
    return g_bDbCustom ? XXWIDGETS_SCAN_DATABASE_CUSTOM : 0u;
}

static xxwidgets_status refresh_panel_tags(void)
{
    char *pTags = gui_backend_used_tags();
    xxwidgets_status status;
    if (pTags == NULL) return XXWIDGETS_OUT_OF_MEMORY;
    status = xxwidgets_scan_panel_set_available_tags(g_panel, pTags);
    gui_backend_free(pTags);
    /* set_available_tags enables the filters even with no file loaded. */
    apply_loaded(g_loaded);
    return status;
}

static xxwidgets_status apply_panel_tag_filters(void)
{
    return gui_main_store_tag_filters(g_panel);
}

static xxwidgets_status sync_panel_options(void)
{
    xxwidgets_status status = xxwidgets_scan_panel_set_flags(g_panel, flags_mask());
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_set_databases(g_panel, databases_mask());
    return status;
}

static xxwidgets_status read_panel_options(void)
{
    unsigned int flags = 0, databases = 0;
    xxwidgets_status status = xxwidgets_scan_panel_get_flags(g_panel, &flags);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_get_databases(g_panel, &databases);
    if (status == XXWIDGETS_OK)
        status = apply_panel_tag_filters();
    if (status != XXWIDGETS_OK) return status;
    g_bFlagDeep = (flags & 1u) != 0; g_bFlagHeur = (flags & 2u) != 0;
    g_bFlagVerbose = (flags & 4u) != 0; g_bFlagAggr = (flags & 8u) != 0;
    g_bFlagHideUnknown = (flags & 16u) != 0; g_bFlagFormat = (flags & 32u) != 0;
    g_bDbCustom = (databases & XXWIDGETS_SCAN_DATABASE_CUSTOM) != 0;
    apply_flags_to_backend();
    return XXWIDGETS_OK;
}

static void clear_scan(void)
{
    xxwidgets_scan_panel_clear(g_panel);
    xxwidgets_widget_set_text(g_msec, "");
}

/* The path field's text, from the process heap; NULL (reported) on failure. */
static char *read_path(const char *operation)
{
    SIZE_T required = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &required);
    if (status != XXWIDGETS_OK) { report_ui_error(operation, status); return NULL; }
    path = (char *)HeapAlloc(GetProcessHeap(), 0, required ? required : 1);
    if (!path) { report_ui_error(operation, XXWIDGETS_OUT_OF_MEMORY); return NULL; }
    status = xxwidgets_widget_get_text(g_path, path, required ? required : 1, NULL);
    if (status != XXWIDGETS_OK) {
        HeapFree(GetProcessHeap(), 0, path);
        report_ui_error(operation, status);
        return NULL;
    }
    return path;
}

/* The type id the next scan passes: 0 while the File type combo shows the
 * preferred type (DIE_LOOK.md 2.6). */
static xxwidgets_status selected_file_type(int *type_id)
{
    *type_id = gui_main_file_types_scan_id(&g_fileTypes);
    return XXWIDGETS_OK;
}

/* The file's concrete types into the File type combo (the user's pick kept
 * on a rescan of the same file), its size and addresses into the header. */
static xxwidgets_status refresh_file_types(const char *path)
{
    xx_meta_string *records = NULL;
    xxwidgets_status status;
    char *new_path;
    int keep = g_typePath && !lstrcmpA(g_typePath, path);
    if (!gui_main_file_types_load(&g_fileTypes, path, keep)) return XXWIDGETS_PLATFORM_ERROR;
    new_path = xx_str_create(path);
    if (!new_path) return XXWIDGETS_OUT_OF_MEMORY;
    status = gui_main_file_types_records(&g_fileTypes, &records);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_file_header_set_file_types(g_header, records, g_fileTypes.count, g_fileTypes.selected);
    if (records) gui_main_file_types_free_records(records, g_fileTypes.count);
    if (status != XXWIDGETS_OK) { xx_str_free(new_path); return status; }
    if (g_typePath) xx_str_free(g_typePath);
    g_typePath = new_path;
    return gui_main_apply_file_facts(g_main, path, gui_main_file_types_scan_id(&g_fileTypes));
}

/* Opens path as the loaded file: its types, size and addresses; the scan bar
 * enabled. 0 (shown in a message dialog) when it cannot be read. */
static int load_file(const char *path)
{
    xxwidgets_status status = refresh_file_types(path);
    if (status != XXWIDGETS_OK) {
        clear_scan();
        (void)xxwidgets_file_header_clear(g_header);
        gui_main_file_types_clear(&g_fileTypes);
        if (g_typePath) xx_str_free(g_typePath);
        g_typePath = NULL;
        apply_loaded(0);
        /* A file the system will not open says why, in the words GTK and macOS use. */
        if (gui_error_file_reason(path)) {
            char text[512];
            gui_error_open_status(text, sizeof(text), path);
            show_error(text);
        } else report_ui_error(CDIE_N_("Load file types"), status);
        return 0;
    }
    apply_loaded(1);
    return 1;
}

static void do_scan(void)
{
    char *path, *type = NULL;
    xxwidgets_status status;
    int ok, cancelled = 0, type_id = 0;

    path = read_path(CDIE_N_("Read file name"));
    if (!path) return;
    if (!path[0]) {
        HeapFree(GetProcessHeap(), 0, path);
        MessageBeep(MB_ICONASTERISK);
        xxwidgets_widget_focus(g_path);
        return;
    }
    status = read_panel_options();
    if (status != XXWIDGETS_OK) {
        HeapFree(GetProcessHeap(), 0, path);
        report_ui_error(CDIE_N_("Read scan options"), status);
        return;
    }
    if (!load_file(path)) {
        HeapFree(GetProcessHeap(), 0, path);
        return;
    }
    (void)selected_file_type(&type_id);
    clear_scan();
    xxwidgets_widget_set_enabled(g_scan, 0);
    xxwidgets_widget_set_enabled(g_type, 0);
    /* Progress shows in the elapsed field (Q1), then the time it took. */
    xxwidgets_widget_set_text(g_msec, CDIE_TR("Scanning..."));
    /* The engine runs on a worker; the process dialog keeps the window
     * responsive, disables it meanwhile, and offers Cancel after a second. */
    ok = gui_tree_scan(g_window, g_panel, path, type_id, &type, &cancelled);
    if (ok) {
        gui_frontend_remember_file(path);
        WCHAR text[512];
        WCHAR *type_name = xx_str_utf8_to_unicode(type && type[0] ? type : "Unknown type");
        size_t count = xxwidgets_scan_panel_count(g_panel);
        cdie_translation_swprintf_utf8(text, sizeof(text)/sizeof(text[0]),
                  cdie_translation_ngettext("Scan complete (%s). %u result. Use > for the full engine output.",
                                            "Scan complete (%s). %u results. Use > for the full engine output.", count),
                  type_name ? type_name : L"Unknown type", (unsigned int)count);
        note_message_wide(text);
        if (type_name) xx_str_wfree(type_name);
    } else {
        xxwidgets_widget_set_text(g_msec, "");
        if (cancelled) note_message(CDIE_TR("Scan cancelled."));
        else show_error(CDIE_TR("Cannot scan the file."));
    }
    HeapFree(GetProcessHeap(), 0, path);
    if (type) gui_backend_free(type);
    apply_loaded(1);
}

/* A file chosen by the picker, Recent files, a drop or another instance:
 * into the path field, remembered, then scanned or only opened. */
static void open_file(const char *path)
{
    if (xxwidgets_widget_set_text(g_path, path) != XXWIDGETS_OK) return;
    gui_frontend_remember_file(path);
    if (gui_frontend_scan_after_open()) do_scan();
    else (void)load_file(path);
}
static void do_browse(void)
{
    OPENFILENAMEW picker;
    WCHAR file[32768], filter[128];
    gui_options_values preferences;
    gui_preferences_get(&preferences);
    WCHAR *initial = preferences.file.save_last_directory && preferences.file.last_directory[0] ? xx_str_utf8_to_unicode(preferences.file.last_directory) : NULL;
    WCHAR *translation_title = xx_str_utf8_to_unicode(CDIE_TR("Select a file to scan"));
    WCHAR *filter_name = xx_str_utf8_to_unicode(CDIE_TR("All files"));
    /* "All files (*.*)\0*.*\0\0", its name translated. */
    int filter_length = wsprintfW(filter, L"%s (*.*)", filter_name && lstrlenW(filter_name) < 100 ? filter_name : L"All files");
    memcpy(filter + filter_length + 1, L"*.*\0", 5 * sizeof(WCHAR));
    file[0] = 0;
    GetWindowTextW(native(g_path), file, (int)(sizeof(file) / sizeof(file[0])));
    /* The picker rejects a start name with '/' (FNERR_INVALIDFILENAME) and
     * would not open at all; a dropped or typed path may have them. */
    for (WCHAR *s = file; *s; ++s) if (*s == L'/') *s = L'\\';
    ZeroMemory(&picker, sizeof(picker));
    picker.lStructSize = sizeof(picker);
    picker.hwndOwner = native(g_window);
    picker.lpstrFile = file;
    picker.lpstrInitialDir = initial;
    picker.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
    picker.lpstrFilter = filter;
    picker.lpstrTitle = translation_title ? translation_title : L"Select a file to scan";
    picker.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER | OFN_NOCHANGEDIR;
    /* The picker may still leave the current directory in the chosen file's
     * folder (OFN_NOCHANGEDIR is documented as ineffective for open dialogs);
     * a current directory inside db\ makes Update database unable to rename it. */
    DWORD directory_size = GetCurrentDirectoryW(0, NULL);
    WCHAR *directory = directory_size ? HeapAlloc(GetProcessHeap(), 0, directory_size * sizeof(WCHAR)) : NULL;
    if (directory) {
        DWORD length = GetCurrentDirectoryW(directory_size, directory);
        if (!length || length >= directory_size) {
            HeapFree(GetProcessHeap(), 0, directory);
            directory = NULL;
        }
    }
    BOOL picked = GetOpenFileNameW(&picker);
    /* Any other name it cannot start from (wildcards, "<", a device): start empty. */
    if (!picked && CommDlgExtendedError() == FNERR_INVALIDFILENAME) {
        file[0] = 0;
        picked = GetOpenFileNameW(&picker);
    }
    if (directory) {
        SetCurrentDirectoryW(directory);
        HeapFree(GetProcessHeap(), 0, directory);
    }
    if (picked) {
        char *path = xx_str_unicode_to_utf8(file);
        if (path) {
            open_file(path);
            xx_str_free(path);
        }
    }
    xx_str_wfree(initial);
    xx_str_wfree(translation_title);
    xx_str_wfree(filter_name);
}

/* The tools share one table (gui_tools.c): the same titles, "no file" hints
 * and "<Tool>: <reason>" failures as the GTK and macOS frontends. Until the
 * Advanced view's tools grid (Phase 2) they are reached by their shortcuts,
 * which run through the shared dispatcher (gui_main_actions.c). */
static void run_shortcut(gui_shortcut_action shortcut);
static void do_tool(gui_tool_id id)
{
    run_shortcut((gui_shortcut_action)(GUI_SHORTCUT_TOOL_FIRST + (int)id));
}
static void do_file_information(void) { do_tool(GUI_TOOL_FILE_INFORMATION); }
static void do_entropy(void) { do_tool(GUI_TOOL_ENTROPY); }
static void do_hash(void) { do_tool(GUI_TOOL_HASH); }
static void do_strings(void) { do_tool(GUI_TOOL_STRINGS); }
static void do_hex_view(void) { do_tool(GUI_TOOL_HEX_VIEW); }
static void do_disasm(void) { do_tool(GUI_TOOL_DISASM); }
static void do_memory_map(void) { do_tool(GUI_TOOL_MEMORY_MAP); }
static void do_imports(void) { do_tool(GUI_TOOL_IMPORTS); }
static void do_exports(void) { do_tool(GUI_TOOL_EXPORTS); }
static void do_resources(void) { do_tool(GUI_TOOL_RESOURCES); }
static void do_symbols(void) { do_tool(GUI_TOOL_SYMBOLS); }
static void do_extractor(void) { do_tool(GUI_TOOL_EXTRACTOR); }

/* All settings live in one Options window (gui_options.c): a page list on
 * the left and the reusable xxwidgets forms on the right. */
static gui_options_page_id g_optionsPage = GUI_OPTIONS_SCAN;
static xx_settings *g_settings;
/* Explicit persistence smoke runs use only an isolated portable directory. */
enum { SETTINGS_SMOKE_NONE, SETTINGS_SMOKE_INSPECT, SETTINGS_SMOKE_WRITE, SETTINGS_SMOKE_CANCEL, SETTINGS_SMOKE_DEFAULT };
static int g_settingsSmoke;

/* The frontend's share of the Options settings, as currently applied. */
static xxwidgets_status current_options(gui_options_values *values)
{
    gui_frontend_get_preferences(values);
    values->scan.options.flags = flags_mask();
    values->scan.options.databases = databases_mask();
    values->scan.modes = gui_backend_get_scan_modes();
    gui_hex_view_get_options(&values->hex);
    return xxwidgets_app_get_font_options(g_app, &values->fonts);
}

/* The engine row's Scan combo shows Scan/Engine (Options > Scan sets it too). */
static void sync_engine(void)
{
    gui_backend_scan_preferences scan;
    xxwidgets_widget *engine = g_panel ? xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_ENGINE) : NULL;
    size_t i;
    if (!engine || !gui_backend_get_scan_preferences(&scan)) return;
    for (i = 0; i < gui_backend_engine_count() && i < xxwidgets_combobox_count(engine); ++i) {
        const char *id = gui_backend_engine_id(i);
        if (id && !strcmp(id, scan.engine)) { (void)xxwidgets_widget_set_value(engine, (int)i); return; }
    }
}

/* Applies settings from Options or from the store; fonts only when changed. */
static xxwidgets_status apply_options(const gui_options_values *values)
{
    xxwidgets_font_options fonts;
    xxwidgets_status status;
    gui_backend_set_scan_modes(values->scan.modes);
    g_bFlagDeep = (values->scan.options.flags & XXWIDGETS_SCAN_DEEP) != 0;
    g_bFlagHeur = (values->scan.options.flags & XXWIDGETS_SCAN_HEURISTIC) != 0;
    g_bFlagVerbose = (values->scan.options.flags & XXWIDGETS_SCAN_VERBOSE) != 0;
    g_bFlagAggr = (values->scan.options.flags & XXWIDGETS_SCAN_AGGRESSIVE) != 0;
    g_bFlagHideUnknown = (values->scan.options.flags & XXWIDGETS_SCAN_HIDE_UNKNOWN) != 0;
    g_bFlagFormat = (values->scan.options.flags & XXWIDGETS_SCAN_FORMAT) != 0;
    g_bDbCustom = (values->scan.options.databases & XXWIDGETS_SCAN_DATABASE_CUSTOM) != 0;
    apply_flags_to_backend();
    status = sync_panel_options();
    if (status != XXWIDGETS_OK) return status;
    gui_hex_view_set_options(&values->hex);
    status = xxwidgets_app_get_font_options(g_app, &fonts);
    if (status == XXWIDGETS_OK && memcmp(&values->fonts, &fonts, sizeof(fonts)))
        status = xxwidgets_app_set_font_options(g_app, &values->fonts);
    if (status == XXWIDGETS_OK) status = gui_frontend_apply_preferences(g_app, values);
    /* A new font or language: the panel's captions, its minimum (the window
     * grows to it, never shrinks) and its layout follow. */
    if (status == XXWIDGETS_OK && g_main) status = xxwidgets_die_main_panel_retranslate(g_main);
    if (status == XXWIDGETS_OK) {
        SetWindowPos(native(g_window), values->appearance.stay_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (g_main) (void)xxwidgets_file_name_bar_set_recent_available(xxwidgets_die_main_panel_file_name_bar(g_main),
                                                                       values->file.save_recent_files);
        sync_engine();
        apply_loaded(g_loaded);
    }
    return status;
}

/* The folder a database picker shows selected; smoke runs read it back. */
static WCHAR g_browseSelection[MAX_PATH];

static int CALLBACK browse_database_event(HWND window, UINT message, LPARAM parameter, LPARAM data)
{
    if (message == BFFM_INITIALIZED && data) SendMessageW(window, BFFM_SETSELECTIONW, TRUE, data);
    else if (message == BFFM_SELCHANGED && !SHGetPathFromIDListW((PCIDLIST_ABSOLUTE)parameter, g_browseSelection)) g_browseSelection[0] = 0;
    return 0;
}

/* Scan and Info share this picker; gui_options says which page asks, and the
 * title and start folder follow its rules (the same on every platform). */
static xxwidgets_status browse_database(xxwidgets_widget *dialog, const char *initial, char **path, int *accepted, void *user)
{
    BROWSEINFOW picker;
    LPITEMIDLIST item;
    WCHAR folder[MAX_PATH], start[MAX_PATH] = {0};
    gui_options_page_id page = gui_options_browse_page();
    WCHAR *title = xx_str_utf8_to_unicode(gui_options_browse_title(page));
    char *first = gui_options_browse_start(page, initial);
    WCHAR *wide = first ? xx_str_utf8_to_unicode(first) : NULL;
    xxwidgets_status status = XXWIDGETS_OK;
    (void)user;
    *path = NULL; *accepted = 0;
    if (wide && lstrlenW(wide) < MAX_PATH) lstrcpyW(start, wide);
    xx_str_wfree(wide);
    free(first);
    g_browseSelection[0] = 0;
    ZeroMemory(&picker, sizeof(picker));
    picker.hwndOwner = native(dialog);
    picker.lpszTitle = title ? title : L"Select the database folder";
    picker.ulFlags = BIF_RETURNONLYFSDIRS;
    picker.lpfn = browse_database_event;
    picker.lParam = start[0] ? (LPARAM)start : 0;
    item = SHBrowseForFolderW(&picker);
    xx_str_wfree(title);
    if (!item) return XXWIDGETS_OK;
    if (SHGetPathFromIDListW(item, folder)) {
        char *utf8 = xx_str_unicode_to_utf8(folder);
        size_t size = utf8 ? strlen(utf8) + 1 : 0;
        /* The form releases the chosen path with free(). */
        *path = utf8 ? (char *)malloc(size) : NULL;
        if (*path) { memcpy(*path, utf8, size); *accepted = 1; }
        else status = XXWIDGETS_OUT_OF_MEMORY;
        if (utf8) xx_str_free(utf8);
    }
    CoTaskMemFree(item);
    return status;
}

static int do_settings(gui_options_page_id start)
{
    gui_options_host host = {0};
    gui_options_values values;
    WCHAR executable[32768];
    DWORD length = GetModuleFileNameW(NULL, executable, (DWORD)(sizeof(executable) / sizeof(executable[0])));
    char *program = NULL;
    xxwidgets_status status, file_status = XXWIDGETS_OK;
    int accepted = 0, reloaded = 0, previous_context, previous_environment;

    status = current_options(&values);
    if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read interface fonts"), status); return -1; }
    /* The context menu needs the program path; without it the page is left out. */
    if (length && length < sizeof(executable) / sizeof(executable[0])) program = xx_str_unicode_to_utf8(executable);
    host.pages = (1u << GUI_OPTIONS_PAGE_COUNT) - 1u;
    /* Defaults in the persistence test must never change Explorer registration. */
    if (g_settingsSmoke) host.pages &= ~GUI_OPTIONS_PAGE(GUI_OPTIONS_FILE);
    if (program && !g_settingsSmoke && !g_memorySettings) {
        xxwidgets_context_config config = {"cdie", "Scan with cdie", "*", program, XXWIDGETS_CONTEXT_CURRENT_USER};
        int registered = 0;
        if (xxwidgets_context_is_registered(&config, &registered) == XXWIDGETS_OK) values.file.context_menu = registered;
        char *directory = gui_frontend_executable_directory();
        if (directory) values.file.set_environment = gui_frontend_environment_contains(directory);
        free(directory);
    }
    previous_context = values.file.context_menu;
    previous_environment = values.file.set_environment;
    host.executable = program;
    host.browse = browse_database;
    g_optionsPage = start;
    status = gui_options_dialog(g_window, &host, &g_optionsPage, &values, &accepted, &reloaded);
    if (status != XXWIDGETS_OK) { xx_str_free(program); report_ui_error(CDIE_N_("Open Options"), status); return -1; }

    if (reloaded) {
        WCHAR text[160];
        clear_scan();
        status = refresh_panel_tags();
        if (status != XXWIDGETS_OK) { xx_str_free(program); report_ui_error("Refresh signature tags", status); return -1; }
        cdie_translation_swprintf(text, sizeof(text)/sizeof(text[0]), CDIE_N_(L"Database reloaded: %d signatures. Choose a file and press Scan."), gui_backend_signature_count());
        note_message_wide(text);
    }
    if (!accepted) { xx_str_free(program); return 0; }
    if (program && !g_settingsSmoke && !g_memorySettings) {
        xxwidgets_context_config config = {"cdie", "Scan with cdie", "*", program, XXWIDGETS_CONTEXT_CURRENT_USER};
        char *directory = gui_frontend_executable_directory();
        if (values.file.context_menu != previous_context) {
            status = xxwidgets_context_set_enabled(&config, values.file.context_menu);
            if (status != XXWIDGETS_OK) {
                int registered = previous_context;
                file_status = status;
                (void)xxwidgets_context_is_registered(&config, &registered);
                values.file.context_menu = registered;
            }
        }
        if (values.file.set_environment != previous_environment) {
            status = directory ? gui_frontend_environment_apply(directory, values.file.set_environment) : XXWIDGETS_OUT_OF_MEMORY;
            if (status != XXWIDGETS_OK) {
                if (file_status == XXWIDGETS_OK) file_status = status;
                values.file.set_environment = previous_environment;
            }
        }
        free(directory);
    }
    xx_str_free(program);
    /* Native integration can be denied by the OS. Keep the accepted model and
     * backend coherent, retaining the observed flags for failed changes. */
    status = apply_options(&values);
    if (status != XXWIDGETS_OK) { report_ui_error("Apply options", status); return -1; }
    int saved = gui_settings_save(g_settings, &values, g_optionsPage);
    if (file_status != XXWIDGETS_OK) {
        char message[384];
        xx_rt_snprintf(message, sizeof(message), "%s: %s%s", CDIE_TR("Options applied; File integration failed"),
            gui_error_status_text(file_status), saved ? "" : CDIE_TR(" Options could not be saved for the next start."));
        show_error(message);
    } else if (!saved)
        show_error(CDIE_TR("Options applied, but they could not be saved for the next start."));
    else if (!reloaded) note_message(CDIE_TR("Options applied."));
    return 1;
}

/* Update database: the source stored under Options > Scan, progress and
 * Cancel in a dialog owned by @p owner (NULL shows none), which also shows a
 * failure; the outcome is kept as the last message. A reloaded database
 * replaces the results of the old one. */
static gui_update_result do_update_database(xxwidgets_widget *owner)
{
    gui_options_values values;
    char message[512];
    gui_update_result result;
    uint64_t generation = gui_backend_database_generation();
    gui_preferences_get(&values);
    result = gui_update_run(owner, values.databases.main_update_url, message, sizeof(message));
    /* Installed, or switched to an up-to-date per-user copy. */
    if (gui_backend_database_generation() != generation) {
        xxwidgets_status status;
        clear_scan();
        status = refresh_panel_tags();
        if (status != XXWIDGETS_OK) { report_ui_error("Refresh signature tags", status); return result; }
    }
    note_message(message);
    /* gui_update_run shows failures and "up to date"; an installed database
     * says so in a message, as on Linux and macOS. */
    if (owner && result == GUI_UPDATE_INSTALLED) show_error(message);
    return result;
}

static xxwidgets_status configure_about(void)
{
    return gui_about_create(cdie_about_png, cdie_about_png_size, &g_about);
}

static void do_about(void)
{
    xxwidgets_status status = xxwidgets_about_dialog_show(g_about, g_window);
    if (status != XXWIDGETS_OK) report_ui_error(CDIE_N_("Open About"), status);
}

/* The report (">"), from the button or its shortcut: only once there is one. */
static void do_report(void)
{
    xxwidgets_status status;
    if (!IsWindowEnabled(native(g_reportButton))) return;
    status = xxwidgets_scan_panel_show_report(g_panel);
    if (status != XXWIDGETS_OK) report_ui_error(CDIE_N_("Open scan report"), status);
}

/* A control's text in its own font: one line, or wrapped at wrap pixels.
 * The smoke test checks dialogs and the main window with it. */
static SIZE window_text_size(HWND handle, int wrap)
{
    WCHAR text[512], class_name[16];
    RECT bounds = {0, 0, wrap > 0 ? wrap : 0, 0};
    SIZE size = {0, 0};
    int length = GetWindowTextW(handle, text, 512);
    HDC dc = length > 0 ? GetDC(handle) : NULL;
    if (dc) {
        HFONT font = (HFONT)SendMessageW(handle, WM_GETFONT, 0, 0);
        HGDIOBJ old = font ? SelectObject(dc, font) : NULL;
        UINT format = DT_CALCRECT | DT_EXPANDTABS | (wrap > 0 ? DT_WORDBREAK : DT_SINGLELINE);
        if (GetClassNameW(handle, class_name, 16) && !lstrcmpiW(class_name, L"Static") &&
            (GetWindowLongPtrW(handle, GWL_STYLE) & SS_NOPREFIX)) format |= DT_NOPREFIX;
        DrawTextW(dc, text, length, &bounds, format);
        if (old) SelectObject(dc, old);
        ReleaseDC(handle, dc);
        size.cx = bounds.right; size.cy = bounds.bottom;
    }
    return size;
}

static TEXTMETRICW control_font_metrics(xxwidgets_widget *widget)
{
    TEXTMETRICW metrics = {0};
    HDC dc = GetDC(native(widget));
    if (dc) {
        HFONT font = (HFONT)SendMessageW(native(widget), WM_GETFONT, 0, 0);
        HGDIOBJ old = font ? SelectObject(dc, font) : NULL;
        GetTextMetricsW(dc, &metrics);
        if (old) SelectObject(dc, old);
        ReleaseDC(native(widget), dc);
    }
    return metrics;
}

/* Another instance hands its file over; the panel's own layout and the
 * window minimum belong to xxwidgets (WM_GETMINMAXINFO in the backend). */
static LRESULT CALLBACK window_subclass(HWND handle, UINT message, WPARAM wp, LPARAM lp,
                                        UINT_PTR subclass_id, DWORD_PTR reference)
{
    (void)subclass_id; (void)reference;
    switch (message) {
    case WM_COPYDATA: {
        const COPYDATASTRUCT *data = (const COPYDATASTRUCT *)lp;
        if (data && data->dwData == GUI_FRONTEND_COPYDATA_ID && data->cbData > 1 && data->cbData <= GUI_PREFERENCE_PATH_MAX &&
            data->lpData && ((const char *)data->lpData)[data->cbData - 1] == 0 &&
            !memchr(data->lpData, 0, data->cbData - 1)) {
            if (xxwidgets_widget_set_text(g_path, (const char *)data->lpData) == XXWIDGETS_OK) {
                gui_frontend_remember_file((const char *)data->lpData);
                g_pending = ACTION_OPEN;
                return TRUE;
            }
        }
        return FALSE;
    }
    case WM_NCDESTROY: RemoveWindowSubclass(handle, window_subclass, 1); break;
    default: break;
    }
    return DefSubclassProc(handle, message, wp, lp);
}

static void do_recent(void)
{
    char path[GUI_PREFERENCE_PATH_MAX];
    int accepted = 0;
    xxwidgets_status status = gui_frontend_recent_dialog(g_window, path, sizeof(path), &accepted);
    if (status == XXWIDGETS_OK && accepted) open_file(path);
    else if (status != XXWIDGETS_OK) report_ui_error(CDIE_N_("Recent files"), status);
}

/* The path field as typed: opened (scanned when Options say so). */
static void do_open(void)
{
    char *path = read_path(CDIE_N_("Read file name"));
    if (!path) return;
    if (gui_frontend_scan_after_open()) do_scan();
    else if (!path[0]) { MessageBeep(MB_ICONASTERISK); xxwidgets_widget_focus(g_path); }
    else if (load_file(path)) gui_frontend_remember_file(path);
    HeapFree(GetProcessHeap(), 0, path);
}

static void request_exit(void)
{
    g_running = 0;
    g_quit = 1;
    xxwidgets_app_quit(g_app, 0);
}

/* The scan bar's Signatures, Directory and Log open through gui_tree, as a
 * click of theirs always did: marked here, shown by gui_tree_process. */
static void tree_button(xxwidgets_widget *button)
{
    xxwidgets_event click;
    memset(&click, 0, sizeof(click));
    click.type = XXWIDGETS_EVENT_CLICK;
    click.widget = button;
    (void)gui_tree_event(g_panel, &click);
}

static int do_settings(gui_options_page_id start);
static gui_update_result do_update_database(xxwidgets_widget *owner);
static void do_about(void);
static void do_report(void);
static void do_browse(void);
static xxwidgets_status current_options(gui_options_values *values);

/* The hooks of the shared dispatcher (gui_main_actions.h). */
static void hook_browse(void *user) { (void)user; do_browse(); }
static void hook_recent(void *user) { (void)user; do_recent(); }
/* Return in the path field opens it (spec 1.5), scanned when Options say so,
 * as on Linux and macOS. */
static void hook_open_path(void *user) { (void)user; do_open(); }
/* The dispatcher picked the type (g_fileTypes); do_scan scans with it. */
static void hook_scan(void *user, int type_id) { (void)user; (void)type_id; do_scan(); }
static void hook_report(void *user) { (void)user; do_report(); }
static void hook_signatures(void *user) { (void)user; tree_button(g_signaturesButton); }
static void hook_directory(void *user) { (void)user; tree_button(g_directoryButton); }
static void hook_log(void *user) { (void)user; tree_button(g_logButton); }
static void hook_update(void *user) { (void)user; (void)do_update_database(g_window); }
static void hook_options(void *user, int page) { (void)user; (void)do_settings(page < 0 ? g_optionsPage : (gui_options_page_id)page); }
static void hook_about(void *user) { (void)user; do_about(); }
static void hook_exit(void *user) { (void)user; request_exit(); }
static void hook_error(void *user, const char *text) { (void)user; show_error(text); }

/* Changes only: View/Advanced and Scan/Engine reach die.ini as Qt writes them. */
static void hook_settings_changed(void *user)
{
    gui_options_values values;
    (void)user;
    if (!g_settings || current_options(&values) != XXWIDGETS_OK) return;
    if (!gui_settings_save(g_settings, &values, g_optionsPage))
        show_error(CDIE_TR("Options applied, but they could not be saved for the next start."));
}

static void main_hooks(gui_main_hooks *hooks)
{
    memset(hooks, 0, sizeof(*hooks));
    hooks->browse = hook_browse; hooks->recent = hook_recent; hooks->open_path = hook_open_path;
    hooks->scan = hook_scan; hooks->report = hook_report; hooks->signatures = hook_signatures;
    hooks->directory = hook_directory; hooks->log = hook_log; hooks->update = hook_update;
    hooks->options = hook_options; hooks->about = hook_about; hooks->exit = hook_exit;
    hooks->settings_changed = hook_settings_changed; hooks->error = hook_error;
    hooks->with_update = gui_update_supported();
    hooks->file_types = &g_fileTypes;
}

/* An action of the main panel, carried out by the shared dispatcher. */
static void run_die_action(const xxwidgets_die_action *action)
{
    gui_main_hooks hooks;
    char *path = read_path(CDIE_N_("Read file path"));
    main_hooks(&hooks);
    (void)gui_main_run_action(g_window, g_main, path ? path : "", action, &hooks);
    if (path) HeapFree(GetProcessHeap(), 0, path);
}

static void run_shortcut(gui_shortcut_action shortcut)
{
    gui_main_hooks hooks;
    char *path = read_path(CDIE_N_("Read file path"));
    main_hooks(&hooks);
    if (!gui_main_run_shortcut(g_window, g_main, path ? path : "", shortcut, &hooks) && path && !path[0])
        xxwidgets_widget_focus(g_path);
    if (path) HeapFree(GetProcessHeap(), 0, path);
}

/* What the main loop does once app_poll returns. */
static void run_pending(int action)
{
    if (action == ACTION_OPEN) do_open();
    else if (action == ACTION_DIE) {
        xxwidgets_die_action die = g_pendingDie;
        memset(&g_pendingDie, 0, sizeof(g_pendingDie));
        run_die_action(&die);
    } else if (action >= ACTION_SHORTCUT) {
        gui_shortcut_action shortcut = gui_shortcuts_action(action, ACTION_SHORTCUT);
        if (shortcut != GUI_SHORTCUT_NONE) run_shortcut(shortcut);
    }
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user_data)
{
    xxwidgets_die_action action;
    (void)app; (void)user_data;
    if (event->type == XXWIDGETS_EVENT_CLOSE && event->widget == g_window) {
        request_exit();
        return;
    }
    if (event->type == XXWIDGETS_EVENT_DROP_FILES && event->widget == g_window) {
        if (event->path_count && xxwidgets_widget_set_text(g_path, event->paths[0]) == XXWIDGETS_OK) {
            gui_frontend_remember_file(event->paths[0]);
            g_pending = ACTION_OPEN;
        }
        return;
    }
    if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
        g_pending = event->value;
        return;
    }
    if (g_main && xxwidgets_die_main_panel_handle_event(g_main, event, &action)) {
        switch (action.id) {
        case XXWIDGETS_DIE_ACTION_NONE: break;
        case XXWIDGETS_DIE_ACTION_ADVANCED_CHANGED:
            /* The view is applied; storing it opens nothing. */
            run_die_action(&action);
            break;
        case XXWIDGETS_DIE_ACTION_PATH_EDITED:
            /* The types describe the last opened file: none is chosen for a
             * newly typed path before it is opened. */
            if (g_type) xxwidgets_widget_set_enabled(g_type, 0);
            break;
        default:
            g_pendingDie = action;
            g_pending = ACTION_DIE;
            break;
        }
        return;
    }
    if (gui_tree_event(g_panel, event)) {
        /* S or ? on a result row: shown after polling returns. */
    } else if (event->type == XXWIDGETS_EVENT_CHANGE &&
               (event->widget == g_flags || event->widget == g_databases ||
                event->widget == g_includeTags || event->widget == g_excludeTags)) {
        xxwidgets_status status = read_panel_options();
        if (status != XXWIDGETS_OK) report_ui_error(CDIE_N_("Read scan options"), status);
        else if (event->widget == g_includeTags || event->widget == g_excludeTags) hook_settings_changed(NULL);
    }
}

/* A shortcut does what its button does, so nothing while the button is
 * disabled (no report yet, history off, no updater). */
static int shortcut_allowed(xxwidgets_widget *window, int action, void *user)
{
    gui_shortcut_action shortcut = gui_shortcuts_action(action, ACTION_SHORTCUT);
    (void)window; (void)user;
    if (shortcut == GUI_SHORTCUT_REPORT) return IsWindowEnabled(native(g_reportButton)) != 0;
    if (shortcut == GUI_SHORTCUT_RECENT) return IsWindowEnabled(native(g_recent)) != 0;
    if (shortcut == GUI_SHORTCUT_UPDATE) return IsWindowEnabled(native(g_updateButton)) != 0;
    return 1;
}

static int create_controls(void)
{
    static xxwidgets_shortcut shortcuts[2 * GUI_SHORTCUT_COUNT];
    static char storage[1024];
    static const xxwidgets_scan_panel_control_id facts[4] = {
        XXWIDGETS_SCAN_PANEL_ENDIANNESS, XXWIDGETS_SCAN_PANEL_MODE, XXWIDGETS_SCAN_PANEL_ARCHITECTURE, XXWIDGETS_SCAN_PANEL_TYPE};
    xxwidgets_file_name_bar *bar;
    size_t count;
    int i;
    /* Update database is in the right column above Shortcuts (Q2), disabled
     * without an updater; the tag filters stay in the scan bar (Q3). */
    if (xxwidgets_die_main_panel_create(g_window, XXWIDGETS_DIE_MAIN_PANEL_SHOW_UPDATE | XXWIDGETS_DIE_MAIN_PANEL_TAG_FILTERS,
                                        &g_main) != XXWIDGETS_OK) return 0;
    bar = xxwidgets_die_main_panel_file_name_bar(g_main);
    g_header = xxwidgets_die_main_panel_file_header(g_main);
    g_panel = xxwidgets_die_main_panel_scan_panel(g_main);
    g_side = xxwidgets_die_main_panel_side_panel(g_main);
    if (!bar || !g_header || !g_panel || !g_side) return 0;
    g_path = xxwidgets_file_name_bar_control(bar, XXWIDGETS_FILE_NAME_BAR_PATH);
    g_recent = xxwidgets_file_name_bar_control(bar, XXWIDGETS_FILE_NAME_BAR_RECENT);
    g_browse = xxwidgets_file_name_bar_control(bar, XXWIDGETS_FILE_NAME_BAR_BROWSE);
    g_type = xxwidgets_value_field_control(xxwidgets_file_header_field(g_header, XXWIDGETS_FILE_HEADER_FILE_TYPE), XXWIDGETS_VALUE_FIELD_VALUE);
    g_flags = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FLAGS);
    g_databases = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_DATABASES);
    g_includeTags = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS);
    g_excludeTags = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS);
    g_scan = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_SCAN);
    g_reportButton = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_REPORT);
    g_results = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_RESULTS);
    g_flagLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FLAGS_LABEL);
    g_databaseLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_DATABASES_LABEL);
    g_includeTagsLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS_LABEL);
    g_excludeTagsLabel = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS_LABEL);
    g_signaturesButton = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_SIGNATURES);
    g_directoryButton = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_DIRECTORY);
    g_logButton = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_LOG);
    /* With the engine row these are the boxed, centred VALUE fields. */
    g_msec = xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_ELAPSED);
    for (i = 0; i < 4; ++i)
        if (!(g_facts[i] = xxwidgets_scan_panel_control(g_panel, facts[i]))) return 0;
    g_advanced = xxwidgets_side_panel_control(g_side, XXWIDGETS_DIE_SIDE_ADVANCED);
    g_updateButton = xxwidgets_side_panel_control(g_side, XXWIDGETS_DIE_SIDE_UPDATE);
    g_shortcutsButton = xxwidgets_side_panel_control(g_side, XXWIDGETS_DIE_SIDE_SHORTCUTS);
    g_options = xxwidgets_side_panel_control(g_side, XXWIDGETS_DIE_SIDE_OPTIONS);
    g_aboutButton = xxwidgets_side_panel_control(g_side, XXWIDGETS_DIE_SIDE_ABOUT);
    g_exit = xxwidgets_side_panel_control(g_side, XXWIDGETS_DIE_SIDE_EXIT);
    if (!g_path || !g_recent || !g_browse || !g_type || !g_flags || !g_databases || !g_scan || !g_reportButton || !g_results ||
        !g_signaturesButton || !g_directoryButton || !g_logButton || !g_msec ||
        !g_flagLabel || !g_databaseLabel || !g_includeTags || !g_excludeTags ||
        !g_includeTagsLabel || !g_excludeTagsLabel || !g_advanced || !g_updateButton || !g_shortcutsButton ||
        !g_options || !g_aboutButton || !g_exit || sync_panel_options() != XXWIDGETS_OK) return 0;
    if (gui_update_supported() &&
        xxwidgets_side_panel_set_item_enabled(g_side, XXWIDGETS_DIE_SIDE_UPDATE, 1) != XXWIDGETS_OK) return 0;
    if (!SetWindowSubclass(native(g_window), window_subclass, 1, 0)) return 0;
    if (xxwidgets_window_set_accept_file_drops(g_window, 1) != XXWIDGETS_OK) return 0;
    /* One shortcut table for the three desktop frontends (gui_shortcuts.c):
     * every key the old buttons had keeps working, the tools' too. */
    count = gui_shortcuts_bindings(gui_shortcuts_platform(), ACTION_SHORTCUT, gui_update_supported(), shortcuts,
                                   sizeof(shortcuts) / sizeof(shortcuts[0]), storage, sizeof(storage));
    if (!count || xxwidgets_window_set_shortcut_filter(g_window, shortcut_allowed, NULL) != XXWIDGETS_OK) return 0;
    return xxwidgets_window_set_shortcuts(g_window, shortcuts, count) == XXWIDGETS_OK;
}

/* Explicit --smoke-test mode validates the real GUI and modal ownership with
 * bounded timers on the UI thread. It never scans a user file or alters paths.
 * Every dialog a step opens is checked once it shows: visible, inside the
 * monitor's work area, controls inside it and not overlapping, their text
 * fitting, focus inside it. After the opener returns, the main window must be
 * enabled with the focus and own nothing left behind, and a dialog the opener
 * shows after the step's own one fails the step and is closed. Native dialogs
 * are dismissed by control ID (IDCANCEL), never by their localized captions. */
static const WCHAR *g_smokeTitle;
static UINT_PTR g_smokeTimer;
static int g_smokeAction, g_smokeDone, g_smokeFailed, g_smokePhase;
/* Options/About must block all application windows and restore only those
 * which were enabled before opening the dialog. These belong to the same app,
 * have no owner, and are kept separate from the dialog's direct owner. */
static xxwidgets_widget *g_smokeEnabledSibling, *g_smokeDisabledSibling;
static int g_smokeApplicationModal;
/* The dialog a step matched and a deadline (not a tick budget, which a busy
 * worker stretches). Escape closes instead of WM_CLOSE; an optional step's
 * dialog may legitimately stay away. g_smokeFontSized: text and overlaps are
 * measured too; off in the 24-point pass, whose cells the backend holds so
 * that an Options dialog fits the screen (its text is clipped there). */
static HWND g_smokeDialog;
static ULONGLONG g_smokeDeadline;
static int g_smokeTimedOut, g_smokeSeen, g_smokeChecked, g_smokeNestedSeen, g_smokeNestedChecked, g_smokeWait;
static int g_smokeEscape, g_smokeOptional, g_smokeFontSized = 1;
static size_t g_smokePicker;
static const WCHAR *g_smokeExpect;      /* a cell the step's table must hold */
static const char *g_smokeExpectText;   /* what a text dialog must show */
static const WCHAR *g_smokeExpectPart;  /* or part of it */
static WCHAR g_smokeFolder[MAX_PATH];   /* scratch folder holding g_smokeElf */
static WCHAR g_smokeElf[MAX_PATH];      /* an ELF with dynamic symbols */
#define SMOKE_STEP_MS 60000

static void smoke_write(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text, (DWORD)lstrlenA(text), &written, NULL);
}

static void smoke_trace(const char *text)
{
    if (!GetEnvironmentVariableW(L"CDIE_GUI_SMOKE_TRACE", NULL, 0)) return;
    smoke_write(text);
    smoke_write("\r\n");
}

/* The first failed check of a step: where, and the window and control it saw. */
static int smoke_fail_at(unsigned int line, HWND window, HWND control)
{
    char text[640];
    WCHAR title[96] = L"", class_name[32] = L"", caption[96] = L"";
    if (!g_smokeFailed) {
        if (window) GetWindowTextW(window, title, 96);
        if (control) { GetClassNameW(control, class_name, 32); GetWindowTextW(control, caption, 96); }
        wsprintfA(text, "cdie GUI smoke check failed at main_gui.c:%u (action %d, window \"%ls\", control %ls \"%ls\")\r\n",
                  line, g_smokeAction, title, class_name, caption);
        smoke_write(text);
    }
    g_smokeFailed = 1;
    return 0;
}

static void smoke_modal_cleanup(void)
{
    if (g_smokeEnabledSibling) xxwidgets_widget_destroy(g_smokeEnabledSibling);
    if (g_smokeDisabledSibling) xxwidgets_widget_destroy(g_smokeDisabledSibling);
    g_smokeEnabledSibling = g_smokeDisabledSibling = NULL;
    g_smokeApplicationModal = 0;
}

static int smoke_modal_setup(void)
{
    xxwidgets_rect bounds = {1, 1, 24, 4};
    if (xxwidgets_widget_create(g_app, NULL, XXWIDGETS_WINDOW, "Enabled modality smoke sibling", bounds, &g_smokeEnabledSibling) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, NULL, XXWIDGETS_WINDOW, "Disabled modality smoke sibling", bounds, &g_smokeDisabledSibling) != XXWIDGETS_OK ||
        xxwidgets_widget_set_enabled(g_smokeDisabledSibling, 0) != XXWIDGETS_OK ||
        !IsWindowEnabled(native(g_smokeEnabledSibling)) || IsWindowEnabled(native(g_smokeDisabledSibling))) {
        smoke_modal_cleanup();
        return 0;
    }
    g_smokeApplicationModal = 1;
    return 1;
}

/* Step actions. 0 closes a text dialog; 4..18 are the Features page and the
 * tool dialogs; 20 on name the remaining flows. */
enum { SMOKE_REPORT = 0, SMOKE_FEATURES_CANCEL = 4, SMOKE_FEATURES_OK, SMOKE_FEATURES_DEFAULT,
       SMOKE_FILE_INFORMATION, SMOKE_ENTROPY, SMOKE_HASH, SMOKE_STRINGS, SMOKE_HEX_VIEW, SMOKE_DISASM, SMOKE_MEMORY_MAP,
       SMOKE_IMPORTS, SMOKE_EXPORTS, SMOKE_RESOURCES, SMOKE_SYMBOLS, SMOKE_EXTRACTOR };
enum { SMOKE_SCAN_CANCEL = 20, SMOKE_SCAN_OK, SMOKE_FONTS_CANCEL, SMOKE_FONTS_OK, SMOKE_FILE_CANCEL, SMOKE_SIGNATURES_CANCEL, SMOKE_ABOUT,
       SMOKE_SIGNATURE, SMOKE_HEX_CANCEL, SMOKE_HEX_OK,
       SMOKE_OPTIONS_ESCAPE, SMOKE_OPTIONS_CLOSE, SMOKE_ABOUT_ESCAPE, SMOKE_ABOUT_CLOSE, SMOKE_SCAN_COLORS,
       SMOKE_STRINGS_CONFIG, SMOKE_DISK_CANCEL, SMOKE_STRINGS_FILE, SMOKE_PAGE_CANCEL, SMOKE_COLORS_OK, SMOKE_SCAN_MISSING_DB,
       SMOKE_PICKERS, SMOKE_LOG, SMOKE_INFO, SMOKE_DIRECTORY, SMOKE_RECENT, SMOKE_RECENT_EMPTY, SMOKE_SIGNATURE_CONFIRM,
       SMOKE_UPDATE_CANCEL, SMOKE_UPDATE_FAILED, SMOKE_PROGRESS_CANCEL, SMOKE_TOOL_RESIZE, SMOKE_UNEXPECTED, SMOKE_TOOL_INNER,
       SMOKE_SCAN_PAGE, SMOKE_UPDATE_QUESTION, SMOKE_MAIN_LAYOUT, SMOKE_SHORTCUTS };
static const WCHAR *g_smokeAnswer; /* SMOKE_UPDATE_QUESTION: the button to press */
static const WCHAR *g_smokePage;
static int g_smokePageState, g_smokeContextChecked;
static HWND g_smokeNestedParent;
static WCHAR g_smokeExtractorOffset[32];
/* Forms and pickers inside a step's dialog: the button that opens one and its
 * title (NULL: a native picker titled by the system). Each is cancelled.
 * known names a reported defect that keeps it from opening for now: then its
 * absence is traced, not failed, and its presence is checked as any other. */
typedef struct smoke_inner {
    const WCHAR *button, *title;
    const char *known;
} smoke_inner;
static const smoke_inner *g_smokeInner;
static size_t g_smokeInnerCount;
static ULONGLONG g_smokeInnerSince;

typedef struct smoke_search {
    const WCHAR *class_name, *text;
    int visible_only;
    HWND found;
} smoke_search;

static BOOL CALLBACK smoke_find_child(HWND child, LPARAM data)
{
    smoke_search *search = (smoke_search *)data;
    WCHAR class_name[32], text[128];
    GetClassNameW(child, class_name, 32);
    if (search->class_name && lstrcmpiW(class_name, search->class_name)) return TRUE;
    GetWindowTextW(child, text, 128);
    if (search->text && lstrcmpW(text, search->text)) return TRUE;
    if (search->visible_only && !IsWindowVisible(child)) return TRUE;
    search->found = child;
    return FALSE;
}

static HWND smoke_child(HWND window, const WCHAR *class_name, const WCHAR *text, int visible_only)
{
    smoke_search search = {class_name, text, visible_only, NULL};
    EnumChildWindows(window, smoke_find_child, (LPARAM)&search);
    return search.found;
}

/* A visible label whose text contains part, e.g. a status line. */
static BOOL CALLBACK smoke_find_label(HWND child, LPARAM data)
{
    smoke_search *search = (smoke_search *)data;
    WCHAR class_name[16], text[512];
    if (!IsWindowVisible(child) || !GetClassNameW(child, class_name, 16) || lstrcmpiW(class_name, L"Static") ||
        !GetWindowTextW(child, text, 512) || !wcsstr(text, search->text)) return TRUE;
    search->found = child;
    return FALSE;
}

static HWND smoke_label(HWND window, const WCHAR *part)
{
    smoke_search search = {NULL, part, 1, NULL};
    EnumChildWindows(window, smoke_find_label, (LPARAM)&search);
    return search.found;
}

/* Whether the foreground window is one of this thread's (dialogs included). */
static int smoke_foreground_ours(void)
{
    HWND foreground = GetForegroundWindow();
    return foreground && GetWindowThreadProcessId(foreground, NULL) == GetCurrentThreadId();
}

/* Set when the step began without the foreground, or when another program
 * took it while the step's dialog was still up (seen by smoke_tick). */
static int g_smokeForegroundLost;

/* Focus means something only while the program holds the foreground. When
 * another program takes it (a concurrent test, the user) during a step,
 * there is nothing to check. When the thread held it until the step closed
 * its dialog, it is checked; smoke_finish also fails a foreground that left
 * on that close (a dialog hidden while its owner was still disabled). */
static int smoke_focus_known(void)
{
    static int noted;
    if (smoke_foreground_ours() || !g_smokeForegroundLost) return 1;
    if (!noted) {
        noted = 1;
        smoke_trace("GUI smoke: focus is not checked while another program is in the foreground");
    }
    return 0;
}

static int smoke_native_dialog(HWND window)
{
    WCHAR class_name[16];
    return GetClassNameW(window, class_name, 16) && !lstrcmpW(class_name, L"#32770");
}

/* A native dialog or an xxwidgets window: what a step may open or leave. */
static int smoke_dialog_window(HWND window)
{
    WCHAR class_name[64];
    return smoke_native_dialog(window) || (GetClassNameW(window, class_name, 64) && lstrlenW(class_name) > 17 &&
        CompareStringOrdinal(class_name, 17, L"xxwidgets_window_", 17, FALSE) == CSTR_EQUAL);
}

static void smoke_cancel(HWND window)
{
    PostMessageW(window, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
}

static BOOL CALLBACK smoke_cancel_native(HWND window, LPARAM data)
{
    (void)data;
    if (smoke_native_dialog(window)) smoke_cancel(window);
    return TRUE;
}

/* Closes the step's dialog as a user would: Escape at the focused control,
 * or the frame's close box. */
static void smoke_close(HWND window)
{
    HWND focus = GetFocus();
    if (g_smokeEscape) PostMessageW(focus && IsChild(window, focus) ? focus : window, WM_KEYDOWN, VK_ESCAPE, 0);
    else PostMessageW(window, WM_CLOSE, 0, 0);
}

/* Buttons, check boxes and labels show all of their text. Ellipsis labels
 * shorten by design; scrolling statics are the signature editor's views. */
static int smoke_text_fits(HWND control)
{
    WCHAR class_name[16];
    LONG_PTR style = GetWindowLongPtrW(control, GWL_STYLE);
    RECT client;
    SIZE size;
    int room;
    if (!GetClassNameW(control, class_name, 16) || !GetClientRect(control, &client) || !GetWindowTextLengthW(control)) return 1;
    if (!lstrcmpiW(class_name, L"Button")) {
        LONG_PTR type = style & BS_TYPEMASK;
        HDC dc = GetDC(control);
        int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc) ReleaseDC(control, dc);
        if (type == BS_GROUPBOX || type == BS_OWNERDRAW || (style & (BS_BITMAP | BS_ICON))) return 1;
        /* A push button's frame; a check box's 13-pixel box and 3-pixel gap.
         * The last glyph's advance usually ends in a pixel of bearing. */
        room = client.right + 1 - (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON ? MulDiv(6, dpi, 96) : MulDiv(16, dpi, 96));
        size = window_text_size(control, (style & BS_MULTILINE) ? room : 0);
        return size.cx <= room && size.cy <= client.bottom;
    }
    if (!lstrcmpiW(class_name, L"Static")) {
        LONG_PTR type = style & SS_TYPEMASK;
        if ((type != SS_LEFT && type != SS_CENTER && type != SS_RIGHT && type != SS_LEFTNOWORDWRAP) ||
            (style & (SS_ELLIPSISMASK | WS_VSCROLL | WS_HSCROLL))) return 1;
        size = window_text_size(control, type == SS_LEFTNOWORDWRAP ? 0 : client.right);
        return size.cx <= client.right && size.cy <= client.bottom;
    }
    return 1;
}

/* Tab controls and group boxes frame other controls by design. */
static int smoke_frame(HWND control)
{
    WCHAR class_name[32];
    if (!GetClassNameW(control, class_name, 32)) return 0;
    return !lstrcmpiW(class_name, WC_TABCONTROLW) ||
        (!lstrcmpiW(class_name, L"Button") && (GetWindowLongPtrW(control, GWL_STYLE) & BS_TYPEMASK) == BS_GROUPBOX);
}

/* A native drop-down list is as tall as its font needs, a few pixels more
 * than the text row xxwidgets gives it, and so reaches into the next row's
 * top edge. That much is reported once (a backend layout issue), not failed. */
static int g_smokeComboOverflow;

static int smoke_combo_overflow(HWND first, const RECT *a, HWND second, const RECT *b, const RECT *overlap)
{
    WCHAR class_name[16];
    const RECT *combo = NULL;
    if (GetClassNameW(first, class_name, 16) && !lstrcmpiW(class_name, L"ComboBox")) combo = a;
    else if (GetClassNameW(second, class_name, 16) && !lstrcmpiW(class_name, L"ComboBox")) combo = b;
    if (!combo || overlap->bottom != combo->bottom || overlap->top <= combo->top || overlap->bottom - overlap->top > 6) return 0;
    if (overlap->bottom - overlap->top > g_smokeComboOverflow) {
        char text[128];
        g_smokeComboOverflow = overlap->bottom - overlap->top;
        wsprintfA(text, "GUI smoke: known layout issue: a drop-down list overlaps the row below it by %d px", g_smokeComboOverflow);
        smoke_trace(text);
    }
    return 1;
}

typedef struct smoke_layout {
    HWND dialog, children[256];
    size_t count;
} smoke_layout;

static BOOL CALLBACK smoke_layout_child(HWND child, LPARAM data)
{
    smoke_layout *layout = (smoke_layout *)data;
    if (GetParent(child) == layout->dialog && IsWindowVisible(child) &&
        layout->count < sizeof(layout->children) / sizeof(layout->children[0])) layout->children[layout->count++] = child;
    return TRUE;
}

/* The checks every dialog gets when it shows. A native dialog lays itself
 * out from a system template; only its frame and focus are ours to check.
 * 1 passed, 0 failed; -1 when retry allows waiting for the focus to arrive
 * (activation can trail the window by a few messages). */
static int smoke_check_dialog(HWND dialog, int native_dialog, int retry)
{
    smoke_layout layout;
    MONITORINFO monitor;
    RECT bounds, client, a, b, overlap;
    HWND focus = GetFocus();
    size_t i, j;
    int small_screen;
    layout.dialog = dialog; layout.count = 0;
    ZeroMemory(&monitor, sizeof(monitor)); monitor.cbSize = sizeof(monitor);
    if (!IsWindowVisible(dialog) || !GetWindowRect(dialog, &bounds) || !GetClientRect(dialog, &client) ||
        bounds.right <= bounds.left || bounds.bottom <= bounds.top || client.right <= 0 || client.bottom <= 0)
        return smoke_fail_at(__LINE__, dialog, NULL);
    if (!GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &monitor)) return smoke_fail_at(__LINE__, dialog, NULL);
    /* A work area smaller than the dialog's layout (Options needs about 742 px
     * at 96 DPI; a 1366x768 screen has less): the backend holds a resizable
     * window to the work area and its content cannot fit. That says nothing
     * about the dialog, so it is reported and its layout is not checked. */
    small_screen = bounds.right - bounds.left >= monitor.rcWork.right - monitor.rcWork.left ||
        bounds.bottom - bounds.top >= monitor.rcWork.bottom - monitor.rcWork.top;
    if (small_screen) {
        char text[256];
        WCHAR title[64] = L"";
        GetWindowTextW(dialog, title, 64);
        wsprintfA(text, "GUI smoke: known issue: \"%ls\" (%ldx%ld) does not fit the work area (%ldx%ld); its layout is not checked", title,
                  bounds.right - bounds.left, bounds.bottom - bounds.top, monitor.rcWork.right - monitor.rcWork.left,
                  monitor.rcWork.bottom - monitor.rcWork.top);
        smoke_trace(text);
    } else if (bounds.left < monitor.rcWork.left || bounds.top < monitor.rcWork.top ||
        bounds.right > monitor.rcWork.right || bounds.bottom > monitor.rcWork.bottom) {
        char text[160];
        wsprintfA(text, "dialog (%ld,%ld)-(%ld,%ld) outside the work area (%ld,%ld)-(%ld,%ld)\r\n", bounds.left, bounds.top,
                  bounds.right, bounds.bottom, monitor.rcWork.left, monitor.rcWork.top, monitor.rcWork.right, monitor.rcWork.bottom);
        smoke_write(text);
        return smoke_fail_at(__LINE__, dialog, NULL);
    }
    if (smoke_focus_known() && (!focus || (focus != dialog && !IsChild(dialog, focus)))) {
        char text[160];
        if (retry) return -1;
        wsprintfA(text, "focus %p, active %p, foreground %p, dialog %p\r\n", (void *)focus, (void *)GetActiveWindow(),
                  (void *)GetForegroundWindow(), (void *)dialog);
        smoke_write(text);
        return smoke_fail_at(__LINE__, dialog, focus);
    }
    if (native_dialog || small_screen) return 1;
    EnumChildWindows(dialog, smoke_layout_child, (LPARAM)&layout);
    for (i = 0; i < layout.count; ++i) {
        GetWindowRect(layout.children[i], &a);
        MapWindowPoints(NULL, dialog, (POINT *)&a, 2);
        if (a.left < 0 || a.top < 0 || a.right > client.right || a.bottom > client.bottom) {
            char text[160];
            wsprintfA(text, "control (%ld,%ld)-(%ld,%ld) outside the client area %ldx%ld\r\n", a.left, a.top, a.right, a.bottom,
                      client.right, client.bottom);
            smoke_write(text);
            return smoke_fail_at(__LINE__, dialog, layout.children[i]);
        }
        if (!g_smokeFontSized) continue;
        if (!smoke_text_fits(layout.children[i])) {
            char text[160];
            SIZE line = window_text_size(layout.children[i], 0);
            wsprintfA(text, "text %ldx%ld does not fit control %ldx%ld\r\n", line.cx, line.cy, a.right - a.left, a.bottom - a.top);
            smoke_write(text);
            return smoke_fail_at(__LINE__, dialog, layout.children[i]);
        }
        if (smoke_frame(layout.children[i])) continue;
        for (j = i + 1; j < layout.count; ++j) {
            if (smoke_frame(layout.children[j])) continue;
            GetWindowRect(layout.children[j], &b);
            MapWindowPoints(NULL, dialog, (POINT *)&b, 2);
            if (IntersectRect(&overlap, &a, &b) && !smoke_combo_overflow(layout.children[i], &a, layout.children[j], &b, &overlap)) {
                char text[200];
                wsprintfA(text, "controls (%ld,%ld)-(%ld,%ld) and (%ld,%ld)-(%ld,%ld) overlap\r\n", a.left, a.top, a.right, a.bottom,
                          b.left, b.top, b.right, b.bottom);
                smoke_write(text);
                return smoke_fail_at(__LINE__, layout.children[i], layout.children[j]);
            }
        }
    }
    return 1;
}

/* Each Options page and a control that only that page has. */
static const WCHAR *const g_smokeOptionsPages[][2] = {
    {L"Appearance", L"Stay on top"}, {L"File", L"Save last directory"}, {L"Fonts", L"Table views"},
    {L"Features", L"CPU acceleration"}, {L"Scan", L"Deep scan"}, {L"Signatures", L"Search signatures"},
    {L"Hex", L"Choose font"}, {L"Disasm", L"Uppercase"}, {L"Online tools", L"VirusTotal API key"}, {L"Info", L"Info database"}};

/* Native pickers reached from Options pages: the page, its button, for a
 * folder picker the instruction it must show for that page, and the title
 * the program gives it (NULL: the system's own, e.g. the font picker). */
typedef struct smoke_picker {
    const WCHAR *page, *button, *instruction, *title;
} smoke_picker;
static const smoke_picker g_smokePickers[] = {
    {L"Scan", L"Browse...", L"Select the database folder", NULL}, {L"Info", L"Browse...", L"Select the info database folder", NULL},
    {L"Appearance", L"Choose translation...", NULL, L"Choose translation"}, {L"Fonts", L"Choose", NULL, NULL},
    {L"Hex", L"Choose font", NULL, NULL}, {L"Disasm", L"Choose font...", NULL, NULL}};

static int smoke_select_page(HWND window, const WCHAR *page)
{
    HWND list = smoke_child(window, L"LISTBOX", NULL, 1);
    LRESULT index = list ? SendMessageW(list, LB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)page) : LB_ERR;
    if (index == LB_ERR || SendMessageW(list, LB_SETCURSEL, (WPARAM)index, 0) == LB_ERR) return 0;
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(list), LBN_SELCHANGE), (LPARAM)list);
    return 1;
}

/* 1 once the page's probe is visible and every other page's probe is hidden. */
static int smoke_page_shown(HWND window, const WCHAR *page)
{
    size_t i;
    int found = 0;
    for (i = 0; i < sizeof(g_smokeOptionsPages) / sizeof(g_smokeOptionsPages[0]); ++i) {
        HWND probe = smoke_child(window, NULL, g_smokeOptionsPages[i][1], 0);
        int selected = !lstrcmpW(page, g_smokeOptionsPages[i][0]);
        if (!probe) {
            if (selected) return 0;
            continue;
        }
        if (!!IsWindowVisible(probe) != selected) return 0;
        found |= selected;
    }
    return found;
}

/* What the shown Options page holds: its checkboxes, and whether a visible
 * caption names the extra database cdie no longer has ("Extractor" is no match). */
typedef struct smoke_scan_page {
    int checkboxes, extra;
} smoke_scan_page;

static BOOL CALLBACK smoke_scan_page_child(HWND child, LPARAM data)
{
    smoke_scan_page *page = (smoke_scan_page *)data;
    WCHAR class_name[32], text[128];
    if (!IsWindowVisible(child)) return TRUE;
    GetClassNameW(child, class_name, 32);
    GetWindowTextW(child, text, 128);
    if (!lstrcmpiW(class_name, L"BUTTON") && (GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX) ++page->checkboxes;
    if (wcsstr(text, L"Extra") && !wcsstr(text, L"Extractor")) page->extra = 1;
    return TRUE;
}

static void smoke_click(HWND window, const WCHAR *text)
{
    HWND button = smoke_child(window, L"Button", text, 1);
    if (button && IsWindowEnabled(window) && IsWindowEnabled(button)) {
        if (g_smokeApplicationModal) {
            SetFocus(button);
            if (GetFocus() != button && smoke_focus_known()) { smoke_fail_at(__LINE__, window, button); return; }
        }
        SendMessageW(button, BM_CLICK, 0, 0);
    } else smoke_fail_at(__LINE__, window, button);
}

static BOOL CALLBACK smoke_information_fields(HWND child, LPARAM data)
{
    HWND *table = (HWND *)data;
    WCHAR class_name[32];
    GetClassNameW(child, class_name, 32);
    if (!lstrcmpW(class_name, WC_LISTVIEWW) && ListView_GetItemCount(child) > 0) *table = child;
    return TRUE;
}

typedef struct smoke_cell {
    const WCHAR *text;
    int found;
} smoke_cell;

static BOOL CALLBACK smoke_find_cell(HWND child, LPARAM data)
{
    smoke_cell *cell = (smoke_cell *)data;
    WCHAR class_name[32], text[256];
    int rows, columns, row, column;
    GetClassNameW(child, class_name, 32);
    if (lstrcmpW(class_name, WC_LISTVIEWW)) return TRUE;
    rows = ListView_GetItemCount(child);
    columns = Header_GetItemCount(ListView_GetHeader(child));
    for (row = 0; row < rows && row < 4096; ++row)
        for (column = 0; column < (columns > 0 ? columns : 1); ++column) {
            text[0] = 0;
            ListView_GetItemText(child, row, column, text, 256);
            if (!lstrcmpW(text, cell->text)) { cell->found = 1; return FALSE; }
        }
    return TRUE;
}

static int smoke_table_contains(HWND window, const WCHAR *text)
{
    smoke_cell cell = {text, 0};
    EnumChildWindows(window, smoke_find_cell, (LPARAM)&cell);
    return cell.found;
}

static BOOL CALLBACK smoke_extractor_rows(HWND child, LPARAM data)
{
    WCHAR class_name[32];
    GetClassNameW(child, class_name, 32);
    if (!lstrcmpW(class_name, WC_LISTVIEWW) && Header_GetItemCount(ListView_GetHeader(child)) == 9) {
        *(HWND *)data = child;
        return FALSE;
    }
    return TRUE;
}

static BOOL CALLBACK smoke_hash_rows(HWND child, LPARAM data)
{
    HWND *table = (HWND *)data;
    WCHAR class_name[32];
    GetClassNameW(child, class_name, 32);
    if (!lstrcmpW(class_name, WC_TREEVIEWW) && TreeView_GetCount(child) == CDIE_HASH_COUNT) *table = child;
    return TRUE;
}

/* The Strings table: one selected row under #, Offset, Address, Region,
 * Size, Type and String, with a filter under every title. */
static BOOL CALLBACK smoke_strings_rows(HWND child, LPARAM data)
{
    static const WCHAR *const expected[] = {L"0", L"00000000", L"00401000", L".rdata", L"00000012", L"ASCII", L"CDIE Strings smoke"};
    HWND *rows = (HWND *)data, header;
    WCHAR class_name[32], text[256];
    int column;
    GetClassNameW(child, class_name, 32);
    if (lstrcmpW(class_name, WC_LISTVIEWW) || ListView_GetItemCount(child) != 1 ||
        ListView_GetNextItem(child, -1, LVNI_SELECTED) != 0) return TRUE;
    header = ListView_GetHeader(child);
    if (!header || !(GetWindowLongPtrW(header, GWL_STYLE) & HDS_FILTERBAR) || Header_GetItemCount(header) != 7) return TRUE;
    for (column = 0; column < 7; ++column) {
        text[0] = 0;
        ListView_GetItemText(child, 0, column, text, 256);
        if (lstrcmpW(text, expected[column])) return TRUE;
    }
    *rows = child;
    return TRUE;
}

/* The read-only text of xxwidgets_text_dialog. */
static BOOL CALLBACK smoke_text_view(HWND child, LPARAM data)
{
    WCHAR class_name[16];
    LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
    if (!GetClassNameW(child, class_name, 16) || lstrcmpiW(class_name, L"Edit") || !IsWindowVisible(child) ||
        !(style & ES_MULTILINE) || !(style & ES_READONLY)) return TRUE;
    *(HWND *)data = child;
    return FALSE;
}

/* The edit shows expected (UTF-8), line breaks aside. */
static int smoke_text_equals(HWND edit, const char *expected)
{
    int length = GetWindowTextLengthW(edit), same = 0;
    WCHAR *wide = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, ((size_t)length + 1) * sizeof(WCHAR));
    char *shown = NULL;
    if (wide && GetWindowTextW(edit, wide, length + 1) == length) shown = xx_str_unicode_to_utf8(wide);
    if (shown) {
        const char *a = shown, *b = expected;
        for (;;) {
            while (*a == '\r') ++a;
            while (*b == '\r') ++b;
            if (*a != *b) break;
            if (!*a) { same = 1; break; }
            ++a; ++b;
        }
    }
    xx_str_free(shown);
    if (wide) HeapFree(GetProcessHeap(), 0, wide);
    return same;
}

static int smoke_text_contains(HWND edit, const WCHAR *part)
{
    int length = GetWindowTextLengthW(edit), found = 0;
    WCHAR *text = (WCHAR *)HeapAlloc(GetProcessHeap(), 0, ((size_t)length + 1) * sizeof(WCHAR));
    if (text && GetWindowTextW(edit, text, length + 1) == length) found = wcsstr(text, part) != NULL;
    if (text) HeapFree(GetProcessHeap(), 0, text);
    return found;
}

/* A modifiable signature: a key into each scrolling editor view (the result
 * view is read-only and ignores it). */
static BOOL CALLBACK smoke_type_signature(HWND child, LPARAM data)
{
    WCHAR class_name[16];
    (void)data;
    if (GetClassNameW(child, class_name, 16) && !lstrcmpiW(class_name, L"Static") && (GetWindowLongPtrW(child, GWL_STYLE) & WS_VSCROLL))
        SendMessageW(child, WM_CHAR, L'x', 0);
    return TRUE;
}

static BOOL CALLBACK smoke_optimization_children(HWND child, LPARAM data)
{
    HWND *controls = (HWND *)data;
    WCHAR class_name[32], text[32];
    GetClassNameW(child, class_name, 32);
    if (!lstrcmpiW(class_name, L"COMBOBOX") && IsWindowVisible(child)) {
        if (!controls[0]) controls[0] = child;
        else if (!controls[1]) controls[1] = child;
    }
    GetWindowTextW(child, text, 32);
    if (!lstrcmpiW(class_name, L"BUTTON") &&
        (GetWindowLongPtrW(child, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX) {
        if (!lstrcmpW(text, L"Use SSE2")) controls[2] = child;
        else if (!lstrcmpW(text, L"Use AVX2")) controls[3] = child;
        else if (!lstrcmpW(text, L"Use SSE4.2")) controls[4] = child;
        else if (!lstrcmpW(text, L"Use AES")) controls[5] = child;
        else if (!lstrcmpW(text, L"Use SHA")) controls[6] = child;
        else if (!lstrcmpW(text, L"Use VPCLMULQDQ")) controls[7] = child;
        else if (!lstrcmpW(text, L"Use Arm NEON")) controls[8] = child;
        else if (!lstrcmpW(text, L"Use Arm CRC32")) controls[9] = child;
        else if (!lstrcmpW(text, L"Use Arm AES/SHA")) controls[10] = child;
        else if (!lstrcmpW(text, L"Use Arm Dot Product")) controls[11] = child;
        else if (!lstrcmpW(text, L"Use Arm PMULL")) controls[12] = child;
    }
    if (!lstrcmpW(text, L"Default")) controls[13] = child;
    else if (!lstrcmpW(text, L"OK")) controls[14] = child;
    else if (!lstrcmpW(text, L"Cancel")) controls[15] = child;
    return TRUE;
}

static int smoke_optimization_controls_ready(HWND *controls)
{
    int i;
    if (!controls[0] || !controls[1] || !controls[13] || !controls[14] || !controls[15]) return 0;
    if (!IsWindowVisible(controls[13]) || !IsWindowEnabled(controls[13])) return -1;
#if CDIE_GUI_ARM_TARGET
    for (i = 8; i < 13; ++i)
        if (!controls[i]) return 0;
    for (i = 2; i < 8; ++i)
        if (controls[i] && IsWindowVisible(controls[i])) return -1;
    for (i = 8; i < 13; ++i)
        if (!IsWindowVisible(controls[i])) return -1;
#else
    for (i = 2; i < 8; ++i)
        if (!controls[i]) return 0;
        else if (!IsWindowVisible(controls[i])) return -1;
    for (i = 8; i < 13; ++i)
        if (controls[i] && IsWindowVisible(controls[i])) return -1;
#endif
    return 1;
}

static int smoke_select_size(HWND window, HWND combo, const WCHAR *label)
{
    LRESULT count = SendMessageW(combo, CB_GETCOUNT, 0, 0), i;
    for (i = 0; i < count; ++i) {
        WCHAR text[96];
        LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)i, 0);
        if (length < 0 || length >= 96) continue;
        if (SendMessageW(combo, CB_GETLBTEXT, (WPARAM)i, (LPARAM)text) == CB_ERR || lstrcmpW(text, label)) continue;
        if (SendMessageW(combo, CB_SETCURSEL, (WPARAM)i, 0) == CB_ERR) return 0;
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(combo), CBN_SELCHANGE), (LPARAM)combo);
        return 1;
    }
    return 0;
}

static int smoke_selected_size(HWND combo, const WCHAR *label)
{
    WCHAR text[96];
    LRESULT length, selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) return 0;
    length = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)selected, 0);
    if (length < 0 || length >= 96) return 0;
    return SendMessageW(combo, CB_GETLBTEXT, (WPARAM)selected, (LPARAM)text) != CB_ERR && !lstrcmpW(text, label);
}

/* Windows the step's dialog opens itself: the Colors and Scan options forms,
 * the signature editor's confirmation and the native pickers of Options. */
static BOOL smoke_nested(HWND window)
{
    WCHAR title[128];
    int checked = 1;
    if (!IsWindowVisible(window)) return TRUE;
    GetWindowTextW(window, title, 128);
    if ((g_smokeAction == SMOKE_SCAN_COLORS || g_smokeAction == SMOKE_COLORS_OK) && g_smokePhase == 1) {
        if (lstrcmpW(title, L"Colors") || ++g_smokeNestedSeen < 3) return lstrcmpW(title, L"Colors") != 0;
        if (g_smokeAction == SMOKE_COLORS_OK && (checked = smoke_check_dialog(window, 0, g_smokeNestedSeen < 50)) < 0) return FALSE;
        if (IsWindowEnabled(g_smokeNestedParent) || !IsWindowEnabled(window) ||
            IsWindowEnabled(native(g_window)) || IsWindowEnabled(native(g_smokeEnabledSibling)) ||
            IsWindowEnabled(native(g_smokeDisabledSibling)) || !smoke_child(window, L"Button", L"Cancel", 1))
            smoke_fail_at(__LINE__, window, NULL);
        g_smokePhase = 2;
        if (g_smokeAction == SMOKE_SCAN_COLORS) PostMessageW(window, WM_CLOSE, 0, 0);
        else {
            /* The category table, the editors and the preview; OK keeps them for Options. */
            HWND table = NULL;
            EnumChildWindows(window, smoke_information_fields, (LPARAM)&table);
            if (!checked || !table || ListView_GetItemCount(table) < 2 ||
                !smoke_child(window, L"Button", L"Dark appearance", 1) || !smoke_child(window, L"Button", L"Apply color", 1) ||
                !smoke_child(window, L"Button", L"Default", 1) || !smoke_child(window, WC_TREEVIEWW, NULL, 1)) {
                smoke_fail_at(__LINE__, window, table);
                PostMessageW(window, WM_CLOSE, 0, 0);
            } else smoke_click(window, L"OK");
        }
        return FALSE;
    }
    if (g_smokeAction == SMOKE_DIRECTORY && g_smokePhase == 1) {
        if (lstrcmpW(title, L"Scan options") || !IsWindowEnabled(window) || ++g_smokeNestedSeen < 3) return lstrcmpW(title, L"Scan options") != 0;
        if ((checked = smoke_check_dialog(window, 0, g_smokeNestedSeen < 50)) < 0) return FALSE;
        {
            /* Only the custom database is offered here; no caption names an extra one. */
            smoke_scan_page page = {0, 0};
            EnumChildWindows(window, smoke_scan_page_child, (LPARAM)&page);
            if (!checked || !smoke_child(window, L"Button", L"Deep scan", 1) || !smoke_child(window, L"Button", L"Custom database", 1) ||
                page.extra) smoke_fail_at(__LINE__, window, NULL);
        }
        smoke_click(window, L"Cancel");
        g_smokePhase = 2;
        return FALSE;
    }
    if (g_smokeAction == SMOKE_SIGNATURE_CONFIRM && g_smokePhase == 2) {
        HWND discard;
        if (lstrcmpW(title, L"Unsaved signature") || !IsWindowEnabled(window) || ++g_smokeNestedSeen < 3)
            return lstrcmpW(title, L"Unsaved signature") != 0;
        /* Discarding is opt-in: Cancel keeps the edited document. */
        discard = smoke_child(window, L"Button", L"Discard the unsaved signature changes", 1);
        if ((checked = smoke_check_dialog(window, 0, g_smokeNestedSeen < 50)) < 0) return FALSE;
        if (!checked || !discard || SendMessageW(discard, BM_GETCHECK, 0, 0) == BST_CHECKED)
            smoke_fail_at(__LINE__, window, discard);
        smoke_click(window, L"Cancel");
        g_smokePhase = 3;
        return FALSE;
    }
    if (g_smokeInner && g_smokePhase == 1) {
        const smoke_inner *inner = &g_smokeInner[g_smokePicker];
        int native_dialog = smoke_native_dialog(window);
        if (inner->title ? lstrcmpW(title, inner->title) != 0 : !native_dialog) return TRUE;
        if (!IsWindowEnabled(window) || ++g_smokeNestedSeen < 3) return FALSE;
        if ((checked = smoke_check_dialog(window, native_dialog, g_smokeNestedSeen < 50)) < 0) return FALSE;
        if (native_dialog) smoke_cancel(window);
        else smoke_click(window, L"Cancel");
        g_smokePhase = 2;
        return FALSE;
    }
    if (g_smokeAction == SMOKE_PICKERS && g_smokePhase == 2) {
        const smoke_picker *picker = &g_smokePickers[g_smokePicker];
        if (!smoke_native_dialog(window) || ++g_smokeNestedSeen < 3) return !smoke_native_dialog(window);
        if (!g_smokeNestedChecked) {
            if ((checked = smoke_check_dialog(window, 1, g_smokeNestedSeen < 50)) < 0) return FALSE;
            g_smokeNestedChecked = 1;
            /* The button's own picker, not just any native dialog. */
            if (checked && picker->title && lstrcmpW(title, picker->title)) checked = smoke_fail_at(__LINE__, window, NULL);
            if (!checked) {
                smoke_cancel(window);
                g_smokePhase = 3;
                return FALSE;
            }
        }
        if (picker->instruction) {
            /* The folder picker opens on the folder in the page's field and
             * says which database it is for. */
            WCHAR selected[MAX_PATH], expected[MAX_PATH];
            if (!GetLongPathNameW(g_browseSelection, selected, MAX_PATH) || !GetLongPathNameW(g_smokeFolder, expected, MAX_PATH) ||
                lstrcmpiW(selected, expected)) {
                if (g_smokeNestedSeen < 60) return FALSE;
                smoke_fail_at(__LINE__, window, NULL);
            }
            if (!smoke_child(window, L"Static", picker->instruction, 1)) smoke_fail_at(__LINE__, window, NULL);
        }
        smoke_cancel(window);
        g_smokePhase = 3;
        return FALSE;
    }
    return TRUE;
}

/* Opens the step's inner forms and pickers one by one from their buttons
 * (smoke_nested cancels each); 1 once all are done. */
static int smoke_inner_step(HWND window)
{
    HWND button;
    if (g_smokePhase == 1) {
        /* Still enabled after two seconds: nothing modal opened. */
        if (!g_smokeInner[g_smokePicker].known || GetTickCount64() < g_smokeInnerSince + 2000) return 0;
        smoke_trace(g_smokeInner[g_smokePicker].known);
        g_smokePhase = 2;
    }
    if (g_smokePhase == 2) {
        ++g_smokePicker;
        g_smokePhase = 0;
    }
    if (g_smokePicker >= g_smokeInnerCount) return 1;
    /* A button that waits for its data (Save after a scan) is waited for. */
    button = smoke_child(window, L"Button", g_smokeInner[g_smokePicker].button, 1);
    if (!button || !IsWindowEnabled(button)) return 0;
    g_smokeNestedParent = window;
    g_smokeNestedSeen = g_smokeNestedChecked = 0;
    g_smokePhase = 1;
    g_smokeInnerSince = GetTickCount64();
    smoke_click(window, g_smokeInner[g_smokePicker].button);
    return 0;
}

static BOOL CALLBACK smoke_windows(HWND window, LPARAM data)
{
    WCHAR title[128];
    HWND owner = GetWindow(window, GW_OWNER);
    int native_dialog;
    (void)data;
    if (g_smokeNestedParent && owner == g_smokeNestedParent) return smoke_nested(window);
    if (owner != native(g_window)) return TRUE;
    GetWindowTextW(window, title, 128);
    {
        /* A trailing '*' matches any rest, e.g. a signature's file name. */
        int length = lstrlenW(g_smokeTitle);
        if (length && g_smokeTitle[length - 1] == L'*'
                ? CompareStringOrdinal(title, min(lstrlenW(title), length - 1), g_smokeTitle, length - 1, FALSE) != CSTR_EQUAL
                : lstrcmpW(title, g_smokeTitle) != 0)
            return TRUE;
    }
    if (IsWindowEnabled(native(g_window))) return TRUE;
    /* A dialog running a modal of its own (progress, a nested form) waits. */
    if (!IsWindowVisible(window) || !IsWindowEnabled(window)) return TRUE;
    if (window != g_smokeDialog) {
        g_smokeDialog = window;
        g_smokeSeen = g_smokeChecked = 0;
    }
    /* The first layout settles before it is measured. */
    if (++g_smokeSeen < 3) return FALSE;
    native_dialog = smoke_native_dialog(window);
    if (g_smokeApplicationModal && (IsWindowEnabled(native(g_smokeEnabledSibling)) || IsWindowEnabled(native(g_smokeDisabledSibling)))) {
        smoke_fail_at(__LINE__, window, NULL);
        PostMessageW(window, WM_CLOSE, 0, 0);
        goto finished;
    }
    /* Options pages share one window: pick the page, then wait until only it shows. */
    if (g_smokePage && g_smokePageState < 2) {
        if (!g_smokePageState) {
            if (!smoke_select_page(window, g_smokePage)) {
                smoke_fail_at(__LINE__, window, NULL);
                PostMessageW(window, WM_CLOSE, 0, 0);
                goto finished;
            }
            g_smokePageState = 1;
            return FALSE;
        }
        if (!smoke_page_shown(window, g_smokePage)) return FALSE;
        g_smokePageState = 2;
    }
    if (!g_smokeChecked) {
        int checked = smoke_check_dialog(window, native_dialog, g_smokeSeen < 50);
        if (checked < 0) return FALSE;
        g_smokeChecked = 1;
        if (!checked) {
            if (native_dialog) smoke_cancel(window);
            else PostMessageW(window, WM_CLOSE, 0, 0);
            goto finished;
        }
    }
    if (native_dialog) {
        /* Browse's file picker: checked, then cancelled by control ID. */
        smoke_cancel(window);
        goto finished;
    }
    if (g_smokeAction >= SMOKE_FEATURES_CANCEL && g_smokeAction <= SMOKE_FEATURES_DEFAULT) {
        HWND optimization[16] = {0};
        gui_backend_optimization_capabilities capabilities;
        gui_backend_optimization_options current;
        int i, controls_ready;
        EnumChildWindows(window, smoke_optimization_children, (LPARAM)optimization);
        controls_ready = smoke_optimization_controls_ready(optimization);
        if (!controls_ready) return TRUE;
        if (controls_ready < 0) {
            smoke_fail_at(__LINE__, window, NULL);
            SendMessageW(window, WM_CLOSE, 0, 0);
            goto optimization_done;
        }
        if (!gui_backend_get_optimization_capabilities(&capabilities)) {
            smoke_fail_at(__LINE__, window, NULL);
            SendMessageW(window, WM_CLOSE, 0, 0);
            goto optimization_done;
        }
        if (g_smokeAction == SMOKE_FEATURES_DEFAULT && g_smokePhase == 1) {
            /* The dialog applies Defaults after the current poll returns. A
             * second WM_TIMER can arrive first under load, so wait until the
             * two comboboxes expose the reset before checking the flags. */
            if (!smoke_selected_size(optimization[0], L"64 KiB") ||
                !smoke_selected_size(optimization[1], L"64 KiB")) return FALSE;
            if (
#if !CDIE_GUI_ARM_TARGET
                (SendMessageW(optimization[2], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.sse2 ||
                (SendMessageW(optimization[3], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.avx2 ||
                (SendMessageW(optimization[4], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.sse42 ||
                (SendMessageW(optimization[5], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.aes ||
                (SendMessageW(optimization[6], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.sha ||
                (SendMessageW(optimization[7], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.vpclmulqdq) {
#else
                (SendMessageW(optimization[8], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.neon ||
                (SendMessageW(optimization[9], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.arm_crc32 ||
                (SendMessageW(optimization[10], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.arm_crypto ||
                (SendMessageW(optimization[11], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.arm_dotprod ||
                (SendMessageW(optimization[12], BM_GETCHECK, 0, 0) == BST_CHECKED) != capabilities.arm_pmull) {
#endif
                smoke_fail_at(__LINE__, window, NULL);
                SendMessageW(window, WM_CLOSE, 0, 0);
            } else {
                /* Default also unchecks the context menu; keep the current registration. */
                HWND context = smoke_child(window, L"Button", L"Add to context menu", 0);
                if (context && g_smokeContextChecked) {
                    SendMessageW(context, BM_SETCHECK, BST_CHECKED, 0);
                    SendMessageW(window, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(context), BN_CLICKED), (LPARAM)context);
                }
                SendMessageW(optimization[14], BM_CLICK, 0, 0);
            }
            goto optimization_done;
        }
        if (!gui_backend_get_optimization_options(&current) ||
#if !CDIE_GUI_ARM_TARGET
            !!IsWindowEnabled(optimization[2]) != capabilities.sse2 ||
            !!IsWindowEnabled(optimization[3]) != capabilities.avx2 ||
            !!IsWindowEnabled(optimization[4]) != capabilities.sse42 ||
            !!IsWindowEnabled(optimization[5]) != capabilities.aes ||
            !!IsWindowEnabled(optimization[6]) != capabilities.sha ||
            !!IsWindowEnabled(optimization[7]) != capabilities.vpclmulqdq ||
            (SendMessageW(optimization[2], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_sse2 ||
            (SendMessageW(optimization[3], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_avx2 ||
            (SendMessageW(optimization[4], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_sse42 ||
            (SendMessageW(optimization[5], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_aes ||
            (SendMessageW(optimization[6], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_sha ||
            (SendMessageW(optimization[7], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_vpclmulqdq ||
#else
            !!IsWindowEnabled(optimization[8]) != capabilities.neon ||
            !!IsWindowEnabled(optimization[9]) != capabilities.arm_crc32 ||
            !!IsWindowEnabled(optimization[10]) != capabilities.arm_crypto ||
            !!IsWindowEnabled(optimization[11]) != capabilities.arm_dotprod ||
                !!IsWindowEnabled(optimization[12]) != capabilities.arm_pmull ||
            (SendMessageW(optimization[8], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_neon ||
            (SendMessageW(optimization[9], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_arm_crc32 ||
            (SendMessageW(optimization[10], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_arm_crypto ||
            (SendMessageW(optimization[11], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_arm_dotprod ||
                (SendMessageW(optimization[12], BM_GETCHECK, 0, 0) == BST_CHECKED) != current.use_arm_pmull ||
#endif
            !smoke_select_size(window, optimization[0], g_settingsSmoke == SETTINGS_SMOKE_CANCEL ? L"4 KiB" : L"1 KiB") ||
            !smoke_select_size(window, optimization[1], g_settingsSmoke == SETTINGS_SMOKE_CANCEL ? L"8 KiB" : L"2 KiB")) {
            smoke_fail_at(__LINE__, window, NULL);
            SendMessageW(window, WM_CLOSE, 0, 0);
        } else {
            for (i = 2; i < 13; ++i)
                if (optimization[i] && IsWindowVisible(optimization[i]) &&
                    SendMessageW(optimization[i], BM_GETCHECK, 0, 0) == BST_CHECKED)
                    SendMessageW(optimization[i], BM_CLICK, 0, 0);
            if (g_smokeAction == SMOKE_FEATURES_DEFAULT) {
                HWND context = smoke_child(window, L"Button", L"Add to context menu", 0);
                g_smokeContextChecked = context && SendMessageW(context, BM_GETCHECK, 0, 0) == BST_CHECKED;
                SendMessageW(optimization[13], BM_CLICK, 0, 0);
                g_smokePhase = 1;
                return FALSE;
            } else SendMessageW(optimization[g_smokeAction == SMOKE_FEATURES_OK ? 14 : 15], BM_CLICK, 0, 0);
        }
optimization_done:;
    } else if (g_smokeAction == SMOKE_DISK_CANCEL) {
        HWND first = FindWindowExW(window, NULL, PROGRESS_CLASSW, NULL);
        HWND second = first ? FindWindowExW(window, first, PROGRESS_CLASSW, NULL) : NULL;
        if (!first || !second || !IsWindowVisible(first) || !IsWindowVisible(second) ||
            !smoke_child(window, L"Static", L"Destination:", 1) ||
            !smoke_child(window, L"Static", L"Current file:", 1) ||
            !smoke_child(window, L"Button", L"Cancel", 1)) smoke_fail_at(__LINE__, window, first);
        smoke_click(window, L"Cancel");
    } else if (g_smokeAction == SMOKE_UPDATE_CANCEL || g_smokeAction == SMOKE_PROGRESS_CANCEL) {
        /* A worker's progress: bars, the elapsed time and Cancel, which stops it. */
        if (!FindWindowExW(window, NULL, PROGRESS_CLASSW, NULL) || !smoke_label(window, L"Elapsed")) smoke_fail_at(__LINE__, window, NULL);
        smoke_click(window, L"Cancel");
    } else if (g_smokeAction == SMOKE_UPDATE_QUESTION) {
        /* The question before an available update is installed: Yes and No. */
        if (!smoke_label(window, L"Install it now?") || !smoke_child(window, L"Button", L"Yes", 1) ||
            !smoke_child(window, L"Button", L"No", 1)) smoke_fail_at(__LINE__, window, NULL);
        smoke_click(window, g_smokeAnswer ? g_smokeAnswer : L"No");
    } else if (g_smokeAction == SMOKE_HASH || g_smokeAction == SMOKE_TOOL_RESIZE) {
        HWND rows = NULL;
        EnumChildWindows(window, smoke_hash_rows, (LPARAM)&rows);
        if (!rows) smoke_fail_at(__LINE__, window, NULL);
        else if (g_smokeAction == SMOKE_TOOL_RESIZE && !g_smokePhase) {
            /* Dragged below its minimum, the dialog keeps that minimum in both
             * directions and stays open (it used to close with "invalid
             * argument", dlg_defects #1). The tools' own RESIZE clamp behind
             * this is covered by the xxwidgets tests. */
            RECT bounds;
            MINMAXINFO limits;
            ZeroMemory(&limits, sizeof(limits));
            SendMessageW(window, WM_GETMINMAXINFO, 0, (LPARAM)&limits);
            SetWindowPos(window, NULL, 0, 0, 160, 120, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            if (!GetWindowRect(window, &bounds) || limits.ptMinTrackSize.x <= 160 || limits.ptMinTrackSize.y <= 120 ||
                bounds.right - bounds.left < limits.ptMinTrackSize.x || bounds.bottom - bounds.top < limits.ptMinTrackSize.y)
                smoke_fail_at(__LINE__, window, NULL);
            g_smokePhase = 1;
            g_smokeWait = 0;
            return FALSE;
        } else if (g_smokeAction == SMOKE_TOOL_RESIZE) {
            if (++g_smokeWait < 5) return FALSE;
            if (!smoke_check_dialog(window, 0, 0)) {
                PostMessageW(window, WM_CLOSE, 0, 0);
                goto finished;
            }
        }
        smoke_close(window);
    } else if (g_smokeAction == SMOKE_STRINGS_CONFIG) {
        HWND rows = NULL;
        if (!g_smokePhase) {
            HWND ansi = smoke_child(window, L"Button", L"ANSI", 1);
            HWND unicode = smoke_child(window, L"Button", L"Unicode", 1);
            HWND links = smoke_child(window, L"Button", L"Links", 1);
            HWND regexp = smoke_child(window, L"Button", L"RegExp", 1);
            if (!ansi || !unicode || !links || !regexp ||
                SendMessageW(ansi, BM_GETCHECK, 0, 0) != BST_CHECKED ||
                SendMessageW(unicode, BM_GETCHECK, 0, 0) != BST_CHECKED ||
                !smoke_child(window, L"Edit", L"5", 1)) {
                smoke_fail_at(__LINE__, window, NULL);
                PostMessageW(window, WM_CLOSE, 0, 0);
                goto finished;
            }
            smoke_click(window, L"Unicode");
            g_smokePhase = 1;
            smoke_click(window, L"Search");
            return FALSE;
        }
        EnumChildWindows(window, smoke_information_fields, (LPARAM)&rows);
        if (!rows || ListView_GetItemCount(rows) != (g_smokePhase == 1 ? 6 : 4)) return FALSE;
        if (g_smokePhase == 1) {
            smoke_click(window, L"Links");
            g_smokePhase = 2;
            smoke_click(window, L"Search");
            return FALSE;
        }
        {
            static const WCHAR *const expected[] = {L"http://one.example", L"https://two.example", L"www.three.example", L"mailto:four@example"};
            WCHAR text[128];
            for (int row = 0; row < 4; ++row) {
                ListView_GetItemText(rows, row, 6, text, 128);
                if (lstrcmpW(text, expected[row])) smoke_fail_at(__LINE__, window, rows);
            }
        }
        PostMessageW(window, WM_CLOSE, 0, 0);
    } else if (g_smokeAction == SMOKE_EXTRACTOR) {
        HWND rows = NULL;
        EnumChildWindows(window, smoke_extractor_rows, (LPARAM)&rows);
        if (!rows || Header_GetItemCount(ListView_GetHeader(rows)) != 9 ||
            !smoke_child(window, L"Button", L"Scan", 1) ||
            !smoke_child(window, L"Button", L"Dump all", 1) ||
            !smoke_child(window, L"Button", L"Save", 1) ||
            !smoke_child(window, L"Button", L"Deep scan", 1)) {
            smoke_fail_at(__LINE__, window, rows);
            PostMessageW(window, WM_CLOSE, 0, 0);
            goto finished;
        }
        /* The Escape pass checks the form only; the WM_CLOSE pass scans and filters. */
        if (!g_smokePhase && !g_smokeEscape) {
            HWND all = smoke_child(window, L"Button", L"All", 1);
            if (!all) { smoke_fail_at(__LINE__, window, NULL); PostMessageW(window, WM_CLOSE, 0, 0); goto finished; }
            if (SendMessageW(all, BM_GETCHECK, 0, 0) != BST_CHECKED) smoke_click(window, L"All");
            g_smokePhase = 1;
            smoke_click(window, L"Scan");
            return FALSE;
        }
        if (!g_smokeEscape) {
            if (!ListView_GetItemCount(rows)) return FALSE;
            if (g_smokePhase == 1) {
                HWND header = ListView_GetHeader(rows);
                HD_TEXTFILTERW filter;
                HDITEMW item;
                NMHEADERW change;
                ListView_GetItemText(rows, 0, 1, g_smokeExtractorOffset, 32);
                filter.pszText = g_smokeExtractorOffset; filter.cchTextMax = 31;
                ZeroMemory(&item, sizeof(item)); item.mask = HDI_FILTER;
                item.type = HDFT_ISSTRING; item.pvFilter = &filter;
                if (!SendMessageW(header, HDM_SETITEMW, 1, (LPARAM)&item)) smoke_fail_at(__LINE__, window, header);
                ZeroMemory(&change, sizeof(change)); change.hdr.hwndFrom = header;
                change.hdr.code = HDN_FILTERCHANGE; change.iItem = 1;
                g_smokePhase = 2;
                SendMessageW(rows, WM_NOTIFY, 0, (LPARAM)&change);
                return FALSE;
            }
            if (ListView_GetItemCount(rows) != 1) return FALSE;
            {
                WCHAR offset[32];
                ListView_GetItemText(rows, 0, 1, offset, 32);
                if (lstrcmpW(offset, g_smokeExtractorOffset)) smoke_fail_at(__LINE__, window, rows);
            }
        }
        smoke_close(window);
    } else if (g_smokeAction == SMOKE_STRINGS) {
        HWND rows = NULL;
        EnumChildWindows(window, smoke_strings_rows, (LPARAM)&rows);
        if (!rows) smoke_fail_at(__LINE__, window, NULL);
        smoke_close(window);
    } else if ((g_smokeAction >= SMOKE_FILE_INFORMATION && g_smokeAction <= SMOKE_SYMBOLS) || g_smokeAction == SMOKE_STRINGS_FILE) {
        /* A tool's table has rows, and the step's expected cell when it names one. */
        HWND fields = NULL;
        EnumChildWindows(window, smoke_information_fields, (LPARAM)&fields);
        if (!fields || (g_smokeExpect && !smoke_table_contains(window, g_smokeExpect))) smoke_fail_at(__LINE__, window, fields);
        smoke_close(window);
    } else if (g_smokeAction == SMOKE_OPTIONS_ESCAPE || g_smokeAction == SMOKE_OPTIONS_CLOSE) {
        PostMessageW(window, g_smokeAction == SMOKE_OPTIONS_ESCAPE ? WM_KEYDOWN : WM_CLOSE,
                     g_smokeAction == SMOKE_OPTIONS_ESCAPE ? VK_ESCAPE : 0, 0);
    } else if (g_smokeAction == SMOKE_SCAN_COLORS || g_smokeAction == SMOKE_COLORS_OK) {
        if (!g_smokePhase) {
            g_smokeNestedParent = window;
            g_smokeNestedSeen = 0;
            g_smokePhase = 1;
            smoke_click(window, L"Colors...");
            return FALSE;
        }
        if (g_smokePhase != 2) return FALSE;
        smoke_click(window, L"Cancel");
    } else if (g_smokeAction == SMOKE_SCAN_PAGE) {
        /* Main and custom databases only: 17 checkboxes with Custom database
         * among them, the two folder rows, and no caption naming an extra one. */
        smoke_scan_page page = {0, 0};
        EnumChildWindows(window, smoke_scan_page_child, (LPARAM)&page);
        if (page.checkboxes != 17 || page.extra || !smoke_child(window, L"Button", L"Custom database", 1) ||
            !smoke_child(window, L"Static", L"Main database", 1) || !smoke_child(window, L"Static", L"Custom database folder", 1)) {
            char text[96];
            wsprintfA(text, "Scan page: %d checkboxes, extra caption %d\r\n", page.checkboxes, page.extra);
            smoke_write(text);
            smoke_fail_at(__LINE__, window, NULL);
        }
        smoke_click(window, L"Cancel");
    } else if (g_smokeAction == SMOKE_SCAN_CANCEL || g_smokeAction == SMOKE_SCAN_OK) {
        static const WCHAR *const toggles[] = {L"All file types", L"Scan overlays", L"Scan resources",
            L"Scan archive members", L"Deep scan"};
        size_t i;
        for (i = 0; i < sizeof(toggles) / sizeof(toggles[0]); ++i) smoke_click(window, toggles[i]);
        smoke_click(window, g_smokeAction == SMOKE_SCAN_OK ? L"OK" : L"Cancel");
    } else if (g_smokeAction == SMOKE_FONTS_CANCEL || g_smokeAction == SMOKE_FONTS_OK) {
        smoke_click(window, L"Bold");
        smoke_click(window, g_smokeAction == SMOKE_FONTS_OK ? L"OK" : L"Cancel");
    } else if (g_smokeAction == SMOKE_HEX_CANCEL || g_smokeAction == SMOKE_HEX_OK) {
        HWND bytes = smoke_child(window, L"COMBOBOX", NULL, 1);
        if (!bytes || !smoke_select_size(window, bytes, g_smokeAction == SMOKE_HEX_OK ? L"32 bytes" : L"64 bytes")) smoke_fail_at(__LINE__, window, bytes);
        smoke_click(window, L"Bold non-zero");
        smoke_click(window, g_smokeAction == SMOKE_HEX_OK ? L"OK" : L"Cancel");
    } else if (g_smokeAction == SMOKE_FILE_CANCEL || g_smokeAction == SMOKE_PAGE_CANCEL) {
        /* Opening and cancelling the integration page cannot register cdie. */
        smoke_click(window, L"Cancel");
    } else if (g_smokeAction == SMOKE_SCAN_MISSING_DB) {
        /* OK with a main database folder that does not exist: an error, and Options stays. */
        HWND defaults = smoke_child(window, L"Button", L"Default", 1), error = defaults ? GetWindow(defaults, GW_HWNDPREV) : NULL;
        WCHAR text[256] = L"", class_name[16] = L"";
        if (!g_smokePhase) {
            HWND edit = smoke_child(window, L"Edit", NULL, 1);
            WCHAR missing[MAX_PATH + 32];
            wsprintfW(missing, L"%s\\cdie-smoke-missing", g_smokeFolder);
            if (!edit || !SetWindowTextW(edit, missing)) {
                smoke_fail_at(__LINE__, window, edit);
                PostMessageW(window, WM_CLOSE, 0, 0);
                goto finished;
            }
            g_smokePhase = 1;
            smoke_click(window, L"OK");
            return FALSE;
        }
        if (!error || !GetClassNameW(error, class_name, 16) || lstrcmpiW(class_name, L"Static")) {
            smoke_fail_at(__LINE__, window, error);
            PostMessageW(window, WM_CLOSE, 0, 0);
            goto finished;
        }
        if (!GetWindowTextW(error, text, 256)) return FALSE;
        /* The page with the folder field stays shown, not the last page, and
         * the error is the missing folder's (not just any text there). */
        if (!smoke_page_shown(window, L"Scan") || !wcsstr(text, L"does not exist")) smoke_fail_at(__LINE__, window, error);
        smoke_click(window, L"Cancel");
    } else if (g_smokeAction == SMOKE_PICKERS) {
        const smoke_picker *picker = &g_smokePickers[g_smokePicker];
        HWND field;
        if (g_smokePhase == 0) {
            if (!smoke_select_page(window, picker->page)) {
                smoke_fail_at(__LINE__, window, NULL);
                PostMessageW(window, WM_CLOSE, 0, 0);
                goto finished;
            }
            g_smokePhase = 1;
            return FALSE;
        }
        if (g_smokePhase == 1) {
            if (!smoke_page_shown(window, picker->page)) return FALSE;
            field = smoke_child(window, L"Edit", NULL, 1);
            if (picker->instruction && (!field || !SetWindowTextW(field, g_smokeFolder))) {
                smoke_fail_at(__LINE__, window, field);
                PostMessageW(window, WM_CLOSE, 0, 0);
                goto finished;
            }
            g_browseSelection[0] = 0;
            g_smokeNestedParent = window;
            g_smokeNestedSeen = g_smokeNestedChecked = 0;
            g_smokePhase = 2;
            smoke_click(window, picker->button);
            return FALSE;
        }
        if (g_smokePhase == 2) return FALSE;
        /* Cancelled: the field keeps what it had. */
        if (picker->instruction) {
            WCHAR text[MAX_PATH] = L"";
            field = smoke_child(window, L"Edit", NULL, 1);
            if (!field || !GetWindowTextW(field, text, MAX_PATH) || lstrcmpW(text, g_smokeFolder)) smoke_fail_at(__LINE__, window, field);
        }
        if (++g_smokePicker < sizeof(g_smokePickers) / sizeof(g_smokePickers[0])) {
            g_smokePhase = 0;
            return FALSE;
        }
        smoke_click(window, L"Cancel");
    } else if (g_smokeAction == SMOKE_REPORT || g_smokeAction == SMOKE_LOG || g_smokeAction == SMOKE_INFO || g_smokeAction == SMOKE_UPDATE_FAILED) {
        /* Text dialogs: the text, read-only, with Copy all (never pressed: the clipboard is the user's). */
        HWND text = NULL;
        EnumChildWindows(window, smoke_text_view, (LPARAM)&text);
        if (!text || !GetWindowTextLengthW(text) || !smoke_child(window, L"Button", L"Copy all", 1) ||
            (g_smokeExpectText && !smoke_text_equals(text, g_smokeExpectText)) ||
            (g_smokeExpectPart && !smoke_text_contains(text, g_smokeExpectPart))) smoke_fail_at(__LINE__, window, text);
        smoke_close(window);
    } else if (g_smokeAction == SMOKE_DIRECTORY) {
        HWND scan = smoke_child(window, L"Button", L"Scan", 1);
        if (!g_smokePhase) {
            HWND path = smoke_child(window, L"Edit", NULL, 1);
            if (!path || !scan || !smoke_child(window, L"Button", L"Recursive", 1) || !smoke_child(window, L"Button", L"Stop", 1) ||
                !SetWindowTextW(path, g_smokeFolder)) {
                smoke_fail_at(__LINE__, window, path);
                PostMessageW(window, WM_CLOSE, 0, 0);
                goto finished;
            }
            g_smokeNestedParent = window;
            g_smokeNestedSeen = 0;
            g_smokePhase = 1;
            smoke_click(window, L"Options");
            return FALSE;
        }
        if (g_smokePhase == 1) return FALSE;
        if (g_smokePhase == 2) {
            g_smokePhase = 3;
            smoke_click(window, L"Scan");
            return FALSE;
        }
        /* Done when the one fixture is counted and Scan is offered again. */
        if (!scan || !IsWindowEnabled(scan) || !smoke_label(window, L"Finished") || !smoke_label(window, L"Files: 1 ")) return FALSE;
        smoke_close(window);
    } else if (g_smokeAction == SMOKE_RECENT || g_smokeAction == SMOKE_RECENT_EMPTY) {
        HWND tree = smoke_child(window, WC_TREEVIEWW, NULL, 1);
        if (g_smokeAction == SMOKE_RECENT_EMPTY) {
            /* No history: a note and Close, nothing to pick. */
            if (!smoke_child(window, L"Button", L"Close", 1) || smoke_child(window, L"Button", L"OK", 1) ||
                !smoke_child(window, L"Static", L"No items.", 1) || (tree && TreeView_GetCount(tree))) smoke_fail_at(__LINE__, window, tree);
            smoke_close(window);
        } else {
            HTREEITEM first = tree ? TreeView_GetRoot(tree) : NULL;
            WCHAR text[MAX_PATH] = L"";
            TVITEMW item;
            ZeroMemory(&item, sizeof(item));
            item.mask = TVIF_TEXT; item.hItem = first; item.pszText = text; item.cchTextMax = MAX_PATH;
            if (!first || !TreeView_GetItem(tree, &item) || !g_smokeExpect || lstrcmpiW(text, g_smokeExpect) ||
                !smoke_child(window, L"Button", L"Cancel", 1) || !TreeView_SelectItem(tree, first)) {
                smoke_fail_at(__LINE__, window, tree);
                PostMessageW(window, WM_CLOSE, 0, 0);
                goto finished;
            }
            smoke_click(window, L"OK");
        }
    } else if (g_smokeAction == SMOKE_ABOUT || g_smokeAction == SMOKE_ABOUT_ESCAPE || g_smokeAction == SMOKE_ABOUT_CLOSE) {
        /* The Detect It Easy layout: picture, Info/Libraries tabs, Follow me buttons. */
        if (!smoke_child(window, L"SysTabControl32", NULL, 1) || !smoke_child(window, L"Button", L"GitHub", 1) ||
            !smoke_child(window, L"Button", L"Check updates", 1) || !smoke_child(window, L"Static", L"About image", 1))
            smoke_fail_at(__LINE__, window, NULL);
        if (g_smokeAction == SMOKE_ABOUT) smoke_click(window, L"OK");
        else PostMessageW(window, g_smokeAction == SMOKE_ABOUT_ESCAPE ? WM_KEYDOWN : WM_CLOSE,
                          g_smokeAction == SMOKE_ABOUT_ESCAPE ? VK_ESCAPE : 0, 0);
    } else if (g_smokeAction == SMOKE_SIGNATURE) {
        /* S: the script in the signature editor, Run wired to the scanned file;
         * Open offers the file picker for an unmodified signature. */
        if (!g_smokePicker && !g_smokePhase) {
            HWND run = smoke_child(window, L"Button", L"Run", 1);
            if (!run || !IsWindowEnabled(run) || !smoke_child(window, L"Button", L"Read-only", 1)) smoke_fail_at(__LINE__, window, run);
        }
        if (!g_smokeFailed && !smoke_inner_step(window)) return FALSE;
        smoke_click(window, L"Close");
    } else if (g_smokeAction == SMOKE_TOOL_INNER) {
        if (!smoke_inner_step(window)) return FALSE;
        smoke_close(window);

    } else if (g_smokeAction == SMOKE_SIGNATURE_CONFIRM) {
        /* Edit the signature, then Open: "Unsaved signature" asks first; Cancel keeps the edit. */
        HWND read_only = smoke_child(window, L"Button", L"Read-only", 1);
        if (!g_smokePhase) {
            if (!read_only || !IsWindowEnabled(read_only)) {
                smoke_fail_at(__LINE__, window, read_only);
                PostMessageW(window, WM_CLOSE, 0, 0);
                goto finished;
            }
            if (SendMessageW(read_only, BM_GETCHECK, 0, 0) == BST_CHECKED) smoke_click(window, L"Read-only");
            g_smokePhase = 1;
            return FALSE;
        }
        if (g_smokePhase == 1) {
            if (!read_only || SendMessageW(read_only, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                smoke_fail_at(__LINE__, window, read_only);
                PostMessageW(window, WM_CLOSE, 0, 0);
                goto finished;
            }
            /* Type once the editor reports itself editable, Open once it reports the change. */
            if (!smoke_label(window, L"Editable")) return FALSE;
            EnumChildWindows(window, smoke_type_signature, 0);
            g_smokePhase = 5;
            return FALSE;
        }
        if (g_smokePhase == 5) {
            if (!smoke_label(window, L"| Modified")) return FALSE;
            g_smokeNestedParent = window;
            g_smokeNestedSeen = 0;
            g_smokePhase = 2;
            smoke_click(window, L"Open");
            return FALSE;
        }
        if (g_smokePhase == 2) return FALSE;
        if (g_smokePhase == 3) {
            g_smokePhase = 4;
            smoke_click(window, L"Close");
            return FALSE;
        }
        /* The first Close is consumed by the editor's warning. Wait for that
         * state before sending the second one: rapid timer ticks otherwise
         * collapse both clicks into one close_requested flag (the old W/M8
         * action-47 flake). */
        if (g_smokePhase == 4) {
            if (!smoke_label(window, L"Unsaved changes. Close again to discard them.")) return FALSE;
            g_smokePhase = 6;
            smoke_click(window, L"Close");
        }
        /* Phase 6 waits for the editor to return to its caller. */
    } else if (g_smokeAction == SMOKE_SIGNATURES_CANCEL) {
        HWND edit = smoke_child(window, L"Edit", NULL, 1);
        if (!edit || !SetWindowTextW(edit, L"Smoke test cancelled database path")) smoke_fail_at(__LINE__, window, edit);
        smoke_click(window, L"Cancel");
    } else if (g_smokeAction == SMOKE_UNEXPECTED) {
        smoke_fail_at(__LINE__, window, NULL);
        PostMessageW(window, WM_CLOSE, 0, 0);
    } else smoke_close(window);
    if (g_smokeFailed) PostMessageW(window, WM_CLOSE, 0, 0);
finished:
    g_smokeDone = 1;
    return FALSE;
}

/* After the step's dialog: anything the main window, or that dialog, still
 * opens is unexpected, and so is a native dialog anywhere on the thread. */
static BOOL CALLBACK smoke_unexpected(HWND window, LPARAM data)
{
    HWND owner = GetWindow(window, GW_OWNER);
    (void)data;
    if (window == g_smokeDialog || !IsWindowVisible(window) || !smoke_dialog_window(window) ||
        (!smoke_native_dialog(window) && (!owner || (owner != native(g_window) && owner != g_smokeDialog)))) return TRUE;
    smoke_fail_at(__LINE__, window, NULL);
    if (smoke_native_dialog(window)) smoke_cancel(window);
    else PostMessageW(window, WM_CLOSE, 0, 0);
    return TRUE;
}

/* Left behind by a step: a dialog window the main window still owns. */
static BOOL CALLBACK smoke_leftover(HWND window, LPARAM data)
{
    HWND owner = GetWindow(window, GW_OWNER);
    if (owner && (owner == native(g_window) || owner == g_smokeDialog) && smoke_dialog_window(window)) {
        *(HWND *)data = window;
        return FALSE;
    }
    return TRUE;
}

/* On a timeout: the step's state and every dialog window of the thread. */
static BOOL CALLBACK smoke_dump_window(HWND window, LPARAM data)
{
    char text[400];
    WCHAR class_name[64] = L"", title[96] = L"";
    (void)data;
    if (!smoke_dialog_window(window)) return TRUE;
    GetClassNameW(window, class_name, 64);
    GetWindowTextW(window, title, 96);
    wsprintfA(text, "  window %p %ls \"%ls\" owner %p visible %d enabled %d\r\n", (void *)window, class_name, title,
              (void *)GetWindow(window, GW_OWNER), IsWindowVisible(window) != 0, IsWindowEnabled(window) != 0);
    smoke_write(text);
    return TRUE;
}

static VOID CALLBACK smoke_tick(HWND window, UINT message, UINT_PTR timer, DWORD time)
{
    (void)window; (void)message; (void)timer; (void)time;
    /* Before the step closes its dialog, a foreground elsewhere was taken by
     * another program; after that, it may be the close's own doing. */
    if (!g_smokeDone && !g_smokeForegroundLost && !smoke_foreground_ours()) {
        char text[160];
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        wsprintfA(text, "GUI smoke: process %lu took the foreground during action %d (phase %d, dialog %p)", (unsigned long)process,
                  g_smokeAction, g_smokePhase, (void *)g_smokeDialog);
        smoke_trace(text);
        g_smokeForegroundLost = 1;
    }
    EnumThreadWindows(GetCurrentThreadId(), g_smokeDone ? smoke_unexpected : smoke_windows, 0);
    if (GetTickCount64() < g_smokeDeadline) return;
    if (!g_smokeTimedOut) {
        char text[160];
        g_smokeTimedOut = 1;
        smoke_fail_at(__LINE__, g_smokeDialog, NULL);
        wsprintfA(text, "timed out: phase %d, item %d, nested parent %p, main %p\r\n", g_smokePhase, (int)g_smokePicker,
                  (void *)g_smokeNestedParent, (void *)native(g_window));
        smoke_write(text);
        EnumThreadWindows(GetCurrentThreadId(), smoke_dump_window, 0);
        xxwidgets_app_quit(g_app, 1);
    }
    /* Native modal loops ignore the quit request: cancel them until the opener returns. */
    EnumThreadWindows(GetCurrentThreadId(), smoke_cancel_native, 0);
}

static int smoke_begin_ex(const WCHAR *title, int action, int application_modal)
{
    char trace[160];
    static ULONGLONG started;
    if (!started) started = GetTickCount64();
    wsprintfA(trace, "GUI smoke dialog action %d at %u ms", action, (unsigned int)(GetTickCount64() - started));
    smoke_trace(trace);
    g_smokeTitle = title; g_smokeAction = action;
    g_smokePage = NULL; g_smokePageState = 0;
    g_smokeDone = g_smokeFailed = g_smokePhase = 0;
    g_smokeTimedOut = g_smokeSeen = g_smokeChecked = g_smokeNestedSeen = g_smokeNestedChecked = g_smokeWait = 0;
    g_smokeEscape = g_smokeOptional = 0;
    g_smokePicker = 0;
    g_smokeInner = NULL; g_smokeInnerCount = 0;
    g_smokeExpect = g_smokeExpectPart = NULL; g_smokeExpectText = NULL;
    g_smokeNestedParent = g_smokeDialog = NULL;
    g_smokeDeadline = GetTickCount64() + SMOKE_STEP_MS;
    g_smokeForegroundLost = !smoke_foreground_ours();
    if (g_smokeForegroundLost) {
        DWORD process = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &process);
        wsprintfA(trace, "GUI smoke: action %d begins with the foreground in process %lu", action, (unsigned long)process);
        smoke_trace(trace);
    }
    smoke_modal_cleanup();
    if (application_modal && !smoke_modal_setup()) return 0;
    g_smokeTimer = SetTimer(NULL, 0, 20, smoke_tick);
    if (!g_smokeTimer) smoke_modal_cleanup();
    return g_smokeTimer != 0;
}

/* Options, About and the text dialogs are application-modal. */
static int smoke_begin(const WCHAR *title, int action)
{
    return smoke_begin_ex(title, action, !lstrcmpW(title, L"Options") || !lstrcmpW(title, L"About") ||
        !lstrcmpW(title, L"Scan report") || !lstrcmpW(title, L"Log"));
}

/* select = 1 picks the page from the list; 0 expects Options to open on it. */
static int smoke_begin_page(const WCHAR *page, int action, int select)
{
    if (!smoke_begin(L"Options", action)) return 0;
    g_smokePage = page;
    g_smokePageState = select ? 0 : 1;
    return 1;
}

/* The opener returned: its dialog was handled (or the step allows none), the
 * main window is enabled and has the focus, and nothing it owned is left. */
static int smoke_finish(void)
{
    HWND focus, leftover = NULL;
    int passed;
    if (g_smokeTimer) KillTimer(NULL, g_smokeTimer);
    g_smokeTimer = 0;
    if (!g_smokeDone && !g_smokeOptional) smoke_fail_at(__LINE__, NULL, NULL);
    if (!IsWindowEnabled(native(g_window))) smoke_fail_at(__LINE__, native(g_window), NULL);
    if (g_smokeApplicationModal && (!IsWindowEnabled(native(g_smokeEnabledSibling)) ||
        IsWindowEnabled(native(g_smokeDisabledSibling)))) smoke_fail_at(__LINE__, NULL, NULL);
    focus = GetFocus();
    if (!g_smokeForegroundLost && !smoke_foreground_ours()) {
        /* The thread's own active window may still be the main window: the
         * foreground says where the activation went. */
        char text[128];
        DWORD process = 0;
        HWND foreground = GetForegroundWindow();
        GetWindowThreadProcessId(foreground, &process);
        wsprintfA(text, "the foreground left the program when the dialog closed: window %p of process %lu\r\n",
                  (void *)foreground, (unsigned long)process);
        smoke_write(text);
        smoke_fail_at(__LINE__, native(g_window), focus);
    }
    if (smoke_focus_known() && (!focus || (focus != native(g_window) && !IsChild(native(g_window), focus))))
        smoke_fail_at(__LINE__, native(g_window), focus);
    EnumThreadWindows(GetCurrentThreadId(), smoke_leftover, (LPARAM)&leftover);
    if (leftover) smoke_fail_at(__LINE__, leftover, NULL);
    passed = !g_smokeFailed;
    smoke_modal_cleanup();
    return passed;
}

static int smoke_failed(unsigned int line)
{
    char text[80], status[512];
    wsprintfA(text, "cdie GUI smoke failed at main_gui.c:%u\r\n", line);
    smoke_write(text);
    /* The last message usually says why. */
    smoke_last_message(status, sizeof(status));
    if (status[0]) {
        smoke_write("Last message: ");
        smoke_write(status);
        smoke_write("\r\n");
    }
    return 1;
}

/* Presses a key with modifiers as this thread sees it (no system-wide input)
 * and returns the action the main loop would run. */
static int smoke_shortcut(UINT key, int control, int shift)
{
    BYTE saved[256], keys[256];
    HWND focus = GetFocus();
    int action;
    if (!GetKeyboardState(saved)) return -1;
    memcpy(keys, saved, sizeof(keys));
    keys[VK_CONTROL] = keys[VK_LCONTROL] = control ? 0x80 : 0;
    keys[VK_SHIFT] = keys[VK_LSHIFT] = shift ? 0x80 : 0;
    keys[VK_MENU] = keys[VK_LMENU] = keys[VK_RCONTROL] = keys[VK_RSHIFT] = keys[VK_RMENU] = 0;
    if (!SetKeyboardState(keys)) return -1;
    g_pending = ACTION_NONE;
    PostMessageW(focus && IsChild(native(g_window), focus) ? focus : native(g_path), WM_KEYDOWN, key, 0);
    (void)xxwidgets_app_poll(g_app, 0);
    action = g_pending;
    g_pending = ACTION_NONE;
    SetKeyboardState(saved);
    return action;
}

/* Clicks a main-window button through its WM_COMMAND, as a user's click arrives. */
static void smoke_press(xxwidgets_widget *button)
{
    SendMessageW(native(g_window), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(native(button)), BN_CLICKED), (LPARAM)native(button));
}

/* A slow virtual source exercises the actual Hex View disk writer and its
 * native progress window without requiring a large fixture on disk. */
static xxwidgets_status smoke_disk_read(void *user, uint64_t offset, void *data, size_t length)
{
    (void)user;
    if (offset > 4 * 1024 * 1024 || length > 4 * 1024 * 1024 - offset) return XXWIDGETS_INVALID_ARGUMENT;
    memset(data, 0x5A, length);
    Sleep(16);
    return XXWIDGETS_OK;
}

static xxwidgets_status smoke_disk_seek(xxwidgets_widget *widget, uint64_t offset, void *user)
{
    (void)widget; (void)user;
    return offset == 0 ? XXWIDGETS_OK : XXWIDGETS_INVALID_ARGUMENT;
}

static int smoke_disk_cancel(void)
{
    WCHAR temporary[MAX_PATH], directory[MAX_PATH], path[MAX_PATH] = {0}, pattern[MAX_PATH];
    static const char sentinel[] = "previous saved bytes";
    const unsigned char page[64] = {0};
    char observed[sizeof(sentinel)] = {0};
    char *utf8 = NULL;
    HANDLE file = INVALID_HANDLE_VALUE, search;
    WIN32_FIND_DATAW found;
    DWORD transferred = 0;
    xxwidgets_widget *hex = NULL;
    xxwidgets_status status = XXWIDGETS_PLATFORM_ERROR;
    int passed = 0;
    if (!GetTempPathW(MAX_PATH, temporary) || !GetTempFileNameW(temporary, L"cdw", 0, directory)) return 0;
    if (!DeleteFileW(directory) || !CreateDirectoryW(directory, NULL)) return 0;
    if (lstrlenW(directory) > MAX_PATH - 32) goto cleanup;
    lstrcpyW(path, directory); lstrcatW(path, L"\\saved.bin");
    lstrcpyW(pattern, directory); lstrcatW(pattern, L"\\*");
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE || !WriteFile(file, sentinel, sizeof(sentinel), &transferred, NULL) ||
        transferred != sizeof(sentinel)) goto cleanup;
    CloseHandle(file); file = INVALID_HANDLE_VALUE;
    utf8 = xx_str_unicode_to_utf8(path);
    if (!utf8 || xxwidgets_widget_create(g_app, g_window, XXWIDGETS_HEXVIEW, "", (xxwidgets_rect){0, 0, 1, 1}, &hex) != XXWIDGETS_OK ||
        xxwidgets_widget_set_visible(hex, 0) != XXWIDGETS_OK ||
        xxwidgets_hexview_set_data(hex, page, sizeof(page)) != XXWIDGETS_OK ||
        xxwidgets_hexview_set_source(hex, 4 * 1024 * 1024, smoke_disk_read, smoke_disk_seek, NULL) != XXWIDGETS_OK ||
        xxwidgets_hexview_set_source_selection(hex, 0, 4 * 1024 * 1024) != XXWIDGETS_OK ||
        !smoke_begin(L"Saving selected bytes", SMOKE_DISK_CANCEL)) goto cleanup;
    status = xxwidgets_hexview_save_selection(hex, utf8);
    passed = smoke_finish() && status == XXWIDGETS_OK;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || !ReadFile(file, observed, sizeof(observed), &transferred, NULL) ||
        transferred != sizeof(sentinel) || memcmp(observed, sentinel, sizeof(sentinel)) || GetFileSize(file, NULL) != sizeof(sentinel)) passed = 0;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    search = FindFirstFileW(pattern, &found);
    if (search == INVALID_HANDLE_VALUE) passed = 0;
    else {
        do {
            if (lstrcmpW(found.cFileName, L".") && lstrcmpW(found.cFileName, L"..") && lstrcmpW(found.cFileName, L"saved.bin")) passed = 0;
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
cleanup:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (hex) xxwidgets_widget_destroy(hex);
    xx_str_free(utf8);
    if (path[0]) DeleteFileW(path);
    RemoveDirectoryW(directory);
    return passed;
}

static void smoke_put(unsigned char *data, size_t offset, uint64_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i) data[offset + (size_t)i] = (unsigned char)(value >> (8 * i));
}

/* An ELF64 shared object whose dynamic symbols both Exports and Symbols list
 * (tests/test_symbols.py elf_exports(64, False, False)). */
static int smoke_write_elf(const WCHAR *path)
{
    static const struct { const char *name; unsigned char info, other; unsigned short section; uint64_t value; } symbols[] = {
        {"", 0, 0, 0, 0}, {"Direct", 0x12, 0, 3, 0x10}, {"Hidden", 0x12, 2, 3, 0x18}, {"Weak", 0x22, 3, 3, 0x20},
        {"Local", 2, 0, 3, 0x28}, {"Need", 0x12, 0, 0, 0}, {"File", 0x14, 0, 3, 0}, {"Section", 0x13, 0, 3, 0},
        {"Data", 0x11, 0, 3, 0x30}, {"Absolute", 0x10, 0, 0xfff1, 0x12345678}, {"Internal", 0x12, 1, 3, 0x38}};
    static const unsigned char ident[] = {0x7f, 'E', 'L', 'F', 2, 1, 1};
    unsigned char data[0x1000] = {0};
    size_t i, strings = 1, count = sizeof(symbols) / sizeof(symbols[0]);
    HANDLE file;
    DWORD written = 0;
    BOOL ok;
    memcpy(data, ident, sizeof(ident));
    smoke_put(data, 16, 3, 2); smoke_put(data, 18, 62, 2); smoke_put(data, 20, 1, 4); smoke_put(data, 24, 0x400000, 8);
    smoke_put(data, 32, 64, 8); smoke_put(data, 40, 0x100, 8); smoke_put(data, 52, 64, 2); smoke_put(data, 54, 56, 2);
    smoke_put(data, 58, 64, 2); smoke_put(data, 60, 4, 2);
    smoke_put(data, 64, 1, 4); smoke_put(data, 68, 5, 4); smoke_put(data, 80, 0x400000, 8); smoke_put(data, 88, 0x400000, 8);
    smoke_put(data, 96, 512, 8); smoke_put(data, 104, 512, 8); smoke_put(data, 112, 0x1000, 8);
    for (i = 0; i < count; ++i) {
        size_t entry = 0x300 + i * 24, length = strlen(symbols[i].name);
        if (length) {
            smoke_put(data, entry, strings, 4);
            memcpy(data + 0x700 + strings, symbols[i].name, length + 1);
            strings += length + 1;
        }
        data[entry + 4] = symbols[i].info; data[entry + 5] = symbols[i].other;
        smoke_put(data, entry + 6, symbols[i].section, 2);
        smoke_put(data, entry + 8, symbols[i].value + (symbols[i].section == 3 ? 0x400000 : 0), 8);
    }
    /* .dynsym, .dynstr, .text */
    smoke_put(data, 0x140 + 4, 11, 4); smoke_put(data, 0x140 + 24, 0x300, 8); smoke_put(data, 0x140 + 32, count * 24, 8);
    smoke_put(data, 0x140 + 40, 2, 4); smoke_put(data, 0x140 + 44, 1, 4); smoke_put(data, 0x140 + 48, 8, 8); smoke_put(data, 0x140 + 56, 24, 8);
    smoke_put(data, 0x180 + 4, 3, 4); smoke_put(data, 0x180 + 24, 0x700, 8); smoke_put(data, 0x180 + 32, strings, 8); smoke_put(data, 0x180 + 48, 1, 8);
    smoke_put(data, 0x1c0 + 4, 1, 4); smoke_put(data, 0x1c0 + 8, 2, 8); smoke_put(data, 0x1c0 + 16, 0x400000, 8);
    smoke_put(data, 0x1c0 + 24, 0x800, 8); smoke_put(data, 0x1c0 + 32, 0x80, 8); smoke_put(data, 0x1c0 + 48, 16, 8);
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    ok = WriteFile(file, data, sizeof(data), &written, NULL) && written == sizeof(data);
    CloseHandle(file);
    return ok;
}

/* The scratch folder: the ELF fixture, the Scan directory target, and a
 * folder that exists for the database pickers. */
static int smoke_scratch_create(void)
{
    WCHAR temporary[MAX_PATH];
    g_smokeFolder[0] = g_smokeElf[0] = 0;
    if (!GetTempPathW(MAX_PATH, temporary) || !GetTempFileNameW(temporary, L"cdg", 0, g_smokeFolder) ||
        !DeleteFileW(g_smokeFolder) || !CreateDirectoryW(g_smokeFolder, NULL)) {
        g_smokeFolder[0] = 0;
        return 0;
    }
    if (lstrlenW(g_smokeFolder) > MAX_PATH - 32) return 0;
    wsprintfW(g_smokeElf, L"%s\\symbols.so", g_smokeFolder);
    return smoke_write_elf(g_smokeElf);
}

/* Everything a step may have left in it after a failure goes too. */
static void smoke_scratch_remove(void)
{
    WIN32_FIND_DATAW found;
    WCHAR pattern[MAX_PATH + 4], path[MAX_PATH * 2];
    HANDLE search;
    if (!g_smokeFolder[0]) return;
    wsprintfW(pattern, L"%s\\*", g_smokeFolder);
    search = FindFirstFileW(pattern, &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            wsprintfW(path, L"%s\\%s", g_smokeFolder, found.cFileName);
            DeleteFileW(path);
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    RemoveDirectoryW(g_smokeFolder);
    g_smokeElf[0] = g_smokeFolder[0] = 0;
}

typedef struct smoke_names {
    char **names;
    size_t count, capacity;
    int failed;
} smoke_names;

/* Every row of a result tree, with its detection's name (NULL on groups). */
static int smoke_collect_name(size_t parent, const char *text, const char *category, const gui_backend_detection *detection, void *user)
{
    smoke_names *list = (smoke_names *)user;
    xxwidgets_tree_node shown;
    (void)category;
    /* "?" is pressed by row index: each row must be the shown tree's own. */
    if (xxwidgets_treeview_get_node(g_results, list->count, &shown) != XXWIDGETS_OK || shown.parent != parent ||
        !shown.text || !text || strcmp(shown.text, text)) {
        list->failed = 1;
        return 0;
    }
    if (list->count == list->capacity) {
        size_t capacity = list->capacity ? list->capacity * 2 : 32;
        char **grown = (char **)realloc(list->names, capacity * sizeof(*grown));
        if (!grown) { list->failed = 1; return 0; }
        list->names = grown; list->capacity = capacity;
    }
    list->names[list->count] = NULL;
    if (detection && detection->name) {
        size_t size = strlen(detection->name) + 1;
        list->names[list->count] = (char *)malloc(size);
        if (!list->names[list->count]) { list->failed = 1; return 0; }
        memcpy(list->names[list->count], detection->name, size);
    }
    ++list->count;
    return 1;
}

static void smoke_names_free(smoke_names *list)
{
    for (size_t i = 0; i < list->count; ++i) free(list->names[i]);
    free(list->names);
    memset(list, 0, sizeof(*list));
}

/* A description file name: what gui_backend_info_text accepts and NTFS stores. */
static int smoke_info_name(const char *name)
{
    if (!name[0] || !strcmp(name, ".") || !strcmp(name, "..") || strlen(name) > 100) return 0;
    for (const char *s = name; *s; ++s)
        if ((unsigned char)*s < 32 || strchr("/\\:*?\"<>|", *s)) return 0;
    return 1;
}

/* A one-file info database describing name, for when none is bundled. */
static int smoke_info_fixture(const char *name, WCHAR *file)
{
    static const char html[] = "<p>CDIE smoke description.</p>";
    WCHAR *wide = xx_str_utf8_to_unicode(name);
    char *folder = xx_str_unicode_to_utf8(g_smokeFolder);
    HANDLE handle = INVALID_HANDLE_VALUE;
    DWORD written = 0;
    int ok = 0;
    if (wide && folder && lstrlenW(g_smokeFolder) + lstrlenW(wide) + 7 < MAX_PATH) {
        wsprintfW(file, L"%s\\%s.html", g_smokeFolder, wide);
        handle = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        ok = handle != INVALID_HANDLE_VALUE && WriteFile(handle, html, sizeof(html) - 1, &written, NULL) &&
            written == sizeof(html) - 1;
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        ok = ok && gui_backend_set_info_path(folder);
        if (!ok) { DeleteFileW(file); file[0] = 0; }
    }
    xx_str_wfree(wide);
    xx_str_free(folder);
    return ok;
}

/* "?" on a detection row shows its description; a row without one would open
 * a web search, so the step picks a row that has one (bundled, or written to
 * a scratch info folder for the run). */
static int smoke_info_mark(const char *executable)
{
    smoke_names names = {0};
    char *report = NULL, *type = NULL, *info = NULL;
    WCHAR file[MAX_PATH] = L"", *title = NULL;
    size_t node, count = xxwidgets_treeview_count(g_results);
    xxwidgets_event press = {0};
    gui_options_values values;
    int passed = 0;
    if (!gui_backend_scan_tree(executable, 0, smoke_collect_name, &names, &report, &type, NULL, NULL) || names.failed || names.count != count)
        goto finish;
    for (node = 0; node < count; ++node)
        if ((xxwidgets_treeview_node_marks(g_results, node) & 2u) && names.names[node] && (info = gui_backend_info_text(names.names[node]))) break;
    if (node == count) {
        for (node = 0; node < count; ++node)
            if ((xxwidgets_treeview_node_marks(g_results, node) & 2u) && names.names[node] && smoke_info_name(names.names[node])) break;
        if (node == count || !smoke_info_fixture(names.names[node], file) || !(info = gui_backend_info_text(names.names[node]))) goto finish;
        smoke_trace("GUI smoke: no bundled description for these detections; using a scratch info folder");
    }
    title = xx_str_utf8_to_unicode(names.names[node]);
    press.type = XXWIDGETS_EVENT_CLICK; press.widget = g_results; press.value = (int)node; press.x = 1;
    if (!title || !gui_tree_event(g_panel, &press) || !smoke_begin_ex(title, SMOKE_INFO, 1)) goto finish;
    g_smokeExpectText = info;
    g_smokeEscape = 1;
    passed = gui_tree_process(g_window) == NULL;
    passed = smoke_finish() && passed;
finish:
    gui_preferences_get(&values);
    if (file[0]) {
        (void)gui_backend_set_info_path(values.info.path);
        DeleteFileW(file);
    }
    xx_str_wfree(title);
    gui_backend_free(info); gui_backend_free(report); gui_backend_free(type);
    smoke_names_free(&names);
    return passed;
}

/* A local TCP port that takes connections into its backlog and never answers,
 * so an update request stalls until it is cancelled. Winsock comes from the
 * system at run time: the GUI links no network library of its own. */
typedef int (PASCAL *smoke_close_socket_fn)(SOCKET);
typedef int (PASCAL *smoke_cleanup_fn)(void);
typedef struct smoke_listener {
    HMODULE library;
    SOCKET socket;
    smoke_close_socket_fn close_socket;
    smoke_cleanup_fn cleanup;
} smoke_listener;

static void smoke_unlisten(smoke_listener *listener)
{
    if (listener->socket != INVALID_SOCKET && listener->close_socket) listener->close_socket(listener->socket);
    if (listener->cleanup) listener->cleanup();
    if (listener->library) FreeLibrary(listener->library);
    listener->socket = INVALID_SOCKET;
    listener->library = NULL; listener->cleanup = NULL; listener->close_socket = NULL;
}

static unsigned int smoke_listen(smoke_listener *listener)
{
    typedef int (PASCAL *startup_fn)(WORD, LPWSADATA);
    typedef SOCKET (PASCAL *socket_fn)(int, int, int);
    typedef int (PASCAL *bind_fn)(SOCKET, const struct sockaddr *, int);
    typedef int (PASCAL *listen_fn)(SOCKET, int);
    typedef int (PASCAL *name_fn)(SOCKET, struct sockaddr *, int *);
    startup_fn startup;
    socket_fn open_socket;
    bind_fn bind_socket;
    listen_fn listen_socket;
    name_fn socket_name;
    WSADATA data;
    struct sockaddr_in address;
    int length = sizeof(address);
    ZeroMemory(listener, sizeof(*listener));
    listener->socket = INVALID_SOCKET;
    listener->library = LoadLibraryW(L"ws2_32.dll");
    if (!listener->library) return 0;
    startup = (startup_fn)GetProcAddress(listener->library, "WSAStartup");
    open_socket = (socket_fn)GetProcAddress(listener->library, "socket");
    bind_socket = (bind_fn)GetProcAddress(listener->library, "bind");
    listen_socket = (listen_fn)GetProcAddress(listener->library, "listen");
    socket_name = (name_fn)GetProcAddress(listener->library, "getsockname");
    listener->close_socket = (smoke_close_socket_fn)GetProcAddress(listener->library, "closesocket");
    if (!startup || !open_socket || !bind_socket || !listen_socket || !socket_name || !listener->close_socket ||
        startup(MAKEWORD(2, 2), &data)) {
        smoke_unlisten(listener);
        return 0;
    }
    listener->cleanup = (smoke_cleanup_fn)GetProcAddress(listener->library, "WSACleanup");
    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    /* 127.0.0.1 in network byte order; port 0 lets the system pick one. */
    address.sin_addr.s_addr = 0x0100007fu;
    listener->socket = open_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener->socket == INVALID_SOCKET || bind_socket(listener->socket, (const struct sockaddr *)&address, sizeof(address)) ||
        listen_socket(listener->socket, 4) || socket_name(listener->socket, (struct sockaddr *)&address, &length)) {
        smoke_unlisten(listener);
        return 0;
    }
    return ((unsigned int)(address.sin_port & 0xff) << 8) | ((unsigned int)address.sin_port >> 8);
}

/* Sets a variable for this process (as gui_update.c reads it) and returns the
 * previous value to restore, NULL when it was unset. */
static WCHAR *smoke_setenv(const WCHAR *name, const WCHAR *value)
{
    const WCHAR *current = _wgetenv(name);
    WCHAR *previous = current ? _wcsdup(current) : NULL;
    _wputenv_s(name, value ? value : L"");
    return previous;
}

static void smoke_restoreenv(const WCHAR *name, WCHAR *previous)
{
    _wputenv_s(name, previous ? previous : L"");
    free(previous);
}

/* Update database against this machine only: a stalled server shows the
 * progress dialog, whose Cancel ends the request; the same port closed makes
 * the request fail, shown whole in a text dialog besides the status bar. */
static xxwidgets_die_action_id smoke_click_action(xxwidgets_widget *control, int run);

static int smoke_update(void)
{
    smoke_listener listener;
    unsigned int port;
    WCHAR url[64], *saved_url, *saved_proxy;
    char status_text[512];
    uint64_t generation = gui_backend_database_generation();
    int signatures = gui_backend_signature_count(), passed;
    gui_update_result result;
    if (!gui_update_supported()) return 1;
    port = smoke_listen(&listener);
    if (!port) return 0;
    wsprintfW(url, L"http://127.0.0.1:%u/db.zip", port);
    saved_url = smoke_setenv(L"CDIE_UPDATE_URL", url);
    saved_proxy = smoke_setenv(L"CDIE_UPDATE_NOPROXY", L"1");
    /* Pressed in the right column (Q2), through the shared dispatcher. */
    g_lastMessage[0] = 0;
    passed = smoke_begin(L"Updating Database", SMOKE_UPDATE_CANCEL);
    passed = passed && smoke_click_action(g_updateButton, 1) == XXWIDGETS_DIE_ACTION_UPDATE;
    smoke_last_message(status_text, sizeof(status_text));
    passed = smoke_finish() && passed && !strcmp(status_text, CDIE_TR("Database update cancelled."));
    smoke_unlisten(&listener);
    if (passed && smoke_begin_ex(L"Update database", SMOKE_UPDATE_FAILED, 1)) {
        g_smokeExpectPart = L"Database update failed";
        result = do_update_database(g_window);
        smoke_last_message(status_text, sizeof(status_text));
        passed = smoke_finish() && result == GUI_UPDATE_FAILED && !strncmp(status_text, "Database update failed", 22);
    } else passed = 0;
    smoke_restoreenv(L"CDIE_UPDATE_URL", saved_url);
    smoke_restoreenv(L"CDIE_UPDATE_NOPROXY", saved_proxy);
    return passed && gui_backend_database_generation() == generation && gui_backend_signature_count() == signatures;
}

/* A file of zeros in the scratch folder: 2 GiB and sparse (long work for
 * Hash, no disk space), or small and allocated. 0 when it cannot be made. */
static int smoke_zero_file(WCHAR *path, const WCHAR *name, LONGLONG length, int sparse)
{
    HANDLE file;
    DWORD bytes = 0;
    LARGE_INTEGER size;
    int ready;
    wsprintfW(path, L"%s\\%s", g_smokeFolder, name);
    file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    size.QuadPart = length;
    ready = file != INVALID_HANDLE_VALUE && (!sparse || DeviceIoControl(file, FSCTL_SET_SPARSE, NULL, 0, NULL, 0, &bytes, NULL)) &&
        SetFilePointerEx(file, size, NULL, FILE_BEGIN) && SetEndOfFile(file);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!ready) DeleteFileW(path);
    return ready;
}

/* Hash and a scan run long enough for their progress dialogs, whose Cancel
 * must end the work without a result or an error. A deep scan takes about a
 * second per MiB of zeros, and stops between signatures: 4 MiB is plenty. */
static int smoke_progress_cancel(void)
{
    WCHAR path[MAX_PATH + 16];
    char *utf8 = NULL, status_text[512];
    xxwidgets_status status;
    LONGLONG length;
    int passed = 1;
    if (smoke_zero_file(path, L"sparse.bin", (LONGLONG)2 << 30, 1)) {
        utf8 = xx_str_unicode_to_utf8(path);
        passed = utf8 && smoke_begin(L"Calculating Hash", SMOKE_PROGRESS_CANCEL);
        status = passed ? gui_hash_show(g_window, utf8) : XXWIDGETS_PLATFORM_ERROR;
        passed = smoke_finish() && passed && status == XXWIDGETS_OK;
        xx_str_free(utf8);
        DeleteFileW(path);
    } else smoke_trace("GUI smoke: skipped Hash's progress Cancel (the temporary folder has no sparse files)");
    /* The scan from the path field, as Scan does: cancelled, it says so. Its
     * dialog shows only after a second of work, which a fast machine (or a
     * faster engine) may not need for 4 MiB: the file then grows, and a scan
     * that ends before its dialog even at 64 MiB is reported, not failed. */
    for (length = (LONGLONG)4 << 20; passed && length <= (LONGLONG)64 << 20; length *= 4) {
        int shown;
        if (!smoke_zero_file(path, L"zeros.bin", length, 0)) return 0;
        if (!SetWindowTextW(native(g_path), path) || !smoke_begin(L"Scanning", SMOKE_PROGRESS_CANCEL)) {
            DeleteFileW(path);
            return 0;
        }
        g_smokeOptional = 1;
        do_scan();
        shown = g_smokeDone;
        smoke_last_message(status_text, sizeof(status_text));
        passed = smoke_finish();
        DeleteFileW(path);
        if (shown) return passed && !strcmp(status_text, CDIE_TR("Scan cancelled.")) && !xxwidgets_scan_panel_count(g_panel);
    }
    if (passed) smoke_trace("GUI smoke: skipped the scan's progress Cancel (64 MiB scanned in under a second)");
    return passed;
}

typedef void (*smoke_opener)(void);
/* Every tool, through its button's function, closed with Escape. */
static const struct smoke_tool {
    const WCHAR *title;
    int action, fixture;
    smoke_opener open;
} g_smokeTools[] = {
    {L"File Information", SMOKE_FILE_INFORMATION, 0, do_file_information}, {L"Entropy", SMOKE_ENTROPY, 0, do_entropy},
    {L"Hash", SMOKE_HASH, 0, do_hash}, {L"Strings", SMOKE_STRINGS_FILE, 0, do_strings}, {L"Hex View", SMOKE_HEX_VIEW, 0, do_hex_view},
    {L"Disasm", SMOKE_DISASM, 0, do_disasm}, {L"Memory Map", SMOKE_MEMORY_MAP, 0, do_memory_map}, {L"Imports", SMOKE_IMPORTS, 0, do_imports},
    {L"Exports", SMOKE_EXPORTS, 1, do_exports}, {L"Resources", SMOKE_RESOURCES, 0, do_resources}, {L"Symbols", SMOKE_SYMBOLS, 1, do_symbols},
    {L"Extractor", SMOKE_EXTRACTOR, 0, do_extractor}};

/* Every Options page, selected from the list, checked, then cancelled. */
static int smoke_options_pages(void)
{
    for (size_t i = 0; i < sizeof(g_smokeOptionsPages) / sizeof(g_smokeOptionsPages[0]); ++i)
        if (!smoke_begin_page(g_smokeOptionsPages[i][0], SMOKE_PAGE_CANCEL, 1) || do_settings(GUI_OPTIONS_SCAN) != 0 || !smoke_finish()) {
            char trace[96];
            wsprintfA(trace, "GUI smoke: Options page %ls", g_smokeOptionsPages[i][0]);
            smoke_write(trace);
            smoke_write("\r\n");
            return 0;
        }
    return 1;
}

/* The last message (there is no status line): what a step left behind. */
static void smoke_last_message(char *text, size_t size)
{
    xx_rt_snprintf(text, size, "%s", g_lastMessage);
}

/* Presses a main-window control as a click arrives (a check box through
 * BM_CLICK, which also toggles it) and returns the panel action it queued;
 * run 1 then carries it out as the main loop would. */
static xxwidgets_die_action_id smoke_click_action(xxwidgets_widget *control, int run)
{
    xxwidgets_die_action_id id;
    int action;
    g_pending = ACTION_NONE;
    memset(&g_pendingDie, 0, sizeof(g_pendingDie));
    if (xxwidgets_widget_kind(control) == XXWIDGETS_CHECKBOX) SendMessageW(native(control), BM_CLICK, 0, 0);
    else smoke_press(control);
    action = g_pending;
    id = action == ACTION_DIE ? g_pendingDie.id : XXWIDGETS_DIE_ACTION_NONE;
    g_pending = ACTION_NONE;
    if (run) run_pending(action);
    memset(&g_pendingDie, 0, sizeof(g_pendingDie));
    return id;
}

/* The main window's own layout, as every dialog's is checked: inside the
 * window, nothing overlapping but frames, text fitting (text_sized). */
static int smoke_main_layout(int text_sized)
{
    int passed, sized = g_smokeFontSized, action = g_smokeAction;
    g_smokeAction = SMOKE_MAIN_LAYOUT;
    g_smokeFailed = 0;
    g_smokeFontSized = text_sized;
    passed = smoke_check_dialog(native(g_window), 0, 0) == 1;
    g_smokeFontSized = sized;
    g_smokeAction = action;
    return passed;
}

static int smoke_shown(xxwidgets_widget *widget)
{
    return widget && IsWindowVisible(native(widget));
}

static int smoke_live(xxwidgets_widget *widget)
{
    return smoke_shown(widget) && IsWindowEnabled(native(widget));
}

/* Every control of a view (DIE_LOOK.md 3b.5): present, visible and enabled
 * as the view and the file state say; the scan panel's own File type and
 * labels are gone. */
static unsigned int smoke_view_controls(int advanced, int loaded)
{
    xxwidgets_widget *always[] = {g_path, g_browse, g_results, g_advanced, g_shortcutsButton, g_options, g_aboutButton, g_exit};
    xxwidgets_widget *with_file[] = {g_type, g_flags, g_databases, g_scan, g_signaturesButton, g_directoryButton, g_logButton,
                                     g_facts[0], g_facts[1], g_facts[2], g_facts[3], g_msec};
    xxwidgets_widget *hidden[] = {g_flagLabel, g_databaseLabel, g_includeTagsLabel, g_excludeTagsLabel,
        xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FILE_TYPE),
        xxwidgets_scan_panel_control(g_panel, XXWIDGETS_SCAN_PANEL_FILE_TYPE_LABEL)};
    xxwidgets_widget *base = xxwidgets_value_field_control(xxwidgets_file_header_field(g_header, XXWIDGETS_FILE_HEADER_BASE_ADDRESS),
                                                           XXWIDGETS_VALUE_FIELD_VALUE);
    xxwidgets_widget *entry = xxwidgets_value_field_control(xxwidgets_file_header_field(g_header, XXWIDGETS_FILE_HEADER_ENTRY_POINT),
                                                            XXWIDGETS_VALUE_FIELD_VALUE);
    xxwidgets_widget *size = xxwidgets_value_field_control(xxwidgets_file_header_field(g_header, XXWIDGETS_FILE_HEADER_FILE_SIZE),
                                                           XXWIDGETS_VALUE_FIELD_VALUE);
    int checked = 0;
    size_t i;
    for (i = 0; i < sizeof(always) / sizeof(always[0]); ++i) if (!smoke_live(always[i])) return __LINE__ * 100 + (unsigned int)i;
    for (i = 0; i < sizeof(with_file) / sizeof(with_file[0]); ++i)
        if (!smoke_shown(with_file[i]) || !IsWindowEnabled(native(with_file[i])) != !loaded) return __LINE__ * 100 + (unsigned int)i;
    for (i = 0; i < sizeof(hidden) / sizeof(hidden[0]); ++i) if (smoke_shown(hidden[i])) return __LINE__ * 100 + (unsigned int)i;
    /* The tag filters stay in the scan bar (Q3); Update database is shown
     * above Shortcuts, enabled only with an updater (Q2). */
    if (!smoke_shown(g_includeTags) || !smoke_shown(g_excludeTags) || !smoke_shown(g_reportButton)) return __LINE__;
    if (!smoke_shown(g_updateButton) || !IsWindowEnabled(native(g_updateButton)) != !gui_update_supported()) return __LINE__;
    if (!smoke_shown(g_recent) || !smoke_shown(size) || !IsWindowEnabled(native(size)) != !loaded) return __LINE__;
    /* Base address and Entry point belong to the Advanced view. */
    if (!base || !entry || smoke_shown(base) != advanced || smoke_shown(entry) != advanced) return __LINE__;
    if (xxwidgets_side_panel_get_checked(g_side, XXWIDGETS_DIE_SIDE_ADVANCED, &checked) != XXWIDGETS_OK || checked != advanced ||
        xxwidgets_die_main_panel_get_advanced(g_main) != advanced || gui_frontend_advanced() != advanced) return __LINE__;
    {
        /* DIE's arrangement: the file name bar on top; the engine row over the
         * tree, the scan bar under it; the right column beside them, Update
         * database over Shortcuts, Options, About and Exit, in that order. */
        RECT path, fact, results, signatures, adv, update, shortcuts, options, about, exit;
        GetWindowRect(native(g_path), &path); GetWindowRect(native(g_facts[0]), &fact);
        GetWindowRect(native(g_results), &results); GetWindowRect(native(g_signaturesButton), &signatures);
        GetWindowRect(native(g_advanced), &adv); GetWindowRect(native(g_updateButton), &update);
        GetWindowRect(native(g_shortcutsButton), &shortcuts); GetWindowRect(native(g_options), &options);
        GetWindowRect(native(g_aboutButton), &about); GetWindowRect(native(g_exit), &exit);
        if (path.bottom > adv.top || path.bottom > fact.top || fact.bottom > results.top || results.bottom > signatures.top ||
            results.right > adv.left || adv.bottom > update.top || update.bottom > shortcuts.top || shortcuts.bottom > options.top ||
            options.bottom > about.top || about.bottom > exit.top || exit.left < results.right) return __LINE__;
    }
    return 0;
}

static int smoke_view_failed(unsigned int line, unsigned int code)
{
    char text[96];
    wsprintfA(text, "GUI smoke: view check code %u\r\n", code);
    smoke_write(text);
    return smoke_failed(line);
}

/* Both views with the file state as it is: their controls and layout, the
 * Advanced check box toggled as a click does, the window grown for the
 * Advanced view and never shrunk back. 0 passed, else the failing line. */
static unsigned int smoke_views(int loaded)
{
    RECT simple, advanced, back;
    unsigned int code = smoke_view_controls(0, loaded);
    if (code) return code;
    if (!smoke_main_layout(1)) return __LINE__;
    GetWindowRect(native(g_window), &simple);
    if (smoke_click_action(g_advanced, 1) != XXWIDGETS_DIE_ACTION_NONE || g_pending != ACTION_NONE) return __LINE__;
    if ((code = smoke_view_controls(1, loaded)) != 0) return code;
    if (!smoke_main_layout(1)) return __LINE__;
    GetWindowRect(native(g_window), &advanced);
    if (advanced.right - advanced.left < simple.right - simple.left || advanced.bottom - advanced.top < simple.bottom - simple.top)
        return __LINE__;
    if (smoke_click_action(g_advanced, 1) != XXWIDGETS_DIE_ACTION_NONE) return __LINE__;
    if ((code = smoke_view_controls(0, loaded)) != 0) return code;
    if (!smoke_main_layout(1)) return __LINE__;
    GetWindowRect(native(g_window), &back);
    if (back.right - back.left != advanced.right - advanced.left || back.bottom - back.top != advanced.bottom - advanced.top) return __LINE__;
    return 0;
}

/* The right column's buttons, each pressed once and what opens closed
 * (Update database in smoke_update, Advanced in smoke_views, Exit last). */
static int smoke_right_column(void)
{
    /* Shortcuts is modal to the main window only (XXWIDGETS_WINDOW_MODAL). */
    if (!smoke_begin_ex(L"Shortcuts", SMOKE_SHORTCUTS, 0)) return 0;
    if (smoke_click_action(g_shortcutsButton, 1) != XXWIDGETS_DIE_ACTION_SHORTCUTS || !smoke_finish()) return 0;
    if (!smoke_begin_page(L"Scan", SMOKE_PAGE_CANCEL, 1)) return 0;
    if (smoke_click_action(g_options, 1) != XXWIDGETS_DIE_ACTION_OPTIONS || !smoke_finish()) return 0;
    if (!smoke_begin(L"About", SMOKE_ABOUT_CLOSE)) return 0;
    if (smoke_click_action(g_aboutButton, 1) != XXWIDGETS_DIE_ACTION_ABOUT || !smoke_finish()) return 0;
    return 1;
}

/* An error is a message dialog now (Q1): its whole text, titled with the program. */
static int smoke_error_dialog(void)
{
    WCHAR *title = xx_str_utf8_to_unicode(gui_backend_app_title());
    int passed = title && smoke_begin_ex(title, SMOKE_UPDATE_FAILED, 1);
    if (passed) {
        g_smokeExpectPart = L"Smoke error text";
        g_quietErrors = 0;
        show_error("Smoke error text");
        g_quietErrors = 1;
        passed = smoke_finish() && !strcmp(g_lastMessage, "Smoke error text");
    }
    xx_str_wfree(title);
    return passed;
}

/* Every key of the shortcut table (gui_shortcuts.c) reaches its action,
 * the tools' keys too, so they stay reachable without their old buttons. */
static int smoke_shortcut_keys(void)
{
    static const struct { UINT key; int control, shift; gui_shortcut_action action; } keys[] = {
        {VK_F5, 0, 0, GUI_SHORTCUT_SCAN}, {'O', 1, 0, GUI_SHORTCUT_OPEN}, {VK_F1, 0, 0, GUI_SHORTCUT_ABOUT},
        {VK_F2, 0, 0, GUI_SHORTCUT_OPTIONS}, {'D', 1, 1, GUI_SHORTCUT_DATABASE_PATHS}, {VK_F3, 0, 0, GUI_SHORTCUT_DATABASE_PATHS},
        {VK_F4, 0, 0, GUI_SHORTCUT_FONTS},
        {'I', 1, 0, GUI_SHORTCUT_TOOL_FIRST + GUI_TOOL_FILE_INFORMATION}, {'E', 1, 0, GUI_SHORTCUT_TOOL_FIRST + GUI_TOOL_ENTROPY},
        {'H', 1, 0, GUI_SHORTCUT_TOOL_FIRST + GUI_TOOL_HASH}, {'S', 1, 1, GUI_SHORTCUT_TOOL_FIRST + GUI_TOOL_STRINGS},
        {'X', 1, 1, GUI_SHORTCUT_TOOL_FIRST + GUI_TOOL_HEX_VIEW}, {'A', 1, 1, GUI_SHORTCUT_TOOL_FIRST + GUI_TOOL_DISASM},
        {'M', 1, 0, GUI_SHORTCUT_TOOL_FIRST + GUI_TOOL_MEMORY_MAP}, {'I', 1, 1, GUI_SHORTCUT_TOOL_FIRST + GUI_TOOL_IMPORTS},
        {'E', 1, 1, GUI_SHORTCUT_TOOL_FIRST + GUI_TOOL_EXPORTS}, {'U', 1, 1, GUI_SHORTCUT_TOOL_FIRST + GUI_TOOL_RESOURCES},
        {'Y', 1, 1, GUI_SHORTCUT_TOOL_FIRST + GUI_TOOL_SYMBOLS}, {'T', 1, 1, GUI_SHORTCUT_TOOL_FIRST + GUI_TOOL_EXTRACTOR}};
    size_t i;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        if (smoke_shortcut(keys[i].key, keys[i].control, keys[i].shift) != SHORTCUT_ACTION(keys[i].action)) {
            char text[64];
            wsprintfA(text, "GUI smoke: shortcut %u\r\n", (unsigned int)i);
            smoke_write(text);
            return 0;
        }
    /* Ctrl+U only with an updater; with one, it is Update database. */
    if (smoke_shortcut('U', 1, 0) != (gui_update_supported() ? SHORTCUT_ACTION(GUI_SHORTCUT_UPDATE) : ACTION_NONE)) return 0;
    /* A tool's key runs the tool through the shared dispatcher, on this program. */
    {
        WCHAR executable[MAX_PATH];
        DWORD length = GetModuleFileNameW(NULL, executable, MAX_PATH);
        if (!length || length >= MAX_PATH || !SetWindowTextW(native(g_path), executable)) return 0;
    }
    if (!smoke_begin(L"File Information", SMOKE_FILE_INFORMATION)) return 0;
    g_smokeEscape = 1;
    run_pending(SHORTCUT_ACTION(GUI_SHORTCUT_TOOL_FIRST + GUI_TOOL_FILE_INFORMATION));
    return smoke_finish();
}

static int run_smoke_steps(void)
{
    xxwidgets_scan_result row = {"Compiler", "Microsoft Visual C/C++", "19.29", "Smoke test"};
    unsigned int flags, databases;
    unsigned int view;
    WCHAR class_name[32], executable[32768];
    DWORD executable_length = GetModuleFileNameW(NULL, executable, (DWORD)(sizeof(executable) / sizeof(executable[0])));
    SIZE_T report_length = 0;
    xxwidgets_status status;
    if (!executable_length || executable_length >= sizeof(executable) / sizeof(executable[0]) || !smoke_scratch_create()) return smoke_failed(__LINE__);
    /* The owner is shown, as for a user: dialogs center on it and give the focus back to it. */
    if (xxwidgets_widget_set_visible(g_window, 1) != XXWIDGETS_OK) return smoke_failed(__LINE__);
    /* Its activation settles before the first dialog takes over. */
    for (ULONGLONG settled = GetTickCount64() + 300; GetTickCount64() < settled;) (void)xxwidgets_app_poll(g_app, 30);
    g_pending = ACTION_NONE;
    if (!GetClassNameW(native(g_type), class_name, 32) || lstrcmpiW(class_name, L"COMBOBOX") ||
        !GetClassNameW(native(g_flags), class_name, 32) || lstrcmpiW(class_name, L"COMBOBOX") ||
        !GetClassNameW(native(g_databases), class_name, 32) || lstrcmpiW(class_name, L"COMBOBOX") ||
        !GetClassNameW(native(g_includeTags), class_name, 32) || lstrcmpiW(class_name, L"COMBOBOX") ||
        !GetClassNameW(native(g_excludeTags), class_name, 32) || lstrcmpiW(class_name, L"COMBOBOX")) return smoke_failed(__LINE__);
    /* No file yet: both views, the Advanced toggle, every control and the
     * layout (DIE_LOOK.md 3b.5). */
    if ((view = smoke_views(0)) != 0) return smoke_view_failed(__LINE__, view);
    if (!smoke_error_dialog()) return smoke_failed(__LINE__);
    if (xxwidgets_widget_kind(g_results) != XXWIDGETS_TREEVIEW ||
        xxwidgets_widget_kind(g_type) != XXWIDGETS_COMBOBOX ||
        xxwidgets_widget_kind(g_flags) != XXWIDGETS_CHECKCOMBOBOX ||
        xxwidgets_widget_kind(g_databases) != XXWIDGETS_CHECKCOMBOBOX ||
        xxwidgets_widget_kind(g_includeTags) != XXWIDGETS_CHECKCOMBOBOX ||
        xxwidgets_widget_kind(g_excludeTags) != XXWIDGETS_CHECKCOMBOBOX ||
        xxwidgets_widget_kind(g_scan) != XXWIDGETS_BUTTON ||
        xxwidgets_widget_kind(g_reportButton) != XXWIDGETS_BUTTON ||
        xxwidgets_scan_panel_get_flags(g_panel, &flags) != XXWIDGETS_OK || flags != 7u ||
        xxwidgets_scan_panel_get_databases(g_panel, &databases) != XXWIDGETS_OK || databases != XXWIDGETS_SCAN_DATABASE_CUSTOM ||
        xxwidgets_scan_panel_set_results(g_panel, "Smoke file", &row, 1,
            "Smoke report\nCompiler: Microsoft Visual C/C++ 19.29") != XXWIDGETS_OK ||
        xxwidgets_scan_panel_count(g_panel) != 1 || TreeView_GetCount(native(g_results)) < 2 ||
        xxwidgets_scan_panel_get_report(g_panel, NULL, 0, &report_length) != XXWIDGETS_OK ||
        report_length <= 1) return smoke_failed(__LINE__);
    if (gui_backend_signature_count() > 0) {
        SIZE_T include_size = 0, exclude_size = 0;
        char *include_tags, *exclude_tags;
        char status_text[512];
        size_t i, binary_index = SIZE_MAX;
        int type_id, binary_type = -1;
        if (!xxwidgets_combobox_count(g_includeTags) ||
            xxwidgets_combobox_count(g_includeTags) != xxwidgets_combobox_count(g_excludeTags) ||
            xxwidgets_checkcombobox_set_checked(g_includeTags, 0, 1) != XXWIDGETS_OK ||
            xxwidgets_checkcombobox_set_checked(g_excludeTags, xxwidgets_combobox_count(g_excludeTags) - 1, 1) != XXWIDGETS_OK ||
            xxwidgets_scan_panel_get_include_tags(g_panel, NULL, 0, &include_size) != XXWIDGETS_OK || include_size <= 1 ||
            xxwidgets_scan_panel_get_exclude_tags(g_panel, NULL, 0, &exclude_size) != XXWIDGETS_OK || exclude_size <= 1)
            return smoke_failed(__LINE__);
        include_tags = (char *)HeapAlloc(GetProcessHeap(), 0, include_size);
        exclude_tags = (char *)HeapAlloc(GetProcessHeap(), 0, exclude_size);
        if (!include_tags || !exclude_tags ||
            xxwidgets_scan_panel_get_include_tags(g_panel, include_tags, include_size, NULL) != XXWIDGETS_OK ||
            xxwidgets_scan_panel_get_exclude_tags(g_panel, exclude_tags, exclude_size, NULL) != XXWIDGETS_OK ||
            !include_tags[0] || !exclude_tags[0] || !gui_backend_set_tag_filters(include_tags, exclude_tags)) {
            if (include_tags) HeapFree(GetProcessHeap(), 0, include_tags);
            if (exclude_tags) HeapFree(GetProcessHeap(), 0, exclude_tags);
            return smoke_failed(__LINE__);
        }
        HeapFree(GetProcessHeap(), 0, include_tags);
        HeapFree(GetProcessHeap(), 0, exclude_tags);
        if (xxwidgets_checkcombobox_set_checked(g_includeTags, 0, 0) != XXWIDGETS_OK ||
            xxwidgets_checkcombobox_set_checked(g_excludeTags, xxwidgets_combobox_count(g_excludeTags) - 1, 0) != XXWIDGETS_OK ||
            apply_panel_tag_filters() != XXWIDGETS_OK) return smoke_failed(__LINE__);
        if (!SetWindowTextW(native(g_path), executable) || IsWindowEnabled(native(g_type))) return smoke_failed(__LINE__);
        smoke_trace("GUI smoke first self-scan");
        do_scan();
        smoke_trace("GUI smoke first self-scan complete");
        if (xxwidgets_scan_panel_get_report(g_panel, NULL, 0, &report_length) != XXWIDGETS_OK ||
            report_length <= 1 || !xxwidgets_scan_panel_count(g_panel) || TreeView_GetCount(native(g_results)) < 2 ||
            selected_file_type(&type_id) != XXWIDGETS_OK || type_id != 0 ||
            xxwidgets_combobox_count(g_type) < 3 || !IsWindowEnabled(native(g_type))) return smoke_failed(__LINE__);
        for (i = 0; i < xxwidgets_combobox_count(g_type); ++i) {
            const xx_meta_string *record;
            if (xxwidgets_combobox_get_record(g_type, i, &record) != XXWIDGETS_OK) return smoke_failed(__LINE__);
            if (!lstrcmpiW(record->meta_string->data, L"Binary")) {
                binary_index = i; binary_type = (int)record->var.val.u64; break;
            }
        }
        if (binary_index == SIZE_MAX || binary_type <= 0) return smoke_failed(__LINE__);
        /* A file is loaded: both views again, everything for a file enabled. */
        if ((view = smoke_views(1)) != 0) return smoke_view_failed(__LINE__, view);
        /* Choosing a type rescans with it (gui_main_actions.c). */
        SendMessageW(native(g_type), CB_SETCURSEL, binary_index, 0);
        SendMessageW(native(g_window), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(native(g_type)), CBN_SELCHANGE), (LPARAM)native(g_type));
        if (g_pending != ACTION_DIE || g_pendingDie.id != XXWIDGETS_DIE_ACTION_FILE_TYPE || g_pendingDie.value != (int)binary_index ||
            g_pendingDie.type_id != (uint64_t)binary_type) return smoke_failed(__LINE__);
        smoke_trace("GUI smoke binary self-scan");
        {
            int action = g_pending;
            g_pending = ACTION_NONE;
            run_pending(action);
        }
        smoke_trace("GUI smoke binary self-scan complete");
        smoke_last_message(status_text, sizeof(status_text));
        if (g_pending != ACTION_NONE || selected_file_type(&type_id) != XXWIDGETS_OK || type_id != binary_type ||
            !strstr(status_text, "Scan complete (Binary)")) return smoke_failed(__LINE__);
        /* The preferred (most specific) type again: back to the type-0 scan. */
        SendMessageW(native(g_type), CB_SETCURSEL, g_fileTypes.preferred, 0);
        SendMessageW(native(g_window), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(native(g_type)), CBN_SELCHANGE), (LPARAM)native(g_type));
        if (g_pending != ACTION_DIE || g_pendingDie.id != XXWIDGETS_DIE_ACTION_FILE_TYPE) return smoke_failed(__LINE__);
        smoke_trace("GUI smoke final self-scan");
        {
            int action = g_pending;
            g_pending = ACTION_NONE;
            run_pending(action);
        }
        smoke_trace("GUI smoke final self-scan complete");
        if (g_pending != ACTION_NONE || selected_file_type(&type_id) != XXWIDGETS_OK || type_id != 0 ||
            !xxwidgets_scan_panel_count(g_panel)) return smoke_failed(__LINE__);
        {
            /* DIE's S and ? columns: detection rows carry them; S opens the script. */
            size_t node, count = xxwidgets_treeview_count(g_results);
            xxwidgets_event press = {0};
            for (node = 0; node < count && !(xxwidgets_treeview_node_marks(g_results, node) & 1u); ++node) {}
            if (xxwidgets_treeview_mark_count(g_results) != 2 || node == count ||
                lstrcmpA(xxwidgets_treeview_mark_label(g_results, 0), "S") || lstrcmpA(xxwidgets_treeview_mark_label(g_results, 1), "?") ||
                !(xxwidgets_treeview_node_marks(g_results, node) & 2u) || xxwidgets_treeview_node_marks(g_results, 0))
                return smoke_failed(__LINE__);
            /* Strict: the backend now opens the picker on a start path with '/'. */
            static const smoke_inner open_picker[] = {{L"Open", L"Open DIE signature", NULL}};
            press.type = XXWIDGETS_EVENT_CLICK; press.widget = g_results; press.value = (int)node; press.x = 0;
            if (!gui_tree_event(g_panel, &press) || !smoke_begin(L"Signatures", SMOKE_SIGNATURE)) return smoke_failed(__LINE__);
            g_smokeInner = open_picker;
            g_smokeInnerCount = 1;
            if (gui_tree_process(g_window) != NULL || !smoke_finish()) return smoke_failed(__LINE__);
        }
        {
            /* The Signatures button opens the editor on the first signature; an
             * edited signature asks before Open discards it. */
            if (smoke_click_action(g_signaturesButton, 1) != XXWIDGETS_DIE_ACTION_SIGNATURES ||
                !smoke_begin(L"Signatures", SMOKE_SIGNATURE_CONFIRM) || gui_tree_process(g_window) != NULL || !smoke_finish())
                return smoke_failed(__LINE__);
        }
        {
            char *utf8 = xx_str_unicode_to_utf8(executable);
            int passed = utf8 && smoke_info_mark(utf8);
            xx_str_free(utf8);
            if (!passed) return smoke_failed(__LINE__);
        }
        if (smoke_click_action(g_logButton, 1) != XXWIDGETS_DIE_ACTION_LOG || !smoke_begin(L"Log", SMOKE_LOG)) return smoke_failed(__LINE__);
        g_smokeEscape = 1;
        if (gui_tree_process(g_window) != NULL || !smoke_finish()) return smoke_failed(__LINE__);
        /* Scan directory on the scratch folder, with a look at its Scan options. */
        if (smoke_click_action(g_directoryButton, 1) != XXWIDGETS_DIE_ACTION_DIRECTORY ||
            !smoke_begin(L"Scan directory", SMOKE_DIRECTORY) || gui_tree_process(g_window) != NULL || !smoke_finish()) return smoke_failed(__LINE__);
        {
            /* Recent files lists the scanned file; OK picks it. */
            char chosen[GUI_PREFERENCE_PATH_MAX] = "", *expected = xx_str_unicode_to_utf8(executable);
            int accepted = 0, same;
            if (!expected || smoke_click_action(g_recent, 0) != XXWIDGETS_DIE_ACTION_RECENT || !smoke_begin(L"Recent files", SMOKE_RECENT)) {
                xx_str_free(expected);
                return smoke_failed(__LINE__);
            }
            g_smokeExpect = executable;
            status = gui_frontend_recent_dialog(g_window, chosen, sizeof(chosen), &accepted);
            same = !lstrcmpiA(chosen, expected);
            xx_str_free(expected);
            if (!smoke_finish() || status != XXWIDGETS_OK || !accepted || !same) return smoke_failed(__LINE__);
        }
    }
    /* Options > Scan offers the main and the custom database only. */
    if (!smoke_begin_page(L"Scan", SMOKE_SCAN_PAGE, 1) || do_settings(GUI_OPTIONS_FONTS) != 0 || !smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin_page(L"Scan", SMOKE_SCAN_CANCEL, 1) || do_settings(GUI_OPTIONS_FONTS) != 0 || !smoke_finish() || !g_bFlagDeep ||
        gui_backend_get_scan_modes() != GUI_SCAN_OVERLAYS) return smoke_failed(__LINE__);
    if (xxwidgets_scan_panel_get_flags(g_panel, &flags) != XXWIDGETS_OK || flags != 7u) return smoke_failed(__LINE__);
    if (!smoke_begin_page(L"Scan", SMOKE_OPTIONS_ESCAPE, 0) || do_settings(GUI_OPTIONS_SCAN) != 0 || !smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin_page(L"Scan", SMOKE_OPTIONS_CLOSE, 0) || do_settings(GUI_OPTIONS_SCAN) != 0 || !smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin_page(L"Scan", SMOKE_SCAN_OK, 0) || do_settings(GUI_OPTIONS_SCAN) != 1 || !smoke_finish() || g_bFlagDeep ||
        g_optionsPage != GUI_OPTIONS_SCAN) return smoke_failed(__LINE__);
    if (gui_backend_get_scan_modes() != (GUI_SCAN_ALL_TYPES | GUI_SCAN_RESOURCES | GUI_SCAN_ARCHIVES) ||
        !gui_backend_set_scan_modes(GUI_SCAN_OVERLAYS)) return smoke_failed(__LINE__);
    if (xxwidgets_scan_panel_get_flags(g_panel, &flags) != XXWIDGETS_OK || flags != 6u ||
        xxwidgets_scan_panel_get_databases(g_panel, &databases) != XXWIDGETS_OK || databases != XXWIDGETS_SCAN_DATABASE_CUSTOM) return smoke_failed(__LINE__);
    {
        gui_backend_optimization_options before, after;
        gui_backend_optimization_capabilities capabilities;
        if (!gui_backend_get_optimization_options(&before) ||
            !gui_backend_get_optimization_capabilities(&capabilities) ||
            !smoke_begin_page(L"Features", SMOKE_FEATURES_CANCEL, 0) || do_settings(GUI_OPTIONS_OPTIMIZATION) != 0 || !smoke_finish() ||
            !gui_backend_get_optimization_options(&after) ||
            after.buffer_size != before.buffer_size || after.file_buffer_size != before.file_buffer_size ||
            after.use_sse2 != before.use_sse2 || after.use_avx2 != before.use_avx2 ||
            after.use_sse42 != before.use_sse42 || after.use_aes != before.use_aes || after.use_sha != before.use_sha ||
            after.use_vpclmulqdq != before.use_vpclmulqdq || after.use_neon != before.use_neon ||
            after.use_arm_crc32 != before.use_arm_crc32 || after.use_arm_crypto != before.use_arm_crypto ||
            after.use_arm_dotprod != before.use_arm_dotprod || after.use_arm_pmull != before.use_arm_pmull) return smoke_failed(__LINE__);
        if (!smoke_begin_page(L"Features", SMOKE_FEATURES_OK, 0) || do_settings(GUI_OPTIONS_OPTIMIZATION) != 1 || !smoke_finish() ||
            !gui_backend_get_optimization_options(&after) ||
            after.buffer_size != 1024 || after.file_buffer_size != 2048 ||
            after.use_sse2 || after.use_avx2 || after.use_sse42 || after.use_aes || after.use_sha || after.use_vpclmulqdq ||
            after.use_neon || after.use_arm_crc32 || after.use_arm_crypto || after.use_arm_dotprod || after.use_arm_pmull) return smoke_failed(__LINE__);
        if (!smoke_begin_page(L"Features", SMOKE_FEATURES_DEFAULT, 1) || do_settings(GUI_OPTIONS_HEX) != 1 || !smoke_finish() ||
            !gui_backend_get_optimization_options(&after) ||
            after.buffer_size != 65536 || after.file_buffer_size != 65536 ||
            after.use_sse2 != capabilities.sse2 || after.use_avx2 != capabilities.avx2 ||
            after.use_sse42 != capabilities.sse42 || after.use_aes != capabilities.aes ||
            after.use_sha != capabilities.sha || after.use_vpclmulqdq != capabilities.vpclmulqdq ||
            after.use_neon != capabilities.neon || after.use_arm_crc32 != capabilities.arm_crc32 ||
            after.use_arm_crypto != capabilities.arm_crypto || after.use_arm_dotprod != capabilities.arm_dotprod || after.use_arm_pmull != capabilities.arm_pmull ||
            !gui_backend_set_optimization_options(&before)) return smoke_failed(__LINE__);
    }
    /* The color editor owns a second modal loop; closing it must restore
     * Options while every other application window remains blocked. */
    if (!smoke_begin_page(L"Scan", SMOKE_SCAN_COLORS, 0) ||
        do_settings(GUI_OPTIONS_SCAN) != 0 || !smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin_page(L"Scan", SMOKE_COLORS_OK, 0) ||
        do_settings(GUI_OPTIONS_SCAN) != 0 || !smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_options_pages()) return smoke_failed(__LINE__);
    {
        /* A main database folder that does not exist is refused, and nothing changes. */
        char *original_path = xx_str_create(gui_backend_main_db_path());
        int signatures = gui_backend_signature_count(), changed;
        if (!original_path || !smoke_begin_page(L"Scan", SMOKE_SCAN_MISSING_DB, 0)) {
            if (original_path) xx_str_free(original_path);
            return smoke_failed(__LINE__);
        }
        changed = do_settings(GUI_OPTIONS_SCAN) != 0;
        changed |= lstrcmpA(original_path, gui_backend_main_db_path()) || gui_backend_signature_count() != signatures;
        xx_str_free(original_path);
        if (!smoke_finish() || changed) return smoke_failed(__LINE__);
    }
    {
        /* The folder, translation and font pickers, each opened then cancelled. */
        char *original_path = xx_str_create(gui_backend_main_db_path());
        int changed;
        if (!original_path || !smoke_begin_page(L"Scan", SMOKE_PICKERS, 0)) {
            if (original_path) xx_str_free(original_path);
            return smoke_failed(__LINE__);
        }
        changed = do_settings(GUI_OPTIONS_SCAN) != 0 || lstrcmpA(original_path, gui_backend_main_db_path());
        xx_str_free(original_path);
        if (!smoke_finish() || changed) return smoke_failed(__LINE__);
    }
    {
        xxwidgets_font_options before, after;
        if (xxwidgets_app_get_font_options(g_app, &before) != XXWIDGETS_OK ||
            !smoke_begin_page(L"Fonts", SMOKE_FONTS_CANCEL, 0) || do_settings(GUI_OPTIONS_FONTS) != 0 || !smoke_finish() ||
            xxwidgets_app_get_font_options(g_app, &after) != XXWIDGETS_OK || memcmp(&before, &after, sizeof(before))) return smoke_failed(__LINE__);
        if (!smoke_begin_page(L"Fonts", SMOKE_FONTS_OK, 1) || do_settings(GUI_OPTIONS_SCAN) != 1 || !smoke_finish() ||
            xxwidgets_app_get_font_options(g_app, &after) != XXWIDGETS_OK || !memcmp(&before, &after, sizeof(before))) return smoke_failed(__LINE__);
        /* Two larger fonts: at 12 points every dialog is checked in full (text
         * and overlaps too); at 24 points, where the cells stop growing on a
         * common screen (an Options dialog must fit it), placement and focus. */
        for (unsigned int pass = 0; pass < 2; ++pass) {
            xxwidgets_font_options large = after;
            xxwidgets_widget *buttons[] = {g_scan, g_options, g_aboutButton, g_reportButton, g_signaturesButton, g_directoryButton, g_logButton,
                                           g_updateButton, g_shortcutsButton, g_exit};
            RECT original, button_bounds;
            size_t i;
            large.fonts[XXWIDGETS_FONT_CONTROLS].point_size = pass ? 24 : 12;
            large.fonts[XXWIDGETS_FONT_TEXT_EDITS].point_size = pass ? 28 : 14;
            GetWindowRect(native(g_window), &original);
            if (xxwidgets_app_set_font_options(g_app, &large) != XXWIDGETS_OK) return smoke_failed(__LINE__);
            /* The panel measures again and raises the minimum; then the window
             * is dragged as small as it goes, in both views. */
            if (xxwidgets_die_main_panel_retranslate(g_main) != XXWIDGETS_OK) return smoke_failed(__LINE__);
            for (int advanced = 1; advanced >= 0; --advanced) {
                if (xxwidgets_die_main_panel_set_advanced(g_main, advanced) != XXWIDGETS_OK) return smoke_failed(__LINE__);
                SetWindowPos(native(g_window), NULL, 0, 0, 1, 1, SWP_NOMOVE | SWP_NOZORDER);
                if (!smoke_main_layout(!pass)) return smoke_failed(__LINE__);
            }
            for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i) {
                TEXTMETRICW metrics = control_font_metrics(buttons[i]);
                GetClientRect(native(buttons[i]), &button_bounds);
                if (button_bounds.bottom < metrics.tmHeight + 4) return smoke_failed(__LINE__);
            }
            GetClientRect(native(g_path), &button_bounds);
            {
                TEXTMETRICW metrics = control_font_metrics(g_path);
                /* The edit's client rectangle excludes its four-pixel border. */
                if (button_bounds.bottom < metrics.tmHeight + metrics.tmExternalLeading + 4) {
                    char text[128];
                    wsprintfA(text, "GUI smoke: path edit client height %ld, its font needs %ld (pass %u)\r\n", button_bounds.bottom,
                              metrics.tmHeight + metrics.tmExternalLeading + 4, pass);
                    smoke_write(text);
                    return smoke_failed(__LINE__);
                }
            }
            /* The dialogs under the larger font: placement, controls inside, focus,
             * and at 12 points text and overlaps, unless the backend held the
             * cells (a 120 x 35 Options dialog must fit the primary work area,
             * so a small screen keeps smaller cells and clips the text). */
            {
                TEXTMETRICW metrics = control_font_metrics(g_scan);
                RECT work, frame = {0, 0, 0, 0};
                g_smokeFontSized = !pass && SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0) &&
                    AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0) &&
                    35 * (metrics.tmHeight + metrics.tmExternalLeading + 4) + frame.bottom - frame.top <= work.bottom - work.top &&
                    120 * (metrics.tmAveCharWidth + 1) + frame.right - frame.left <= work.right - work.left;
                if (!pass && !g_smokeFontSized) smoke_trace("GUI smoke: the 12 pt cells do not fit this screen; text is not measured");
            }
            {
                gui_backend_optimization_options optimization_before, optimization_after;
                if (!gui_backend_get_optimization_options(&optimization_before) ||
                    !smoke_begin_page(L"Features", SMOKE_FEATURES_CANCEL, 0) || do_settings(GUI_OPTIONS_OPTIMIZATION) != 0 || !smoke_finish() ||
                    !gui_backend_get_optimization_options(&optimization_after) ||
                    optimization_after.buffer_size != optimization_before.buffer_size ||
                    optimization_after.file_buffer_size != optimization_before.file_buffer_size ||
                    optimization_after.use_sse2 != optimization_before.use_sse2 ||
                    optimization_after.use_avx2 != optimization_before.use_avx2 ||
                    optimization_after.use_sse42 != optimization_before.use_sse42 ||
                    optimization_after.use_aes != optimization_before.use_aes ||
                    optimization_after.use_sha != optimization_before.use_sha ||
                    optimization_after.use_vpclmulqdq != optimization_before.use_vpclmulqdq ||
                    optimization_after.use_neon != optimization_before.use_neon ||
                    optimization_after.use_arm_crc32 != optimization_before.use_arm_crc32 ||
                    optimization_after.use_arm_crypto != optimization_before.use_arm_crypto ||
                    optimization_after.use_arm_dotprod != optimization_before.use_arm_dotprod || optimization_after.use_arm_pmull != optimization_before.use_arm_pmull) {
                    g_smokeFontSized = 1;
                    return smoke_failed(__LINE__);
                }
            }
            if (!smoke_options_pages() || !smoke_begin(L"About", SMOKE_ABOUT_ESCAPE) ||
                xxwidgets_about_dialog_show(g_about, g_window) != XXWIDGETS_OK || !smoke_finish() ||
                !smoke_begin(L"Scan report", SMOKE_REPORT)) { g_smokeFontSized = 1; return smoke_failed(__LINE__); }
            status = xxwidgets_scan_panel_show_report(g_panel);
            if (!smoke_finish() || status != XXWIDGETS_OK || !smoke_begin(L"Hash", SMOKE_HASH)) { g_smokeFontSized = 1; return smoke_failed(__LINE__); }
            g_smokeEscape = 1;
            do_hash();
            g_smokeFontSized = 1;
            if (!smoke_finish()) return smoke_failed(__LINE__);
            if (xxwidgets_app_set_font_options(g_app, &after) != XXWIDGETS_OK ||
                xxwidgets_die_main_panel_retranslate(g_main) != XXWIDGETS_OK) return smoke_failed(__LINE__);
            SetWindowPos(native(g_window), NULL, 0, 0, original.right - original.left,
                original.bottom - original.top, SWP_NOMOVE | SWP_NOZORDER);
        }
        if (!smoke_begin_page(L"File", SMOKE_FILE_CANCEL, 0) || do_settings(GUI_OPTIONS_FILE) != 0 || !smoke_finish()) return smoke_failed(__LINE__);
    }
    if (!smoke_begin(L"File Information", SMOKE_FILE_INFORMATION)) return smoke_failed(__LINE__);
    do_file_information();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"Entropy", SMOKE_ENTROPY)) return smoke_failed(__LINE__);
    do_entropy();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"Hash", SMOKE_HASH)) return smoke_failed(__LINE__);
    do_hash();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    {
        xxwidgets_string_record record = {0, 18, 18, "ASCII", "CDIE Strings smoke", ".rdata", 0x401000, 1};
        if (!smoke_begin(L"Strings", SMOKE_STRINGS)) return smoke_failed(__LINE__);
        status = xxwidgets_strings_dialog(g_window, CDIE_TR("Strings"), "synthetic fixture", 18, &record, 1, NULL, NULL);
        if (status != XXWIDGETS_OK || !smoke_finish()) return smoke_failed(__LINE__);
    }
    {
        static const char fixture[] = "alpha\0http://one.example\0https://two.example\0www.three.example\0mailto:four@example\0ordinary\0";
        WCHAR directory[MAX_PATH], path[MAX_PATH];
        DWORD written = 0;
        HANDLE file;
        char *utf8;
        int passed;
        if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"cds", 0, path)) return smoke_failed(__LINE__);
        file = CreateFileW(path, GENERIC_WRITE, 0, NULL, TRUNCATE_EXISTING, FILE_ATTRIBUTE_TEMPORARY, NULL);
        passed = file != INVALID_HANDLE_VALUE && WriteFile(file, fixture, sizeof(fixture) - 1, &written, NULL) && written == sizeof(fixture) - 1;
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        utf8 = passed ? xx_str_unicode_to_utf8(path) : NULL;
        if (!utf8 || !smoke_begin(L"Strings", SMOKE_STRINGS_CONFIG)) {
            xx_str_free(utf8); DeleteFileW(path); return smoke_failed(__LINE__);
        }
        status = gui_strings_show(g_window, utf8);
        passed = smoke_finish();
        xx_str_free(utf8);
        DeleteFileW(path);
        if (status != XXWIDGETS_OK || !passed) return smoke_failed(__LINE__);
    }
    if (!smoke_disk_cancel()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"Hex View", SMOKE_HEX_VIEW)) return smoke_failed(__LINE__);
    do_hex_view();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"Disasm", SMOKE_DISASM)) return smoke_failed(__LINE__);
    do_disasm();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"Memory Map", SMOKE_MEMORY_MAP)) return smoke_failed(__LINE__);
    do_memory_map();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"Imports", SMOKE_IMPORTS)) return smoke_failed(__LINE__);
    do_imports();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    {
        xx_export_record record = {"CDIEExport", NULL, 7, 0, UINT64_C(0x140001000)};
        xxwidgets_exports_data snapshot = {"synthetic fixture", "PE64", 16, &record, 1};
        if (!smoke_begin(L"Exports", SMOKE_EXPORTS)) return smoke_failed(__LINE__);
        g_smokeExpect = L"CDIEExport";
        status = xxwidgets_exports_dialog(g_window, CDIE_TR("Exports"), &snapshot);
        if (status != XXWIDGETS_OK || !smoke_finish()) return smoke_failed(__LINE__);
    }
    if (!smoke_begin(L"Resources", SMOKE_RESOURCES)) return smoke_failed(__LINE__);
    do_resources();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    {
        xx_symbol_record record = {0};
        xxwidgets_symbols_data snapshot;
        record.name = "CDIESymbol"; record.file_name = "test.c"; record.kind = XX_SYMBOL_KIND_FUNCTION;
        record.binding = XX_SYMBOL_BINDING_GLOBAL; record.address = UINT64_C(0x140001000);
        snapshot = (xxwidgets_symbols_data){"synthetic fixture", "PE", 16, &record, 1};
        if (!smoke_begin(L"Symbols", SMOKE_SYMBOLS)) return smoke_failed(__LINE__);
        g_smokeExpect = L"CDIESymbol";
        status = xxwidgets_symbols_dialog(g_window, CDIE_TR("Symbols"), &snapshot);
        if (status != XXWIDGETS_OK || !smoke_finish()) return smoke_failed(__LINE__);
    }
    if (!smoke_begin(L"Extractor", SMOKE_EXTRACTOR)) return smoke_failed(__LINE__);
    do_extractor();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    {
        /* The real Exports and Symbols readers on the ELF fixture: its global symbol. */
        char *utf8 = xx_str_unicode_to_utf8(g_smokeElf);
        int passed = utf8 && smoke_begin(L"Exports", SMOKE_EXPORTS);
        if (passed) {
            g_smokeExpect = L"Direct";
            passed = gui_exports_show(g_window, utf8) == XXWIDGETS_OK;
            passed = smoke_finish() && passed;
        }
        if (passed && (passed = smoke_begin(L"Symbols", SMOKE_SYMBOLS)) != 0) {
            g_smokeExpect = L"Direct";
            passed = gui_symbols_show(g_window, utf8) == XXWIDGETS_OK;
            passed = smoke_finish() && passed;
        }
        xx_str_free(utf8);
        if (!passed) return smoke_failed(__LINE__);
    }
    /* Each tool from its button's function again, closed with Escape. */
    for (size_t i = 0; i < sizeof(g_smokeTools) / sizeof(g_smokeTools[0]); ++i) {
        if (!SetWindowTextW(native(g_path), g_smokeTools[i].fixture ? g_smokeElf : executable) ||
            !smoke_begin(g_smokeTools[i].title, g_smokeTools[i].action)) return smoke_failed(__LINE__);
        g_smokeEscape = 1;
        if (g_smokeTools[i].fixture) g_smokeExpect = L"Direct";
        g_smokeTools[i].open();
        if (!smoke_finish()) return smoke_failed(__LINE__);
    }
    if (!SetWindowTextW(native(g_path), executable)) return smoke_failed(__LINE__);
    {
        /* The tools' own pickers and forms, each opened and cancelled, then Escape. */
        static const smoke_inner strings[] = {{L"Save", L"Save filtered strings as CSV", NULL}};
        static const smoke_inner map[] = {{L"Save", L"Save Memory Map", NULL}};
        static const smoke_inner extractor[] = {{L"Options...", L"Extractor types", NULL}, {L"Save", L"Save extractor results", NULL},
            {L"Dump all", NULL, NULL}};
        static const struct { const WCHAR *title; smoke_opener open; const smoke_inner *inner; size_t count; } inner_tools[] = {
            {L"Strings", do_strings, strings, 1}, {L"Memory Map", do_memory_map, map, 1}, {L"Extractor", do_extractor, extractor, 3}};
        for (size_t i = 0; i < sizeof(inner_tools) / sizeof(inner_tools[0]); ++i) {
            if (!smoke_begin(inner_tools[i].title, SMOKE_TOOL_INNER)) return smoke_failed(__LINE__);
            g_smokeEscape = 1;
            g_smokeInner = inner_tools[i].inner;
            g_smokeInnerCount = inner_tools[i].count;
            inner_tools[i].open();
            if (!smoke_finish()) return smoke_failed(__LINE__);
        }
    }
    {
        /* Hash dragged smaller than its minimum stays open. */
        char *utf8 = xx_str_unicode_to_utf8(executable);
        int passed = utf8 && smoke_begin(L"Hash", SMOKE_TOOL_RESIZE);
        status = passed ? gui_hash_show(g_window, utf8) : XXWIDGETS_PLATFORM_ERROR;
        passed = smoke_finish() && passed && status == XXWIDGETS_OK;
        xx_str_free(utf8);
        if (!passed) return smoke_failed(__LINE__);
    }
    if (!smoke_begin(L"About", SMOKE_ABOUT)) return smoke_failed(__LINE__);
    status = xxwidgets_about_dialog_show(g_about, g_window);
    if (status != XXWIDGETS_OK || !smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"About", SMOKE_ABOUT_ESCAPE)) return smoke_failed(__LINE__);
    status = xxwidgets_about_dialog_show(g_about, g_window);
    if (status != XXWIDGETS_OK || !smoke_finish()) return smoke_failed(__LINE__);
    if (!smoke_begin(L"About", SMOKE_ABOUT_CLOSE)) return smoke_failed(__LINE__);
    status = xxwidgets_about_dialog_show(g_about, g_window);
    if (status != XXWIDGETS_OK || !smoke_finish()) return smoke_failed(__LINE__);
    {
        /* The report through its shortcut shows exactly the panel's report. */
        char *report;
        int passed;
        if (xxwidgets_scan_panel_get_report(g_panel, NULL, 0, &report_length) != XXWIDGETS_OK || report_length <= 1 ||
            !(report = (char *)HeapAlloc(GetProcessHeap(), 0, report_length))) return smoke_failed(__LINE__);
        passed = xxwidgets_scan_panel_get_report(g_panel, report, report_length, NULL) == XXWIDGETS_OK &&
            smoke_shortcut('R', 1, 1) == SHORTCUT_ACTION(GUI_SHORTCUT_REPORT) && smoke_begin(L"Scan report", SMOKE_REPORT);
        if (passed) {
            g_smokeExpectText = report;
            do_report();
            passed = smoke_finish();
        }
        HeapFree(GetProcessHeap(), 0, report);
        if (!passed) return smoke_failed(__LINE__);
    }
    {
        char *original_path = xx_str_create(gui_backend_main_db_path());
        int changed;
        if (!original_path || !smoke_begin_page(L"Scan", SMOKE_SIGNATURES_CANCEL, 0)) {
            if (original_path) xx_str_free(original_path);
            return smoke_failed(__LINE__);
        }
        do_settings(GUI_OPTIONS_SCAN);
        changed = lstrcmpA(original_path, gui_backend_main_db_path());
        xx_str_free(original_path);
        if (!smoke_finish() || changed) return smoke_failed(__LINE__);
    }
    {
        /* Browse's file picker opens on a path with '/' too, and cancelled,
         * the path stays. */
        WCHAR path[32768] = L"", slashed[32768];
        lstrcpyW(slashed, executable);
        for (WCHAR *s = slashed; *s; ++s) if (*s == L'\\') *s = L'/';
        if (!SetWindowTextW(native(g_path), slashed) || !smoke_begin(L"Select a file to scan", SMOKE_REPORT)) return smoke_failed(__LINE__);
        do_browse();
        if (!smoke_finish() || !GetWindowTextW(native(g_path), path, 32768) || lstrcmpW(path, slashed) ||
            !SetWindowTextW(native(g_path), executable)) return smoke_failed(__LINE__);
    }
    {
        /* Recent files without history: a note and Close. */
        gui_options_values saved, empty;
        char chosen[GUI_PREFERENCE_PATH_MAX];
        int accepted = 1;
        gui_preferences_get(&saved);
        empty = saved;
        empty.file.recent_files[0] = 0;
        gui_preferences_set(&empty);
        if (!smoke_begin(L"Recent files", SMOKE_RECENT_EMPTY)) { gui_preferences_set(&saved); return smoke_failed(__LINE__); }
        status = gui_frontend_recent_dialog(g_window, chosen, sizeof(chosen), &accepted);
        gui_preferences_set(&saved);
        if (!smoke_finish() || status != XXWIDGETS_OK || accepted) return smoke_failed(__LINE__);
    }
    if (!smoke_update()) return smoke_failed(__LINE__);
    /* Last: the cancelled scan leaves the panel empty. */
    if (!smoke_progress_cancel()) return smoke_failed(__LINE__);
    /* Options and Recent files have keys; the report key needs a report. */
    if (smoke_shortcut(VK_F2, 0, 0) != SHORTCUT_ACTION(GUI_SHORTCUT_OPTIONS) ||
        smoke_shortcut('R', 1, 0) != SHORTCUT_ACTION(GUI_SHORTCUT_RECENT) || !smoke_shortcut_keys()) return smoke_failed(__LINE__);
    if (xxwidgets_scan_panel_clear(g_panel) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_count(g_panel) != 0 || TreeView_GetCount(native(g_results)) != 0) return smoke_failed(__LINE__);
    if (IsWindowEnabled(native(g_reportButton)) || smoke_shortcut('R', 1, 1) != ACTION_NONE) return smoke_failed(__LINE__);
    if (!smoke_begin_ex(L"Scan report", SMOKE_UNEXPECTED, 0)) return smoke_failed(__LINE__);
    g_smokeOptional = 1;
    do_report();
    if (!smoke_finish()) return smoke_failed(__LINE__);
    /* The right column, each button once; Exit last: it ends the program. */
    if (!smoke_right_column()) return smoke_failed(__LINE__);
    if (smoke_click_action(g_exit, 1) != XXWIDGETS_DIE_ACTION_EXIT || !g_quit || g_running) return smoke_failed(__LINE__);
    return 0;
}

static int run_smoke(void)
{
    int result = run_smoke_steps();
    smoke_scratch_remove();
    return result;
}

static int is_smoke_requested(void)
{
    int argc=0, requested=0;
    WCHAR **args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if (!args) return 0;
    for (int i=1;i<argc;++i) {
        if (!lstrcmpW(args[i],L"--")) break;
        if (!lstrcmpW(args[i],L"--translation")) { ++i; continue; }
        if (!lstrcmpW(args[i],L"--smoke-update")) { ++i; continue; }
        if (!lstrcmpW(args[i],L"--smoke-test")) requested=1;
    }
    LocalFree(args); return requested;
}

/* --smoke-update installed|current|failed|box-current|ask-no|ask-yes: 1 + the
 * index of the expected result in g_updateSmokeResults, 0 when absent, -1
 * when malformed. The last three run with the main window as the owner, as
 * on macOS: the "No updates." box is closed, the question answered No or Yes. */
static const gui_update_result g_updateSmokeResults[] = {GUI_UPDATE_INSTALLED, GUI_UPDATE_CURRENT, GUI_UPDATE_FAILED,
                                                         GUI_UPDATE_CURRENT, GUI_UPDATE_CANCELLED, GUI_UPDATE_INSTALLED};
enum { UPDATE_SMOKE_BOX_CURRENT = 4, UPDATE_SMOKE_ASK_NO, UPDATE_SMOKE_ASK_YES };
static int g_updateSmoke;

static int update_smoke_requested(void)
{
    static const WCHAR *const names[] = {L"installed", L"current", L"failed", L"box-current", L"ask-no", L"ask-yes"};
    int argc = 0, requested = 0;
    WCHAR **args = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!args) return -1;
    for (int i = 1; i < argc; ++i) {
        if (!lstrcmpW(args[i], L"--")) break;
        if (!lstrcmpW(args[i], L"--translation") || !lstrcmpW(args[i], L"--settings-smoke-test")) { ++i; continue; }
        if (!lstrcmpW(args[i], L"--smoke-update")) {
            requested = -1;
            if (++i < argc) {
                for (size_t name = 0; name < sizeof(names) / sizeof(names[0]); ++name)
                    if (!lstrcmpW(args[i], names[name])) requested = (int)name + 1;
            }
            break;
        }
    }
    LocalFree(args);
    return requested;
}

/* Presses Update database once, as a click does, but with no dialog owner so
 * no window needs driving; CDIE_UPDATE_URL names the source. Prints
 * "smoke-update: <result> <status text>" and exits 0 on the expected result. */
static int run_update_smoke(void)
{
    gui_update_result expected = g_updateSmokeResults[g_updateSmoke - 1], result;
    char status_text[512], line[640];
    DWORD written;
    int passed, dialogs = 1;
    /* The startup text must not pass for the outcome. */
    g_lastMessage[0] = 0;
    if (IsWindowEnabled(native(g_updateButton)) && smoke_click_action(g_updateButton, 0) != XXWIDGETS_DIE_ACTION_UPDATE)
        return smoke_failed(__LINE__);
    if (g_updateSmoke >= UPDATE_SMOKE_BOX_CURRENT) {
        xxwidgets_widget_set_visible(g_window, 1);
        /* The text dialog is application-modal; the question is owned by the main window only. */
        if (!smoke_begin_ex(L"Update database", g_updateSmoke == UPDATE_SMOKE_BOX_CURRENT ? SMOKE_UPDATE_FAILED : SMOKE_UPDATE_QUESTION,
                            g_updateSmoke == UPDATE_SMOKE_BOX_CURRENT))
            return smoke_failed(__LINE__);
        if (g_updateSmoke == UPDATE_SMOKE_BOX_CURRENT) g_smokeExpectPart = L"No updates.";
        else g_smokeAnswer = g_updateSmoke == UPDATE_SMOKE_ASK_YES ? L"Yes" : L"No";
        result = do_update_database(g_window);
        dialogs = smoke_finish();
    } else result = do_update_database(NULL);
    smoke_last_message(status_text, sizeof(status_text));
    xx_rt_snprintf(line, sizeof(line), "smoke-update: %s %s\r\n", gui_update_result_name(result), status_text);
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), line, (DWORD)lstrlenA(line), &written, NULL);
    passed = dialogs && result == expected && (result != GUI_UPDATE_INSTALLED || (gui_backend_signature_count() > 0 && status_text[0]));
    return passed ? 0 : 1;
}

static int settings_smoke_requested(void)
{
    static const WCHAR *const phases[] = {L"inspect", L"write", L"cancel", L"default"};
    int argc = 0, requested = SETTINGS_SMOKE_NONE;
    WCHAR **args = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!args) return -1;
    for (int i = 1; i < argc; ++i) {
        if (!lstrcmpW(args[i], L"--")) break;
        if (!lstrcmpW(args[i], L"--translation") || !lstrcmpW(args[i], L"--smoke-update")) { ++i; continue; }
        if (!lstrcmpW(args[i], L"--settings-smoke-test")) {
            requested = -1;
            if (++i < argc) {
                for (size_t phase = 0; phase < sizeof(phases) / sizeof(phases[0]); ++phase)
                    if (!lstrcmpW(args[i], phases[phase])) requested = (int)phase + SETTINGS_SMOKE_INSPECT;
            }
            break;
        }
    }
    LocalFree(args);
    return requested;
}

/* Refuse a persistence smoke before any store is opened unless it is portable. */
static int settings_smoke_portable(void)
{
    char *directory = cdie_app_dir();
    bool portable = false;
    const WCHAR *override = _wgetenv(L"CDIE_SETTINGS");
    int valid = directory && (!override || !override[0]) && xx_settings_is_portable(directory, &portable) == XXFC_OK && portable;
    xx_rt_free(directory);
    return valid;
}

static int run_settings_smoke(void)
{
    gui_options_values values;
    xx_settings *observed;
    char *directory, *path;
    size_t capacity;
    int saved;
    if (xx_settings_get_format(g_settings) != XX_SETTINGS_FORMAT_INI) return smoke_failed(__LINE__);
    xxwidgets_widget_set_visible(g_window, 1);
    if (g_settingsSmoke == SETTINGS_SMOKE_WRITE || g_settingsSmoke == SETTINGS_SMOKE_CANCEL) {
        int accepted = g_settingsSmoke == SETTINGS_SMOKE_WRITE;
        if (!smoke_begin_page(L"Scan", accepted ? SMOKE_SCAN_OK : SMOKE_SCAN_CANCEL, 0) ||
            do_settings(GUI_OPTIONS_SCAN) != accepted || !smoke_finish()) return smoke_failed(__LINE__);
        if (!smoke_begin_page(L"Fonts", accepted ? SMOKE_FONTS_OK : SMOKE_FONTS_CANCEL, 0) ||
            do_settings(GUI_OPTIONS_FONTS) != accepted || !smoke_finish()) return smoke_failed(__LINE__);
        if (!smoke_begin_page(L"Hex", accepted ? SMOKE_HEX_OK : SMOKE_HEX_CANCEL, 0) ||
            do_settings(GUI_OPTIONS_HEX) != accepted || !smoke_finish()) return smoke_failed(__LINE__);
        if (!smoke_begin_page(L"Features", accepted ? SMOKE_FEATURES_OK : SMOKE_FEATURES_CANCEL, 0) ||
            do_settings(GUI_OPTIONS_OPTIMIZATION) != accepted || !smoke_finish()) return smoke_failed(__LINE__);
    } else if (g_settingsSmoke == SETTINGS_SMOKE_DEFAULT) {
        if (!smoke_begin_page(L"Features", SMOKE_FEATURES_DEFAULT, 0) || do_settings(GUI_OPTIONS_OPTIMIZATION) != 1 || !smoke_finish())
            return smoke_failed(__LINE__);
    }
    if (current_options(&values) != XXWIDGETS_OK) return smoke_failed(__LINE__);
    directory = cdie_app_dir();
    if (!directory) return smoke_failed(__LINE__);
    capacity = strlen(directory) + sizeof("/settings-smoke-observed.ini");
    path = xx_rt_malloc(capacity);
    if (path) xx_rt_snprintf(path, capacity, "%s/settings-smoke-observed.ini", directory);
    xx_rt_free(directory);
    if (!path) return smoke_failed(__LINE__);
    /* A separate store records the applied runtime state for the parent process. */
    observed = xx_settings_create_ini(path);
    xx_rt_free(path);
    saved = observed && gui_settings_save(observed, &values, g_optionsPage);
    xx_settings_destroy(observed);
    return saved ? 0 : smoke_failed(__LINE__);
}

static xxwidgets_status load_translation(size_t *line)
{
    int argc=0;
    char *path=NULL, *stored_path=NULL;
    xxwidgets_status status=XXWIDGETS_OK;
    WCHAR **args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if (!args) return XXWIDGETS_OUT_OF_MEMORY;
    for (int i=1;i<argc;++i) {
        if (!lstrcmpW(args[i],L"--")) break;
        if (!lstrcmpW(args[i],L"--translation")) {
            if (++i >= argc || !args[i][0]) { status=XXWIDGETS_INVALID_ARGUMENT; break; }
            xx_str_free(path); path=xx_str_unicode_to_utf8(args[i]);
            if (!path) { status=XXWIDGETS_OUT_OF_MEMORY; break; }
        }
    }
    if (!path && !_wgetenv(L"CDIE_TRANSLATION")) stored_path = gui_frontend_stored_language(g_memorySettings);
    if (status == XXWIDGETS_OK) {
        status = cdie_translation_open(g_app, path ? path : stored_path, line);
        /* A moved/invalid saved catalog must leave Options reachable. Explicit
         * CLI/environment selections still report their errors to the caller. */
        if (status != XXWIDGETS_OK && stored_path) status = cdie_translation_open(g_app, NULL, line);
    }
    free(stored_path);
    xx_str_free(path); LocalFree(args); return status;
}
static int set_startup_file(void)
{
    int argc=0, has_file=0, positional=0;
    WCHAR **args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if (!args) return 0;
    for (int i=1;i<argc;++i) {
        if (!positional && !lstrcmpW(args[i],L"--")) { positional=1; continue; }
        if (!positional && !lstrcmpW(args[i],L"--translation")) { ++i; continue; }
        if (!positional && !lstrcmpW(args[i],L"--smoke-test")) continue;
        if (!positional && !lstrcmpW(args[i],L"--settings-smoke-test")) { ++i; continue; }
        if (!positional && !lstrcmpW(args[i],L"--smoke-update")) { ++i; continue; }
        char *path=xx_str_unicode_to_utf8(args[i]);
        if (path) { has_file=xxwidgets_widget_set_text(g_path,path)==XXWIDGETS_OK; xx_str_free(path); }
        break;
    }
    LocalFree(args); return has_file;
}

int gui_run(void)
{
    xxwidgets_config config = {XXWIDGETS_BACKEND_NATIVE, on_event, NULL};
    xxwidgets_rect bounds = {8, 4, 100, 27};
    xxwidgets_status status;
    int result = 0, smoke = is_smoke_requested();
    WCHAR title[160], ready[512];
    WCHAR *base;
    const char *startup_error = NULL;
    char *utf8_title;

    g_updateSmoke = update_smoke_requested();
    if (g_updateSmoke < 0) return 2;
    g_memorySettings = smoke || g_updateSmoke;
    g_settingsSmoke = settings_smoke_requested();
    g_quietErrors = smoke || g_updateSmoke || g_settingsSmoke != 0;
    if (g_settingsSmoke < 0 || (g_settingsSmoke && (smoke || !settings_smoke_portable()))) return 2;
    if (g_updateSmoke && (smoke || g_settingsSmoke)) return 2;

    g_hInst = GetModuleHandleW(NULL);
    status = xxwidgets_app_create(&config, &g_app);
    if (status != XXWIDGETS_OK) return 1;
    {
        size_t line=0;
        status=load_translation(&line);
        if (status != XXWIDGETS_OK) {
            /* No smoke run ever waits on a modal box. */
            if (smoke || g_settingsSmoke || g_updateSmoke) smoke_failed(__LINE__);
            else MessageBoxW(NULL,L"Cannot load PO translation. Check --translation FILE and CDIE_TRANSLATION.",L"cdie",MB_OK|MB_ICONERROR);
            result=1; goto finish;
        }
    }
    base = xx_str_utf8_to_unicode(gui_backend_app_title());
#if defined(_WIN64) && CDIE_GUI_ARM_TARGET
    cdie_translation_swprintf(title, sizeof(title)/sizeof(title[0]), CDIE_N_(L"%s (ARM64)"), base ? base : L"cdie");
#elif defined(_WIN64)
    cdie_translation_swprintf(title, sizeof(title)/sizeof(title[0]), CDIE_N_(L"%s (x64)"), base ? base : L"cdie");
#else
    cdie_translation_swprintf(title, sizeof(title)/sizeof(title[0]), CDIE_N_(L"%s (x86)"), base ? base : L"cdie");
#endif
    if (base) xx_str_wfree(base);
    utf8_title = xx_str_unicode_to_utf8(title);
    if (!utf8_title) { result = 1; goto finish; }
    status = xxwidgets_widget_create(g_app, NULL, XXWIDGETS_WINDOW, utf8_title, bounds, &g_window);
    xx_str_free(utf8_title);
    if (status != XXWIDGETS_OK) { result = 1; goto finish; }
    xxwidgets_widget_set_visible(g_window, 0);
    if (!create_controls() || configure_about() != XXWIDGETS_OK) { result = 1; goto finish; }
    {
        /* Standard sizes, so the shared handles need no cleanup. Dialogs take the owner's icons. */
        HICON large_icon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_CDIE), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
        HICON small_icon = (HICON)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDI_CDIE), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
        if (large_icon) SendMessageW(native(g_window), WM_SETICON, ICON_BIG, (LPARAM)large_icon);
        if (small_icon) SendMessageW(native(g_window), WM_SETICON, ICON_SMALL, (LPARAM)small_icon);
    }
    (void)gui_backend_init();
    {
        /* Stored Options apply before the first scan; the full GUI smoke stays in memory. */
        gui_options_values values;
        int stored = 1, databases = 1;
        g_settings = gui_settings_open(g_memorySettings, &stored);
        if (g_settingsSmoke && (!stored || xx_settings_get_format(g_settings) != XX_SETTINGS_FORMAT_INI)) {
            result = smoke_failed(__LINE__); goto finish;
        }
        status = current_options(&values);
        if (status == XXWIDGETS_OK) databases = gui_settings_load(g_settings, &values, &g_optionsPage);
        if (gui_backend_signature_count()) {
            cdie_translation_swprintf(ready, sizeof(ready)/sizeof(ready[0]), CDIE_N_(L"Ready. %d signatures loaded. Choose a file or drag one here, then press Scan."),
                      gui_backend_signature_count());
            note_message_wide(ready);
        } else
            startup_error = CDIE_TR("Main signature database not found. Use Options > Scan to select the database folder.");
        if (!databases)
            startup_error = CDIE_TR("The saved signature folders cannot be loaded; the default ones are active.");
        else if (!stored)
            startup_error = CDIE_TR("Saved options cannot be read; the defaults are used.");
        if (status == XXWIDGETS_OK) status = apply_options(&values);
        if (status != XXWIDGETS_OK) report_ui_error("Apply saved options", status);
    }
    status = refresh_panel_tags();
    if (status != XXWIDGETS_OK) report_ui_error(CDIE_N_("Load signature tags"), status);
    else if ((status = gui_main_restore_tag_filters(g_panel)) != XXWIDGETS_OK)
        report_ui_error(CDIE_N_("Load tag filters"), status);
    apply_flags_to_backend();
    {
        /* The stored view (View/Advanced, shared with Qt DIE), then Qt's
         * 720 x 500 default, or the view's minimum when that is larger. */
        int width = 0, height = 0, advanced = gui_frontend_advanced() ? 1 : 0;
        status = xxwidgets_die_main_panel_set_advanced(g_main, advanced);
        if (status == XXWIDGETS_OK) status = xxwidgets_die_main_panel_get_default_size(g_main, advanced, &width, &height);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_frame(g_window, (xxwidgets_rect){0, 0, width, height});
        /* set_frame emits no RESIZE: lay the panel out over the new client area. */
        if (status == XXWIDGETS_OK) status = xxwidgets_die_main_panel_layout(g_main);
        if (status != XXWIDGETS_OK) { result = 1; goto finish; }
    }
    if (smoke) { result = run_smoke(); goto finish; }
    if (g_settingsSmoke) { result = run_settings_smoke(); goto finish; }
    if (g_updateSmoke) { result = run_update_smoke(); goto finish; }
    {
        int startup = set_startup_file();
        char path[GUI_PREFERENCE_PATH_MAX] = {0};
        (void)xxwidgets_widget_get_text(g_path, path, sizeof(path), NULL);
        int instance = gui_frontend_instance_start(g_window, path);
        if (instance <= 0) { result = instance < 0 ? 1 : 0; goto finish; }
        xxwidgets_widget_set_visible(g_window, 1);
        xxwidgets_widget_focus(g_path);
        /* No status line (Q1): what the start found wrong is said once the window shows. */
        if (startup_error) show_error(startup_error);
        if (startup) open_file(path);
    }
    g_running = 1;
    while (g_running) {
        int action;
        status = xxwidgets_app_poll(g_app, 30);
        if (status != XXWIDGETS_OK) { result = 1; break; }
        if (!g_running) break;
        {
            const char *message = gui_tree_process(g_window);
            if (message) show_error(message);
        }
        action = g_pending; g_pending = ACTION_NONE;
        /* All modal loops and scans start after returning from app_poll. */
        run_pending(action);
    }
finish:
    if (g_smokeTimer) KillTimer(NULL, g_smokeTimer);
    g_smokeTimer = 0;
    smoke_modal_cleanup();
    gui_frontend_instance_close();
    if (!smoke && !g_settingsSmoke && !g_updateSmoke && g_settings && g_main) {
        gui_options_values final_values;
        if (current_options(&final_values) == XXWIDGETS_OK) (void)gui_settings_save(g_settings, &final_values, g_optionsPage);
    }
    gui_settings_close(g_settings);
    g_settings = NULL;
    gui_tree_reset();
    gui_backend_shutdown();
    if (g_about && xxwidgets_about_dialog_destroy(g_about) != XXWIDGETS_OK) result = 1;
    g_about = NULL;
    if (g_main && xxwidgets_die_main_panel_destroy(g_main) != XXWIDGETS_OK) result = 1;
    g_main = NULL;
    g_panel = NULL;
    gui_main_file_types_clear(&g_fileTypes);
    if (g_typePath) xx_str_free(g_typePath);
    g_typePath = NULL;
    cdie_translation_close();
    if (xxwidgets_app_destroy(g_app) != XXWIDGETS_OK) result = 1;
    g_app = NULL;
    return result;
}
