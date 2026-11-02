/* Copyright (c) 2026 hors<horsicq@gmail.com> · SPDX-License-Identifier: MIT */
#include "app/cdie_translation.h"
#include "../gui/gui_backend.h"
#include "../gui/gui_tree.h"
#include "../gui/gui_scan_options.h"
#include "../gui/gui_options.h"
#include "gui_frontend_preferences.h"
#include "../gui/gui_about.h"
#include "../gui/gui_settings.h"
#include "global.h"
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_combobox.h>
#include <xxwidgets/xxwidgets_scan_panel.h>
#include <xxfclib/strings/xx_string.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

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
#include "gui_update.h"
#include "gui_error.h"
#include "gui_tools.h"
#include <gtk/gtk.h>

enum { ACTION_NONE, ACTION_SCAN, ACTION_REPORT, ACTION_QUIT, ACTION_OPTIONS, ACTION_BROWSE,
    ACTION_DATABASE_PATHS, ACTION_FONTS, ACTION_ABOUT, ACTION_RECENT, ACTION_UPDATE, ACTION_TOOL_BASE = 100 };
typedef struct tui_app {
    xxwidgets_app *app;
    xxwidgets_widget *window, *path, *status, *recent, *browse, *update, *options, *about, *tools[12];
    xxwidgets_scan_panel *panel;
    char *type_path;
    gui_options_page_id options_page;
    xx_settings *settings;
    int running, pending, busy, needs_layout;
} tui_app;
typedef struct file_type { int id; wchar_t *label; } file_type;
typedef struct file_types { file_type *items; size_t count; int failed; } file_types;
static void set_status(tui_app *ui, const char *message) { (void)xxwidgets_widget_set_text(ui->status, message); }
/* Failures on the status line, as sentences (gui_error.h), never status
 * names: a window that did not open, as the result tree says it (gui_tree.c),
 * or "<what failed>: <reason>". what and window are translated. */
static void report_window_error(tui_app *ui, const char *window, xxwidgets_status status)
{
    char message[512];
    cdie_translation_snprintf(message, sizeof(message), CDIE_N_("Cannot open %s. %s"), window, gui_error_status_text(status));
    set_status(ui, message);
}
static void report_error(tui_app *ui, const char *what, xxwidgets_status status)
{
    char message[512];
    cdie_translation_snprintf(message, sizeof(message), CDIE_N_("%s: %s"), what, gui_error_status_text(status));
    set_status(ui, message);
}
static xxwidgets_widget *control(tui_app *ui, xxwidgets_scan_panel_control_id id)
{
    return xxwidgets_scan_panel_control(ui->panel, id);
}

static char *copy_text(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
}

static void collect_type(int id, const char *name, void *user)
{
    file_types *types = user;
    file_type *items;
    wchar_t *label;
    if (types->failed) return;
    if (types->count >= SIZE_MAX / sizeof(*items) - 1) { types->failed = 1; return; }
    label = xx_str_utf8_to_unicode(id == 0 ? CDIE_TR("Automatic") : name);
    if (!label) { types->failed = 1; return; }
    items = realloc(types->items, (types->count + 1) * sizeof(*items));
    if (!items) { xx_str_wfree(label); types->failed = 1; return; }
    types->items = items;
    types->items[types->count++] = (file_type){id, label};
}

static int refresh_types(tui_app *ui, const char *path)
{
    file_types types = {0};
    xx_var selected = {0};
    xx_meta_string *records = NULL;
    xx_str_w_s *labels = NULL;
    xxwidgets_widget *combo = control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE);
    char *saved_path = NULL;
    int ok = 0, index = 0;
    if (ui->type_path && !strcmp(ui->type_path, path)) (void)xxwidgets_combobox_get_current(combo, &selected);
    if (!gui_backend_file_types(path, collect_type, &types) || types.failed || !types.count) goto cleanup;
    records = calloc(types.count, sizeof(*records));
    labels = calloc(types.count, sizeof(*labels));
    saved_path = copy_text(path);
    if (!records || !labels || !saved_path) goto cleanup;
    for (size_t i = 0; i < types.count; ++i) {
        labels[i].data = types.items[i].label;
        labels[i].length = wcslen(labels[i].data);
        labels[i].capacity = labels[i].length + 1;
        labels[i].is_view = true;
        records[i].meta_string = &labels[i];
        records[i].var.type = XX_VAR_TYPE_UINT64;
        records[i].var.val.u64 = (uint64_t)types.items[i].id;
        if (selected.type == XX_VAR_TYPE_UINT64 && selected.val.u64 == records[i].var.val.u64) index = (int)i;
    }
    ok = xxwidgets_combobox_set_records(combo, records, types.count) == XXWIDGETS_OK &&
        xxwidgets_widget_set_value(combo, index) == XXWIDGETS_OK;
    if (ok) { free(ui->type_path); ui->type_path = saved_path; saved_path = NULL; }
cleanup:
    for (size_t i = 0; i < types.count; ++i) xx_str_wfree(types.items[i].label);
    free(types.items); free(records); free(labels); free(saved_path);
    return ok;
}

