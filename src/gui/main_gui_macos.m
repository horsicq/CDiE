/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Cocoa application integration around the shared xxwidgets scan panel.
 * All engine calls and widget updates run on the main thread.
 */
#import <AppKit/AppKit.h>
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_scan_panel.h>
#include <xxwidgets/xxwidgets_combobox.h>
#include <xxwidgets/xxwidgets_font_options.h>
#include <xxwidgets/xxwidgets_process.h>
#include <xxfclib/strings/xx_string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "gui_backend.h"
#include "app/cdie_translation.h"
#include "gui_scan_options.h"
#include "gui_options.h"
#include "gui_frontend_preferences.h"
#include "gui_about.h"
#include "gui_settings.h"
#include "gui_tree.h"
#include "gui_file_information.h"
#include "gui_entropy.h"
#include "gui_hash.h"
#include "gui_strings.h"
#include "gui_hex_view.h"
#include "gui_disasm.h"
#include "gui_memory_map.h"
#include "gui_imports.h"
#include "gui_exports.h"
#include "gui_resources.h"
#include "gui_symbols.h"
#include "gui_extractor.h"
#include "gui_extractor_source.h"
#include "gui_update.h"
#include "gui_error.h"

enum { ACTION_NONE, ACTION_SCAN, ACTION_OPEN, ACTION_REPORT, ACTION_OPTIONS,
       ACTION_DATABASES, ACTION_ABOUT, ACTION_FILE_INFORMATION, ACTION_ENTROPY, ACTION_HASH, ACTION_STRINGS, ACTION_HEX_VIEW, ACTION_DISASM, ACTION_MEMORY_MAP, ACTION_IMPORTS, ACTION_EXPORTS, ACTION_RESOURCES, ACTION_SYMBOLS, ACTION_EXTRACTOR, ACTION_RECENT, ACTION_UPDATE };
static xxwidgets_app *g_app;
static xxwidgets_widget *g_window, *g_path, *g_browse, *g_status, *g_options, *g_update;
static xxwidgets_widget *g_file_information, *g_entropy, *g_hash, *g_strings, *g_hex_view, *g_disasm, *g_memory_map, *g_imports, *g_exports, *g_resources, *g_symbols, *g_extractor;
static xxwidgets_scan_panel *g_panel;
static int g_running = 1, g_pending, g_layout;
static gui_options_page_id g_options_page = GUI_OPTIONS_SCAN;
static xx_settings *g_settings;
static NSString *g_type_path;

static NSString *string(const char *text)
{
    return text ? ([NSString stringWithUTF8String:text] ?: @"") : @"";
}
static NSString *cdie_ns_tr(NSString *source) { return string(CDIE_TR([source UTF8String])); }
static NSString *cdie_ns_fmt(NSString *source) { return string(CDIE_FMT([source UTF8String])); }

static xxwidgets_widget *control(xxwidgets_scan_panel_control_id which)
{
    return xxwidgets_scan_panel_control(g_panel, which);
}
static xxwidgets_status refresh_panel_tags(void)
{
    char *tags = gui_backend_used_tags();
    xxwidgets_status status;
    if (!tags) return XXWIDGETS_OUT_OF_MEMORY;
    status = xxwidgets_scan_panel_set_available_tags(g_panel, tags);
    gui_backend_free(tags);
    return status;
}
static xxwidgets_status apply_panel_tag_filters(void)
{
    size_t include_size = 0, exclude_size = 0;
    char *include_tags = NULL, *exclude_tags = NULL;
    xxwidgets_status status = xxwidgets_scan_panel_get_include_tags(g_panel, NULL, 0, &include_size);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_get_exclude_tags(g_panel, NULL, 0, &exclude_size);
    if (status != XXWIDGETS_OK) return status;
    include_tags = malloc(include_size ? include_size : 1);
    exclude_tags = malloc(exclude_size ? exclude_size : 1);
    if (!include_tags || !exclude_tags) {
        free(include_tags); free(exclude_tags);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    status = xxwidgets_scan_panel_get_include_tags(g_panel, include_tags, include_size ? include_size : 1, NULL);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_get_exclude_tags(g_panel, exclude_tags, exclude_size ? exclude_size : 1, NULL);
    if (status == XXWIDGETS_OK && !gui_backend_set_tag_filters(include_tags, exclude_tags))
        status = XXWIDGETS_OUT_OF_MEMORY;
    free(include_tags); free(exclude_tags);
    return status;
}
/* The operation name is translated as on Windows; UTF-8 throughout (an
 * NSString %s would read a translated name in the system encoding). The
 * reason is gui_error.h's sentence, never the status name. */
static void report_ui_error(const char *operation, xxwidgets_status status)
{
    char message[512];
    cdie_translation_snprintf(message, sizeof(message), CDIE_N_("%s: %s"), CDIE_TR(operation), gui_error_status_text(status));
    xxwidgets_widget_set_text(g_status, message);
}
static void ready(void)
{
    NSString *message = gui_backend_signature_count() ?
        [NSString stringWithFormat:cdie_ns_fmt(@"Ready. %d signatures loaded. Choose a file or drop one into the window."), gui_backend_signature_count()] :
        cdie_ns_tr(@"No signature database loaded. Choose its folder in Options > Scan.");
    xxwidgets_widget_set_text(g_status, [message UTF8String]);
}
static void open_path(NSString *path)
{
    xxwidgets_widget_set_text(g_path, [path UTF8String]);
    gui_frontend_remember_file([path UTF8String]);
    g_pending = gui_frontend_scan_after_open() ? ACTION_SCAN : ACTION_NONE;
}
static void layout(void)
{
    xxwidgets_rect bounds;
    if (!g_panel || xxwidgets_widget_get_rect(g_window, &bounds) != XXWIDGETS_OK) return;
    int width = bounds.width, height = bounds.height;
    xxwidgets_widget_set_rect(g_path, (xxwidgets_rect){2, 1, width - 17, 2});
    xxwidgets_widget_set_rect(g_browse, (xxwidgets_rect){width - 13, 1, 11, 2});
    /* At least the panel's minimum height (14) in a short window. */
    xxwidgets_scan_panel_set_rect(g_panel, (xxwidgets_rect){2, 4, width - 4, height - 13 < 14 ? 14 : height - 13});
    xxwidgets_widget_set_rect(g_options, (xxwidgets_rect){2, height - 8, 16, 2});
    xxwidgets_widget_set_rect(g_imports, (xxwidgets_rect){20, height - 8, 18, 2});
    xxwidgets_widget_set_rect(g_exports, (xxwidgets_rect){40, height - 8, 18, 2});
    xxwidgets_widget_set_rect(g_resources, (xxwidgets_rect){60, height - 8, 22, 2});
    xxwidgets_widget_set_rect(g_symbols, (xxwidgets_rect){84, height - 8, 18, 2});
    xxwidgets_widget_set_rect(g_extractor, (xxwidgets_rect){104, height - 8, 22, 2});
    xxwidgets_widget_set_rect(g_update, (xxwidgets_rect){128, height - 8, 16, 2});
    xxwidgets_widget_set_rect(g_file_information, (xxwidgets_rect){2, height - 5, 26, 2});
    xxwidgets_widget_set_rect(g_entropy, (xxwidgets_rect){30, height - 5, 16, 2});
    xxwidgets_widget_set_rect(g_hash, (xxwidgets_rect){48, height - 5, 14, 2});
    xxwidgets_widget_set_rect(g_strings, (xxwidgets_rect){64, height - 5, 18, 2});
    xxwidgets_widget_set_rect(g_hex_view, (xxwidgets_rect){84, height - 5, 18, 2});
    xxwidgets_widget_set_rect(g_disasm, (xxwidgets_rect){104, height - 5, 18, 2});
    xxwidgets_widget_set_rect(g_memory_map, (xxwidgets_rect){124, height - 5, 20, 2});
    xxwidgets_widget_set_rect(g_status, (xxwidgets_rect){2, height - 2, width - 4, 1});
}
static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    (void)app; (void)user;
    if (event->type == XXWIDGETS_EVENT_CLOSE && event->widget == g_window) g_running = 0;
    else if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == g_window) g_layout = 1;
    else if (event->type == XXWIDGETS_EVENT_SHORTCUT) g_pending = event->value;
    else if (event->type == XXWIDGETS_EVENT_DROP_FILES && event->widget == g_window && event->path_count) open_path(string(event->paths[0]));
    else if (event->type == XXWIDGETS_EVENT_CHANGE && event->widget == g_path) {
        xxwidgets_scan_panel_clear(g_panel);
        xxwidgets_widget_set_enabled(control(XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0);
    } else if (event->type == XXWIDGETS_EVENT_CHANGE &&
               (event->widget == control(XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS) ||
                event->widget == control(XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS))) {
        xxwidgets_status status = apply_panel_tag_filters();
        if (status != XXWIDGETS_OK) report_ui_error(CDIE_N_("Read tag filters"), status);
    } else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == control(XXWIDGETS_SCAN_PANEL_FILE_TYPE)) g_pending = ACTION_SCAN;
    else if (gui_tree_event(g_panel, event)) {
        /* S or ? on a result row: shown after polling returns. */
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == g_browse) g_pending = ACTION_OPEN;
        else if (event->widget == control(XXWIDGETS_SCAN_PANEL_SCAN)) g_pending = ACTION_SCAN;
        else if (event->widget == control(XXWIDGETS_SCAN_PANEL_REPORT)) g_pending = ACTION_REPORT;
        else if (event->widget == g_options) g_pending = ACTION_OPTIONS;
        else if (event->widget == g_file_information) g_pending = ACTION_FILE_INFORMATION;
        else if (event->widget == g_entropy) g_pending = ACTION_ENTROPY;
        else if (event->widget == g_hash) g_pending = ACTION_HASH;
        else if (event->widget == g_strings) g_pending = ACTION_STRINGS;
        else if (event->widget == g_hex_view) g_pending = ACTION_HEX_VIEW;
        else if (event->widget == g_disasm) g_pending = ACTION_DISASM;
        else if (event->widget == g_memory_map) g_pending = ACTION_MEMORY_MAP;
        else if (event->widget == g_imports) g_pending = ACTION_IMPORTS;
        else if (event->widget == g_exports) g_pending = ACTION_EXPORTS;
        else if (event->widget == g_resources) g_pending = ACTION_RESOURCES;
        else if (event->widget == g_symbols) g_pending = ACTION_SYMBOLS;
        else if (event->widget == g_extractor) g_pending = ACTION_EXTRACTOR;
        else if (event->widget == g_update) g_pending = ACTION_UPDATE;
    }
}
static void collect_type(int type, const char *name, void *user)
{
    [(NSMutableArray *)user addObject:@{@"id": @(type), @"name": string(type == 0 ? CDIE_TR("Automatic") : name)}];
}
static int refresh_types(NSString *path)
{
    NSMutableArray *types = [NSMutableArray array];
    xx_var selected = {0};
    xxwidgets_widget *combo = control(XXWIDGETS_SCAN_PANEL_FILE_TYPE);
    if ([path isEqualToString:g_type_path]) xxwidgets_combobox_get_current(combo, &selected);
    if (!gui_backend_file_types([path UTF8String], collect_type, types)) return 0;
    size_t count = [types count];
    xx_meta_string *records = calloc(count, sizeof(*records));
    xx_str_w_s *labels = calloc(count, sizeof(*labels));
    if (!records || !labels) { free(records); free(labels); return 0; }
    int index = 0, ok = 1;
    for (size_t i = 0; i < count; ++i) {
        NSDictionary *item = types[i];
        labels[i].data = xx_str_utf8_to_unicode([item[@"name"] UTF8String]);
        if (!labels[i].data) { ok = 0; break; }
        labels[i].length = wcslen(labels[i].data);
        labels[i].capacity = labels[i].length + 1;
        labels[i].is_view = true;
        records[i].meta_string = &labels[i];
        records[i].var.type = XX_VAR_TYPE_UINT64;
        records[i].var.val.u64 = [item[@"id"] unsignedLongLongValue];
        if (selected.type == XX_VAR_TYPE_UINT64 && records[i].var.val.u64 == selected.val.u64) index = (int)i;
    }
    if (ok) ok = xxwidgets_combobox_set_records(combo, records, count) == XXWIDGETS_OK;
    if (ok) ok = xxwidgets_widget_set_value(combo, index) == XXWIDGETS_OK;
    for (size_t i = 0; i < count; ++i) if (labels[i].data) xx_str_wfree(labels[i].data);
    free(labels); free(records);
    if (ok) { [g_type_path release]; g_type_path = [path copy]; }
    return ok;
}
static int do_scan(void)
{
    size_t length = 0;
    if (xxwidgets_widget_get_text(g_path, NULL, 0, &length) != XXWIDGETS_OK) return 0;
    char *text = malloc(length ? length : 1);
    if (!text) return 0;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, text, length, NULL);
    NSString *path = status == XXWIDGETS_OK ? [string(text) stringByExpandingTildeInPath] : nil;
    free(text);
    if (!path) return 0;
    xxwidgets_widget *combo = control(XXWIDGETS_SCAN_PANEL_FILE_TYPE);
    xxwidgets_scan_panel_clear(g_panel);
    if (![path length]) { NSBeep(); xxwidgets_widget_focus(g_path); return 0; }
    if (!refresh_types(path)) {
        xxwidgets_widget_set_enabled(combo, 0);
        xxwidgets_widget_set_text(g_status, CDIE_TR("Cannot open this file. Check the path and permissions."));
        return 0;
    }
    xx_var type_id = {0};
    unsigned int flags = 0, databases = 0;
    if (xxwidgets_combobox_get_current(combo, &type_id) != XXWIDGETS_OK || type_id.type != XX_VAR_TYPE_UINT64 ||
        xxwidgets_scan_panel_get_flags(g_panel, &flags) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_get_databases(g_panel, &databases) != XXWIDGETS_OK) return 0;
    {
        xxwidgets_status status = apply_panel_tag_filters();
        if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read tag filters"), status); return 0; }
    }
    gui_backend_set_flags(flags & 1, flags & 2, flags & 4, flags & 8, flags & 16, flags & 32);
    gui_backend_set_databases((databases & XXWIDGETS_SCAN_DATABASE_CUSTOM) != 0);
    xxwidgets_widget_set_text(g_status, CDIE_TR("Scanning…"));
    xxwidgets_widget_set_enabled(g_window, 0);
    [NSApp updateWindows];
    NSTimeInterval started = [NSDate timeIntervalSinceReferenceDate];
    char *type = NULL;
    int cancelled = 0;
    int ok = gui_tree_scan(g_window, g_panel, [path UTF8String], (int)type_id.val.u64, &type, &cancelled);
    NSString *message = ok ? [NSString stringWithFormat:cdie_ns_fmt(@"Scan complete (%@). %lu results · %.0f ms"), string(type),
        (unsigned long)xxwidgets_scan_panel_count(g_panel), ([NSDate timeIntervalSinceReferenceDate] - started) * 1000] :
        cancelled ? cdie_ns_tr(@"Scan cancelled.") : cdie_ns_tr(@"Scan failed.");
    xxwidgets_widget_set_text(g_status, [message UTF8String]);
    gui_backend_free(type);
    xxwidgets_widget_set_enabled(g_window, 1);
    xxwidgets_widget_set_enabled(combo, 1);
    if (ok) gui_frontend_remember_file([path UTF8String]);
    return ok;
}
/* The pickers stay application-modal (runModal) rather than sheets: a sheet
 * returns at once, so the xxwidgets loops would keep dispatching the window
 * shortcuts AppKit does not block (they are matched before sendEvent:), and
 * browse_database must answer Options synchronously from its page step. */
