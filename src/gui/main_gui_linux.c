/* Copyright (c) 2026 hors<horsicq@gmail.com> · SPDX-License-Identifier: MIT */
#include "app/cdie_translation.h"
#include "../gui/gui_backend.h"
#include "../gui/gui_tree.h"
#include "../gui/gui_scan_options.h"
#include "../gui/gui_options.h"
#include "gui_frontend_preferences.h"
#include "../gui/gui_about.h"
#include "../gui/gui_settings.h"
#include "gui_main_actions.h"
#include "gui_shortcuts.h"
#include "global.h"
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_combobox.h>
#include <xxwidgets/xxwidgets_scan_panel.h>
#include <xxwidgets/xxwidgets_die_main_panel.h>
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

/* The main window is xxwidgets_die_main_panel (DIE_LOOK.md): the file name
 * row, File type and File size (Base address and Entry point in the Advanced
 * view), the engine row, the result tree, the scan bar and the right column
 * (Advanced, Update database, Shortcuts, Options, About, Exit). Its actions
 * and the shortcuts run through gui_main_actions.c after polling returns, as
 * on Windows and macOS. There is no status line (Qt has none): failures are
 * message dialogs, the scan time is the elapsed field. */
enum { SHORTCUT_BASE = 1000 }; /* SHORTCUT event values: SHORTCUT_BASE + gui_shortcut_action */
typedef struct tui_app {
    xxwidgets_app *app;
    xxwidgets_widget *window, *path; /* path: the file name row's box (borrowed) */
    xxwidgets_die_main_panel *main_panel; /* not "main": the GTK probes #define main */
    /* The main panel's parts, borrowed. */
    xxwidgets_file_name_bar *bar;
    xxwidgets_file_header *header;
    xxwidgets_scan_panel *panel;
    xxwidgets_side_panel *side;
    gui_main_file_types types;   /* the loaded file's File type list */
    char *loaded;                /* the loaded file (UTF-8), NULL when none */
    char *message;               /* the last message shown (the probes read it) */
    xxwidgets_die_action action; /* pending: a main panel action */
    gui_shortcut_action shortcut;/* pending: a shortcut */
    gui_options_page_id options_page;
    gui_update_result update_result; /* the last Update database */
    xx_settings *settings;
    int running, busy, quiet;    /* quiet: messages go to stderr (smoke runs) */
} tui_app;

static char *copy_text(const char *text)
{
    size_t length = strlen(text) + 1;
    char *copy = malloc(length);
    if (copy) memcpy(copy, text, length);
    return copy;
}

/* What went to the status line before (Q1): a message dialog over the main
 * window, titled with the application's name; on stderr in smoke runs. */
static void show_message(tui_app *ui, const char *text)
{
    if (!text || !text[0]) return;
    free(ui->message); ui->message = copy_text(text);
    if (ui->quiet || !ui->window) { fprintf(stderr, "%s\n", text); return; }
    (void)xxwidgets_text_dialog(ui->window, gui_backend_app_title(), text);
}
/* Failures as sentences (gui_error.h), never status names: a window that did
 * not open, as the result tree says it (gui_tree.c), or "<what failed>:
 * <reason>". what and window are translated. */
static void report_window_error(tui_app *ui, const char *window, xxwidgets_status status)
{
    char message[512];
    cdie_translation_snprintf(message, sizeof(message), CDIE_N_("Cannot open %s. %s"), window, gui_error_status_text(status));
    show_message(ui, message);
}
static void report_error(tui_app *ui, const char *what, xxwidgets_status status)
{
    char message[512];
    cdie_translation_snprintf(message, sizeof(message), CDIE_N_("%s: %s"), what, gui_error_status_text(status));
    show_message(ui, message);
}
static xxwidgets_widget *control(tui_app *ui, xxwidgets_scan_panel_control_id id)
{
    return xxwidgets_scan_panel_control(ui->panel, id);
}