static char *read_path(tui_app *ui)
{
    size_t size = 0;
    char *path;
    if (xxwidgets_widget_get_text(ui->path, NULL, 0, &size) != XXWIDGETS_OK || !size) return NULL;
    path = malloc(size);
    if (!path) return NULL;
    if (xxwidgets_widget_get_text(ui->path, path, size, NULL) != XXWIDGETS_OK) { free(path); return NULL; }
    if (path[0] == '~' && (path[1] == '/' || !path[1])) {
        const char *home = getenv("HOME");
        if (home) {
            size_t home_length = strlen(home), tail_length = strlen(path + 1);
            char *expanded;
            if (home_length > SIZE_MAX - tail_length - 1) { free(path); return NULL; }
            expanded = malloc(home_length + tail_length + 1);
            if (!expanded) { free(path); return NULL; }
            memcpy(expanded, home, home_length);
            memcpy(expanded + home_length, path + 1, tail_length + 1);
            free(path); path = expanded;
        }
    }
    return path;
}

static char *read_tags(const xxwidgets_scan_panel *panel, int include)
{
    size_t size = 0;
    char *tags;
    xxwidgets_status status = include ?
        xxwidgets_scan_panel_get_include_tags(panel, NULL, 0, &size) :
        xxwidgets_scan_panel_get_exclude_tags(panel, NULL, 0, &size);
    if (status != XXWIDGETS_OK || !size) return NULL;
    tags = (char *)malloc(size);
    if (!tags) return NULL;
    status = include ?
        xxwidgets_scan_panel_get_include_tags(panel, tags, size, NULL) :
        xxwidgets_scan_panel_get_exclude_tags(panel, tags, size, NULL);
    if (status != XXWIDGETS_OK) {
        free(tags);
        return NULL;
    }
    return tags;
}

static int scan(tui_app *ui, int smoke)
{
    char message[192], *type = NULL, *path = read_path(ui);
    char *include_tags = NULL, *exclude_tags = NULL;
    xx_var selected = {0};
    unsigned int flags = 0, databases = 0;
    int cancelled = 0, ok = 0;
    if (!path) { set_status(ui, CDIE_TR("Cannot read the file path (out of memory or widget error).")); return 0; }
    if (!path[0]) { set_status(ui, CDIE_TR("Enter a file path, then press Enter or F5.")); (void)xxwidgets_widget_focus(ui->path); goto cleanup; }
    if (!gui_backend_signature_count()) { set_status(ui, CDIE_TR("No signatures loaded. Choose the main folder in Options > Scan (F3).")); goto cleanup; }
    if (xxwidgets_scan_panel_clear(ui->panel) != XXWIDGETS_OK) {
        set_status(ui, CDIE_TR("Cannot clear the previous scan results.")); goto cleanup;
    }
    if (!refresh_types(ui, path)) {
        /* One sentence on every platform, with the file system's reason (gui_error.h). */
        char reason[512];
        (void)xxwidgets_widget_set_enabled(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0);
        gui_error_open_status(reason, sizeof(reason), path);
        set_status(ui, reason); goto cleanup;
    }
    if (xxwidgets_combobox_get_current(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), &selected) != XXWIDGETS_OK ||
        selected.type != XX_VAR_TYPE_UINT64 || selected.val.u64 > INT_MAX ||
        xxwidgets_scan_panel_get_flags(ui->panel, &flags) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_get_databases(ui->panel, &databases) != XXWIDGETS_OK) {
        set_status(ui, CDIE_TR("Cannot read the selected scan options.")); goto cleanup;
    }
    include_tags = read_tags(ui->panel, 1);
    exclude_tags = read_tags(ui->panel, 0);
    if (!include_tags || !exclude_tags || !gui_backend_set_tag_filters(include_tags, exclude_tags)) {
        set_status(ui, CDIE_TR("Cannot apply the selected tag filters.")); goto cleanup;
    }
    gui_backend_set_flags(flags & XXWIDGETS_SCAN_DEEP, flags & XXWIDGETS_SCAN_HEURISTIC,
        flags & XXWIDGETS_SCAN_VERBOSE, flags & XXWIDGETS_SCAN_AGGRESSIVE,
        flags & XXWIDGETS_SCAN_HIDE_UNKNOWN, flags & XXWIDGETS_SCAN_FORMAT);
    gui_backend_set_databases((databases & XXWIDGETS_SCAN_DATABASE_CUSTOM) != 0);
    set_status(ui, CDIE_TR("Scanning..."));
    ui->busy = 1;
    ok = gui_tree_scan(smoke ? NULL : ui->window, ui->panel, path, (int)selected.val.u64, &type, &cancelled);
    ui->busy = 0;
    if (ok) cdie_translation_snprintf(message, sizeof(message), CDIE_N_("Scan complete (%s). %zu detections."), type ? type : "Unknown", xxwidgets_scan_panel_count(ui->panel));
    else cdie_translation_snprintf(message, sizeof(message), CDIE_N_("%s"), cancelled ? CDIE_TR("Scan cancelled.") : CDIE_TR("Scan failed."));
    set_status(ui, message);
    /* The focus stays where it was, as on Windows. */
    if (ok) gui_frontend_remember_file(path);
    (void)xxwidgets_widget_set_enabled(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), 1);
cleanup:
    gui_backend_free(type);
    free(include_tags);
    free(exclude_tags);
    free(path);
    return ok;
}


/* The button labels, in gui_tools order (gui_tool_run opens them). */
static const char *tool_names[] = {CDIE_N_("File Information"), CDIE_N_("Entropy"), CDIE_N_("Hash"), CDIE_N_("Strings"), CDIE_N_("Hex View"), CDIE_N_("Disasm"), CDIE_N_("Memory Map"), CDIE_N_("Imports"), CDIE_N_("Exports"), CDIE_N_("Resources"), CDIE_N_("Symbols"), CDIE_N_("Extractor")};

