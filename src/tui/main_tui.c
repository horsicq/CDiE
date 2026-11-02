/* Copyright (c) 2026 hors<horsicq@gmail.com> · SPDX-License-Identifier: MIT */
#include "app/cdie_translation.h"
#include "../gui/gui_backend.h"
#include "../gui/gui_tree.h"
#include "../gui/gui_scan_options.h"
#include "../gui/gui_options.h"
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

enum { ACTION_NONE, ACTION_SCAN, ACTION_REPORT, ACTION_PATH, ACTION_QUIT, ACTION_HELP, ACTION_OPTIONS, ACTION_ABOUT };
enum { MIN_COLUMNS = 64, MIN_ROWS = 25 };

typedef struct tui_options {
    const char *file, *main_db, *custom_db;
    int smoke, monochrome;
} tui_options;

typedef struct tui_app {
    xxwidgets_app *app;
    xxwidgets_widget *window, *path_label, *path, *status, *hint, *help, *quit;
    xxwidgets_scan_panel *panel;
    char *type_path;
    gui_options_page_id options_page;
    /* Shared with the GUI: keep its fonts and Hex settings when saving. */
    xx_settings *settings;
    gui_options_values stored;
    int running, pending, busy, columns, rows, small;
} tui_app;

typedef struct file_type { int id; wchar_t *label; } file_type;
typedef struct file_types { file_type *items; size_t count; int failed; } file_types;

static const char help_text[] =
    CDIE_N_("Enter a file path, then press Enter or F5 to scan.\n"
    "F2 opens Options: scan flags and modes, signature folders and CPU optimization.\n"
    "F3 shows About.\n"
    "Ctrl+O focuses the path. Ctrl+R or Ctrl+P opens the full report. Ctrl+Q exits.\n"
    "Tab / Shift+Tab move between controls. Enter opens a choice list;\n"
    "Up / Down select an item, Space toggles a scan flag or tag, Escape closes the list.\n"
    "Results: Up / Down, Home / End and PageUp / PageDown scroll;\n"
    "Left / Right collapse or expand; Shift+Left / Shift+Right scroll sideways.\n"
    "On a result row, S opens the signature that found it ([S]) and ? its description ([?]).\n"
    "A scan runs on a worker. After one second its progress dialog offers Cancel.\n"
    "Flags, database checks and include/exclude tags apply to the next scan.\n"
    "Main database is always used. Empty include tags allow every signature.\n"
    "Database folders default to db and db_custom beside the executable\n"
    "or in the current directory. Override them with -D and -C at startup.\n"
    "Use a terminal of at least 64 columns and 25 rows.");

static void print_help(void)
{
    printf("%s terminal interface\n\n"
        "Usage: cdie_tui [options] [file]\n"
        "  -D, --database PATH        Main signatures folder\n"
        "  -C, --customdatabase PATH  Custom signatures folder\n"
        "      --translation FILE     Load a UTF-8 PO translation catalog\n"
        "      --no-color             Monochrome terminal output\n"
        "      --smoke-test FILE      Scan with hidden TUI controls, print report, exit\n"
        "  -h, --help                 Show this help without opening the terminal\n"
        "  -v, --version              Show version\n"
        "      --                     End options (for filenames starting with '-')\n\n"
        "%s\n", gui_backend_app_title(), help_text);
}