/* The path box's text, with a leading ~ expanded; NULL on failure. */
static char *read_path(tui_app *ui)
{
    size_t size = 0;
    char *path;
    if (xxwidgets_file_name_bar_get_path(ui->bar, NULL, 0, &size) != XXWIDGETS_OK || !size) return NULL;
    path = malloc(size);
    if (!path) return NULL;
    if (xxwidgets_file_name_bar_get_path(ui->bar, path, size, NULL) != XXWIDGETS_OK) { free(path); return NULL; }
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

/* No file: the file's controls disabled (DIE_LOOK.md 1.5), the path kept. */
static void unload(tui_app *ui)
{
    free(ui->loaded); ui->loaded = NULL;
    gui_main_file_types_clear(&ui->types);
    (void)xxwidgets_file_header_clear(ui->header);
    (void)xxwidgets_scan_panel_clear(ui->panel);
    (void)xxwidgets_die_main_panel_set_loaded(ui->main_panel, 0);
}

static int scan(tui_app *ui, int type_id, int smoke);
/* Opens path (UTF-8): its File type list, File size, Base address and Entry
 * point, the path in the file name row, the history; then a scan when
 * scan_after. A file that cannot be read unloads the previous one and says
 * why. Returns 1 when the file was opened. */
static int open_file(tui_app *ui, const char *path, int scan_after)
{
    xx_meta_string *records = NULL;
    gui_backend_facts facts;
    xxwidgets_status status;
    char *copy;
    int same;
    if (!path || !path[0]) {
        show_message(ui, CDIE_TR("Enter a file path, then press Enter or F5."));
        (void)xxwidgets_widget_focus(ui->path);
        return 0;
    }
    same = ui->loaded && !strcmp(ui->loaded, path);
    copy = copy_text(path);
    if (!copy) { show_message(ui, CDIE_TR("Cannot read the file path (out of memory or widget error).")); return 0; }
    if (!gui_main_file_types_load(&ui->types, path, same)) {
        /* One sentence on every platform, with the file system's reason (gui_error.h). */
        char reason[512];
        free(copy);
        unload(ui);
        gui_error_open_status(reason, sizeof(reason), path);
        show_message(ui, reason);
        return 0;
    }
    status = gui_main_file_types_records(&ui->types, &records);
    if (status == XXWIDGETS_OK) status = xxwidgets_file_header_set_file_types(ui->header, records, ui->types.count, ui->types.selected);
    gui_main_file_types_free_records(records, ui->types.count);
    memset(&facts, 0, sizeof(facts));
    if (status == XXWIDGETS_OK && gui_backend_file_facts(path, 0, &facts)) {
        status = xxwidgets_file_header_set_file_size(ui->header, facts.file_size);
        /* The Advanced view's values; formats without them keep the fields empty. */
        if (facts.base_bits) (void)xxwidgets_file_header_set_address(ui->header, XXWIDGETS_FILE_HEADER_BASE_ADDRESS, facts.base_address, facts.base_bits);
        if (facts.entry_bits) (void)xxwidgets_file_header_set_address(ui->header, XXWIDGETS_FILE_HEADER_ENTRY_POINT, facts.entry_point, facts.entry_bits);
    }
    gui_backend_facts_free(&facts);
    if (status == XXWIDGETS_OK) status = xxwidgets_file_name_bar_set_loaded_path(ui->bar, path);
    if (status != XXWIDGETS_OK) { free(copy); unload(ui); report_error(ui, CDIE_TR("Open file"), status); return 0; }
    free(ui->loaded); ui->loaded = copy;
    if (!same) { (void)xxwidgets_scan_panel_clear(ui->panel); gui_tree_reset(); }
    (void)xxwidgets_die_main_panel_set_loaded(ui->main_panel, 1);
    gui_frontend_remember_file(path);
    if (scan_after) (void)scan(ui, 0, 0);
    return 1;
}

/* Scans the loaded file as type_id (0: automatic). Text typed into the path
 * box since is opened first, which keeps F5 working on a typed path. */
static int scan(tui_app *ui, int type_id, int smoke)
{
    char *type = NULL, *path = read_path(ui);
    char *include_tags = NULL, *exclude_tags = NULL;
    unsigned int flags = 0, databases = 0;
    int cancelled = 0, ok = 0;
    if (!path) { show_message(ui, CDIE_TR("Cannot read the file path (out of memory or widget error).")); return 0; }
    if (!ui->loaded || strcmp(path, ui->loaded)) {
        if (!open_file(ui, path, 0)) goto cleanup;
        type_id = 0;
    }
    if (!gui_backend_signature_count()) { show_message(ui, CDIE_TR("No signatures loaded. Choose the main folder in Options > Scan (F3).")); goto cleanup; }
    if (xxwidgets_scan_panel_clear(ui->panel) != XXWIDGETS_OK) {
        show_message(ui, CDIE_TR("Cannot clear the previous scan results.")); goto cleanup;
    }
    if (xxwidgets_scan_panel_get_flags(ui->panel, &flags) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_get_databases(ui->panel, &databases) != XXWIDGETS_OK) {
        show_message(ui, CDIE_TR("Cannot read the selected scan options.")); goto cleanup;
    }
    include_tags = read_tags(ui->panel, 1);
    exclude_tags = read_tags(ui->panel, 0);
    if (!include_tags || !exclude_tags || !gui_backend_set_tag_filters(include_tags, exclude_tags)) {
        show_message(ui, CDIE_TR("Cannot apply the selected tag filters.")); goto cleanup;
    }
    gui_backend_set_flags(flags & XXWIDGETS_SCAN_DEEP, flags & XXWIDGETS_SCAN_HEURISTIC,
        flags & XXWIDGETS_SCAN_VERBOSE, flags & XXWIDGETS_SCAN_AGGRESSIVE,
        flags & XXWIDGETS_SCAN_HIDE_UNKNOWN, flags & XXWIDGETS_SCAN_FORMAT);
    gui_backend_set_databases((databases & XXWIDGETS_SCAN_DATABASE_CUSTOM) != 0);
    ui->busy = 1;
    ok = gui_tree_scan(smoke ? NULL : ui->window, ui->panel, ui->loaded, type_id, &type, &cancelled);
    ui->busy = 0;
    /* The elapsed field shows the time; only a failure is reported. */
    if (!ok && !cancelled) show_message(ui, CDIE_TR("Scan failed."));
    /* The focus stays where it was, as on Windows. */
    if (ok) gui_frontend_remember_file(ui->loaded);
    /* Report follows the new results; the tags keep their own rule. */
    (void)xxwidgets_die_main_panel_set_loaded(ui->main_panel, 1);
cleanup:
    gui_backend_free(type);
    free(include_tags);
    free(exclude_tags);
    free(path);
    return ok;
}

/* The scan engine combo shows Scan/Engine (Options > Scan may change it). */
static void show_engine(tui_app *ui)
{
    gui_backend_scan_preferences scan_preferences;
    xxwidgets_widget *engine = control(ui, XXWIDGETS_SCAN_PANEL_ENGINE);
    if (!engine || !gui_backend_get_scan_preferences(&scan_preferences)) return;
    for (size_t i = 0; gui_backend_engine_id(i); ++i)
        if (!strcmp(gui_backend_engine_id(i), scan_preferences.engine)) { (void)xxwidgets_widget_set_value(engine, (int)i); break; }
}

/* The size Qt opens with (720 x 500 at 96 DPI), or the view's minimum. */
static void default_size(tui_app *ui)
{
    int width = 0, height = 0;
    if (xxwidgets_die_main_panel_get_default_size(ui->main_panel, xxwidgets_die_main_panel_get_advanced(ui->main_panel), &width, &height) == XXWIDGETS_OK)
        (void)xxwidgets_widget_set_frame(ui->window, (xxwidgets_rect){0, 0, width, height});
    (void)xxwidgets_die_main_panel_layout(ui->main_panel);
}
/* View/Advanced as stored (no event, no write), at its default size. */
static void apply_stored_view(tui_app *ui)
{
    (void)xxwidgets_die_main_panel_set_advanced(ui->main_panel, gui_frontend_advanced());
    default_size(ui);
}

static void on_event(xxwidgets_app *app, const xxwidgets_event *event, void *user)
{
    tui_app *ui = user;
    xxwidgets_die_action action;
    (void)app;
    if (!ui->main_panel) return;
    /* The panel lays itself out again, also under a modal scan. */
    if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == ui->window) { (void)xxwidgets_die_main_panel_handle_event(ui->main_panel, event, &action); return; }
    if (ui->busy) return;
    if (event->type == XXWIDGETS_EVENT_CLOSE && event->widget == ui->window) ui->running = 0;
    else if (event->type == XXWIDGETS_EVENT_DROP_FILES && event->widget == ui->window && event->path_count) {
        /* Opened after polling returns, as a typed path and Enter. */
        (void)xxwidgets_file_name_bar_set_path(ui->bar, event->paths[0]);
        memset(&ui->action, 0, sizeof(ui->action));
        ui->action.id = XXWIDGETS_DIE_ACTION_OPEN_PATH;
    } else if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
        gui_shortcut_action shortcut = gui_shortcuts_action(event->value, SHORTCUT_BASE);
        if (shortcut == GUI_SHORTCUT_EXIT) ui->running = 0;
        else if (shortcut != GUI_SHORTCUT_NONE) ui->shortcut = shortcut;
    } else if (gui_tree_event(ui->panel, event)) {
        /* Signatures, Directory, Log, and S or ? on a result row: shown
         * by gui_tree_process after polling returns. */
    } else if (xxwidgets_die_main_panel_handle_event(ui->main_panel, event, &action)) {
        if (action.id != XXWIDGETS_DIE_ACTION_NONE && action.id != XXWIDGETS_DIE_ACTION_PATH_EDITED) ui->action = action;
    }
}