static void layout(tui_app *ui)
{
    xxwidgets_rect bounds;
    if (xxwidgets_widget_get_rect(ui->window, &bounds) != XXWIDGETS_OK) return;
    int w = bounds.width, h = bounds.height, panel = h - 14 < 14 ? 14 : h - 14;
    xxwidgets_rect log_rect, report_rect;
    /* As on Windows: Recent files (a down arrow), the path, then "...". */
    (void)xxwidgets_widget_set_rect(ui->recent, (xxwidgets_rect){2, 1, 6, 2});
    (void)xxwidgets_widget_set_rect(ui->path, (xxwidgets_rect){9, 1, w - 18, 2});
    (void)xxwidgets_widget_set_rect(ui->browse, (xxwidgets_rect){w - 8, 1, 6, 2});
    /* The panel keeps its minimum height (14) in a short window. */
    (void)xxwidgets_scan_panel_set_rect(ui->panel, (xxwidgets_rect){2, 4, w - 4, panel});
    /* Update database in the panel's lower button row, after Log and short of
     * the report button, as on Windows. */
    if (xxwidgets_widget_get_rect(control(ui, XXWIDGETS_SCAN_PANEL_LOG), &log_rect) == XXWIDGETS_OK &&
        xxwidgets_widget_get_rect(control(ui, XXWIDGETS_SCAN_PANEL_REPORT), &report_rect) == XXWIDGETS_OK) {
        int x = log_rect.x + log_rect.width + 1, room = report_rect.x - 1 - x;
        if (room > 0) (void)xxwidgets_widget_set_rect(ui->update, (xxwidgets_rect){x, log_rect.y, room < 20 ? room : 20, 2});
    }
    /* Under the panel, as on Windows: the status line (two rows, for long
     * messages), then Options, About, Imports ... Extractor, then File
     * Information ... Memory Map, in two rows of seven. */
    (void)xxwidgets_widget_set_rect(ui->status, (xxwidgets_rect){2, h - 9, w - 4, 2});
    for (int i = 0; i < 14; ++i) {
        xxwidgets_widget *button = i == 0 ? ui->options : i == 1 ? ui->about : i < 7 ? ui->tools[i + 5] : ui->tools[i - 7];
        int row = i / 7, col = i % 7;
        int x = 2 + col * (w - 4) / 7;
        int end = 2 + (col + 1) * (w - 4) / 7;
        (void)xxwidgets_widget_set_rect(button, (xxwidgets_rect){x, h - 6 + row * 3, end - x - 1, 2});
    }
}
static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    tui_app *ui = user; (void)app;
    if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == ui->window) { ui->needs_layout = 1; return; }
    if (ui->busy) return;
    if (event->type == XXWIDGETS_EVENT_CLOSE && event->widget == ui->window) ui->running = 0;
    else if (event->type == XXWIDGETS_EVENT_DROP_FILES && event->widget == ui->window && event->path_count) {
        (void)xxwidgets_widget_set_text(ui->path, event->paths[0]);
        gui_frontend_remember_file(event->paths[0]);
        ui->pending = gui_frontend_scan_after_open() ? ACTION_SCAN : ACTION_NONE;
    } else if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
        if (event->value == ACTION_QUIT) ui->running = 0; else ui->pending = event->value;
    } else if (event->type == XXWIDGETS_EVENT_ACTIVATE && event->widget == ui->path) ui->pending = ACTION_SCAN;
    else if (event->type == XXWIDGETS_EVENT_CHANGE && event->widget == ui->path) {
        /* As on Windows: the results stay; the file types describe the last
         * scanned file, so they wait for the next scan. */
        (void)xxwidgets_widget_set_enabled(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0);
    } else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE)) {
        size_t size = 0;
        if (xxwidgets_widget_get_text(ui->path, NULL, 0, &size) == XXWIDGETS_OK && size > 1) ui->pending = ACTION_SCAN;
    } else if (gui_tree_event(ui->panel, event)) {
        /* S or ? on a result row: shown after polling returns. */
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == ui->browse) ui->pending = ACTION_BROWSE;
        else if (event->widget == ui->recent) ui->pending = ACTION_RECENT;
        else if (event->widget == ui->update) ui->pending = ACTION_UPDATE;
        else if (event->widget == ui->options) ui->pending = ACTION_OPTIONS;
        else if (event->widget == ui->about) ui->pending = ACTION_ABOUT;
        else if (event->widget == control(ui, XXWIDGETS_SCAN_PANEL_SCAN)) ui->pending = ACTION_SCAN;
        else if (event->widget == control(ui, XXWIDGETS_SCAN_PANEL_REPORT)) ui->pending = ACTION_REPORT;
        else for (int i = 0; i < 12; ++i) if (event->widget == ui->tools[i]) ui->pending = ACTION_TOOL_BASE + i;
    }
}
/* Ctrl+U does what its button does, so nothing without an updater, as on
 * Windows. Report and Recent files say why on the status line (run_action). */