/* 1 = run; 0 = successful early exit; -1 = invalid arguments. */
static int parse_options(int argc, char **argv, tui_options *options)
{
    int i, positional = 0;
    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        const char **destination = NULL;
        if (!positional && !strcmp(arg, "--")) { positional = 1; continue; }
        if (!positional && (!strcmp(arg, "--help") || !strcmp(arg, "-h"))) { print_help(); return 0; }
        if (!positional && (!strcmp(arg, "--version") || !strcmp(arg, "-v"))) {
            printf("%s terminal interface\n", gui_backend_app_title()); return 0;
        }
        if (!positional && !strcmp(arg, "--no-color")) { options->monochrome = 1; continue; }
        if (!positional && !strcmp(arg, "--smoke-test")) { options->smoke = 1; continue; }
        if (!positional && (!strcmp(arg, "-E") || !strcmp(arg, "--extradatabase"))) {
            /* The extra database is gone. Refused, as by diec, rather than
             * quietly scanning with other rules; its path is not a file. */
            fprintf(stderr, "cdie_tui: %s was removed: cdie uses only the main (-D) and custom (-C) databases\n", arg);
            return -1;
        }
        if (!positional && (!strcmp(arg, "-D") || !strcmp(arg, "--database"))) destination = &options->main_db;
        else if (!positional && (!strcmp(arg, "-C") || !strcmp(arg, "--customdatabase"))) destination = &options->custom_db;
        if (destination) {
            if (++i >= argc || !argv[i][0]) {
                fprintf(stderr, "cdie_tui: %s requires a database path.\n", arg); return -1;
            }
            *destination = argv[i]; continue;
        }
        if (!positional && arg[0] == '-') {
            fprintf(stderr, "cdie_tui: unknown option %s; use --help.\n", arg); return -1;
        }
        if (options->file) { fprintf(stderr, "cdie_tui: only one initial file is supported.\n"); return -1; }
        options->file = arg;
    }
    if (options->smoke && !options->file) {
        fprintf(stderr, "cdie_tui: --smoke-test requires a file.\n"); return -1;
    }
    return 1;
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

static int interactive_terminal(void)
{
#ifdef _WIN32
    return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#else
    const char *term = getenv("TERM");
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && (!term || strcmp(term, "dumb"));
#endif
}

static void terminal_size(int *columns, int *rows)
{
    *columns = 80; *rows = 25;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    HANDLE output = CreateFileW(L"CONOUT$", GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    int have_info = 0;
    /* xxwidgets renders in its own active buffer. Standard output still names
     * the original buffer, whose viewport can keep the size from startup. */
    if (output != INVALID_HANDLE_VALUE) {
        have_info = GetConsoleScreenBufferInfo(output, &info) != 0;
        CloseHandle(output);
    }
    if (!have_info)
        have_info = GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &info) != 0;
    if (have_info) {
        *columns = info.srWindow.Right - info.srWindow.Left + 1;
        *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
#else
    struct winsize size = {0};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col && size.ws_row) {
        *columns = size.ws_col; *rows = size.ws_row;
    }
#endif
    if (*columns > 1000) *columns = 1000;
    if (*rows > 500) *rows = 500;
}

static void set_status(tui_app *ui, const char *message)
{
    (void)xxwidgets_widget_set_text(ui->status, message);
}

static void ready(tui_app *ui)
{
    char message[160];
    if (gui_backend_signature_count())
        cdie_translation_snprintf(message, sizeof(message), CDIE_N_("Ready. %d signatures loaded. Enter a file path."), gui_backend_signature_count());
    else
        cdie_translation_snprintf(message, sizeof(message), CDIE_N_("No signatures loaded. Place db beside cdie_tui and restart."));
    set_status(ui, message);
}

/* The terminal backend clips to its viewport; poll terminal dimensions here
 * because it does not synthesize a native window-resize event. */