static int create_controls(tui_app *ui, const char *path)
{
    xxwidgets_shortcut shortcuts[GUI_SHORTCUT_COUNT * 2];
    char storage[512];
    unsigned int flags = XXWIDGETS_DIE_MAIN_PANEL_SHOW_UPDATE | XXWIDGETS_DIE_MAIN_PANEL_TAG_FILTERS;
    size_t count = gui_shortcuts_bindings(GUI_SHORTCUTS_LINUX, SHORTCUT_BASE, gui_update_supported(), shortcuts,
        sizeof(shortcuts) / sizeof(shortcuts[0]), storage, sizeof(storage));
    char *tags;
    xxwidgets_status s;
    if (!count) return 0;
    /* The panel sizes the window (its minimum, then Qt's default size). */
    if (xxwidgets_widget_create(ui->app, NULL, XXWIDGETS_WINDOW, gui_backend_app_title(), (xxwidgets_rect){4, 3, 132, 34}, &ui->window) != XXWIDGETS_OK ||
        xxwidgets_die_main_panel_create(ui->window, flags, &ui->main_panel) != XXWIDGETS_OK) return 0;
    ui->bar = xxwidgets_die_main_panel_file_name_bar(ui->main_panel);
    ui->header = xxwidgets_die_main_panel_file_header(ui->main_panel);
    ui->panel = xxwidgets_die_main_panel_scan_panel(ui->main_panel);
    ui->side = xxwidgets_die_main_panel_side_panel(ui->main_panel);
    ui->path = xxwidgets_file_name_bar_control(ui->bar, XXWIDGETS_FILE_NAME_BAR_PATH);
    if (!ui->bar || !ui->header || !ui->panel || !ui->side || !ui->path) return 0;
    tags = gui_backend_used_tags();
    if (!tags) return 0;
    s = xxwidgets_scan_panel_set_available_tags(ui->panel, tags); gui_backend_free(tags);
    /* Windows' defaults: Deep, Heuristic and Verbose. */
    if (s != XXWIDGETS_OK || xxwidgets_window_set_shortcuts(ui->window, shortcuts, count) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_set_flags(ui->panel, XXWIDGETS_SCAN_DEEP | XXWIDGETS_SCAN_HEURISTIC | XXWIDGETS_SCAN_VERBOSE) != XXWIDGETS_OK ||
        xxwidgets_scan_panel_set_databases(ui->panel, XXWIDGETS_SCAN_DATABASE_CUSTOM) != XXWIDGETS_OK ||
        /* Update database is shown (Q2) and works only with an updater. */
        xxwidgets_side_panel_set_item_enabled(ui->side, XXWIDGETS_DIE_SIDE_UPDATE, gui_update_supported()) != XXWIDGETS_OK ||
        xxwidgets_window_set_accept_file_drops(ui->window, 1) != XXWIDGETS_OK) return 0;
    /* The tags enable their combos: no file is loaded yet. */
    if (xxwidgets_die_main_panel_set_loaded(ui->main_panel, 0) != XXWIDGETS_OK) return 0;
    if (path && xxwidgets_file_name_bar_set_path(ui->bar, path) != XXWIDGETS_OK) return 0;
    show_engine(ui);
    apply_stored_view(ui);
    (void)xxwidgets_widget_focus(ui->path);
    return 1;
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
        /* New text sizes: the minimum and the layout follow. */
        if (status == XXWIDGETS_OK) status = xxwidgets_die_main_panel_retranslate(ui->main_panel);
    }
    if (status == XXWIDGETS_OK) status = gui_frontend_apply_preferences(ui->app, values);
    if (status == XXWIDGETS_OK) gtk_window_set_keep_above(GTK_WINDOW(xxwidgets_widget_native_handle(ui->window)), values->appearance.stay_on_top != 0);
    /* As on Windows, Recent files is disabled while no history is kept. */
    if (status == XXWIDGETS_OK) status = xxwidgets_file_name_bar_set_recent_available(ui->bar, values->file.save_recent_files != 0);
    if (status == XXWIDGETS_OK) show_engine(ui);
    return status;
}