static void do_open(void)
{
    NSOpenPanel *picker = [NSOpenPanel openPanel];
    [picker setTitle:cdie_ns_tr(@"Choose a file to scan")];
    [picker setCanChooseDirectories:NO];
    [picker setAllowsMultipleSelection:NO];
    gui_options_values preferences;
    gui_preferences_get(&preferences);
    if (preferences.file.save_last_directory && preferences.file.last_directory[0])
        [picker setDirectoryURL:[NSURL fileURLWithPath:string(preferences.file.last_directory) isDirectory:YES]];
    if ([picker runModal] == NSModalResponseOK) open_path([[picker URL] path]);
}
static void do_file_information(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read file path"), status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error(CDIE_N_("Read file path"), XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a PE, ELF or Mach-O file to inspect."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_file_information_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("File Information", status);
}

static void do_entropy(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read file path"), status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error(CDIE_N_("Read file path"), XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to calculate entropy."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_entropy_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Entropy", status);
}

static void do_hash(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read file path"), status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error(CDIE_N_("Read file path"), XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to calculate hashes."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_hash_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Hash", status);
}

static void do_strings(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read file path"), status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error(CDIE_N_("Read file path"), XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to extract strings."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_strings_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Strings", status);
}

static void do_hex_view(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read file path"), status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error(CDIE_N_("Read file path"), XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to view its bytes."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_hex_view_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Hex View", status);
}

static void do_disasm(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read file path"), status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error(CDIE_N_("Read file path"), XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to disassemble."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_disasm_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Disasm", status);
}

static void do_memory_map(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read file path"), status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error(CDIE_N_("Read file path"), XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to map."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_memory_map_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Memory Map", status);
}

static void do_imports(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read file path"), status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error(CDIE_N_("Read file path"), XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to inspect imports."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_imports_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Imports", status);
}

static void do_exports(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read file path"), status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error(CDIE_N_("Read file path"), XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to inspect exports."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_exports_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Exports", status);
}

static void do_resources(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read file path"), status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error(CDIE_N_("Read file path"), XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to inspect resources."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_resources_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Resources", status);
}

static void do_symbols(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read file path"), status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error(CDIE_N_("Read file path"), XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to inspect symbols."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_symbols_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Symbols", status);
}

static void do_extractor(void)
{
    size_t length = 0;
    char *path;
    xxwidgets_status status = xxwidgets_widget_get_text(g_path, NULL, 0, &length);
    if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Read file path"), status); return; }
    path = malloc(length ? length : 1);
    if (!path) { report_ui_error(CDIE_N_("Read file path"), XXWIDGETS_OUT_OF_MEMORY); return; }
    status = xxwidgets_widget_get_text(g_path, path, length ? length : 1, NULL);
    if (status == XXWIDGETS_OK && !path[0]) {
        xxwidgets_widget_set_text(g_status, CDIE_TR("Choose a file to extract embedded files."));
        xxwidgets_widget_focus(g_path);
    } else if (status == XXWIDGETS_OK) {
        NSString *expanded = [string(path) stringByExpandingTildeInPath];
        status = gui_extractor_show(g_window, [expanded UTF8String]);
    }
    free(path);
    if (status != XXWIDGETS_OK) report_ui_error("Extractor", status);
}

static xxwidgets_status browse_database(xxwidgets_widget *dialog, const char *initial, char **path, int *accepted, void *user)
{
    NSOpenPanel *picker = [NSOpenPanel openPanel];
    (void)dialog; (void)user;
    *path = NULL; *accepted = 0;
    [picker setCanChooseDirectories:YES]; [picker setCanChooseFiles:NO];
    [picker setTitle:cdie_ns_tr(@"Choose a signature database folder")];
    if (initial && initial[0]) [picker setDirectoryURL:[NSURL fileURLWithPath:string(initial)]];
    if ([picker runModal] != NSModalResponseOK) return XXWIDGETS_OK;
    const char *chosen = [[[picker URL] path] UTF8String];
    if (!chosen) return XXWIDGETS_INVALID_ARGUMENT;
    /* The form releases the chosen path with free(). */
    *path = strdup(chosen);
    if (!*path) return XXWIDGETS_OUT_OF_MEMORY;
    *accepted = 1;
    return XXWIDGETS_OK;
}

/* The frontend's share of the Options settings, as currently applied. */
static xxwidgets_status current_options(gui_options_values *values)
{
    xxwidgets_status status;
    gui_frontend_get_preferences(values);
    status = xxwidgets_scan_panel_get_flags(g_panel, &values->scan.options.flags);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_get_databases(g_panel, &values->scan.options.databases);
    if (status == XXWIDGETS_OK) status = xxwidgets_app_get_font_options(g_app, &values->fonts);
    values->scan.modes = gui_backend_get_scan_modes();
    gui_hex_view_get_options(&values->hex);
    return status;
}

/* Applies settings from Options or from the store; fonts only when changed. */
static xxwidgets_status apply_options(const gui_options_values *values)
{
    xxwidgets_font_options fonts;
    xxwidgets_status status;
    gui_backend_set_scan_modes(values->scan.modes);
    status = xxwidgets_scan_panel_set_flags(g_panel, values->scan.options.flags);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_set_databases(g_panel, values->scan.options.databases);
    gui_hex_view_set_options(&values->hex);
    if (status == XXWIDGETS_OK) status = xxwidgets_app_get_font_options(g_app, &fonts);
    if (status == XXWIDGETS_OK && memcmp(&values->fonts, &fonts, sizeof(fonts))) {
        status = xxwidgets_app_set_font_options(g_app, &values->fonts);
        if (status == XXWIDGETS_OK) layout();
    }
    if (status == XXWIDGETS_OK) status = gui_frontend_apply_preferences(g_app, values);
    if (status == XXWIDGETS_OK) [(NSWindow *)xxwidgets_widget_native_handle(g_window) setLevel:values->appearance.stay_on_top ? NSFloatingWindowLevel : NSNormalWindowLevel];
    return status;
}

/* The shared Options window; unsupported platform integrations are disabled. */
static void do_options(gui_options_page_id page)
{
    gui_options_host host = {(1u << GUI_OPTIONS_PAGE_COUNT) - 1u, NULL, browse_database, NULL};
    gui_options_values values;
    int accepted = 0, reloaded = 0;
    xxwidgets_status status = current_options(&values);
    g_options_page = page;
    if (status == XXWIDGETS_OK) status = gui_options_dialog(g_window, &host, &g_options_page, &values, &accepted, &reloaded);
    if (status != XXWIDGETS_OK) { report_ui_error("Options", status); return; }
    if (reloaded) {
        status = refresh_panel_tags();
        if (status != XXWIDGETS_OK) { report_ui_error("Refresh signature tags", status); return; }
        ready();
    }
    if (!accepted) return;
    status = apply_options(&values);
    if (status != XXWIDGETS_OK) report_ui_error("Apply options", status);
    else if (!gui_settings_save(g_settings, &values, g_options_page))
        xxwidgets_widget_set_text(g_status, CDIE_TR("Options applied, but they could not be saved for the next start."));
    else if (!reloaded) xxwidgets_widget_set_text(g_status, CDIE_TR("Options applied."));
}

static void do_about(void)
{
    xxwidgets_about_dialog *about = NULL;
    xxwidgets_status status = gui_about_create(cdie_about_png, cdie_about_png_size, &about);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_show(about, g_window);
    if (about) xxwidgets_about_dialog_destroy(about);
    if (status != XXWIDGETS_OK) report_ui_error("About", status);
}

/* Update database: download and install from the stored update URL, refresh
 * the tags after a reload, and report on the status line. A NULL owner runs
 * without the progress dialog (--smoke-update). */
static gui_update_result do_update(xxwidgets_widget *owner)
{
    char message[512];
    gui_options_values preferences;
    gui_update_result result;
    uint64_t generation = gui_backend_database_generation();
    gui_preferences_get(&preferences);
    result = gui_update_run(owner, preferences.databases.main_update_url, message, sizeof(message));
    /* Installed, or switched to an up-to-date per-user copy. */
    if (gui_backend_database_generation() != generation) {
        xxwidgets_status status = refresh_panel_tags();
        if (status != XXWIDGETS_OK) { report_ui_error("Refresh signature tags", status); return result; }
    }
    xxwidgets_widget_set_text(g_status, message);
    return result;
}

/* The panel enables its Report button exactly while it holds a report. */
static int report_available(void)
{
    size_t length = 0;
    return g_panel && xxwidgets_scan_panel_get_report(g_panel, NULL, 0, &length) == XXWIDGETS_OK && length > 1;
}
static int history_enabled(void)
{
    gui_options_values values;
    gui_preferences_get(&values);
    return values.file.save_recent_files != 0;
}
/* Report and Recent from the menu or a shortcut do what their disabled
 * controls do: nothing (Windows disables Recent while history is off). */
static void do_report(void)
{
    xxwidgets_status status;
    if (!report_available()) { NSBeep(); return; }
    status = xxwidgets_scan_panel_show_report(g_panel);
    if (status != XXWIDGETS_OK) report_ui_error("Scan report", status);
}
static void do_recent(void)
{
    char recent[GUI_PREFERENCE_PATH_MAX];
    int accepted = 0;
    xxwidgets_status status;
    if (!history_enabled()) { NSBeep(); return; }
    status = gui_frontend_recent_dialog(g_window, recent, sizeof(recent), &accepted);
    if (status == XXWIDGETS_OK && accepted) open_path(string(recent));
    else if (status != XXWIDGETS_OK) report_ui_error("Recent files", status);
}