static int layout(tui_app *ui)
{
    int columns, rows, width, height, small;
    terminal_size(&columns, &rows);
    if (columns == ui->columns && rows == ui->rows) return 1;
    ui->columns = columns;
    ui->rows = rows;
    small = columns < MIN_COLUMNS || rows < MIN_ROWS;
    width = columns > 2 ? columns - 2 : 1;
    height = rows > 2 ? rows - 2 : 1;
    if (xxwidgets_widget_set_rect(ui->window, (xxwidgets_rect){0, 0, width, height}) != XXWIDGETS_OK) return 0;
    if (xxwidgets_scan_panel_set_visible(ui->panel, !small) != XXWIDGETS_OK) return 0;
    if (xxwidgets_widget_set_visible(ui->path_label, !small) != XXWIDGETS_OK ||
        xxwidgets_widget_set_visible(ui->path, !small) != XXWIDGETS_OK ||
        xxwidgets_widget_set_visible(ui->hint, !small) != XXWIDGETS_OK ||
        xxwidgets_widget_set_visible(ui->help, !small) != XXWIDGETS_OK) return 0;
    if (small) {
        if (xxwidgets_widget_set_rect(ui->status, (xxwidgets_rect){0, 0, width, 1}) != XXWIDGETS_OK ||
            xxwidgets_widget_set_rect(ui->quit, (xxwidgets_rect){0, height > 2 ? 2 : 0, width < 10 ? width : 10, 1}) != XXWIDGETS_OK) return 0;
        set_status(ui, CDIE_TR("Resize terminal to at least 64 columns x 25 rows."));
        (void)xxwidgets_widget_focus(ui->quit);
    } else {
        if (xxwidgets_widget_set_rect(ui->path_label, (xxwidgets_rect){1, 1, 5, 1}) != XXWIDGETS_OK ||
            xxwidgets_widget_set_rect(ui->path, (xxwidgets_rect){6, 0, width - 7, 3}) != XXWIDGETS_OK ||
            xxwidgets_scan_panel_set_rect(ui->panel, (xxwidgets_rect){1, 4, width - 2, height - 9}) != XXWIDGETS_OK ||
            xxwidgets_widget_set_rect(ui->hint, (xxwidgets_rect){1, height - 4, width - 24, 2}) != XXWIDGETS_OK ||
            xxwidgets_widget_set_rect(ui->help, (xxwidgets_rect){width - 22, height - 4, 10, 2}) != XXWIDGETS_OK ||
            xxwidgets_widget_set_rect(ui->quit, (xxwidgets_rect){width - 11, height - 4, 10, 2}) != XXWIDGETS_OK ||
            xxwidgets_widget_set_rect(ui->status, (xxwidgets_rect){1, height - 1, width - 2, 1}) != XXWIDGETS_OK) return 0;
        if (ui->small) { ready(ui); (void)xxwidgets_widget_focus(ui->path); }
    }
    ui->small = small;
    return 1;
}

static char *read_tags(xxwidgets_scan_panel *panel, int include);

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    tui_app *ui = user;
    (void)app;
    if (ui->busy) return;
    if (event->type == XXWIDGETS_EVENT_CLOSE && event->widget == ui->window) ui->running = 0;
    else if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
        if (event->value == ACTION_QUIT) ui->running = 0;
        else if (event->value == ACTION_PATH) (void)xxwidgets_widget_focus(ui->path);
        else ui->pending = event->value;
    } else if (event->type == XXWIDGETS_EVENT_ACTIVATE && event->widget == ui->path) ui->pending = ACTION_SCAN;
    else if (event->type == XXWIDGETS_EVENT_CHANGE && event->widget == ui->path) {
        (void)xxwidgets_scan_panel_clear(ui->panel);
        (void)xxwidgets_widget_set_enabled(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0);
    } else if (event->type == XXWIDGETS_EVENT_CHANGE &&
        (event->widget == control(ui, XXWIDGETS_SCAN_PANEL_INCLUDE_TAGS) || event->widget == control(ui, XXWIDGETS_SCAN_PANEL_EXCLUDE_TAGS))) {
        char *include_tags = read_tags(ui->panel, 1), *exclude_tags = read_tags(ui->panel, 0);
        if (include_tags && exclude_tags && strlen(include_tags) < sizeof(ui->stored.scan.include_tags) &&
            strlen(exclude_tags) < sizeof(ui->stored.scan.exclude_tags) && gui_backend_set_tag_filters(include_tags, exclude_tags)) {
            strcpy(ui->stored.scan.include_tags, include_tags);
            strcpy(ui->stored.scan.exclude_tags, exclude_tags);
            (void)gui_settings_save(ui->settings, &ui->stored, ui->options_page);
        }
        free(include_tags); free(exclude_tags);
    } else if (event->type == XXWIDGETS_EVENT_SELECT && event->widget == control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE))
        ui->pending = ACTION_SCAN;
    else if (gui_tree_event(ui->panel, event)) {
        /* S or ? typed on a result row: shown after polling returns. */
    } else if (event->type == XXWIDGETS_EVENT_CLICK) {
        if (event->widget == ui->quit) ui->running = 0;
        else if (event->widget == ui->help) ui->pending = ACTION_HELP;
        else if (event->widget == control(ui, XXWIDGETS_SCAN_PANEL_SCAN)) ui->pending = ACTION_SCAN;
        else if (event->widget == control(ui, XXWIDGETS_SCAN_PANEL_REPORT)) ui->pending = ACTION_REPORT;
    }
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
    if (!gui_backend_signature_count()) { set_status(ui, CDIE_TR("No signatures loaded. Place db beside cdie_tui and restart.")); goto cleanup; }
    if (xxwidgets_scan_panel_clear(ui->panel) != XXWIDGETS_OK) {
        set_status(ui, CDIE_TR("Cannot clear the previous scan results.")); goto cleanup;
    }
    if (!refresh_types(ui, path)) {
        (void)xxwidgets_widget_set_enabled(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0);
        set_status(ui, CDIE_TR("Scan failed: cannot open this file. Check its path and permissions.")); goto cleanup;
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
    if (ok) (void)xxwidgets_widget_focus(control(ui, XXWIDGETS_SCAN_PANEL_RESULTS));
    (void)xxwidgets_widget_set_enabled(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), 1);