/* Saves the settings that differ from the store (View/Advanced, Scan/Engine). */
static void save_settings(tui_app *ui)
{
    gui_options_values values;
    if (!ui->settings || current_options(ui, &values) != XXWIDGETS_OK) return;
    if (!gui_settings_save(ui->settings, &values, ui->options_page))
        show_message(ui, CDIE_TR("Options applied, but they could not be saved for the next start."));
}

static int reload_tags(tui_app *ui)
{
    char *tags = gui_backend_used_tags();
    xxwidgets_status status;
    /* The old results came from the old database. */
    status = xxwidgets_scan_panel_clear(ui->panel);
    gui_tree_reset();
    if (status == XXWIDGETS_OK) status = tags ? xxwidgets_scan_panel_set_available_tags(ui->panel, tags) : XXWIDGETS_OUT_OF_MEMORY;
    gui_backend_free(tags);
    /* The tags enable their combos even with no file loaded. */
    (void)xxwidgets_die_main_panel_set_loaded(ui->main_panel, ui->loaded != NULL);
    if (status != XXWIDGETS_OK) report_error(ui, CDIE_TR("Refresh signature tags"), status);
    return status == XXWIDGETS_OK;
}

/* Update database: download and install from the stored update URL, reload
 * the tags after a reload. gui_update_run shows the result over owner; a NULL
 * owner runs without dialogs (--smoke-update). */