/* One queued action, run after polling returns: by the main loop and the smoke test. */
static void run_action(int action)
{
    switch (action) {
    case ACTION_SCAN: do_scan(); break;
    case ACTION_OPEN: do_open(); break;
    case ACTION_REPORT: do_report(); break;
    case ACTION_OPTIONS: do_options(g_options_page); break;
    case ACTION_DATABASES: do_options(GUI_OPTIONS_SCAN); break;
    case ACTION_RECENT: do_recent(); break;
    case ACTION_FILE_INFORMATION: do_file_information(); break;
    case ACTION_ENTROPY: do_entropy(); break;
    case ACTION_HASH: do_hash(); break;
    case ACTION_STRINGS: do_strings(); break;
    case ACTION_HEX_VIEW: do_hex_view(); break;
    case ACTION_DISASM: do_disasm(); break;
    case ACTION_MEMORY_MAP: do_memory_map(); break;
    case ACTION_IMPORTS: do_imports(); break;
    case ACTION_EXPORTS: do_exports(); break;
    case ACTION_RESOURCES: do_resources(); break;
    case ACTION_SYMBOLS: do_symbols(); break;
    case ACTION_EXTRACTOR: do_extractor(); break;
    case ACTION_ABOUT: do_about(); break;
    case ACTION_UPDATE: do_update(g_window); break;
    default: break;
    }
}

@interface CDIEApplication : NSObject <NSApplicationDelegate>
- (void)action:(id)sender;
- (BOOL)validateMenuItem:(NSMenuItem *)item;
@end
@implementation CDIEApplication
- (void)action:(id)sender { g_pending = (int)[sender tag]; }
/* Menu commands are off while a dialog or scan disables the main window,
 * so none is queued behind it; Report and Recent follow their controls. */
- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    if ([item action] != @selector(action:)) return YES;
    if (!g_window || [(NSWindow *)xxwidgets_widget_native_handle(g_window) ignoresMouseEvents]) return NO;
    if ([item tag] == ACTION_REPORT) return report_available() ? YES : NO;
    if ([item tag] == ACTION_RECENT) return history_enabled() ? YES : NO;
    return YES;
}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    (void)sender;
    g_running = 0;
    xxwidgets_app_quit(g_app, 0);
    if ([NSApp modalWindow]) [NSApp abortModal];
    return NSTerminateCancel;
}
- (BOOL)application:(NSApplication *)sender openFile:(NSString *)filename
{ (void)sender; open_path(filename); return YES; }
@end

static void install_menu(CDIEApplication *delegate)
{
    NSMenu *menu = [NSApp mainMenu];
    NSMenu *application = [[menu itemAtIndex:0] submenu];
    NSMenuItem *about = [[[NSMenuItem alloc] initWithTitle:cdie_ns_tr(@"About cdie") action:@selector(action:) keyEquivalent:@""] autorelease];
    [about setTarget:delegate]; [about setTag:ACTION_ABOUT]; [application insertItem:about atIndex:0];
    NSMenuItem *settings = [[[NSMenuItem alloc] initWithTitle:cdie_ns_tr(@"Settings…") action:@selector(action:) keyEquivalent:@","] autorelease];
    [settings setTarget:delegate]; [settings setTag:ACTION_OPTIONS]; [application insertItem:settings atIndex:1];
    /* Options on the Scan page's database folders, as Ctrl+Shift+D on Windows. */
    NSMenuItem *databases = [[[NSMenuItem alloc] initWithTitle:cdie_ns_tr(@"Database folders…") action:@selector(action:) keyEquivalent:@"D"] autorelease];
    [databases setTarget:delegate]; [databases setTag:ACTION_DATABASES]; [application insertItem:databases atIndex:2];
    if (gui_update_supported()) {
        NSMenuItem *update = [[[NSMenuItem alloc] initWithTitle:cdie_ns_tr(@"Update database") action:@selector(action:) keyEquivalent:@"u"] autorelease];
        [update setTarget:delegate]; [update setTag:ACTION_UPDATE]; [application insertItem:update atIndex:3];
    }
    NSMenuItem *file = [[[NSMenuItem alloc] initWithTitle:cdie_ns_tr(@"File") action:NULL keyEquivalent:@""] autorelease];
    NSMenu *submenu = [[[NSMenu alloc] initWithTitle:cdie_ns_tr(@"File")] autorelease];
    NSArray *titles = @[cdie_ns_tr(@"Open…"), cdie_ns_tr(@"Scan"), cdie_ns_tr(@"Report"), cdie_ns_tr(@"File Information"), cdie_ns_tr(@"Entropy"), cdie_ns_tr(@"Hash"), cdie_ns_tr(@"Strings"), cdie_ns_tr(@"Hex View"), cdie_ns_tr(@"Disasm"), cdie_ns_tr(@"Memory Map"), cdie_ns_tr(@"Imports"), cdie_ns_tr(@"Exports"), cdie_ns_tr(@"Resources"), cdie_ns_tr(@"Symbols"), cdie_ns_tr(@"Extractor")];
    NSArray *keys = @[@"o", @"r", @"R", @"i", @"e", @"h", @"s", @"x", @"a", @"m", @"i", @"e", @"u", @"y", @"t"];
    int actions[] = {ACTION_OPEN, ACTION_SCAN, ACTION_REPORT, ACTION_FILE_INFORMATION, ACTION_ENTROPY, ACTION_HASH, ACTION_STRINGS, ACTION_HEX_VIEW, ACTION_DISASM, ACTION_MEMORY_MAP, ACTION_IMPORTS, ACTION_EXPORTS, ACTION_RESOURCES, ACTION_SYMBOLS, ACTION_EXTRACTOR};
    for (NSUInteger i = 0; i < [titles count]; ++i) {
        NSMenuItem *item = [submenu addItemWithTitle:titles[i] action:@selector(action:) keyEquivalent:keys[i]];
        if (actions[i] == ACTION_HASH || actions[i] == ACTION_STRINGS || actions[i] == ACTION_HEX_VIEW || actions[i] == ACTION_DISASM || actions[i] == ACTION_MEMORY_MAP || actions[i] == ACTION_IMPORTS || actions[i] == ACTION_EXPORTS || actions[i] == ACTION_RESOURCES || actions[i] == ACTION_SYMBOLS || actions[i] == ACTION_EXTRACTOR)
            [item setKeyEquivalentModifierMask:NSEventModifierFlagCommand | NSEventModifierFlagShift];
        [item setTarget:delegate]; [item setTag:actions[i]];
    }
    NSMenuItem *recent = [submenu addItemWithTitle:cdie_ns_tr(@"Recent files…") action:@selector(action:) keyEquivalent:@"r"];
    [recent setTarget:delegate]; [recent setTag:ACTION_RECENT];
    for (NSMenuItem *item in [submenu itemArray]) if ([item tag] == ACTION_SCAN) [item setKeyEquivalent:@""];
    [file setSubmenu:submenu]; [menu insertItem:file atIndex:1];
    /* About stays in the application menu, where macOS puts it, instead of a
     * button the two full tool rows have no room for. Help > About (Cmd+?,
     * the Mac help key) and the window's F1 match F1 on Windows and Linux. */
    NSMenuItem *help = [[[NSMenuItem alloc] initWithTitle:cdie_ns_tr(@"Help") action:NULL keyEquivalent:@""] autorelease];
    NSMenu *help_menu = [[[NSMenu alloc] initWithTitle:cdie_ns_tr(@"Help")] autorelease];
    NSMenuItem *help_about = [help_menu addItemWithTitle:cdie_ns_tr(@"About cdie") action:@selector(action:) keyEquivalent:@"?"];
    [help_about setTarget:delegate]; [help_about setTag:ACTION_ABOUT];
    [help setSubmenu:help_menu]; [menu addItem:help]; [NSApp setHelpMenu:help_menu];
}

static int create_controls(void)
{
    if (xxwidgets_widget_create(g_app, g_window, XXWIDGETS_EDIT, "", (xxwidgets_rect){2, 1, 75, 2}, &g_path) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Open…"), (xxwidgets_rect){79, 1, 11, 2}, &g_browse) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_create(g_window, (xxwidgets_rect){2, 4, 88, 21}, &g_panel) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Options…"), (xxwidgets_rect){2, 26, 16, 2}, &g_options) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("File Information…"), (xxwidgets_rect){2, 29, 26, 2}, &g_file_information) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Entropy…"), (xxwidgets_rect){30, 29, 16, 2}, &g_entropy) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Hash…"), (xxwidgets_rect){48, 29, 14, 2}, &g_hash) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Strings…"), (xxwidgets_rect){64, 29, 18, 2}, &g_strings) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Hex View…"), (xxwidgets_rect){84, 29, 18, 2}, &g_hex_view) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Disasm…"), (xxwidgets_rect){104, 29, 18, 2}, &g_disasm) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Memory Map…"), (xxwidgets_rect){124, 29, 20, 2}, &g_memory_map) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Imports…"), (xxwidgets_rect){60, 26, 18, 2}, &g_imports) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Exports…"), (xxwidgets_rect){80, 26, 18, 2}, &g_exports) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Resources…"), (xxwidgets_rect){100, 26, 22, 2}, &g_resources) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Symbols…"), (xxwidgets_rect){124, 26, 18, 2}, &g_symbols) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Extractor…"), (xxwidgets_rect){104, 26, 22, 2}, &g_extractor) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_BUTTON, CDIE_TR("Update database"), (xxwidgets_rect){128, 26, 16, 2}, &g_update) != XXWIDGETS_OK ||
        xxwidgets_widget_create(g_app, g_window, XXWIDGETS_LABEL, CDIE_TR("Loading databases…"), (xxwidgets_rect){2, 29, 88, 1}, &g_status) != XXWIDGETS_OK) return 0;
    NSTextField *field = xxwidgets_widget_native_handle(g_path);
    [field setPlaceholderString:cdie_ns_tr(@"Choose or drop a file to scan")];
    [field setAccessibilityLabel:cdie_ns_tr(@"File to scan")];
    xxwidgets_widget_set_enabled(control(XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0);
    xxwidgets_widget_set_enabled(g_update, gui_update_supported());
    NSButton *scan = xxwidgets_widget_native_handle(control(XXWIDGETS_SCAN_PANEL_SCAN));
    [scan setKeyEquivalent:@"\r"];
    const xxwidgets_shortcut shortcuts[] = {{"F5", ACTION_SCAN}, {"Cmd+O", ACTION_OPEN}, {"Cmd+R", ACTION_RECENT}, {"Cmd+Shift+R", ACTION_REPORT}, {"Cmd+U", ACTION_UPDATE},
        {"F1", ACTION_ABOUT}};
    /* The custom database is on until saved Options say otherwise. */
    return xxwidgets_scan_panel_set_databases(g_panel, XXWIDGETS_SCAN_DATABASE_CUSTOM) == XXWIDGETS_OK &&
        xxwidgets_window_set_shortcuts(g_window, shortcuts, sizeof(shortcuts) / sizeof(shortcuts[0])) == XXWIDGETS_OK;
}

/* Modal dialog smoke driver. smoke_begin arms a repeating NSTimer in
 * NSRunLoopCommonModes, so it fires inside whatever modal loop the opener
 * enters next: the xxwidgets poll (NSDefaultRunLoopMode) and NSOpenPanel's
 * runModal (NSModalPanelRunLoopMode). Each tick finds the dialog by visible
 * window and title (English or translated), checks its content and closes
 * it as a user would: performClick: on a button, performClose:, a posted
 * Escape key, or abortModal for a panel. Ticks run while xxwidgets polls,
 * so they only read and drive AppKit objects; a failure or timeout ends
 * every modal loop with xxwidgets_app_quit and the smoke test stops there.
 * smoke_finish disarms it and requires: dialog seen, content right, dialog
 * gone, main window usable again. */
enum {
    SMOKE_BUTTON = 1, /* check, then press the case's button */
    SMOKE_ESCAPE,     /* check, then the Escape key */
    SMOKE_CLOSE_BOX,  /* check, then performClose: (the title bar's close button) */
    SMOKE_NESTED,     /* Options: press the button, check and Cancel "Colors", then Cancel Options */
    SMOKE_PICKER,     /* Options: press the button, check and abort its NSOpenPanel, then Cancel Options */
    SMOKE_PANEL,      /* the opener's own NSOpenPanel: check and abort it */
    SMOKE_ABSENT      /* the opener must show nothing */
};
enum { SMOKE_CHECK_NONE, SMOKE_CHECK_PAGE, SMOKE_CHECK_ABOUT, SMOKE_CHECK_TEXT, SMOKE_CHECK_TOOL, SMOKE_CHECK_TOOL_OR_ERROR,
       SMOKE_CHECK_EXTRACTOR, SMOKE_CHECK_RECENT, SMOKE_CHECK_SIGNATURES, SMOKE_CHECK_DIRECTORY, SMOKE_CHECK_PROGRESS, SMOKE_CHECK_CANCEL,
       SMOKE_CHECK_UPDATE_BOX, SMOKE_CHECK_QUESTION };