cleanup:
    gui_backend_free(type);
    free(include_tags);
    free(exclude_tags);
    free(path);
    return ok;
}

/* The shared Options window, limited to what a terminal can use. */
static void options_dialog(tui_app *ui)
{
    gui_options_host host = {GUI_OPTIONS_PAGE(GUI_OPTIONS_SCAN) | GUI_OPTIONS_PAGE(GUI_OPTIONS_SIGNATURES) | GUI_OPTIONS_PAGE(GUI_OPTIONS_OPTIMIZATION),
                             NULL, NULL, NULL};
    gui_options_values values = ui->stored;
    int accepted = 0, reloaded = 0;
    xxwidgets_status status;
    status = xxwidgets_scan_panel_get_flags(ui->panel, &values.scan.options.flags);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_get_databases(ui->panel, &values.scan.options.databases);
    values.scan.modes = gui_backend_get_scan_modes();
    if (status == XXWIDGETS_OK) status = gui_options_dialog(ui->window, &host, &ui->options_page, &values, &accepted, &reloaded);
    if (status != XXWIDGETS_OK) { set_status(ui, xxwidgets_status_string(status)); return; }
    if (reloaded) {
        char message[160], *tags = gui_backend_used_tags();
        if (!tags) { set_status(ui, xxwidgets_status_string(XXWIDGETS_OUT_OF_MEMORY)); return; }
        status = xxwidgets_scan_panel_clear(ui->panel);
        if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_set_available_tags(ui->panel, tags);
        if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_set_tag_filters(ui->panel,
            ui->stored.scan.include_tags, ui->stored.scan.exclude_tags);
        gui_backend_free(tags);
        if (status != XXWIDGETS_OK) { set_status(ui, xxwidgets_status_string(status)); return; }
        cdie_translation_snprintf(message, sizeof(message), CDIE_N_("Database reloaded: %d signatures. Choose a file and press Scan."),
                                  gui_backend_signature_count());
        set_status(ui, message);
    }
    if (!accepted) return;
    gui_backend_set_scan_modes(values.scan.modes);
    status = xxwidgets_scan_panel_set_flags(ui->panel, values.scan.options.flags);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_set_databases(ui->panel, values.scan.options.databases);
    if (status != XXWIDGETS_OK) { set_status(ui, xxwidgets_status_string(status)); return; }
    ui->stored = values;
    if (!gui_settings_save(ui->settings, &values, ui->options_page))
        set_status(ui, CDIE_TR("Options applied, but they could not be saved for the next start."));
    else if (!reloaded) set_status(ui, CDIE_TR("Options applied."));
}

/* The terminal shows About as text: there is no picture. */
static void about_dialog(tui_app *ui)
{
    xxwidgets_about_dialog *about = NULL;
    xxwidgets_status status = gui_about_create(NULL, 0, &about);
    if (status == XXWIDGETS_OK) status = xxwidgets_about_dialog_show(about, ui->window);
    if (about) (void)xxwidgets_about_dialog_destroy(about);
    if (status != XXWIDGETS_OK) set_status(ui, xxwidgets_status_string(status));
}

