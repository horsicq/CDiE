/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_OPTIONS_H
#define CDIE_GUI_OPTIONS_H

#include <xxwidgets/xxwidgets_options_pages.h>
#include "gui_scan_options.h"
#include "gui_preferences.h"
#include <xxwidgets/xxwidgets_disasm_options.h>

/* Pages of the shared Options window, in Detect It Easy's order. */
typedef enum gui_options_page_id {
    GUI_OPTIONS_FILE = 0,
    GUI_OPTIONS_FONTS,
    GUI_OPTIONS_SCAN,
    GUI_OPTIONS_SIGNATURES,
    GUI_OPTIONS_HEX,
    GUI_OPTIONS_OPTIMIZATION,
    GUI_OPTIONS_APPEARANCE,
    GUI_OPTIONS_DISASM,
    GUI_OPTIONS_ONLINE,
    GUI_OPTIONS_INFO,
    GUI_OPTIONS_PAGE_COUNT
} gui_options_page_id;
#define GUI_OPTIONS_PAGE(id) (1u << (id))

/* Settings the frontend owns: filled before the dialog, applied after OK. */
typedef struct gui_options_values {
    gui_scan_page_values scan; /* Flags, databases and expansion modes. */
    xxwidgets_font_options fonts;
    xxwidgets_hex_options hex;
    xxwidgets_disasm_options disasm;
    gui_appearance_preferences appearance;
    gui_file_preferences file;
    gui_database_preferences databases;
    gui_signatures_preferences signatures;
    gui_online_preferences online;
    gui_info_preferences info;
} gui_options_values;

typedef struct gui_options_host {
    unsigned int pages;     /* GUI_OPTIONS_PAGE bits this frontend offers. */
    const char *executable; /* File page: absolute UTF-8 program path (Windows). */
    /* Folder chooser for Signatures; NULL disables Browse. Same contract as
     * xxwidgets_database_paths_callbacks.browse. */
    xxwidgets_status (*browse)(xxwidgets_widget *dialog, const char *initial, char **path, int *accepted, void *user);
    void *user;
} gui_options_host;

/* Initialize every preference before overlaying current frontend state. The
 * process copy stores preferences with no independent frontend getter. */
void gui_options_values_init(gui_options_values *values);
void gui_preferences_get(gui_options_values *values);
void gui_preferences_set(const gui_options_values *values);

/* Shows Options on *page and records the last selected category. Forms edit
 * a private copy. OK validates every category before the backend commit, which
 * runs with the Scan page's apply (the last page's without one), so a database
 * folder that is missing or does not load is reported on the Scan page;
 * Cancel/Escape/close preserve values and database/optimization state.
 * databases_reloaded reports a successfully committed database change. */
xxwidgets_status gui_options_dialog(xxwidgets_widget *owner, const gui_options_host *host, gui_options_page_id *page,
                                    gui_options_values *values, int *accepted, int *databases_reloaded);

#endif