static gui_update_result update_database(tui_app *ui, xxwidgets_widget *owner, char *message, size_t size)
{
    gui_options_values preferences;
    gui_update_result result;
    uint64_t generation = gui_backend_database_generation();
    gui_preferences_get(&preferences);
    ui->busy = 1;
    result = gui_update_run(owner, preferences.databases.main_update_url, message, size);
    ui->busy = 0;
    /* Installed, or switched to an up-to-date per-user copy. */
    if (gui_backend_database_generation() != generation) (void)reload_tags(ui);
    return result;
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
        show_message(ui, CDIE_TR("Options applied, but they could not be saved for the next start."));
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
    char *current = read_path(ui), *chosen = NULL;
    int invalid = 0;
    /* As on Windows: the file in the path box, else the last directory when
     * it is kept. */
    if (!chooser_start_file(dialog, current)) (void)chooser_start_last(dialog);
    free(current);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        /* The path box and the scan take UTF-8, as the database Browse does. */
        gchar *path = filename ? g_filename_to_utf8(filename, -1, NULL, NULL, NULL) : NULL;
        if (path) chosen = copy_text(path);
        else if (filename) invalid = 1;
        g_free(path); g_free(filename);
    }
    gtk_widget_destroy(dialog);
    /* Opened once the chooser is gone. */
    if (invalid) show_message(ui, CDIE_TR("Cannot open this file: its name is not valid text (UTF-8)."));
    if (chosen) {
        (void)xxwidgets_file_name_bar_set_path(ui->bar, chosen);
        (void)open_file(ui, chosen, gui_frontend_scan_after_open());
        free(chosen);
    }
}
static void recent_dialog(tui_app *ui)
{
    char recent[GUI_PREFERENCE_PATH_MAX];
    gui_options_values preferences;
    int accepted = 0;
    xxwidgets_status status;
    gui_preferences_get(&preferences);
    /* Its button is disabled while no history is kept; Ctrl+R says why. */
    if (!preferences.file.save_recent_files) { show_message(ui, CDIE_TR("No file history is kept. Turn on Save history in Options > File.")); return; }
    status = gui_frontend_recent_dialog(ui->window, recent, sizeof(recent), &accepted);
    if (status == XXWIDGETS_OK && accepted) {
        (void)xxwidgets_file_name_bar_set_path(ui->bar, recent);
        (void)open_file(ui, recent, gui_frontend_scan_after_open());
    } else if (status != XXWIDGETS_OK) report_window_error(ui, CDIE_TR("Recent files"), status);
}