#define SMOKE_TIMEOUT 15.0 /* seconds for one case */
#define SMOKE_SETTLE 5.0   /* seconds a shown dialog may take to fill in */
typedef struct smoke_driver {
    NSTimer *timer;     /* retained */
    NSString *expected; /* retained: text the dialog must show, or nil */
    NSWindow *owner;    /* the main window */
    const char *title, *button, *waiting, *reason;
    int action, check, page, folder, font_choose, phase, seen, done, failed, aborted;
    NSTimeInterval started, seen_at;
} smoke_driver;
static smoke_driver g_smoke;

static int smoke_same(NSString *text, const char *english)
{
    return text && english && ([text isEqualToString:string(english)] || [text isEqualToString:string(CDIE_TR(english))]);
}
static NSWindow *smoke_window(const char *title)
{
    for (NSWindow *window in [NSApp windows])
        if ([window isVisible] && smoke_same([window title], title)) return window;
    return nil;
}
/* The first visible view of class kind under view; with english, the button
 * title or field text must match it. */
static id smoke_view(NSView *view, Class kind, const char *english)
{
    if (!view || [view isHidden]) return nil;
    if ([view isKindOfClass:kind] && !(kind == [NSButton class] && [view isKindOfClass:[NSPopUpButton class]])) {
        NSString *text = [view isKindOfClass:[NSButton class]] ? [(NSButton *)view title] :
            [view isKindOfClass:[NSTextField class]] ? [(NSTextField *)view stringValue] : nil;
        if (!english || smoke_same(text, english)) return view;
    }
    for (NSView *child in [view subviews]) {
        id found = smoke_view(child, kind, english);
        if (found) return found;
    }
    return nil;
}
/* A visible text field holding value, or containing it when partial. */
static NSTextField *smoke_field(NSView *view, NSString *value, BOOL partial)
{
    if (!view || !value || [view isHidden]) return nil;
    if ([view isKindOfClass:[NSTextField class]]) {
        NSString *text = [(NSTextField *)view stringValue];
        if (text && (partial ? [text rangeOfString:value].location != NSNotFound : [text isEqualToString:value])) return (NSTextField *)view;
    }
    for (NSView *child in [view subviews]) {
        NSTextField *found = smoke_field(child, value, partial);
        if (found) return found;
    }
    return nil;
}
/* Visible buttons titled english; *wrong counts those not in the enabled state. */
static int smoke_buttons(NSView *view, const char *english, BOOL enabled, int *wrong)
{
    int count = 0;
    if (!view || [view isHidden]) return 0;
    if ([view isKindOfClass:[NSButton class]] && ![view isKindOfClass:[NSPopUpButton class]] && smoke_same([(NSButton *)view title], english)) {
        ++count;
        if ([(NSButton *)view isEnabled] != enabled) ++*wrong;
    }
    for (NSView *child in [view subviews]) count += smoke_buttons(child, english, enabled, wrong);
    return count;
}
/* The most rows of a visible table (outline views too), -1 without one; *columns gets the most columns. */
static NSInteger smoke_rows(NSView *view, NSInteger *columns)
{
    NSInteger rows = -1;
    if (!view || [view isHidden]) return -1;
    if ([view isKindOfClass:[NSTableView class]]) {
        rows = [(NSTableView *)view numberOfRows];
        if ([(NSTableView *)view numberOfColumns] > *columns) *columns = [(NSTableView *)view numberOfColumns];
    }
    for (NSView *child in [view subviews]) {
        NSInteger found = smoke_rows(child, columns);
        if (found > rows) rows = found;
    }
    return rows;
}
/* The text of a text dialog (the backend's about content); field editors do not count. */
static NSTextView *smoke_text(NSView *view)
{
    if (!view || [view isHidden]) return nil;
    if ([view isKindOfClass:[NSTextView class]] && ![(NSTextView *)view isFieldEditor]) return (NSTextView *)view;
    for (NSView *child in [view subviews]) {
        NSTextView *found = smoke_text(child);
        if (found) return found;
    }
    return nil;
}
static int smoke_click(NSWindow *window, const char *english)
{
    NSButton *button = english ? smoke_view([window contentView], [NSButton class], english) : nil;
    if (!button || ![button isEnabled]) return 0;
    [button performClick:nil];
    return 1;
}
/* A click on a main-window button through AppKit, so its action and the xxwidgets event run. */
static int smoke_press(xxwidgets_widget *widget)
{
    NSButton *button = widget ? xxwidgets_widget_native_handle(widget) : nil;
    if (![button isKindOfClass:[NSButton class]] || ![button isEnabled]) return 0;
    [button performClick:nil];
    return 1;
}
/* Escape as the keyboard sends it; the xxwidgets poll closes its modal window on it. */
static void smoke_escape(NSWindow *window)
{
    NSEvent *key = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0
        timestamp:[[NSProcessInfo processInfo] systemUptime] windowNumber:[window windowNumber] context:nil
        characters:@"\033" charactersIgnoringModifiers:@"\033" isARepeat:NO keyCode:53];
    if (key) [NSApp postEvent:key atStart:NO];
}
/* Fails the case and ends every modal loop, so the opener returns. */
static void smoke_stop(const char *reason)
{
    if (!g_smoke.failed) { g_smoke.failed = 1; g_smoke.reason = reason; }
    if (g_smoke.aborted) return;
    g_smoke.aborted = 1;
    if ([NSApp modalWindow]) [NSApp abortModal];
    xxwidgets_app_quit(g_app, 1);
}

/* 1 when the dialog shows what the case expects, 0 while it may still fill
 * in, -1 when it is wrong; g_smoke.waiting says what is missing. */
static int smoke_content(NSWindow *window)
{
    /* One control only each page shows, in gui_options_page_id order. */
    static const char *const probes[GUI_OPTIONS_PAGE_COUNT] = {"Save last directory", "Table views", "Colors...", "Search signatures",
        "Bold non-zero", "CPU acceleration", "Stay on top", "Syntax", "VirusTotal API key", "Info database"};
    NSView *content = [window contentView];
    NSTextView *text = smoke_text(content);
    NSInteger columns = 0, rows = smoke_rows(content, &columns);
    NSButton *button;
    const char *choose;
    int i, wrong = 0;
    switch (g_smoke.check) {
    case SMOKE_CHECK_PAGE:
        for (i = 0; i < GUI_OPTIONS_PAGE_COUNT; ++i)
            if ((smoke_view(content, [NSView class], probes[i]) != nil) != (i == g_smoke.page)) {
                g_smoke.waiting = i == g_smoke.page ? "Options does not show the requested page" : "Options shows another page's controls";
                return 0;
            }
        /* Without a native chooser (Cocoa has none) Choose is disabled up front. */
        choose = g_smoke.page == GUI_OPTIONS_FONTS ? "Choose" : g_smoke.page == GUI_OPTIONS_HEX ? "Choose font" :
            g_smoke.page == GUI_OPTIONS_DISASM ? "Choose font..." : NULL;
        g_smoke.waiting = "a Choose font button does not follow xxwidgets_font_choose_available";
        return !choose || (smoke_buttons(content, choose, g_smoke.font_choose ? YES : NO, &wrong) && !wrong) ? 1 : -1;
    case SMOKE_CHECK_ABOUT:
        /* The text layout holds the name in its text view; a rich layout in a label. */
        g_smoke.waiting = "About lacks the program name, its picture or OK";
        return ((text && [[text string] rangeOfString:@"Detect It Easy"].location != NSNotFound) || smoke_field(content, @"Detect It Easy", YES)) &&
            smoke_view(content, [NSImageView class], NULL) && smoke_view(content, [NSButton class], "OK") ? 1 : -1;
    case SMOKE_CHECK_TEXT:
        g_smoke.waiting = g_smoke.expected ? "the text dialog shows other text" : "the text dialog lacks its text or Copy all";
        return text && [[text string] length] && (!g_smoke.expected || [[text string] isEqualToString:g_smoke.expected]) &&
            smoke_view(content, [NSButton class], "Copy all") ? 1 : -1;
    case SMOKE_CHECK_TOOL:
    case SMOKE_CHECK_TOOL_OR_ERROR:
        if (text) {
            /* A tool's error text dialog carries the tool's title. */
            g_smoke.waiting = "the tool showed an error instead of its dialog";
            if (g_smoke.check == SMOKE_CHECK_TOOL) fprintf(stderr, "%s: %s\n", g_smoke.title, [[text string] UTF8String] ?: "");
            return g_smoke.check == SMOKE_CHECK_TOOL_OR_ERROR && [[text string] length] && smoke_view(content, [NSButton class], "Copy all") ? 1 : -1;
        }
        g_smoke.waiting = "the tool dialog has no rows or no Close";
        if (!smoke_view(content, [NSButton class], "Close")) return 0;
        return rows > 0 || (rows == 0 && g_smoke.check == SMOKE_CHECK_TOOL_OR_ERROR) ? 1 : 0;
    case SMOKE_CHECK_EXTRACTOR:
        g_smoke.waiting = "the Extractor lacks its table, Scan, Dump all or Close";
        if (text) return -1;
        return columns >= XXWIDGETS_EXTRACTOR_COLUMN_COUNT && smoke_view(content, [NSButton class], "Scan") &&
            smoke_view(content, [NSButton class], "Dump all") && smoke_view(content, [NSButton class], "Close") ? 1 : 0;
    case SMOKE_CHECK_RECENT:
        g_smoke.waiting = "Recent files lacks the remembered file, OK or Cancel";
        return rows > 0 && smoke_view(content, [NSButton class], "OK") && smoke_view(content, [NSButton class], "Cancel") ? 1 : -1;
    case SMOKE_CHECK_SIGNATURES:
        button = smoke_view(content, [NSButton class], "Run");
        g_smoke.waiting = "the signature editor lacks an enabled Run or Read-only";
        return [button isEnabled] && smoke_view(content, [NSButton class], "Read-only") ? 1 : 0;
    case SMOKE_CHECK_DIRECTORY:
        button = smoke_view(content, [NSButton class], "Scan");
        g_smoke.waiting = "Scan directory does not offer the scanned file's folder";
        return [button isEnabled] && smoke_view(content, [NSButton class], "Recursive") && smoke_field(content, g_smoke.expected, NO) ? 1 : 0;
    case SMOKE_CHECK_PROGRESS:
        g_smoke.waiting = "the progress dialog lacks its bar, step or Cancel";
        return smoke_view(content, [NSProgressIndicator class], NULL) && smoke_field(content, g_smoke.expected, YES) &&
            smoke_view(content, [NSButton class], "Cancel") ? 1 : 0;
    case SMOKE_CHECK_CANCEL:
        g_smoke.waiting = "the progress dialog has no Cancel";
        return smoke_view(content, [NSButton class], "Cancel") ? 1 : 0;
    case SMOKE_CHECK_UPDATE_BOX:
        /* The "No updates" box: a text dialog whose text starts with the expected line. */
        g_smoke.waiting = "the update box lacks the No updates text, Copy all or Close";
        return text && [[text string] hasPrefix:g_smoke.expected] && smoke_view(content, [NSButton class], "Copy all") &&
            smoke_view(content, [NSButton class], "Close") ? 1 : -1;
    case SMOKE_CHECK_QUESTION:
        /* The update question: its lines are labels; the closing line and both answers must be there. */
        g_smoke.waiting = "the update question lacks the update, version/date, its question or Yes/No";
        if (text) return -1;
        return smoke_field(content, string(CDIE_TR("Update")), YES) && smoke_field(content, string(CDIE_TR("Version/date")), YES) &&
            smoke_field(content, g_smoke.expected, NO) && smoke_view(content, [NSButton class], "Yes") &&
            smoke_view(content, [NSButton class], "No") ? 1 : 0;
    default:
        return 1;
    }
}
/* The NSOpenPanel run by the opener or by an Options page: right kind, then aborted. */
static void smoke_panel(NSTimeInterval now)
{
    NSWindow *modal = [NSApp modalWindow];
    BOOL folders = g_smoke.folder ? YES : NO;
    if (![modal isKindOfClass:[NSOpenPanel class]]) { g_smoke.waiting = "the file picker did not open"; return; }
    if (g_smoke.action == SMOKE_PANEL) g_smoke.seen = 1;
    if ([(NSOpenPanel *)modal canChooseDirectories] != folders || [(NSOpenPanel *)modal canChooseFiles] == folders) {
        smoke_stop(folders ? "the folder picker offers files" : "the file picker offers folders");
        return;
    }
    /* runModal returns NSModalResponseAbort: the pickers take it as Cancel. */
    [NSApp abortModal];
    if (g_smoke.action == SMOKE_PANEL) g_smoke.done = 1;
    else { g_smoke.phase = 2; g_smoke.seen_at = now; }
}
/* Colors over Options: both owners disabled, its controls there, then Cancel. */
static void smoke_nested(NSTimeInterval now)
{
    NSWindow *options = smoke_window("Options"), *colors = smoke_window("Colors");
    if (!colors || [colors ignoresMouseEvents]) { g_smoke.waiting = "Colors did not open"; return; }
    if (!options || ![options ignoresMouseEvents] || ![g_smoke.owner ignoresMouseEvents]) {
        smoke_stop("Options or the main window stays enabled under Colors");
        return;
    }
    if (!smoke_view([colors contentView], [NSButton class], "Dark appearance") || !smoke_view([colors contentView], [NSButton class], "Apply color")) {
        smoke_stop("Colors lacks Dark appearance or Apply color");
        return;
    }
    if (!smoke_click(colors, "Cancel")) { g_smoke.waiting = "Colors has no enabled Cancel"; return; }
    g_smoke.phase = 2; g_smoke.seen_at = now;
}
static void smoke_tick(void)
{
    NSTimeInterval now = [NSDate timeIntervalSinceReferenceDate];
    NSWindow *window;
    int content;
    if (g_smoke.aborted) {
        /* A panel that opened late, or ignored the first abort, would keep
         * the opener (and the case) waiting: abort it on every tick. */
        if ([NSApp modalWindow]) [NSApp abortModal];
        return;
    }
    if (now - g_smoke.started > SMOKE_TIMEOUT) { smoke_stop(g_smoke.waiting ? g_smoke.waiting : "timed out"); return; }
    if (g_smoke.done) return;
    if (g_smoke.action == SMOKE_ABSENT) {
        if (smoke_window(g_smoke.title) || [NSApp modalWindow]) { g_smoke.seen = 1; smoke_stop("a dialog opened where nothing may"); }
        return;
    }
    if (g_smoke.action == SMOKE_PANEL || (g_smoke.action == SMOKE_PICKER && g_smoke.phase == 1)) { smoke_panel(now); return; }
    if (g_smoke.action == SMOKE_NESTED && g_smoke.phase == 1) { smoke_nested(now); return; }
    window = smoke_window(g_smoke.title);
    /* Not yet shown, or busy under a nested progress dialog or picker; the
     * time to settle counts from when it takes input again. */
    if (!window || [window ignoresMouseEvents] || [NSApp modalWindow]) {
        if (!g_smoke.seen) g_smoke.waiting = "the dialog did not appear";
        else g_smoke.seen_at = now;
        return;
    }
    if (!g_smoke.seen) { g_smoke.seen = 1; g_smoke.seen_at = now; }
    if (![g_smoke.owner ignoresMouseEvents]) { smoke_stop("the main window stays enabled under the dialog"); return; }
    if (!g_smoke.phase) {
        content = smoke_content(window);
        if (content < 0 || (!content && now - g_smoke.seen_at > SMOKE_SETTLE)) { smoke_stop(g_smoke.waiting); return; }
        if (!content) return;
    }
    g_smoke.waiting = "the dialog did not close";
    if (g_smoke.action == SMOKE_ESCAPE) { smoke_escape(window); g_smoke.done = 1; return; }
    if (g_smoke.action == SMOKE_CLOSE_BOX) { [window performClose:nil]; g_smoke.done = 1; return; }
    /* The case's button; Cancel once a nested dialog or picker is closed. */
    if (!smoke_click(window, g_smoke.phase ? "Cancel" : g_smoke.button)) {
        g_smoke.waiting = "the button to press is missing or disabled";
        if (now - g_smoke.seen_at > SMOKE_SETTLE) smoke_stop(g_smoke.waiting);
        return;
    }
    if ((g_smoke.action == SMOKE_NESTED || g_smoke.action == SMOKE_PICKER) && !g_smoke.phase) { g_smoke.phase = 1; g_smoke.seen_at = now; }
    else g_smoke.done = 1;
}