static int shortcut_allowed(xxwidgets_widget *window, int action, void *user)
{
    (void)window; (void)user;
    return action != ACTION_UPDATE || gui_update_supported();
}
static int create_controls(tui_app *ui, const char *path)
{
    /* The Windows keys, the tools in tool_names order; F3 (also Ctrl+Shift+D)
     * and F4 open Options on Scan and Fonts, Ctrl+Q quits. */
    static const xxwidgets_shortcut shortcuts[] = {{"F5", ACTION_SCAN}, {"Ctrl+R", ACTION_RECENT}, {"Ctrl+Shift+R", ACTION_REPORT}, {"Ctrl+Q", ACTION_QUIT}, {"Ctrl+O", ACTION_BROWSE}, {"F1", ACTION_ABOUT}, {"F2", ACTION_OPTIONS}, {"Ctrl+Shift+D", ACTION_DATABASE_PATHS}, {"F3", ACTION_DATABASE_PATHS}, {"F4", ACTION_FONTS}, {"Ctrl+U", ACTION_UPDATE},
        {"Ctrl+I", ACTION_TOOL_BASE + 0}, {"Ctrl+E", ACTION_TOOL_BASE + 1}, {"Ctrl+H", ACTION_TOOL_BASE + 2}, {"Ctrl+Shift+S", ACTION_TOOL_BASE + 3}, {"Ctrl+Shift+X", ACTION_TOOL_BASE + 4}, {"Ctrl+Shift+A", ACTION_TOOL_BASE + 5},
        {"Ctrl+M", ACTION_TOOL_BASE + 6}, {"Ctrl+Shift+I", ACTION_TOOL_BASE + 7}, {"Ctrl+Shift+E", ACTION_TOOL_BASE + 8}, {"Ctrl+Shift+U", ACTION_TOOL_BASE + 9}, {"Ctrl+Shift+Y", ACTION_TOOL_BASE + 10}, {"Ctrl+Shift+T", ACTION_TOOL_BASE + 11}};
    if (xxwidgets_widget_create(ui->app, NULL, XXWIDGETS_WINDOW, gui_backend_app_title(), (xxwidgets_rect){4, 3, 132, 34}, &ui->window) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_EDIT, path ? path : "", (xxwidgets_rect){9, 1, 114, 2}, &ui->path) != XXWIDGETS_OK ||
        /* A down arrow, as a history list, as on Windows; ">" belongs to the report. */
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_BUTTON, "\xE2\x96\xBC", (xxwidgets_rect){2, 1, 6, 2}, &ui->recent) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_BUTTON, CDIE_TR("..."), (xxwidgets_rect){124, 1, 6, 2}, &ui->browse) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_create(ui->window, (xxwidgets_rect){2, 4, 128, 20}, &ui->panel) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_BUTTON, CDIE_TR("Update database"), (xxwidgets_rect){22, 22, 20, 2}, &ui->update) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_LABEL, "", (xxwidgets_rect){2, 25, 128, 2}, &ui->status) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_BUTTON, CDIE_TR("Options"), (xxwidgets_rect){2, 28, 18, 2}, &ui->options) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_BUTTON, CDIE_TR("About"), (xxwidgets_rect){2, 28, 18, 2}, &ui->about) != XXWIDGETS_OK) return 0;
    for (int i = 0; i < 12; ++i)
        if (xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_BUTTON, CDIE_TR(tool_names[i]), (xxwidgets_rect){2, 28, 18, 2}, &ui->tools[i]) != XXWIDGETS_OK) return 0;
    char *tags = gui_backend_used_tags();
    if (!tags) return 0;
    xxwidgets_status s = xxwidgets_scan_panel_set_available_tags(ui->panel, tags); gui_backend_free(tags);
    /* Windows' defaults: Deep, Heuristic and Verbose; the file types are
     * enabled from the start (Automatic). */
    if (s != XXWIDGETS_OK || xxwidgets_window_set_shortcuts(ui->window, shortcuts, sizeof(shortcuts)/sizeof(shortcuts[0])) != XXWIDGETS_OK ||
        xxwidgets_window_set_shortcut_filter(ui->window, shortcut_allowed, NULL) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_set_flags(ui->panel, XXWIDGETS_SCAN_DEEP | XXWIDGETS_SCAN_HEURISTIC | XXWIDGETS_SCAN_VERBOSE) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_set_databases(ui->panel, XXWIDGETS_SCAN_DATABASE_CUSTOM) != XXWIDGETS_OK ||
        xxwidgets_widget_set_enabled(ui->update, gui_update_supported()) != XXWIDGETS_OK ||
        xxwidgets_window_set_minimum_size(ui->window, 120, 31) != XXWIDGETS_OK ||
        xxwidgets_window_set_accept_file_drops(ui->window, 1) != XXWIDGETS_OK) return 0;
    /* The tooltips Windows has; the backend may have none (no-op). */
    (void)xxwidgets_widget_set_tooltip(ui->recent, CDIE_TR("Recent files (Ctrl+R)"));
    (void)xxwidgets_widget_set_tooltip(control(ui, XXWIDGETS_SCAN_PANEL_REPORT), CDIE_TR("Scan report (Ctrl+Shift+R)"));
    {
        /* Windows names the path box after its "File name" label; here it
         * has no label, so the name is a placeholder and for screen readers. */
        GtkWidget *entry = GTK_WIDGET(xxwidgets_widget_native_handle(ui->path));
        if (GTK_IS_ENTRY(entry)) {
            gtk_entry_set_placeholder_text(GTK_ENTRY(entry), CDIE_TR("File name"));
            atk_object_set_name(gtk_widget_get_accessible(entry), CDIE_TR("File name"));
        }
    }
    layout(ui); (void)xxwidgets_widget_focus(ui->path);
    set_status(ui, gui_backend_signature_count() ? CDIE_TR("Choose a file, drop one here, or enter its path.") :
        CDIE_TR("No signatures loaded. Choose the main folder in Options > Scan (F3).")); return 1;
}
/* Starts a chooser in a UTF-8 folder (relative ones from the working
 * directory); 0 when there is none or GTK cannot open it. */