/* The hooks of gui_main_actions.c. */
static void hook_browse(void *user) { browse(user); }
static void hook_recent(void *user) { recent_dialog(user); }
static void hook_open_path(void *user)
{
    tui_app *ui = user;
    char *path = read_path(ui);
    if (!path) { show_message(ui, CDIE_TR("Cannot read the file path (out of memory or widget error).")); return; }
    (void)open_file(ui, path, gui_frontend_scan_after_open());
    free(path);
}
static void hook_scan(void *user, int type_id) { (void)scan(user, type_id, 0); }
static void hook_report(void *user)
{
    tui_app *ui = user;
    size_t required = 0;
    xxwidgets_status status;
    /* The shortcut does what the Report button does: nothing without a report. */
    if (xxwidgets_scan_panel_get_report(ui->panel, NULL, 0, &required) != XXWIDGETS_OK || required <= 1) return;
    if ((status = xxwidgets_scan_panel_show_report(ui->panel)) != XXWIDGETS_OK) report_window_error(ui, CDIE_TR("Scan report"), status);
}
static void hook_update(void *user)
{
    tui_app *ui = user;
    char message[512] = "";
    /* gui_update_run shows failures and "up to date" itself; an installed
     * database said so on the status line, now in a message. */
    ui->update_result = update_database(ui, ui->window, message, sizeof(message));
    if (ui->update_result == GUI_UPDATE_INSTALLED) show_message(ui, message);
}
static void hook_options(void *user, int page)
{
    tui_app *ui = user;
    options_dialog(ui, page < 0 ? ui->options_page : (gui_options_page_id)page);
}
static void hook_about(void *user) { about_dialog(user); }
static void hook_exit(void *user) { ((tui_app *)user)->running = 0; }
static void hook_settings_changed(void *user) { save_settings(user); }
static void hook_error(void *user, const char *text) { show_message(user, text); }
static void main_hooks(tui_app *ui, gui_main_hooks *hooks)
{
    memset(hooks, 0, sizeof(*hooks));
    hooks->user = ui;
    hooks->browse = hook_browse; hooks->recent = hook_recent; hooks->open_path = hook_open_path;
    hooks->scan = hook_scan; hooks->report = hook_report; hooks->update = hook_update;
    hooks->options = hook_options; hooks->about = hook_about; hooks->exit = hook_exit;
    hooks->settings_changed = hook_settings_changed; hooks->error = hook_error;
    hooks->with_update = gui_update_supported();
    hooks->file_types = &ui->types;
}
/* The pending action and shortcut, after polling returns: the buttons, the
 * shortcuts and drops all end here. */
static void run_pending(tui_app *ui)
{
    xxwidgets_die_action action = ui->action;
    gui_shortcut_action shortcut = ui->shortcut;
    gui_main_hooks hooks;
    memset(&ui->action, 0, sizeof(ui->action));
    ui->shortcut = GUI_SHORTCUT_NONE;
    main_hooks(ui, &hooks);
    if (action.id != XXWIDGETS_DIE_ACTION_NONE) (void)gui_main_run_action(ui->window, ui->main_panel, ui->loaded, &action, &hooks);
    if (shortcut != GUI_SHORTCUT_NONE) (void)gui_main_run_shortcut(ui->window, ui->main_panel, ui->loaded, shortcut, &hooks);
}
/* One pass of the main loop after polling. */
static void main_loop_pass(tui_app *ui)
{
    const char *message = gui_tree_process(ui->window);
    if (message) show_message(ui, message);
    run_pending(ui);
}

/* --smoke-update: one click on Update database; exits 0 when the result is the expected one. */
static int smoke_update(tui_app *ui, const char *expected)
{
    xxwidgets_event click = {0};
    char text[512] = "";
    gui_update_result result;
    int ok;
    click.type = XXWIDGETS_EVENT_CLICK; click.widget = xxwidgets_side_panel_control(ui->side, XXWIDGETS_DIE_SIDE_UPDATE);
    memset(&ui->action, 0, sizeof(ui->action));
    on_event(ui->app, &click, ui);
    if (ui->action.id != XXWIDGETS_DIE_ACTION_UPDATE) { fprintf(stderr, "smoke-update: the button did not request an update\n"); return 1; }
    memset(&ui->action, 0, sizeof(ui->action));
    result = update_database(ui, NULL, text, sizeof(text));
    fprintf(stderr, "smoke-update: %s %s\n", gui_update_result_name(result), text);
    ok = !strcmp(gui_update_result_name(result), expected);
    if (ok && result == GUI_UPDATE_INSTALLED) ok = gui_backend_signature_count() > 0 && text[0];
    return ok ? 0 : 1;
}