@interface CDIESmokeDriver : NSObject
- (void)tick:(NSTimer *)timer;
@end
@implementation CDIESmokeDriver
- (void)tick:(NSTimer *)timer { (void)timer; smoke_tick(); }
@end

/* Arms the driver for one dialog; the opener runs next, then smoke_finish. */
static int smoke_begin(const char *title, int action, int check, const char *button, NSString *expected)
{
    CDIESmokeDriver *target = [[CDIESmokeDriver alloc] init];
    smoke_driver reset = {0};
    g_smoke = reset;
    g_smoke.title = title; g_smoke.action = action; g_smoke.check = check; g_smoke.button = button;
    g_smoke.owner = xxwidgets_widget_native_handle(g_window);
    g_smoke.started = [NSDate timeIntervalSinceReferenceDate];
    /* The timer retains its target and the run loop the timer, until invalidate. */
    if (target) g_smoke.timer = [[NSTimer timerWithTimeInterval:0.02 target:target selector:@selector(tick:) userInfo:nil repeats:YES] retain];
    [target release];
    if (!g_smoke.timer) { fprintf(stderr, "Cocoa dialog smoke: %s: cannot create the timer\n", title); return 0; }
    g_smoke.expected = [expected retain];
    [[NSRunLoop currentRunLoop] addTimer:g_smoke.timer forMode:NSRunLoopCommonModes];
    return 1;
}
static int smoke_main_usable(void)
{
    NSWindow *window = xxwidgets_widget_native_handle(g_window);
    NSButton *options = xxwidgets_widget_native_handle(g_options);
    return window && ![window ignoresMouseEvents] && [window canBecomeKeyWindow] && ![window attachedSheet] && ![NSApp modalWindow] &&
        [options isEnabled] && xxwidgets_app_poll(g_app, 0) == XXWIDGETS_OK;
}
/* Disarms the driver: 1 when the case passed, else its reason goes to stderr. */
static int smoke_finish(void)
{
    int ok;
    [g_smoke.timer invalidate];
    [g_smoke.timer release];
    g_smoke.timer = nil;
    ok = !g_smoke.failed && (g_smoke.action == SMOKE_ABSENT ? !g_smoke.seen : g_smoke.seen && g_smoke.done);
    if (ok && (smoke_window(g_smoke.title) || smoke_window("Colors"))) { ok = 0; g_smoke.reason = "the dialog is still open"; }
    if (ok && !smoke_main_usable()) { ok = 0; g_smoke.reason = "the main window is not usable afterwards"; }
    if (!ok)
        fprintf(stderr, "Cocoa dialog smoke: %s: %s\n", g_smoke.title, g_smoke.reason ? g_smoke.reason : g_smoke.waiting ? g_smoke.waiting :
            !g_smoke.seen ? "the dialog did not appear" : "it was not closed");
    [g_smoke.expected release];
    g_smoke.expected = nil;
    return ok;
}
/* Options on page, checked against that page's controls. */
static int smoke_options(gui_options_page_id page, int action, const char *button)
{
    if (!smoke_begin("Options", action, SMOKE_CHECK_PAGE, button, nil)) return 0;
    g_smoke.page = (int)page;
    g_smoke.font_choose = xxwidgets_font_choose_available(g_app);
    return 1;
}
/* The main menu item that queues action (with this key equivalent when key is set). */
static NSMenuItem *smoke_menu(int action, NSString *key)
{
    for (NSMenuItem *top in [[NSApp mainMenu] itemArray])
        for (NSMenuItem *item in [[top submenu] itemArray])
            if ([item action] == @selector(action:) && [item tag] == action && (!key || [[item keyEquivalent] isEqualToString:key])) return item;
    return nil;
}
/* A loopback port that accepts connections and never answers: an update
 * stalls there, offline, until it is cancelled. */
static int smoke_blackhole(int *port)
{
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&address, 0, sizeof(address));
    address.sin_len = (unsigned char)sizeof(address);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) || listen(fd, 4) ||
        getsockname(fd, (struct sockaddr *)&address, &length)) { close(fd); return -1; }
    *port = ntohs(address.sin_port);
    return fd;
}
typedef struct smoke_steps { unsigned int count; int stopped; } smoke_steps;
/* The job behind the deterministic progress dialog: busy until Cancel. */
static xxwidgets_status smoke_progress_update(void *user, int stop_requested, xx_pd_struct *progress, int *finished)
{
    smoke_steps *steps = user;
    progress->records[0].is_busy = true;
    progress->records[0].current = ++steps->count % 100;
    progress->records[0].total = 100;
    snprintf(progress->records[0].status, sizeof(progress->records[0].status), "Smoke step");
    if (stop_requested) steps->stopped = 1;
    *finished = stop_requested != 0;
    return XXWIDGETS_OK;
}

/* Run the actual Cocoa controls and backend against a supplied file. This also
 * covers bundle database lookup when launched from an unrelated directory.
 * Exit codes: 9-12 optimization options, 1 no signatures, 14 tag pickers,
 * 13 file drop, 2 scan, 3 report and tree, 4-6 Binary type rescan, 7 missing
 * file, 8 rescan; embedded views: 15 File Information, 25 Entropy, 16 Hash,
 * 17 Strings, 18 Hex View, 19 Disasm, 20 Memory Map, 21 Imports, 22 Exports,
 * 23 Resources, 24 Symbols, 26 Extractor. Modal dialogs (smoke_tick): 27 the
 * Report action and menu item do nothing without a report, 28 Automatic
 * rescan enables them, 29 File > Report shows the report (Close), 30 S mark
 * opens Signatures (Close), 31 Log button (Escape), 32 Directory button
 * opens Scan directory in the file's folder (Close), 33 Help > About (Cmd+?)
 * (OK), 34 About Escape, 35 About close box, 36-45 Options on each page in
 * gui_options_page_id order (Cancel; page controls, Choose font state), 46
 * "Database folders…" opens the Scan page (Escape), 47 Options close box,
 * 48 Colors over Options (Cancel, Cancel), 49 Scan page Browse..., 50
 * Choose translation..., 51 Info page Browse... (NSOpenPanel aborted, then
 * Cancel), 52 Open… (NSOpenPanel aborted; nothing changes), 53 Recent files
 * (Cancel), 54 Recent files (OK reopens the scanned file), 55 Recent and its
 * menu item do nothing with history off, 56-67 tool buttons: File
 * Information, Entropy, Hash, Strings, Hex View, Disasm, Memory Map, Imports,
 * Exports, Resources, Symbols, Extractor (Close), 68 Hash Escape, 69-71
 * Exports, Resources and Symbols dialogs on one synthetic row (Close), 72 a
 * progress dialog (Cancel), 73 Updating Database against a stalled loopback
 * address (Cancel; offline). */