static int create_controls(tui_app *ui, const char *startup, int smoke)
{
    static const xxwidgets_shortcut shortcuts[] = {
        {"F5", ACTION_SCAN}, {"Ctrl+R", ACTION_REPORT}, {"Ctrl+P", ACTION_REPORT},
        {"Ctrl+O", ACTION_PATH}, {"Ctrl+Q", ACTION_QUIT}, {"F1", ACTION_HELP}, {"F2", ACTION_OPTIONS}, {"F3", ACTION_ABOUT}
    };
    char *tags;
    if (xxwidgets_widget_create(ui->app, NULL, XXWIDGETS_WINDOW, gui_backend_app_title(), (xxwidgets_rect){0, 0, 78, 23}, &ui->window) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_LABEL, CDIE_TR("File:"), (xxwidgets_rect){1, 1, 5, 1}, &ui->path_label) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_EDIT, startup ? startup : "", (xxwidgets_rect){6, 0, 71, 3}, &ui->path) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_create(ui->window, (xxwidgets_rect){1, 4, 76, 14}, &ui->panel) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_LABEL, CDIE_TR("F2: options  F3: about  F5: scan  Ctrl+R/P: report\nCtrl+O: file path  Esc/Ctrl+Q: quit"), (xxwidgets_rect){1, 19, 54, 2}, &ui->hint) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_BUTTON, CDIE_TR("Help F1"), (xxwidgets_rect){56, 19, 10, 2}, &ui->help) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_BUTTON, CDIE_TR("Quit"), (xxwidgets_rect){67, 19, 10, 2}, &ui->quit) != XXWIDGETS_OK ||
        xxwidgets_widget_create(ui->app, ui->window, XXWIDGETS_LABEL, "", (xxwidgets_rect){1, 22, 76, 1}, &ui->status) != XXWIDGETS_OK) return 0;
    tags = gui_backend_used_tags();
    if (!tags) return 0;
    if (xxwidgets_scan_panel_set_available_tags(ui->panel, tags) != XXWIDGETS_OK) {
        gui_backend_free(tags);
        return 0;
    }
    gui_backend_free(tags);
    if (xxwidgets_scan_panel_set_tag_filters(ui->panel, ui->stored.scan.include_tags, ui->stored.scan.exclude_tags) != XXWIDGETS_OK ||
        !gui_backend_set_tag_filters(ui->stored.scan.include_tags, ui->stored.scan.exclude_tags)) return 0;
    if (xxwidgets_window_set_shortcuts(ui->window, shortcuts, sizeof(shortcuts) / sizeof(shortcuts[0])) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_set_flags(ui->panel, XXWIDGETS_SCAN_DEEP | XXWIDGETS_SCAN_HEURISTIC | XXWIDGETS_SCAN_VERBOSE | XXWIDGETS_SCAN_FORMAT) != XXWIDGETS_OK ||
        xxwidgets_widget_set_enabled(control(ui, XXWIDGETS_SCAN_PANEL_FILE_TYPE), 0) != XXWIDGETS_OK ||
        xxwidgets_window_set_minimum_size(ui->window, MIN_COLUMNS - 2, MIN_ROWS - 2) != XXWIDGETS_OK ||
        (!smoke && xxwidgets_widget_focus(ui->path) != XXWIDGETS_OK)) return 0;
    ready(ui);
    return layout(ui);
}