static int chooser_start(GtkWidget *dialog, const char *folder)
{
    gchar *filename = folder && folder[0] ? g_filename_from_utf8(folder, -1, NULL, NULL, NULL) : NULL;
    int started = 0;
    if (filename) {
        gchar *cwd = g_get_current_dir();
        gchar *absolute = g_path_is_absolute(filename) ? g_strdup(filename) : g_build_filename(cwd, filename, NULL);
        started = g_file_test(absolute, G_FILE_TEST_IS_DIR) && gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), absolute);
        g_free(absolute); g_free(cwd); g_free(filename);
    }
    return started;
}
/* The last directory, when Options > File keeps it. */
static int chooser_start_last(GtkWidget *dialog)
{
    gui_options_values preferences;
    gui_preferences_get(&preferences);
    return preferences.file.save_last_directory && chooser_start(dialog, preferences.file.last_directory);
}
static xxwidgets_status browse_database(xxwidgets_widget *owner, const char *initial, char **path, int *accepted, void *user)
{
    xxwidgets_status status = XXWIDGETS_OK;
    /* Scan and Info share this picker: the asking page's title and start
     * folder, as on Windows and macOS (gui_options.h). */
    gui_options_page_id page = gui_options_browse_page();
    char *start = gui_options_browse_start(page, initial);
    (void)user;
    *path = NULL; *accepted = 0;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(gui_options_browse_title(page),
        GTK_WINDOW(xxwidgets_widget_native_handle(owner)), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        CDIE_TR("Cancel"), GTK_RESPONSE_CANCEL, CDIE_TR("Select"), GTK_RESPONSE_ACCEPT, NULL);
    /* The nearest existing folder of the box (the main database for an empty
     * Scan box); the last directory only when there is none. */
    if (!chooser_start(dialog, start)) (void)chooser_start_last(dialog);
    free(start);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gchar *utf8 = filename ? g_filename_to_utf8(filename, -1, NULL, NULL, NULL) : NULL;
        if (utf8) { *path = copy_text(utf8); if (*path) *accepted = 1; else status = XXWIDGETS_OUT_OF_MEMORY; }
        else status = XXWIDGETS_INVALID_ARGUMENT;
        g_free(utf8); g_free(filename);
    }
    gtk_widget_destroy(dialog);
    return status;
}
/* The frontend's share of the Options settings, as currently applied. */
static xxwidgets_status current_options(tui_app *ui, gui_options_values *values)
{
    xxwidgets_status status;
    gui_frontend_get_preferences(values);
    status = xxwidgets_scan_panel_get_flags(ui->panel, &values->scan.options.flags);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_get_databases(ui->panel, &values->scan.options.databases);
    if (status == XXWIDGETS_OK) status = xxwidgets_app_get_font_options(ui->app, &values->fonts);
    values->scan.modes = gui_backend_get_scan_modes();
    gui_hex_view_get_options(&values->hex);
    return status;
}

/* Applies settings from Options or from the store; fonts only when changed. */
static xxwidgets_status apply_options(tui_app *ui, const gui_options_values *values)
{
    xxwidgets_font_options fonts;
    xxwidgets_status status;
    gui_backend_set_scan_modes(values->scan.modes);
    status = xxwidgets_scan_panel_set_flags(ui->panel, values->scan.options.flags);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_set_databases(ui->panel, values->scan.options.databases);
    gui_hex_view_set_options(&values->hex);
    if (status == XXWIDGETS_OK) status = xxwidgets_app_get_font_options(ui->app, &fonts);
    if (status == XXWIDGETS_OK && memcmp(&values->fonts, &fonts, sizeof(fonts))) {
        status = xxwidgets_app_set_font_options(ui->app, &values->fonts);
        if (status == XXWIDGETS_OK) layout(ui);
    }
    if (status == XXWIDGETS_OK) status = gui_frontend_apply_preferences(ui->app, values);
    if (status == XXWIDGETS_OK) gtk_window_set_keep_above(GTK_WINDOW(xxwidgets_widget_native_handle(ui->window)), values->appearance.stay_on_top != 0);
    /* As on Windows, Recent files is disabled while no history is kept. */
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(ui->recent, values->file.save_recent_files != 0);
    return status;
}

static int reload_tags(tui_app *ui)
{
    char message[160];
    char *tags = gui_backend_used_tags();
    xxwidgets_status status;
    /* File types of a scanned file wait for the next scan; Automatic alone
     * (nothing scanned yet, as at startup) stays enabled. */
    int scanned = ui->type_path != NULL;
    free(ui->type_path); ui->type_path = NULL;
    status = xxwidgets_scan_panel_clear(ui->panel);
    if (status == XXWIDGETS_OK && scanned) status = xxwidgets_widget_set_enabled(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0);
    if (status == XXWIDGETS_OK) status = tags ? xxwidgets_scan_panel_set_available_tags(ui->panel, tags) : XXWIDGETS_OUT_OF_MEMORY;
    gui_backend_free(tags);
    cdie_translation_snprintf(message, sizeof(message), CDIE_N_("Database reloaded: %d signatures. Press F5 to scan."), gui_backend_signature_count());
    if (status == XXWIDGETS_OK) set_status(ui, message);
    else report_error(ui, CDIE_TR("Refresh signature tags"), status);
    return status == XXWIDGETS_OK;
}

