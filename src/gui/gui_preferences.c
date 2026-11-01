/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_options.h"
#include "gui_backend.h"
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/scan/xx_scan_color.h>
#include <string.h>
static gui_options_values preferences;
static int preferences_initialized;
void gui_options_values_init(gui_options_values *values)
{
    if (!values) return;
    memset(values, 0, sizeof(*values));
    (void)xxwidgets_scan_options_init(&values->scan.options);
    values->scan.modes = GUI_SCAN_MODES_DEFAULT;
    values->scan.scan_after_open = 1;
    values->scan.recursive = 1;
    values->scan.sort = 1;
    values->scan.highlight = 1;
    strcpy(values->scan.engine, "auto");
    xx_scan_color_options_init(&values->scan.colors, 0);
    xx_scan_color_options_init(&values->scan.dark_colors, 1);
    (void)xxwidgets_font_options_init(&values->fonts);
    (void)xxwidgets_hex_options_init(&values->hex);
    (void)xxwidgets_disasm_options_init(&values->disasm);
    strcpy(values->appearance.style, "System");
    strcpy(values->appearance.language, "System");
    values->file.save_last_directory = 1;
    values->file.save_recent_files = 1;
    values->file.save_backup = 1;
    strcpy(values->databases.main_update_url, "https://github.com/horsicq/Detect-It-Easy/releases/tag/current-database");
    strcpy(values->databases.peid_update_url, "https://github.com/horsicq/XPEID/releases/tag/peid");
    strcpy(values->databases.yara_update_url, "https://github.com/horsicq/Detect-It-Easy/releases/tag/db");
    strcpy(values->signatures.path, "$data/signatures");
    strcpy(values->info.path, "$data/info");
    strcpy(values->info.update_url, "https://github.com/horsicq/XInfoDB/releases/tag/info");
}
void gui_preferences_get(gui_options_values *values)
{
    if (!values) return;
    if (!preferences_initialized) {
        gui_options_values_init(&preferences);
        preferences_initialized = 1;
    }
    *values = preferences;
}
void gui_preferences_set(const gui_options_values *values)
{
    if (!values) return;
    preferences = *values;
    if (!preferences.file.save_last_directory) memset(preferences.file.last_directory,0,sizeof(preferences.file.last_directory));
    if (!preferences.file.save_recent_files) memset(preferences.file.recent_files,0,sizeof(preferences.file.recent_files));
    preferences_initialized = 1;
}