static int smoke_test(NSString *path)
{
    gui_backend_optimization_options saved, changed, actual;
    gui_backend_optimization_capabilities capabilities;
    if (!gui_backend_get_optimization_options(&saved) ||
        !gui_backend_get_optimization_capabilities(&capabilities)) return 9;
    changed = saved;
    changed.use_arm_pmull = 0;
    if (!gui_backend_set_optimization_options(&changed) ||
        !gui_backend_get_optimization_options(&actual) || actual.use_arm_pmull) return 10;
    changed.use_arm_pmull = 1;
    if (!gui_backend_set_optimization_options(&changed) ||
        !gui_backend_get_optimization_options(&actual) || actual.use_arm_pmull != capabilities.arm_pmull) return 11;
    changed.use_arm_pmull = 2;
    if (gui_backend_set_optimization_options(&changed) || !gui_backend_set_optimization_options(&saved)) return 12;
    if (!gui_backend_signature_count()) { fprintf(stderr, "No bundled signatures loaded\n"); return 1; }
    {
        xxwidgets_widget *include_combo = control(XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS);
        xxwidgets_widget *exclude_combo = control(XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS);
        size_t include_size = 0, exclude_size = 0;
        if (xxwidgets_widget_kind(include_combo) != XXWIDGETS_CHECKCOMBOBOX ||
            xxwidgets_widget_kind(exclude_combo) != XXWIDGETS_CHECKCOMBOBOX ||
            !xxwidgets_combobox_count(include_combo) ||
            xxwidgets_combobox_count(include_combo) != xxwidgets_combobox_count(exclude_combo) ||
            xxwidgets_checkcombobox_set_checked(include_combo, 0, 1) != XXWIDGETS_OK ||
            xxwidgets_checkcombobox_set_checked(exclude_combo, xxwidgets_combobox_count(exclude_combo) - 1, 1) != XXWIDGETS_OK ||
            xxwidgets_scan_panel_get_include_tags(g_panel, NULL, 0, &include_size) != XXWIDGETS_OK || include_size <= 1 ||
            xxwidgets_scan_panel_get_exclude_tags(g_panel, NULL, 0, &exclude_size) != XXWIDGETS_OK || exclude_size <= 1 ||
            apply_panel_tag_filters() != XXWIDGETS_OK ||
            xxwidgets_checkcombobox_set_checked(include_combo, 0, 0) != XXWIDGETS_OK ||
            xxwidgets_checkcombobox_set_checked(exclude_combo, xxwidgets_combobox_count(exclude_combo) - 1, 0) != XXWIDGETS_OK ||
            apply_panel_tag_filters() != XXWIDGETS_OK) return 14;
    }
    const char *dropped_paths[] = {[path UTF8String], "/unused/second-file"};
    xxwidgets_event drop = {0};
    drop.type = XXWIDGETS_EVENT_DROP_FILES; drop.widget = g_window;
    drop.paths = dropped_paths; drop.path_count = 2;
    g_pending = ACTION_NONE;
    on_event(g_app, &drop, NULL);
    size_t drop_length = strlen(dropped_paths[0]) + 1;
    char *drop_text = malloc(drop_length);
    int drop_ok = drop_text && g_pending == ACTION_SCAN &&
        xxwidgets_widget_get_text(g_path, drop_text, drop_length, NULL) == XXWIDGETS_OK && !strcmp(drop_text, dropped_paths[0]);
    free(drop_text);
    if (!drop_ok) return 13;
    g_pending = ACTION_NONE;
    if (!do_scan()) return 2;
    size_t length = 0;
    if (xxwidgets_scan_panel_get_report(g_panel, NULL, 0, &length) != XXWIDGETS_OK || length <= 1 ||
        xxwidgets_treeview_count(control(XXWIDGETS_SCAN_PANEL_RESULTS)) == 0) return 3;
    xxwidgets_widget *combo = control(XXWIDGETS_SCAN_PANEL_FILE_TYPE);
    int found = 0;
    for (size_t i = 0; i < xxwidgets_combobox_count(combo); ++i) {
        const xx_meta_string *record;
        if (xxwidgets_combobox_get_record(combo, i, &record) == XXWIDGETS_OK &&
            wcscmp(record->meta_string->data, L"Binary") == 0) {
            xxwidgets_widget_set_value(combo, (int)i);
            if (!do_scan()) return 4;
            char status[512];
            xxwidgets_widget_get_text(g_status, status, sizeof(status), NULL);
            if (!strstr(status, "(Binary)")) return 5;
            found = 1; break;
        }
    }
    if (!found) return 6;
    open_path(@"/nonexistent/cdie-smoke-test-file"); g_pending = ACTION_NONE;
    if (do_scan() || xxwidgets_scan_panel_count(g_panel)) return 7;
    open_path(path); g_pending = ACTION_NONE;
    if (!do_scan() || xxwidgets_app_poll(g_app, 0) != XXWIDGETS_OK) return 8;
    {
        gui_file_information_data *data = NULL;
        xxwidgets_file_information *information = NULL;
        char *error = NULL;
        int ok = gui_file_information_read([path UTF8String], NULL, &data, &error);
        if (ok) ok = xxwidgets_file_information_create(g_window, (xxwidgets_rect){1, 1, 88, 20}, &information) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_file_information_set_data(information, data->path, data->file_type, data->size, data->items, data->count) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_data_struct_view_count(xxwidgets_file_information_view(information)) == data->count && data->count > 0;
        if (information && xxwidgets_file_information_destroy(information) != XXWIDGETS_OK) ok = 0;
        gui_file_information_free(data); gui_backend_free(error);
        if (!ok) return 15;
    }
    {
        cdie_entropy_data *data = NULL;
        xxwidgets_entropy *view = NULL;
        char *error = NULL;
        int ok = cdie_entropy_read([path UTF8String], NULL, &data, &error);
        if (ok) ok = xxwidgets_entropy_create(g_window, (xxwidgets_rect){1, 1, 100, 27}, &view) == XXWIDGETS_OK;
        if (ok) ok = gui_entropy_set_data(view, data) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_entropy_region_count(view) == data->region_count && data->region_count > 0;
        if (view && xxwidgets_entropy_destroy(view) != XXWIDGETS_OK) ok = 0;
        gui_backend_free(data); gui_backend_free(error);
        if (!ok) return 25;
    }
    {
        cdie_hash_data *data = NULL;
        xxwidgets_hash *view = NULL;
        char *error = NULL;
        int ok = cdie_hash_read([path UTF8String], NULL, &data, &error);
        if (ok) ok = xxwidgets_hash_create(g_window, (xxwidgets_rect){1, 1, 100, 20}, &view) == XXWIDGETS_OK;
        if (ok) ok = gui_hash_set_data(view, [path UTF8String], data) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_hash_count(view) == CDIE_HASH_COUNT;
        if (view && xxwidgets_hash_destroy(view) != XXWIDGETS_OK) ok = 0;
        gui_backend_free(data); gui_backend_free(error);
        if (!ok) return 16;
    }
    {
        xxwidgets_strings *view = NULL;
        xxwidgets_string_record record = {0, 18, 18, "ASCII", "CDIE Strings smoke", NULL, 0, 0};
        int ok = xxwidgets_strings_create(g_window, (xxwidgets_rect){1, 1, 100, 20}, &view) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_strings_set_records(view, &record, 1) == XXWIDGETS_OK && xxwidgets_strings_count(view) == 1;
        if (view && xxwidgets_strings_destroy(view) != XXWIDGETS_OK) ok = 0;
        if (!ok) return 17;
    }
    {
        xx_io_device *device = NULL;
        xxwidgets_hexview_browser *view = NULL;
        char *error = NULL;
        int ok = gui_hex_view_open([path UTF8String], &device, &error);
        if (ok) ok = xxwidgets_hexview_browser_create(g_window, (xxwidgets_rect){1, 1, 106, 24}, &view) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_hexview_browser_set_device(view, [path UTF8String], device) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_hexview_size(xxwidgets_hexview_browser_control(view, XXWIDGETS_HEX_BROWSER_HEX)) > 0;
        if (view && xxwidgets_hexview_browser_destroy(view) != XXWIDGETS_OK) ok = 0;
        if (device) xx_io_close(device);
        gui_backend_free(error);
        if (!ok) return 18;
    }
    {
        cdie_disasm_context *context = NULL;
        gui_disasm_source source;
        xxwidgets_disasm_browser *view = NULL;
        char *error = NULL;
        int ok = cdie_disasm_open([path UTF8String], NULL, &context, &error);
        if (ok) {
            gui_disasm_source_init(&source, context, [path UTF8String]);
            ok = xxwidgets_disasm_browser_create(g_window, (xxwidgets_rect){1,1,106,24}, &source.source, &view) == XXWIDGETS_OK;
        }
        if (ok) ok = xxwidgets_disasm_browser_count(view) > 0;
        if (view && xxwidgets_disasm_browser_destroy(view) != XXWIDGETS_OK) ok = 0;
        cdie_disasm_close(context); gui_backend_free(error);
        if (!ok) return 19;
    }
    {
        cdie_memory_map_data *data = NULL;
        xxwidgets_memory_map *view = NULL;
        char *error = NULL;
        int ok = cdie_memory_map_read([path UTF8String], NULL, &data, &error);
        if (ok) ok = xxwidgets_memory_map_create(g_window, (xxwidgets_rect){1,1,106,24}, &view) == XXWIDGETS_OK;
        if (ok) ok = gui_memory_map_set_data(view, [path UTF8String], data) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_memory_map_table_count(xxwidgets_memory_map_control(view, XXWIDGETS_MEMORY_MAP_TABLE)) > 0;
        if (view && xxwidgets_memory_map_destroy(view) != XXWIDGETS_OK) ok = 0;
        gui_backend_free(data); gui_backend_free(error);
        if (!ok) return 20;
    }
    {
        cdie_imports_data *data = NULL;
        xxwidgets_imports *view = NULL;
        char *error = NULL;
        int ok = cdie_imports_read([path UTF8String], NULL, &data, &error);
        if (ok) ok = xxwidgets_imports_create(g_window, (xxwidgets_rect){1,1,106,24}, &view) == XXWIDGETS_OK;
        if (ok) ok = gui_imports_set_data(view, [path UTF8String], data) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_imports_visible_count(view) > 0;
        if (view && xxwidgets_imports_destroy(view) != XXWIDGETS_OK) ok = 0;
        cdie_imports_free(data); gui_backend_free(error);
        if (!ok) return 21;
    }
    {
        xx_export_record record = {"CDIEExport", NULL, 7, 0, UINT64_C(0x100001000)};
        xxwidgets_exports_data snapshot = {"synthetic fixture", "Mach-O", 16, &record, 1};
        xxwidgets_exports *view = NULL;
        int ok = xxwidgets_exports_create(g_window, (xxwidgets_rect){1,1,106,24}, &view) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_exports_set_data(view, &snapshot) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_exports_visible_count(view) == 1;
        if (view && xxwidgets_exports_destroy(view) != XXWIDGETS_OK) ok = 0;
        if (!ok) return 22;
    }
    {
        xx_resource_record record = {10, 1, 1033, NULL, NULL, NULL, 0, 16, 1200};
        xxwidgets_resources_data snapshot = {"synthetic fixture", "PE resource fixture", 16, &record, 1};
        xxwidgets_resources *view = NULL;
        int ok = xxwidgets_resources_create(g_window, (xxwidgets_rect){1,1,106,24}, &view) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_resources_set_data(view, &snapshot) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_resources_visible_count(view) == 1;
        if (view && xxwidgets_resources_destroy(view) != XXWIDGETS_OK) ok = 0;
        if (!ok) return 23;
    }
    {
        xx_symbol_record record = {0};
        xxwidgets_symbols_data snapshot;
        xxwidgets_symbols *view = NULL;
        record.name = "CDIESymbol"; record.file_name = "test.c"; record.kind = XX_SYMBOL_KIND_FUNCTION;
        record.binding = XX_SYMBOL_BINDING_GLOBAL; record.address = UINT64_C(0x100001000);
        snapshot = (xxwidgets_symbols_data){"synthetic fixture", "Mach-O", 16, &record, 1};
        int ok = xxwidgets_symbols_create(g_window, (xxwidgets_rect){1,1,106,24}, &view) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_symbols_set_data(view, &snapshot) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_symbols_visible_count(view) == 1;
        if (view && xxwidgets_symbols_destroy(view) != XXWIDGETS_OK) ok = 0;
        if (!ok) return 24;
    }
    {
        gui_extractor_source *source = NULL;
        xxwidgets_extractor *view = NULL;
        char *error = NULL;
        int ok = gui_extractor_source_open([path UTF8String], NULL, &source, &error) == XXWIDGETS_OK;
        if (ok) ok = gui_extractor_source_model(source)->format_count > 0 &&
            xxwidgets_extractor_create(g_window, (xxwidgets_rect){1, 1, 120, 30}, gui_extractor_source_model(source), &view) == XXWIDGETS_OK;
        if (ok) ok = xxwidgets_extractor_control(view, XXWIDGETS_EXTRACTOR_TABLE) && xxwidgets_extractor_control(view, XXWIDGETS_EXTRACTOR_SCAN);
        if (view && xxwidgets_extractor_destroy(view) != XXWIDGETS_OK) ok = 0;
        gui_extractor_source_free(source); gui_backend_free(error);
        if (!ok) return 26;
    }
    {
        static const struct { const char *title; xxwidgets_widget **button; int action, check; } tools[] = {
            {"File Information", &g_file_information, ACTION_FILE_INFORMATION, SMOKE_CHECK_TOOL}, {"Entropy", &g_entropy, ACTION_ENTROPY, SMOKE_CHECK_TOOL},
            {"Hash", &g_hash, ACTION_HASH, SMOKE_CHECK_TOOL}, {"Strings", &g_strings, ACTION_STRINGS, SMOKE_CHECK_TOOL},
            {"Hex View", &g_hex_view, ACTION_HEX_VIEW, SMOKE_CHECK_TOOL}, {"Disasm", &g_disasm, ACTION_DISASM, SMOKE_CHECK_TOOL},
            {"Memory Map", &g_memory_map, ACTION_MEMORY_MAP, SMOKE_CHECK_TOOL}, {"Imports", &g_imports, ACTION_IMPORTS, SMOKE_CHECK_TOOL},
            /* A Mach-O may have no exports, resources or symbols: an empty table or the error text is right then. */
            {"Exports", &g_exports, ACTION_EXPORTS, SMOKE_CHECK_TOOL_OR_ERROR}, {"Resources", &g_resources, ACTION_RESOURCES, SMOKE_CHECK_TOOL_OR_ERROR},
            {"Symbols", &g_symbols, ACTION_SYMBOLS, SMOKE_CHECK_TOOL_OR_ERROR}, {"Extractor", &g_extractor, ACTION_EXTRACTOR, SMOKE_CHECK_EXTRACTOR}};
        CDIEApplication *delegate = (CDIEApplication *)[NSApp delegate];
        xxwidgets_widget *results = control(XXWIDGETS_SCAN_PANEL_RESULTS);
        NSString *expected = nil;
        NSMenuItem *item = smoke_menu(ACTION_REPORT, nil);
        gui_options_values preferences;
        char before[GUI_PREFERENCE_PATH_MAX], after[GUI_PREFERENCE_PATH_MAX], *text, *real;
        const char *message, *label;
        size_t i, count, report_size = 0;
        int automatic = 0, history, ok;
        xxwidgets_status status;

        /* 27-28: without a report, Report and its menu item do nothing; an
         * Automatic scan, which the cases below use, enables both. */
        if (!delegate || !item || xxwidgets_scan_panel_clear(g_panel) != XXWIDGETS_OK || report_available() || [delegate validateMenuItem:item] ||
            !smoke_begin("Scan report", SMOKE_ABSENT, SMOKE_CHECK_NONE, NULL, nil)) return 27;
        run_action(ACTION_REPORT);
        if (!smoke_finish()) return 27;
        for (i = 0; i < xxwidgets_combobox_count(combo) && !automatic; ++i) {
            const xx_meta_string *record;
            if (xxwidgets_combobox_get_record(combo, i, &record) == XXWIDGETS_OK && record->var.type == XX_VAR_TYPE_UINT64 && !record->var.val.u64)
                automatic = xxwidgets_widget_set_value(combo, (int)i) == XXWIDGETS_OK;
        }
        if (!automatic || !do_scan() || !xxwidgets_scan_panel_count(g_panel) || !report_available() || ![delegate validateMenuItem:item]) return 28;
        /* 29: File > Report. */
        if (xxwidgets_scan_panel_get_report(g_panel, NULL, 0, &report_size) != XXWIDGETS_OK || !(text = malloc(report_size))) return 29;
        if (xxwidgets_scan_panel_get_report(g_panel, text, report_size, NULL) == XXWIDGETS_OK) expected = string(text);
        free(text);
        g_pending = ACTION_NONE;
        [delegate action:item];
        if (!expected || g_pending != ACTION_REPORT || !smoke_begin("Scan report", SMOKE_BUTTON, SMOKE_CHECK_TEXT, "Close", expected)) return 29;
        g_pending = ACTION_NONE;
        run_action(ACTION_REPORT);
        if (!smoke_finish()) return 29;
        /* 30: S on a detection row opens its script in Signatures. */
        count = xxwidgets_treeview_count(results);
        for (i = 0; i < count && !(xxwidgets_treeview_node_marks(results, i) & 1u); ++i) {}
        label = xxwidgets_treeview_mark_count(results) == 2 ? xxwidgets_treeview_mark_label(results, 0) : NULL;
        if (i == count || !label || strcmp(label, "S")) return 30;
        {
            xxwidgets_event press = {0};
            press.type = XXWIDGETS_EVENT_CLICK; press.widget = results; press.value = (int)i; press.x = 0;
            if (!gui_tree_event(g_panel, &press) || !smoke_begin("Signatures", SMOKE_BUTTON, SMOKE_CHECK_SIGNATURES, "Close", nil)) return 30;
        }
        message = gui_tree_process(g_window);
        if (!smoke_finish() || message) return 30;
        /* 31-32: the panel's Log and Directory buttons. */
        if (!smoke_press(control(XXWIDGETS_SCAN_PANEL_LOG)) || !smoke_begin("Log", SMOKE_ESCAPE, SMOKE_CHECK_TEXT, NULL, nil)) return 31;
        message = gui_tree_process(g_window);
        if (!smoke_finish() || message) return 31;
        expected = [[path stringByExpandingTildeInPath] stringByDeletingLastPathComponent];
        if (!smoke_press(control(XXWIDGETS_SCAN_PANEL_DIRECTORY)) ||
            !smoke_begin("Scan directory", SMOKE_BUTTON, SMOKE_CHECK_DIRECTORY, "Close", expected)) return 32;
        message = gui_tree_process(g_window);
        if (!smoke_finish() || message) return 32;
        /* 33-35: About from Help (Cmd+?), then Escape and the close box. */
        item = smoke_menu(ACTION_ABOUT, @"?");
        g_pending = ACTION_NONE;
        if (item && [delegate validateMenuItem:item]) [delegate action:item];
        if (g_pending != ACTION_ABOUT || !smoke_begin("About", SMOKE_BUTTON, SMOKE_CHECK_ABOUT, "OK", nil)) return 33;
        g_pending = ACTION_NONE;
        run_action(ACTION_ABOUT);
        if (!smoke_finish()) return 33;
        if (!smoke_begin("About", SMOKE_ESCAPE, SMOKE_CHECK_ABOUT, NULL, nil)) return 34;
        do_about();
        if (!smoke_finish()) return 34;
        if (!smoke_begin("About", SMOKE_CLOSE_BOX, SMOKE_CHECK_ABOUT, NULL, nil)) return 35;
        do_about();
        if (!smoke_finish()) return 35;
        /* 36-45: Options opens on every page; Cancel keeps that page. */
        for (i = 0; i < GUI_OPTIONS_PAGE_COUNT; ++i) {
            if (!smoke_options((gui_options_page_id)i, SMOKE_BUTTON, "Cancel")) return 36 + (int)i;
            do_options((gui_options_page_id)i);
            if (!smoke_finish() || g_options_page != (gui_options_page_id)i) return 36 + (int)i;
        }
        /* 46-47: "Database folders…" (Cmd+Shift+D) with Escape; the close box. */
        item = smoke_menu(ACTION_DATABASES, @"D");
        g_pending = ACTION_NONE;
        if (item && [delegate validateMenuItem:item]) [delegate action:item];
        if (g_pending != ACTION_DATABASES || !smoke_options(GUI_OPTIONS_SCAN, SMOKE_ESCAPE, NULL)) return 46;
        g_pending = ACTION_NONE;
        run_action(ACTION_DATABASES);
        if (!smoke_finish()) return 46;
        if (!smoke_options(GUI_OPTIONS_FONTS, SMOKE_CLOSE_BOX, NULL)) return 47;
        do_options(GUI_OPTIONS_FONTS);
        if (!smoke_finish()) return 47;
        /* 48-51: Colors over Options, and the pickers Options pages run. */
        if (!smoke_options(GUI_OPTIONS_SCAN, SMOKE_NESTED, "Colors...")) return 48;
        do_options(GUI_OPTIONS_SCAN);
        if (!smoke_finish()) return 48;
        if (!smoke_options(GUI_OPTIONS_SCAN, SMOKE_PICKER, "Browse...")) return 49;
        g_smoke.folder = 1;
        do_options(GUI_OPTIONS_SCAN);
        if (!smoke_finish()) return 49;
        if (!smoke_options(GUI_OPTIONS_APPEARANCE, SMOKE_PICKER, "Choose translation...")) return 50;
        do_options(GUI_OPTIONS_APPEARANCE);
        if (!smoke_finish()) return 50;
        if (!smoke_options(GUI_OPTIONS_INFO, SMOKE_PICKER, "Browse...")) return 51;
        g_smoke.folder = 1;
        do_options(GUI_OPTIONS_INFO);
        if (!smoke_finish()) return 51;
        /* 52: Open… cancelled changes nothing. */
        if (xxwidgets_widget_get_text(g_path, before, sizeof(before), NULL) != XXWIDGETS_OK ||
            !smoke_begin("Choose a file to scan", SMOKE_PANEL, SMOKE_CHECK_NONE, NULL, nil)) return 52;
        g_pending = ACTION_NONE;
        run_action(ACTION_OPEN);
        if (!smoke_finish() || g_pending != ACTION_NONE || xxwidgets_widget_get_text(g_path, after, sizeof(after), NULL) != XXWIDGETS_OK ||
            strcmp(before, after)) return 52;
        /* 53-55: Recent files: Cancel; OK opens the latest file; nothing with history off. */
        item = smoke_menu(ACTION_RECENT, nil);
        if (!item || ![delegate validateMenuItem:item] || !smoke_begin("Recent files", SMOKE_BUTTON, SMOKE_CHECK_RECENT, "Cancel", nil)) return 53;
        g_pending = ACTION_NONE;
        run_action(ACTION_RECENT);
        if (!smoke_finish() || g_pending != ACTION_NONE) return 53;
        real = realpath([path fileSystemRepresentation], NULL);
        if (!real || !smoke_begin("Recent files", SMOKE_BUTTON, SMOKE_CHECK_RECENT, "OK", nil)) { free(real); return 54; }
        run_action(ACTION_RECENT);
        ok = smoke_finish() && xxwidgets_widget_get_text(g_path, after, sizeof(after), NULL) == XXWIDGETS_OK && !strcmp(after, real) &&
            g_pending == (gui_frontend_scan_after_open() ? ACTION_SCAN : ACTION_NONE);
        free(real);
        g_pending = ACTION_NONE;
        if (!ok || xxwidgets_widget_set_text(g_path, before) != XXWIDGETS_OK) return 54;
        gui_preferences_get(&preferences);
        history = preferences.file.save_recent_files;
        preferences.file.save_recent_files = 0;
        gui_preferences_set(&preferences);
        ok = ![delegate validateMenuItem:item] && smoke_begin("Recent files", SMOKE_ABSENT, SMOKE_CHECK_NONE, NULL, nil);
        if (ok) { run_action(ACTION_RECENT); ok = smoke_finish() && g_pending == ACTION_NONE; }
        preferences.file.save_recent_files = history;
        gui_preferences_set(&preferences);
        if (!ok) return 55;
        /* 56-68: each tool button opens its dialog (Close); Hash once more with Escape. */
        for (i = 0; i < sizeof(tools) / sizeof(tools[0]); ++i) {
            g_pending = ACTION_NONE;
            if (!smoke_press(*tools[i].button) || g_pending != tools[i].action ||
                !smoke_begin(tools[i].title, SMOKE_BUTTON, tools[i].check, "Close", nil)) return 56 + (int)i;
            g_pending = ACTION_NONE;
            run_action(tools[i].action);
            if (!smoke_finish()) return 56 + (int)i;
        }
        if (!smoke_begin("Hash", SMOKE_ESCAPE, SMOKE_CHECK_TOOL, NULL, nil)) return 68;
        run_action(ACTION_HASH);
        if (!smoke_finish()) return 68;
        /* 69-71: the dialogs a Mach-O may leave empty, on one synthetic row. */
        {
            xx_export_record record = {"CDIEExport", NULL, 7, 0, UINT64_C(0x100001000)};
            xxwidgets_exports_data snapshot = {"synthetic fixture", "Mach-O", 16, &record, 1};
            if (!smoke_begin("Exports", SMOKE_BUTTON, SMOKE_CHECK_TOOL, "Close", nil)) return 69;
            status = xxwidgets_exports_dialog(g_window, CDIE_TR("Exports"), &snapshot);
            if (!smoke_finish() || status != XXWIDGETS_OK) return 69;
        }
        {
            xx_resource_record record = {10, 1, 1033, NULL, NULL, NULL, 0, 16, 1200};
            xxwidgets_resources_data snapshot = {"synthetic fixture", "PE resource fixture", 16, &record, 1};
            if (!smoke_begin("Resources", SMOKE_BUTTON, SMOKE_CHECK_TOOL, "Close", nil)) return 70;
            status = xxwidgets_resources_dialog(g_window, CDIE_TR("Resources"), &snapshot);
            if (!smoke_finish() || status != XXWIDGETS_OK) return 70;
        }
        {
            xx_symbol_record record = {0};
            xxwidgets_symbols_data snapshot;
            record.name = "CDIESymbol"; record.file_name = "test.c"; record.kind = XX_SYMBOL_KIND_FUNCTION;
            record.binding = XX_SYMBOL_BINDING_GLOBAL; record.address = UINT64_C(0x100001000);
            snapshot = (xxwidgets_symbols_data){"synthetic fixture", "Mach-O", 16, &record, 1};
            if (!smoke_begin("Symbols", SMOKE_BUTTON, SMOKE_CHECK_TOOL, "Close", nil)) return 71;
            status = xxwidgets_symbols_dialog(g_window, CDIE_TR("Symbols"), &snapshot);
            if (!smoke_finish() || status != XXWIDGETS_OK) return 71;
        }
        /* 72: a progress dialog appears after a second of work; Cancel reaches the job. */
        {
            smoke_steps steps = {0, 0};
            xx_pd_struct progress = xx_pd_init();
            if (!smoke_begin("Smoke progress", SMOKE_BUTTON, SMOKE_CHECK_PROGRESS, "Cancel", @"Smoke step")) return 72;
            status = xxwidgets_process_dialog(g_window, "Smoke progress", &progress, smoke_progress_update, &steps);
            if (!smoke_finish() || status != XXWIDGETS_OK || !steps.stopped) return 72;
        }
        /* 73: Update database owned by the main window, against a loopback
         * port that never answers (offline), cancelled from its dialog. */
        if (gui_update_supported()) {
            static const char *const names[2] = {"CDIE_UPDATE_URL", "CDIE_UPDATE_NOPROXY"};
            char *saved[2], url[96], line[640] = "";
            uint64_t generation = gui_backend_database_generation();
            int port = 0, signatures = gui_backend_signature_count(), fd = smoke_blackhole(&port);
            gui_update_result result = GUI_UPDATE_FAILED;
            if (fd < 0) return 73;
            for (i = 0; i < 2; ++i) saved[i] = getenv(names[i]) ? strdup(getenv(names[i])) : NULL;
            snprintf(url, sizeof(url), "http://127.0.0.1:%d/cdie-smoke/db.zip", port);
            setenv("CDIE_UPDATE_URL", url, 1);
            setenv("CDIE_UPDATE_NOPROXY", "1", 1);
            ok = smoke_begin("Updating Database", SMOKE_BUTTON, SMOKE_CHECK_CANCEL, "Cancel", nil);
            if (ok) { result = do_update(g_window); ok = smoke_finish(); }
            /* The worker has finished: nothing writes to the port any more. */
            close(fd);
            for (i = 0; i < 2; ++i) {
                if (saved[i]) setenv(names[i], saved[i], 1); else unsetenv(names[i]);
                free(saved[i]);
            }
            if (xxwidgets_widget_get_text(g_status, line, sizeof(line), NULL) != XXWIDGETS_OK) line[0] = 0;
            if (!ok || result != GUI_UPDATE_CANCELLED) fprintf(stderr, "Cocoa dialog smoke: Updating Database: %s %s\n", gui_update_result_name(result), line);
            if (!ok || result != GUI_UPDATE_CANCELLED || strcmp(line, CDIE_TR("Database update cancelled.")) ||
                gui_backend_database_generation() != generation || gui_backend_signature_count() != signatures) return 73;
        }
    }
    printf("Cocoa GUI smoke test passed: %d signatures, file drop, scan, type selection, report, missing-file recovery, File Information, Entropy, Hash, Strings, Hex View, Disasm, Memory Map, Imports, Exports, Resources, Symbols, Extractor; dialogs: Scan report, Signatures, Log, Scan directory, About, Options (10 pages, Colors, 3 pickers), Open, Recent files, 12 tools, progress%s\n",
        gui_backend_signature_count(), gui_update_supported() ? ", Updating Database" : "");
    return 0;
}