/* Update database: download and install from the stored update URL, reload
 * the tags after a reload, and report on the status bar. A NULL owner runs
 * without the progress dialog (--smoke-update). */
static gui_update_result update_database(tui_app *ui, xxwidgets_widget *owner)
{
    char message[512];
    gui_options_values preferences;
    gui_update_result result;
    uint64_t generation = gui_backend_database_generation();
    gui_preferences_get(&preferences);
    ui->busy = 1;
    result = gui_update_run(owner, preferences.databases.main_update_url, message, sizeof(message));
    ui->busy = 0;
    /* Installed, or switched to an up-to-date per-user copy. A tag reload
     * failure keeps its own message. */
    if (gui_backend_database_generation() == generation || reload_tags(ui)) set_status(ui, message);
    return result;
}

/* --smoke-update: one click on Update database; exits 0 when the result is the expected one. */
static int smoke_update(tui_app *ui, const char *expected)
{
    xxwidgets_event click = {0};
    char text[640] = "";
    gui_update_result result;
    int ok;
    set_status(ui, "");
    click.type = XXWIDGETS_EVENT_CLICK; click.widget = ui->update;
    ui->pending = ACTION_NONE;
    on_event(ui->app, &click, ui);
    if (ui->pending != ACTION_UPDATE) { fprintf(stderr, "smoke-update: the button did not request an update\n"); return 1; }
    ui->pending = ACTION_NONE;
    result = update_database(ui, NULL);
    if (xxwidgets_widget_get_text(ui->status, text, sizeof(text), NULL) != XXWIDGETS_OK) text[0] = 0;
    fprintf(stderr, "smoke-update: %s %s\n", gui_update_result_name(result), text);
    ok = !strcmp(gui_update_result_name(result), expected);
    if (ok && result == GUI_UPDATE_INSTALLED) ok = gui_backend_signature_count() > 0 && text[0];
    return ok ? 0 : 1;
}

/* The shared Options window; Linux has no Explorer context menu page. */
static void options_dialog(tui_app *ui, gui_options_page_id page)
{
    gui_options_host host = {(1u << GUI_OPTIONS_PAGE_COUNT) - 1u, NULL, browse_database, ui};
    gui_options_values values;
    int accepted = 0, reloaded = 0;
    xxwidgets_status status = current_options(ui, &values);
    ui->options_page = page;
    if (status == XXWIDGETS_OK) status = gui_options_dialog(ui->window, &host, &ui->options_page, &values, &accepted, &reloaded);
    if (status != XXWIDGETS_OK) { report_window_error(ui, CDIE_TR("Options"), status); return; }
    if (reloaded) reload_tags(ui);
    if (!accepted) return;
    status = apply_options(ui, &values);
    if (status != XXWIDGETS_OK) report_error(ui, CDIE_TR("Apply options"), status);
    else if (!gui_settings_save(ui->settings, &values, ui->options_page))
        set_status(ui, CDIE_TR("Options applied, but they could not be saved for the next start."));
    else if (!reloaded) set_status(ui, CDIE_TR("Options applied."));
}
static void about_dialog(tui_app *ui)
{
    xxwidgets_about_dialog *about = NULL;
    xxwidgets_status status = gui_about_create(cdie_about_png, cdie_about_png_size, &about);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_show(about, ui->window);
    if (about) (void)xxwidgets_about_dialog_destroy(about);
    if (status != XXWIDGETS_OK) report_window_error(ui, CDIE_TR("About"), status);
}
/* Starts a chooser at the file in the path box (UTF-8; relative ones from the
 * working directory), selected in its folder, or in a folder the box names;
 * 0 when the box names neither. */
static int chooser_start_file(GtkWidget *dialog, const char *path)
{
    gchar *filename = path && path[0] ? g_filename_from_utf8(path, -1, NULL, NULL, NULL) : NULL;
    int started = 0;
    if (filename) {
        gchar *cwd = g_get_current_dir();
        gchar *absolute = g_path_is_absolute(filename) ? g_strdup(filename) : g_build_filename(cwd, filename, NULL);
        if (g_file_test(absolute, G_FILE_TEST_IS_REGULAR)) started = gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), absolute);
        else started = g_file_test(absolute, G_FILE_TEST_IS_DIR) && gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), absolute);
        g_free(absolute); g_free(cwd); g_free(filename);
    }
    return started;
}
static void browse(tui_app *ui)
{
    GtkWidget *dialog = gtk_file_chooser_dialog_new(CDIE_TR("Select a file to scan"), GTK_WINDOW(xxwidgets_widget_native_handle(ui->window)), GTK_FILE_CHOOSER_ACTION_OPEN, CDIE_TR("Cancel"), GTK_RESPONSE_CANCEL, CDIE_TR("Open"), GTK_RESPONSE_ACCEPT, NULL);
    char *current = read_path(ui);
    /* As on Windows: the file in the path box, else the last directory when
     * it is kept. */
    if (!chooser_start_file(dialog, current)) (void)chooser_start_last(dialog);
    free(current);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        /* The path box and the scan take UTF-8, as the database Browse does. */
        gchar *path = filename ? g_filename_to_utf8(filename, -1, NULL, NULL, NULL) : NULL;
        if (path) { (void)xxwidgets_widget_set_text(ui->path, path); gui_frontend_remember_file(path);
            ui->pending = gui_frontend_scan_after_open() ? ACTION_SCAN : ACTION_NONE; }
        else if (filename) set_status(ui, CDIE_TR("Cannot open this file: its name is not valid text (UTF-8)."));
        g_free(path); g_free(filename);
    }
    gtk_widget_destroy(dialog);
}
static void recent_dialog(tui_app *ui)
{
    char recent[GUI_PREFERENCE_PATH_MAX];
    gui_options_values preferences;
    int accepted = 0;
    xxwidgets_status status;
    gui_preferences_get(&preferences);
    /* Windows disables its Recent button when no history is kept. */
    if (!preferences.file.save_recent_files) { set_status(ui, CDIE_TR("No file history is kept. Turn on Save history in Options > File.")); return; }
    status = gui_frontend_recent_dialog(ui->window, recent, sizeof(recent), &accepted);
    if (status == XXWIDGETS_OK && accepted) {
        (void)xxwidgets_widget_set_text(ui->path, recent); gui_frontend_remember_file(recent);
        ui->pending = gui_frontend_scan_after_open() ? ACTION_SCAN : ACTION_NONE;
    } else if (status != XXWIDGETS_OK) report_window_error(ui, CDIE_TR("Recent files"), status);
}
/* One action of the main loop, after polling returns: the buttons, shortcuts
 * and drops all end here. */
