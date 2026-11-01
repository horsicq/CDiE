/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_SCAN_OPTIONS_H
#define CDIE_GUI_SCAN_OPTIONS_H

#include <xxwidgets/xxwidgets_scan_options.h>
#include <xxwidgets/xxwidgets_options_pages.h>
#include <xxfclib/scan/xx_scan_color.h>
#include "gui_preferences.h"

/* Options-dialog page: the xxwidgets scan form plus the four expansion modes.
 * OK writes both into values; Default restores the backend's initial ones. */
typedef struct gui_scan_page_values {
    xxwidgets_scan_options options;
    unsigned int modes;
    int scan_after_open, recursive, profiling, sort, use_cache, highlight;
    char engine[16];
    xx_scan_color_options colors, dark_colors;
} gui_scan_page_values;
xxwidgets_status gui_scan_options_page(xxwidgets_options_page *page, const char *title, gui_scan_page_values *values);
typedef struct gui_scan_options_page_context {
    gui_scan_page_values *values;
    gui_database_preferences *databases;
    xxwidgets_database_paths_callbacks callbacks;
    void *user;
} gui_scan_options_page_context;
xxwidgets_status gui_scan_options_page_ex(xxwidgets_options_page *page, const char *title, gui_scan_options_page_context *context);
/* The translated sentence for a database the Scan page cannot apply: slot is
 * GUI_DB_MAIN or GUI_DB_CUSTOM; missing says its folder does not exist, otherwise
 * that no signatures load from path ("" = the default folder). The page's own
 * check uses it on OK; Options uses it for a failed commit. */
void gui_scan_options_database_error(int slot, int missing, const char *path, char *error, size_t capacity);

#endif