/* --smoke-update values: a result name runs without dialogs; the owner
 * modes run with the main window, so the check's "No updates" box or the
 * update question opens, and the smoke driver answers it with button. */
typedef struct smoke_update_mode {
    const char *name, *result, *button, *expected;
    int check;
} smoke_update_mode;
static const smoke_update_mode smoke_update_modes[] = {
    {"installed", "installed", NULL, NULL, SMOKE_CHECK_NONE},
    {"current", "current", NULL, NULL, SMOKE_CHECK_NONE},
    {"failed", "failed", NULL, NULL, SMOKE_CHECK_NONE},
    {"box-current", "current", "Close", "No updates.", SMOKE_CHECK_UPDATE_BOX},
    {"ask-no", "cancelled", "No", "Install it now?", SMOKE_CHECK_QUESTION},
    {"ask-yes", "installed", "Yes", "Install it now?", SMOKE_CHECK_QUESTION},
};
static const smoke_update_mode *smoke_update_find(const char *name)
{
    for (size_t i = 0; i < sizeof(smoke_update_modes) / sizeof(smoke_update_modes[0]); ++i)
        if (name && !strcmp(smoke_update_modes[i].name, name)) return &smoke_update_modes[i];
    return NULL;
}
/* --smoke-update: one click on Update database; exits 0 when the result is the expected one. */
static int smoke_update(const smoke_update_mode *mode)
{
    xxwidgets_event click = {0};
    char text[640] = "";
    int ok = 1;
    xxwidgets_widget_set_text(g_status, "");
    click.type = XXWIDGETS_EVENT_CLICK; click.widget = g_update;
    g_pending = ACTION_NONE;
    on_event(g_app, &click, NULL);
    if (g_pending != ACTION_UPDATE) { fprintf(stderr, "smoke-update: the button did not request an update\n"); return 1; }
    g_pending = ACTION_NONE;
    if (mode->button && !smoke_begin("Update database", SMOKE_BUTTON, mode->check, mode->button, string(CDIE_TR(mode->expected)))) return 1;
    gui_update_result result = do_update(mode->button ? g_window : NULL);
    if (mode->button) ok = smoke_finish();
    if (xxwidgets_widget_get_text(g_status, text, sizeof(text), NULL) != XXWIDGETS_OK) text[0] = 0;
    fprintf(stderr, "smoke-update: %s %s\n", gui_update_result_name(result), text);
    if (ok) ok = !strcmp(gui_update_result_name(result), mode->result);
    if (ok && result == GUI_UPDATE_INSTALLED) ok = gui_backend_signature_count() > 0 && text[0];
    return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        const char *translation=NULL;
        if (!cdie_translation_arguments(&argc,argv,&translation)) { fprintf(stderr,"--translation requires a PO file\n"); return 1; }
        BOOL smoke=NO, smoke_scan=NO, positional=NO;
        NSString *startup=nil;
        const char *update_expected=NULL;
        for (int i=1;i<argc;++i) {
            if (!positional && !strcmp(argv[i],"--")) { positional=YES; continue; }
            if (!positional && !strcmp(argv[i],"--smoke-test")) { smoke=smoke_scan=YES; continue; }
            if (!positional && !strcmp(argv[i],"--smoke-update")) {
                /* Checked before anything runs, as on Windows: a bad value must not download first. */
                if (i+1>=argc || !smoke_update_find(argv[i+1])) {
                    fprintf(stderr,"--smoke-update requires installed, current, failed, box-current, ask-no or ask-yes\n"); return 2;
                }
                smoke=YES; update_expected=argv[++i]; continue;
            }
            if (!positional && !strncmp(argv[i],"-psn_",5)) continue;
            if (startup) { fprintf(stderr,"Only one initial file is supported.\n"); return 1; }
            startup=string(argv[i]);
        }
        if (update_expected && (smoke_scan || startup)) { fprintf(stderr,"--smoke-update cannot be combined with --smoke-test or a file\n"); return 2; }
        xxwidgets_config config = {XXWIDGETS_BACKEND_NATIVE, on_event, NULL};
        xxwidgets_status status = xxwidgets_app_create(&config, &g_app);
        if (status != XXWIDGETS_OK) { fprintf(stderr, "%s\n", xxwidgets_status_string(status)); return 1; }
        int result = 0;
        CDIEApplication *delegate = nil;
        size_t translation_line=0;
        char *stored_translation = !translation && !getenv("CDIE_TRANSLATION") ? gui_frontend_stored_language(smoke) : NULL;
        status=cdie_translation_open(g_app,translation ? translation : stored_translation,&translation_line);
        if (status != XXWIDGETS_OK && stored_translation) {
            fprintf(stderr,"Saved translation cannot be loaded; using System language.\n");
            status = cdie_translation_open(g_app,NULL,&translation_line);
        }
        free(stored_translation);
        if (status != XXWIDGETS_OK) { fprintf(stderr,"Cannot load translation (line %zu)\n",translation_line); result=1; goto finish; }
        if (xxwidgets_widget_create(g_app, NULL, XXWIDGETS_WINDOW, gui_backend_app_title(),
                (xxwidgets_rect){4, 3, 146, 34}, &g_window) != XXWIDGETS_OK || !create_controls()) {
            result = 1; goto finish;
        }
        xxwidgets_window_set_minimum_size(g_window, 146, 28);
        if (xxwidgets_window_set_accept_file_drops(g_window, 1) != XXWIDGETS_OK) { result = 1; goto finish; }
        NSWindow *window = xxwidgets_widget_native_handle(g_window);
        delegate = [[CDIEApplication alloc] init];
        [NSApp setDelegate:delegate];
        install_menu(delegate);
        gui_backend_init();
        if (!smoke) {
            /* Folders saved by earlier versions (main, extra, custom); the
             * shared settings below win. The extra folder is not used any more. */
            NSArray *paths = [[NSUserDefaults standardUserDefaults] arrayForKey:@"DatabasePaths"];
            if ([paths count] == 3 && [paths[0] isKindOfClass:[NSString class]] && [paths[2] isKindOfClass:[NSString class]])
                gui_backend_set_db_paths([paths[0] UTF8String], [paths[2] UTF8String]);
        }
        int settings_read = 1, settings_databases = 1;
        {
            /* Saved Options apply before the first scan; smoke tests keep them in memory. */
            gui_options_values values;
            g_settings = gui_settings_open(smoke, &settings_read);
            status = current_options(&values);
            if (status == XXWIDGETS_OK) settings_databases = gui_settings_load(g_settings, &values, &g_options_page);
            if (status == XXWIDGETS_OK) status = apply_options(&values);
            if (status != XXWIDGETS_OK) { report_ui_error("Apply saved options", status); result = 1; goto finish; }
        }
        status = refresh_panel_tags();
        if (status != XXWIDGETS_OK) { report_ui_error(CDIE_N_("Load signature tags"), status); result = 1; goto finish; }
        ready(); layout();
        if (!settings_databases) xxwidgets_widget_set_text(g_status, CDIE_TR("The saved signature folders cannot be loaded; the default ones are active."));
        else if (!settings_read) xxwidgets_widget_set_text(g_status, CDIE_TR("Saved options cannot be read; the defaults are used."));
        if (update_expected) { result = smoke_update(smoke_update_find(update_expected)); goto finish; }
        if (smoke) { result = smoke_test(startup ?: [[NSBundle mainBundle] executablePath]); goto finish; }
        {
            int instance=gui_frontend_instance_start(g_window,startup?[startup UTF8String]:NULL);
            if(instance<=0){if(instance<0){fprintf(stderr,"Cannot acquire application instance lock.\n");result=1;}goto finish;}
        }
        [window center]; [window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
        xxwidgets_widget_focus(g_path);
        if (startup) open_path(startup);
        while (g_running) {
            @autoreleasepool {
                status = xxwidgets_app_poll(g_app, 30);
                if (status != XXWIDGETS_OK) { result = 1; break; }
                if (!g_running) break;
                if (g_layout) { g_layout = 0; layout(); }
                const char *message = gui_tree_process(g_window);
                if (message) xxwidgets_widget_set_text(g_status, message);
                int action = g_pending; g_pending = ACTION_NONE;
                run_action(action);
            }
        }
finish:
        if (!smoke && g_settings && g_window) {
            gui_options_values values;
            if(current_options(&values)==XXWIDGETS_OK)(void)gui_settings_save(g_settings,&values,g_options_page);
        }
        gui_frontend_instance_close();
        gui_settings_close(g_settings);
        gui_tree_reset();
        gui_backend_shutdown();
        [g_type_path release]; g_type_path = nil;
        if (g_panel) xxwidgets_scan_panel_destroy(g_panel);
        (cdie_translation_close(), xxwidgets_app_destroy(g_app));
        [delegate release];
        return result;
    }
}