static void run_action(tui_app *ui, int action)
{
    xxwidgets_status status = XXWIDGETS_OK;
    if (action == ACTION_SCAN) (void)scan(ui, 0);
    else if (action == ACTION_BROWSE) browse(ui);
    else if (action == ACTION_UPDATE) (void)update_database(ui, ui->window);
    else if (action == ACTION_DATABASE_PATHS) options_dialog(ui, GUI_OPTIONS_SCAN);
    else if (action == ACTION_RECENT) recent_dialog(ui);
    else if (action == ACTION_FONTS) options_dialog(ui, GUI_OPTIONS_FONTS);
    else if (action == ACTION_ABOUT) about_dialog(ui);
    else if (action == ACTION_REPORT) {
        /* The shortcut does what the Report button does, and nothing while it is disabled. */
        GtkWidget *report = GTK_WIDGET(xxwidgets_widget_native_handle(control(ui, XXWIDGETS_SCAN_PANEL_REPORT)));
        if (!report || !gtk_widget_get_sensitive(report)) set_status(ui, CDIE_TR("Scan a file first: there is no report yet."));
        else if ((status = xxwidgets_scan_panel_show_report(ui->panel)) != XXWIDGETS_OK) report_window_error(ui, CDIE_TR("Scan report"), status);
    }
    else if (action == ACTION_OPTIONS) options_dialog(ui, ui->options_page);
    else if (action >= ACTION_TOOL_BASE && action < ACTION_TOOL_BASE + GUI_TOOL_COUNT) {
        /* The wording every platform uses (gui_tools.c): the tool's hint for
         * no file, "<Tool>: <reason>" for a failure. */
        char *selected = read_path(ui);
        const char *message = gui_tool_run(ui->window, (gui_tool_id)(action - ACTION_TOOL_BASE), selected);
        if (message) {
            set_status(ui, message);
            if (!selected || !*selected) (void)xxwidgets_widget_focus(ui->path);
        }
        free(selected);
    }
}
int main(int argc, char **argv)
{
    const char *translation = NULL;
    if (!cdie_translation_arguments(&argc,argv,&translation)) { fprintf(stderr,"--translation requires a PO file\n"); return 1; }
    int smoke=0, smoke_test=0, positional=0;
    const char *path=NULL, *update_expected=NULL;
    for (int i=1;i<argc;++i) {
        if (!positional && !strcmp(argv[i],"--")) { positional=1; continue; }
        if (!positional && !strcmp(argv[i],"--smoke-test")) { smoke=smoke_test=1; continue; }
        if (!positional && !strcmp(argv[i],"--smoke-update")) {
            /* Checked before anything runs, as on Windows: a bad value must not download first. */
            if (i+1>=argc || (strcmp(argv[i+1],"installed") && strcmp(argv[i+1],"current") && strcmp(argv[i+1],"failed"))) {
                fprintf(stderr,"--smoke-update requires installed, current or failed\n"); return 2;
            }
            smoke=1; update_expected=argv[++i]; continue;
        }
        if (path) { fprintf(stderr,"Only one initial file is supported.\n"); return 1; }
        path=argv[i];
    }
    tui_app ui = {0}; xxwidgets_config config = {XXWIDGETS_BACKEND_NATIVE, on_event, &ui};
    xxwidgets_status status; int result = 0;
    if (update_expected && (smoke_test || path)) { fprintf(stderr, "--smoke-update cannot be combined with --smoke-test or a file\n"); return 2; }
    if (smoke && !update_expected && !path) { fprintf(stderr, "--smoke-test requires a file\n"); return 1; }
    (void)gui_backend_init();
    /* GTK names the window class after the program; die.desktop's StartupWMClass is "die". */
    g_set_prgname("die");
    status = xxwidgets_app_create(&config, &ui.app);
    if (status != XXWIDGETS_OK) { fprintf(stderr, "Cannot initialize GTK: %s\n", xxwidgets_status_string(status)); result = 1; goto cleanup; }
    {
        size_t line = 0;
        char *stored_translation = !translation && !getenv("CDIE_TRANSLATION") ? gui_frontend_stored_language(smoke) : NULL;
        status = cdie_translation_open(ui.app,translation ? translation : stored_translation,&line);
        if (status != XXWIDGETS_OK && stored_translation) {
            fprintf(stderr,"Saved translation cannot be loaded; using System language.\n");
            status = cdie_translation_open(ui.app,NULL,&line);
        }
        free(stored_translation);
        if (status != XXWIDGETS_OK) {
            fprintf(stderr,"Cannot load translation (line %zu): %s\n",line,xxwidgets_status_string(status));
            if (!smoke) {
                /* As on Windows, a box too, in English: no translation is loaded. */
                GtkWidget *box = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s",
                    "Cannot load PO translation. Check --translation FILE and CDIE_TRANSLATION.");
                if (line) gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(box), "Line %lu: %s", (unsigned long)line, xxwidgets_status_string(status));
                else gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(box), "%s", xxwidgets_status_string(status));
                gtk_window_set_title(GTK_WINDOW(box), "cdie");
                (void)gtk_dialog_run(GTK_DIALOG(box));
                gtk_widget_destroy(box);
            }
            result=1; goto cleanup;
        }
    }
    /* The window icon from the icon theme, under the .desktop file's Icon=
     * name (installed by the FHS layout, the deb and the AppImage). */
    gtk_window_set_default_icon_name("io.github.horsicq.cdie");
    /* Created hidden and shown after the instance check, as on Windows and
     * macOS: a copy that only forwards its file never maps a window. Smoke
     * runs keep every window hidden. */
    (void)xxwidgets_app_set_create_hidden(ui.app, 1);
    /* GTK text is UTF-8: a name that is not stays out of the path box (the
     * window says why), and a later launch only raises the running copy. */
    const char *shown = path && g_utf8_validate(path, -1, NULL) ? path : NULL;
    if (!create_controls(&ui, shown)) { fprintf(stderr, "Cannot create GUI controls.\n"); result = 1; goto cleanup; }
    if (!smoke) (void)xxwidgets_app_set_create_hidden(ui.app, 0);
    ui.options_page = GUI_OPTIONS_SCAN;
    {
        /* Saved Options apply before the first scan; smoke tests keep them in memory. */
        gui_options_values values;
        int stored = 1, databases = 1;
        ui.settings = gui_settings_open(smoke, &stored);
        status = current_options(&ui, &values);
        if (status == XXWIDGETS_OK) databases = gui_settings_load(ui.settings, &values, &ui.options_page);
        if (status == XXWIDGETS_OK) status = apply_options(&ui, &values);
        if (status == XXWIDGETS_OK && !gui_backend_db_paths_current("", "")) reload_tags(&ui);
        if (status != XXWIDGETS_OK) report_error(&ui, CDIE_TR("Apply saved options"), status);
        else if (!databases) set_status(&ui, CDIE_TR("The saved signature folders cannot be loaded; the default ones are active."));
        else if (!stored) set_status(&ui, CDIE_TR("Saved options cannot be read; the defaults are used."));
    }
    if (update_expected) { if (!smoke) (void)xxwidgets_widget_set_visible(ui.window, 1); result = smoke_update(&ui, update_expected); goto cleanup; }
    if (smoke) { result = scan(&ui, 1) ? 0 : 1; if (!result) printf("GTK GUI smoke: %zu detections\n", xxwidgets_scan_panel_count(ui.panel)); goto cleanup; }
    {
        int instance = gui_frontend_instance_start(ui.window, path);
        if (instance <= 0) { if (instance < 0) { fprintf(stderr,"Cannot acquire application instance lock.\n"); result=1; } goto cleanup; }
    }
    if (xxwidgets_widget_set_visible(ui.window, 1) != XXWIDGETS_OK) { result = 1; goto cleanup; }
    if (shown) gui_frontend_remember_file(shown);
    else if (path) set_status(&ui, CDIE_TR("Cannot open this file: its name is not valid text (UTF-8)."));
    ui.running = 1; ui.pending = shown && gui_frontend_scan_after_open() ? ACTION_SCAN : ACTION_NONE;
    while (ui.running) {
        status = xxwidgets_app_poll(ui.app, 30);
        if (status != XXWIDGETS_OK) { result = 1; break; }
        if (ui.needs_layout) { ui.needs_layout = 0; layout(&ui); }
        {
            /* A later launch's file ("Single application"), as WM_COPYDATA on
             * Windows: the path, history, a scan if wanted, and to the front. */
            char *forwarded = NULL;
            if (gui_frontend_instance_poll(ui.window, &forwarded) && forwarded) {
                /* "": a launch without a file, which only raises the window. */
                if (forwarded[0] && xxwidgets_widget_set_text(ui.path, forwarded) == XXWIDGETS_OK) {
                    gui_frontend_remember_file(forwarded);
                    if (gui_frontend_scan_after_open()) ui.pending = ACTION_SCAN;
                }
                gtk_window_present(GTK_WINDOW(xxwidgets_widget_native_handle(ui.window)));
            }
            free(forwarded);
        }
        const char *message = gui_tree_process(ui.window);
        if (message) set_status(&ui, message);
        int action = ui.pending; ui.pending = ACTION_NONE;
        run_action(&ui, action);
    }
cleanup:
    if (!smoke && ui.settings && ui.window) {
        gui_options_values values;
        if (current_options(&ui,&values)==XXWIDGETS_OK) (void)gui_settings_save(ui.settings,&values,ui.options_page);
    }
    gui_frontend_instance_close();
    free(ui.type_path);
    if (ui.panel) (void)xxwidgets_scan_panel_destroy(ui.panel);
    cdie_translation_close();
    if (ui.app) (void)xxwidgets_app_destroy(ui.app);
    gui_settings_close(ui.settings);
    gui_tree_reset();
    gui_backend_shutdown(); return result;
}