static int tui_run_options(int argc, char **argv)
{
    const char *translation = NULL;
    if (!cdie_translation_arguments(&argc,argv,&translation)) { fprintf(stderr,"--translation requires a PO file\n"); return CR_INVALIDPARAMETER; }
    tui_options options = {0};
    tui_app ui = {0};
    xxwidgets_config config = {XXWIDGETS_BACKEND_TUI, on_event, &ui};
    xxwidgets_status status;
    const char *failure = NULL;
    char *report = NULL;
    size_t count = 0, required = 0;
    int parsed = parse_options(argc, argv, &options), result = CR_SUCCESS, loaded, settings_read = 1, settings_databases = 1;
    if (parsed <= 0) return parsed < 0 ? CR_INVALIDPARAMETER : CR_SUCCESS;
    if (!interactive_terminal()) {
        fprintf(stderr, "cdie_tui requires an interactive terminal. Use cdie for console or redirected output.\n");
        return CR_CANNOTOPENFILE;
    }
    loaded = gui_backend_init();
    /* Saved Options first, so database folders given on the command line win. */
    ui.options_page = GUI_OPTIONS_SCAN;
    ui.settings = gui_settings_open(options.smoke, &settings_read);
    ui.stored.scan.options.flags = XXWIDGETS_SCAN_DEEP | XXWIDGETS_SCAN_HEURISTIC | XXWIDGETS_SCAN_VERBOSE | XXWIDGETS_SCAN_FORMAT;
    ui.stored.scan.options.databases = XXWIDGETS_SCAN_DATABASE_CUSTOM;
    (void)xxwidgets_font_options_init(&ui.stored.fonts);
    (void)xxwidgets_hex_options_init(&ui.stored.hex);
    settings_databases = gui_settings_load(ui.settings, &ui.stored, &ui.options_page);
    loaded = gui_backend_signature_count() > 0 || loaded;
    if (options.main_db || options.custom_db)
        loaded = gui_backend_set_db_paths(options.main_db, options.custom_db);
    if (!loaded) {
        fprintf(stderr, "cdie_tui: cannot load main signatures from %s. Use -D PATH.\n",
            options.main_db ? options.main_db : gui_backend_main_db_path());
        gui_settings_close(ui.settings);
        gui_backend_shutdown();
        return CR_CANNOTFINDDATABASE;
    }
    status = xxwidgets_app_create(&config, &ui.app);
    if (status != XXWIDGETS_OK) { failure = xxwidgets_status_string(status); result = CR_CANNOTOPENFILE; goto cleanup; }
    {
        size_t line=0;
        status=cdie_translation_open(ui.app,translation,&line);
        if (status != XXWIDGETS_OK) { fprintf(stderr,"Cannot load translation (line %zu): %s\n",line,xxwidgets_status_string(status)); result=CR_INVALIDPARAMETER; goto cleanup; }
    }
    if (options.smoke) xxwidgets_app_set_create_hidden(ui.app, 1);
    if (options.monochrome) {
        status = xxwidgets_app_set_tui_color(ui.app, 0);
        if (status != XXWIDGETS_OK) { failure = xxwidgets_status_string(status); result = CR_CANNOTOPENFILE; goto cleanup; }
    }
    if (!create_controls(&ui, options.file, options.smoke)) { failure = "cannot create terminal controls"; result = CR_CANNOTOPENFILE; goto cleanup; }
    if (xxwidgets_scan_panel_set_flags(ui.panel, ui.stored.scan.options.flags) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_set_databases(ui.panel, ui.stored.scan.options.databases) != XXWIDGETS_OK) {
        failure = "cannot apply saved scan options"; result = CR_CANNOTOPENFILE; goto cleanup;
    }
    if (!settings_databases) set_status(&ui, CDIE_TR("The saved signature folders cannot be loaded; the default ones are active."));
    else if (!settings_read) set_status(&ui, CDIE_TR("Saved options cannot be read; the defaults are used."));
    ui.running = 1;
    ui.pending = options.file ? ACTION_SCAN : ACTION_NONE;
    while (ui.running) {
        int action;
        if (!layout(&ui)) { failure = "cannot resize terminal controls"; result = CR_CANNOTOPENFILE; break; }
        status = xxwidgets_app_poll(ui.app, 50);
        if (status != XXWIDGETS_OK) { failure = xxwidgets_status_string(status); result = CR_CANNOTOPENFILE; break; }
        if (!ui.running) break;
        if (ui.small && !options.smoke) continue;
        {
            const char *message = gui_tree_process(ui.window);
            if (message) set_status(&ui, message);
        }
        action = ui.pending;
        ui.pending = ACTION_NONE;
        if (action == ACTION_SCAN) {
            int ok = scan(&ui, options.smoke);
            if (options.smoke) {
                count = xxwidgets_scan_panel_count(ui.panel);
                result = ok ? CR_SUCCESS : CR_CANNOTOPENFILE;
                if (ok && xxwidgets_scan_panel_get_report(ui.panel, NULL, 0, &required) == XXWIDGETS_OK) {
                    report = malloc(required);
                    if (!report || xxwidgets_scan_panel_get_report(ui.panel, report, required, NULL) != XXWIDGETS_OK) result = CR_CANNOTOPENFILE;
                } else if (ok) result = CR_CANNOTOPENFILE;
                break;
            }
        } else if (action == ACTION_PATH) (void)xxwidgets_widget_focus(ui.path);
        else if (action == ACTION_REPORT) {
            size_t report_size = 0;
            if (xxwidgets_scan_panel_get_report(ui.panel, NULL, 0, &report_size) == XXWIDGETS_OK && report_size > 1) {
                status = xxwidgets_scan_panel_show_report(ui.panel);
                if (status != XXWIDGETS_OK) set_status(&ui, xxwidgets_status_string(status));
            } else set_status(&ui, CDIE_TR("Scan a file first to view its report."));
        } else if (action == ACTION_OPTIONS) options_dialog(&ui);
        else if (action == ACTION_ABOUT) about_dialog(&ui);
        else if (action == ACTION_HELP) {
            status = xxwidgets_text_dialog(ui.window, CDIE_TR("cdie terminal help"), CDIE_TR(help_text));
            if (status != XXWIDGETS_OK) set_status(&ui, xxwidgets_status_string(status));
        }
    }
cleanup:
    free(ui.type_path);
    if (ui.panel && xxwidgets_scan_panel_destroy(ui.panel) != XXWIDGETS_OK) result = CR_CANNOTOPENFILE;
    cdie_translation_close();
    if (ui.app && xxwidgets_app_destroy(ui.app) != XXWIDGETS_OK) result = CR_CANNOTOPENFILE;
    gui_settings_close(ui.settings);
    gui_tree_reset();
    gui_backend_shutdown();
    if (failure) fprintf(stderr, "cdie_tui: %s.\n", failure);
    if (options.smoke) {
        if (!result) printf("cdie_tui smoke: %zu detections\n%s", count, report ? report : "");
        else fprintf(stderr, "cdie_tui smoke: scan failed\n");
    }
    free(report);
    return result;
}