/* Saved Options apply before the first scan and before the window shows;
 * smoke tests keep them in memory. The message for a problem, or NULL. */
static const char *load_settings(tui_app *ui, int memory_only)
{
    gui_options_values values;
    int stored = 1, databases = 1;
    xxwidgets_status status;
    ui->settings = gui_settings_open(memory_only, &stored);
    status = current_options(ui, &values);
    if (status == XXWIDGETS_OK) databases = gui_settings_load(ui->settings, &values, &ui->options_page);
    if (status == XXWIDGETS_OK) status = apply_options(ui, &values);
    if (status == XXWIDGETS_OK && !gui_backend_db_paths_current("", "")) reload_tags(ui);
    /* View/Advanced (DIE_LOOK.md 2.1), read once before the window shows. */
    apply_stored_view(ui);
    if (status != XXWIDGETS_OK) { report_error(ui, CDIE_TR("Apply saved options"), status); return NULL; }
    if (!databases) return CDIE_TR("The saved signature folders cannot be loaded; the default ones are active.");
    if (!stored) return CDIE_TR("Saved options cannot be read; the defaults are used.");
    return NULL;
}

int main(int argc, char **argv)
{
    const char *translation = NULL;
    if (!cdie_translation_arguments(&argc,argv,&translation)) { fprintf(stderr,"--translation requires a PO file\n"); return 1; }
    int smoke=0, smoke_test=0, positional=0;
    const char *path=NULL, *update_expected=NULL, *startup_message=NULL;
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
    ui.quiet = smoke;
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
    startup_message = load_settings(&ui, smoke);
    if (update_expected) { result = smoke_update(&ui, update_expected); goto cleanup; }
    if (smoke) {
        if (startup_message) fprintf(stderr, "%s\n", startup_message);
        result = scan(&ui, 0, 1) ? 0 : 1;
        if (!result) printf("GTK GUI smoke: %zu detections\n", xxwidgets_scan_panel_count(ui.panel));
        goto cleanup;
    }
    {
        int instance = gui_frontend_instance_start(ui.window, path);
        if (instance <= 0) { if (instance < 0) { fprintf(stderr,"Cannot acquire application instance lock.\n"); result=1; } goto cleanup; }
    }
    if (xxwidgets_widget_set_visible(ui.window, 1) != XXWIDGETS_OK) { result = 1; goto cleanup; }
    show_message(&ui, startup_message);
    if (shown) (void)open_file(&ui, shown, gui_frontend_scan_after_open());
    else if (path) show_message(&ui, CDIE_TR("Cannot open this file: its name is not valid text (UTF-8)."));
    ui.running = 1;
    while (ui.running) {
        status = xxwidgets_app_poll(ui.app, 30);
        if (status != XXWIDGETS_OK) { result = 1; break; }
        {
            /* A later launch's file ("Single application"), as WM_COPYDATA on
             * Windows: the path, history, a scan if wanted, and to the front. */
            char *forwarded = NULL;
            if (gui_frontend_instance_poll(ui.window, &forwarded) && forwarded) {
                gtk_window_present(GTK_WINDOW(xxwidgets_widget_native_handle(ui.window)));
                /* "": a launch without a file, which only raises the window. */
                if (forwarded[0] && xxwidgets_file_name_bar_set_path(ui.bar, forwarded) == XXWIDGETS_OK)
                    (void)open_file(&ui, forwarded, gui_frontend_scan_after_open());
            }
            free(forwarded);
        }
        main_loop_pass(&ui);
    }
cleanup:
    /* Changes only (View/Advanced was saved when it changed). */
    if (!smoke && ui.settings && ui.window) {
        gui_options_values values;
        if (current_options(&ui,&values)==XXWIDGETS_OK) (void)gui_settings_save(ui.settings,&values,ui.options_page);
    }
    gui_frontend_instance_close();
    free(ui.loaded); free(ui.message);
    if (ui.main_panel) (void)xxwidgets_die_main_panel_destroy(ui.main_panel);
    cdie_translation_close();
    if (ui.app) (void)xxwidgets_app_destroy(ui.app);
    gui_settings_close(ui.settings);
    gui_tree_reset();
    gui_backend_shutdown(); return result;
}