/* The Windows entry point owns every UTF-8 argv string. Parse a pointer copy
 * so removing translation options never loses or duplicates its ownership. */
static int tui_run(int argc, char **argv)
{
    char **copy;
    int result;
    if (argc < 1 || (size_t)argc >= SIZE_MAX/sizeof(*copy)) return CR_INVALIDPARAMETER;
    copy=malloc(((size_t)argc+1)*sizeof(*copy));
    if (!copy) return CR_CANNOTOPENFILE;
    memcpy(copy,argv,((size_t)argc+1)*sizeof(*copy));
    result=tui_run_options(argc,copy);
    free(copy); return result;
}

#ifdef _WIN32
/* Hosted CRT supplies the real Unicode command line. The engine expects UTF-8. */
int wmain(int argc, wchar_t **wide_argv)
{
    char **argv;
    int i, result = CR_INVALIDPARAMETER;
    argv = calloc((size_t)argc + 1, sizeof(*argv));
    if (!argv) return CR_CANNOTOPENFILE;
    for (i = 0; i < argc; ++i) {
        int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1, NULL, 0, NULL, NULL);
        if (!required || !(argv[i] = malloc((size_t)required))) goto finish;
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_argv[i], -1, argv[i], required, NULL, NULL)) goto finish;
    }
    result = tui_run(argc, argv);
finish:
    for (i = 0; i < argc; ++i) free(argv[i]);
    free(argv);
    return result;
}
#else
int main(int argc, char **argv) { return tui_run(argc, argv); }
#endif
