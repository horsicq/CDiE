/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_scan_options.h"
#include "gui_backend.h"
#include "app/cdie_translation.h"
#include <xxwidgets/xxwidgets_combobox.h>
/* Format compatibility aliases must not rename options-page callbacks. */
#undef validate
#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/strings/xx_string.h>
#include <stdlib.h>
#include <wchar.h>
#include "gui_error.h"

/* Translated where shown; CDIE_N_ only marks them for the catalog. */
static const char *const color_names[XX_SCAN_COLOR_COUNT] = {
    CDIE_N_("Unknown"), CDIE_N_("Installer"), CDIE_N_("SFX"), CDIE_N_("Archive"), CDIE_N_("Protection"), CDIE_N_("PE tool"),
    CDIE_N_("APK tool"), CDIE_N_("Operation system"), CDIE_N_("Virtual machine"), CDIE_N_("Platform"), CDIE_N_("DOS extender"),
    CDIE_N_("Format"), CDIE_N_("Sign tool"), CDIE_N_("Certificate"), CDIE_N_("Licensing"), CDIE_N_("Language"),
    CDIE_N_("Corrupted data"), CDIE_N_("Personal data"), CDIE_N_("Author"), CDIE_N_("Virus"), CDIE_N_("Trojan"), CDIE_N_("Malware"),
    CDIE_N_("Debug"), CDIE_N_("Debug data"), CDIE_N_("Game engine"), CDIE_N_("Compiler"), CDIE_N_("Compressor"), CDIE_N_("Converter"),
    CDIE_N_("Creator"), CDIE_N_("Data"), CDIE_N_("Database"), CDIE_N_("Document"), CDIE_N_("Generic"), CDIE_N_("Image"),
    CDIE_N_("Installer data"), CDIE_N_("Library"), CDIE_N_("Linker"), CDIE_N_("Loader"), CDIE_N_("Obfuscator"), CDIE_N_("Overlay"),
    CDIE_N_("Package"), CDIE_N_("Player"), CDIE_N_("Producer"), CDIE_N_("Protector data"), CDIE_N_("ROM"), CDIE_N_("SFX data"),
    CDIE_N_("Source code"), CDIE_N_("Stub"), CDIE_N_("Tool")};

/* A portable editor keeps the color value explicit and supports system colors,
 * which cannot be represented by a native RGB picker alone. */
typedef struct palette_page {
    gui_scan_page_values *values;
    xxwidgets_widget *table, *dark, *foreground, *background, *set, *note, *label_fg, *label_bg, *preview;
    xxwidgets_widget *owner;
    int selected, dark_mode;
} palette_page;

static void color_text(char text[8], uint32_t color)
{
    if (color == UINT32_MAX) text[0] = 0;
    else xx_rt_snprintf(text, 8, "#%06x", (unsigned int)color);
}

static int parse_color(const char *text, uint32_t *color)
{
    uint32_t value = 0;
    if (!text[0]) { *color = UINT32_MAX; return 1; }
    if (*text == '#') ++text;
    if (xx_rt_strlen(text) != 6) return 0;
    for (int i = 0; i < 6; ++i) {
        unsigned char c = (unsigned char)text[i];
        unsigned int digit = c >= '0' && c <= '9' ? c - '0' :
            c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
        if (digit > 15) return 0;
        value = value * 16 + digit;
    }
    *color = value;
    return 1;
}

static xx_scan_color_options *palette_options(palette_page *page)
{
    return page->dark_mode ? &page->values->dark_colors : &page->values->colors;
}

static xxwidgets_status palette_rows(palette_page *page)
{
    const char *cells[XX_SCAN_COLOR_COUNT * 3];
    char colors[XX_SCAN_COLOR_COUNT][2][8];
    xx_scan_color_options *options = palette_options(page);
    xxwidgets_status status;
    for (size_t i = 0; i < XX_SCAN_COLOR_COUNT; ++i) {
        color_text(colors[i][0], options->records[i].foreground);
        color_text(colors[i][1], options->records[i].background);
        cells[i * 3] = CDIE_TR(color_names[i]);
        cells[i * 3 + 1] = colors[i][0];
        cells[i * 3 + 2] = colors[i][1];
    }
    status = xxwidgets_table_set_rows(page->table, cells, XX_SCAN_COLOR_COUNT);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(page->table, page->selected);
    return status;
}

static xxwidgets_status palette_show(palette_page *page)
{
    char foreground[8], background[8];
    xx_scan_color_record record = palette_options(page)->records[page->selected];
    xxwidgets_tree_node node = {SIZE_MAX, CDIE_TR(color_names[page->selected]), 0};
    xxwidgets_tree_color color = {record.foreground, record.background, record.foreground, record.background};
    xxwidgets_status status;
    color_text(foreground, record.foreground); color_text(background, record.background);
    status = xxwidgets_widget_set_text(page->foreground, foreground);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(page->background, background);
    if (status == XXWIDGETS_OK) status = xxwidgets_treeview_set_colored_nodes(page->preview, &node, &color, 1);
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(page->preview, -1);
    return status;
}

static int palette_store(palette_page *page, char *error, size_t capacity)
{
    char foreground[16], background[16];
    xx_scan_color_record color;
    if (xxwidgets_widget_get_text(page->foreground, foreground, sizeof(foreground), NULL) != XXWIDGETS_OK ||
        xxwidgets_widget_get_text(page->background, background, sizeof(background), NULL) != XXWIDGETS_OK ||
        !parse_color(foreground, &color.foreground) || !parse_color(background, &color.background)) {
        if (error && capacity) xx_rt_snprintf(error, capacity, "%s", CDIE_TR("Enter #RRGGBB, or leave a color empty for the system default."));
        return 0;
    }
    palette_options(page)->records[page->selected] = color;
    return 1;
}

static xxwidgets_status palette_destroy(void *state)
{
    palette_page *page = state;
    xxwidgets_widget *controls[] = {page->table, page->dark, page->foreground, page->background, page->set, page->note,
        page->label_fg, page->label_bg, page->preview};
    xxwidgets_status status = XXWIDGETS_OK;
    for (size_t i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) if (controls[i]) {
        xxwidgets_status cleanup = xxwidgets_widget_destroy(controls[i]);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    xx_rt_free(page);
    return status;
}

static xxwidgets_status palette_create(xxwidgets_widget *dialog, xxwidgets_rect b, void *user, void **out)
{
    palette_page *page = xx_rt_calloc(1, sizeof(*page));
    const char *titles[3] = {CDIE_TR("Category"), CDIE_TR("Foreground"), CDIE_TR("Background")};
    xxwidgets_app *app = xxwidgets_widget_app(dialog);
    xxwidgets_status status;
    if (!page) return XXWIDGETS_OUT_OF_MEMORY;
    page->values = user; page->owner = dialog;
    status = xxwidgets_table_create(dialog, (xxwidgets_rect){b.x, b.y, b.width, 15}, titles, 3, &page->table);
#define PC(field, kind, text, px, py, pw, ph) if (status == XXWIDGETS_OK) status = xxwidgets_widget_create(app, dialog, kind, CDIE_TR(text), (xxwidgets_rect){b.x + (px), b.y + (py), pw, ph}, &page->field)
    PC(dark, XXWIDGETS_CHECKBOX, CDIE_N_("Dark appearance"), 0, 16, 24, 1);
    PC(label_fg, XXWIDGETS_LABEL, CDIE_N_("Foreground"), 0, 18, 14, 1);
    PC(foreground, XXWIDGETS_EDIT, "", 14, 18, 14, 1);
    PC(label_bg, XXWIDGETS_LABEL, CDIE_N_("Background"), 30, 18, 14, 1);
    PC(background, XXWIDGETS_EDIT, "", 44, 18, 14, 1);
    PC(set, XXWIDGETS_BUTTON, CDIE_N_("Apply color"), 0, 20, 14, 1);
    PC(preview, XXWIDGETS_TREEVIEW, "", 17, 20, b.width - 17, 2);
    PC(note, XXWIDGETS_LABEL, CDIE_N_("Empty = system color; otherwise enter #RRGGBB."), 0, 23, b.width, 1);
#undef PC
    if (status == XXWIDGETS_OK) status = palette_rows(page);
    if (status == XXWIDGETS_OK) status = palette_show(page);
    if (status != XXWIDGETS_OK) { palette_destroy(page); return status; }
    *out = page;
    return XXWIDGETS_OK;
}

static void palette_event(void *state, const xxwidgets_event *event)
{
    palette_page *page = state;
    char error[192] = "";
    int selected = page->selected, dark = page->dark_mode;
    if (event->widget == page->table && event->type == XXWIDGETS_EVENT_SELECT && event->value >= 0) selected = event->value;
    else if (event->widget == page->dark && (event->type == XXWIDGETS_EVENT_CLICK || event->type == XXWIDGETS_EVENT_CHANGE))
        (void)xxwidgets_widget_get_value(page->dark, &dark);
    else if (event->widget != page->set || event->type != XXWIDGETS_EVENT_CLICK) return;
    if (selected >= XX_SCAN_COLOR_COUNT || !palette_store(page, error, sizeof(error))) {
        (void)xxwidgets_widget_set_text(page->note, error);
        (void)xxwidgets_widget_set_value(page->table, page->selected);
        (void)xxwidgets_widget_set_value(page->dark, page->dark_mode);
        return;
    }
    page->selected = selected; page->dark_mode = dark;
    (void)palette_rows(page); (void)palette_show(page);
    (void)xxwidgets_widget_set_text(page->note, CDIE_TR("Empty = system color; otherwise enter #RRGGBB."));
}

static xxwidgets_status palette_reset(void *state)
{
    palette_page *page = state;
    xx_scan_color_options_init(&page->values->colors, 0);
    xx_scan_color_options_init(&page->values->dark_colors, 1);
    xxwidgets_status status = palette_rows(page);
    return status == XXWIDGETS_OK ? palette_show(page) : status;
}

static int palette_apply(void *state, char *error, size_t capacity) { return palette_store(state, error, capacity); }

/* The Scan page's checkboxes, in order: the six scan flags (xxwidgets bit
 * order), the custom database, the four expansion modes (GUI_SCAN_* bit
 * order), then the six behaviours of behavior() below. */
enum { CHECK_FLAGS = 0, CHECK_CUSTOM = 6, CHECK_MODES = 7, CHECK_BEHAVIORS = 11, CHECK_COUNT = 17 };
/* One folder row per GUI_DB_* slot. */
enum { PATH_COUNT = GUI_DB_COUNT, CONTROL_CAPACITY = 64 };
typedef struct scan_page {
    gui_scan_page_values *values, edited;
    gui_database_preferences *databases;
    xxwidgets_database_paths_callbacks callbacks;
    void *user;
    xxwidgets_widget *owner, *checks[CHECK_COUNT], *engine, *colors;
    xxwidgets_widget *paths[PATH_COUNT], *browse[PATH_COUNT], *url, *update, *notice;
    xxwidgets_widget *controls[CONTROL_CAPACITY];
    size_t control_count;
    int pending_browse, pending_colors, pending_update;
} scan_page;

static xxwidgets_status control(scan_page *page, xxwidgets_kind kind, const char *text, xxwidgets_rect bounds, xxwidgets_widget **out)
{
    xxwidgets_widget *widget = NULL;
    xxwidgets_status status;
    if (page->control_count == CONTROL_CAPACITY) return XXWIDGETS_INVALID_ARGUMENT;
    status = xxwidgets_widget_create(xxwidgets_widget_app(page->owner), page->owner, kind, CDIE_TR(text), bounds, &widget);
    if (status == XXWIDGETS_OK) {
        page->controls[page->control_count++] = widget;
        if (out) *out = widget;
    }
    return status;
}

static int behavior(const gui_scan_page_values *v, int index)
{
    const int values[] = {v->scan_after_open, v->recursive, v->profiling, v->sort, v->use_cache, v->highlight};
    return values[index] != 0;
}

static xxwidgets_status scan_fill(scan_page *page, const gui_scan_page_values *values, int reset_paths)
{
    xxwidgets_status status = XXWIDGETS_OK;
    for (int i = 0; status == XXWIDGETS_OK && i < CHECK_COUNT; ++i) {
        int value = i < CHECK_CUSTOM ? (int)((values->options.flags >> (i - CHECK_FLAGS)) & 1u) :
            i == CHECK_CUSTOM ? (values->options.databases & XXWIDGETS_SCAN_DATABASE_CUSTOM) != 0 :
            i < CHECK_BEHAVIORS ? (int)((values->modes >> (i - CHECK_MODES)) & 1u) : behavior(values, i - CHECK_BEHAVIORS);
        status = xxwidgets_widget_set_value(page->checks[i], value);
    }
    if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_value(page->engine, 0);
    if (status == XXWIDGETS_OK && page->databases) {
        const char *paths[PATH_COUNT] = {page->databases->main_path, page->databases->custom_path};
        for (int i = 0; status == XXWIDGETS_OK && i < PATH_COUNT; ++i)
            status = xxwidgets_widget_set_text(page->paths[i], reset_paths ? "" : paths[i]);
        if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_text(page->url, reset_paths ?
            "https://github.com/horsicq/Detect-It-Easy/releases/tag/current-database" : page->databases->main_update_url);
    }
    if (status == XXWIDGETS_OK) page->edited = *values;
    return status;
}

static xxwidgets_status scan_destroy(void *state)
{
    scan_page *page = state;
    xxwidgets_status status = XXWIDGETS_OK;
    for (size_t i = page->control_count; i > 0; --i) {
        xxwidgets_status cleanup = xxwidgets_widget_destroy(page->controls[i - 1]);
        if (status == XXWIDGETS_OK) status = cleanup;
    }
    xx_rt_free(page);
    return status;
}

static xxwidgets_status scan_create_common(xxwidgets_widget *dialog, xxwidgets_rect b, gui_scan_page_values *values,
    gui_scan_options_page_context *context, void **out)
{
    static const char *const labels[CHECK_COUNT] = {CDIE_N_("Deep scan"), CDIE_N_("Heuristic scan"), CDIE_N_("Verbose"),
        CDIE_N_("Aggressive scan"), CDIE_N_("Hide unknown"), CDIE_N_("Format result"), CDIE_N_("Custom database"),
        CDIE_N_("All file types"), CDIE_N_("Scan overlays"), CDIE_N_("Scan resources"), CDIE_N_("Scan archive members"),
        CDIE_N_("Scan after open"), CDIE_N_("Recursive scan"), CDIE_N_("Profiling"), CDIE_N_("Sort"), CDIE_N_("Use cache"), CDIE_N_("Highlight")};
    static const char *const path_labels[PATH_COUNT] = {CDIE_N_("Main database"), CDIE_N_("Custom database folder")};
    scan_page *page = xx_rt_calloc(1, sizeof(*page));
    xxwidgets_status status = XXWIDGETS_OK;
    int half = (b.width - 2) / 2;
    if (!page) return XXWIDGETS_OUT_OF_MEMORY;
    page->owner = dialog; page->values = values; page->pending_browse = -1;
    if (context) { page->databases = context->databases; page->callbacks = context->callbacks; page->user = context->user; }
    for (int i = 0; status == XXWIDGETS_OK && i < CHECK_COUNT; ++i)
        status = control(page, XXWIDGETS_CHECKBOX, labels[i], (xxwidgets_rect){b.x + (i & 1) * (half + 2), b.y + i / 2, half, 1}, &page->checks[i]);
    if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_LABEL, CDIE_N_("Scan engine"), (xxwidgets_rect){b.x, b.y + 10, 14, 1}, NULL);
    if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_COMBOBOX, "", (xxwidgets_rect){b.x + 14, b.y + 10, 28, 1}, &page->engine);
    if (status == XXWIDGETS_OK) {
        const char *engines[1] = {"Detect It Easy (DiE)"};
        xx_meta_string records[1] = {0};
        xx_str_w_s names[1] = {{0}};
        for (size_t i = 0; i < 1 && status == XXWIDGETS_OK; ++i) {
            names[i].data = xx_str_utf8_to_unicode(engines[i]);
            if (!names[i].data) { status = XXWIDGETS_OUT_OF_MEMORY; break; }
            names[i].length = wcslen(names[i].data); names[i].capacity = names[i].length + 1; names[i].is_view = true;
            records[i].meta_string = &names[i]; records[i].var.type = XX_VAR_TYPE_UINT64; records[i].var.val.u64 = i;
        }
        if (status == XXWIDGETS_OK) status = xxwidgets_combobox_set_records(page->engine, records, 1);
        for (size_t i = 0; i < 1; ++i) xx_str_free_unicode(names[i].data);
    }
    if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_BUTTON, CDIE_N_("Colors..."), (xxwidgets_rect){b.x + b.width - 18, b.y + 10, 18, 1}, &page->colors);
    if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_LABEL,
        CDIE_N_("Recursive: resource/overlay children."), (xxwidgets_rect){b.x, b.y + 11, b.width, 1}, NULL);
    if (page->databases) {
        for (int i = 0; status == XXWIDGETS_OK && i < PATH_COUNT; ++i) {
            status = control(page, XXWIDGETS_LABEL, path_labels[i], (xxwidgets_rect){b.x, b.y + 12 + i * 2, b.width, 1}, NULL);
            if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_EDIT, "", (xxwidgets_rect){b.x, b.y + 13 + i * 2, b.width - 12, 1}, &page->paths[i]);
            if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_BUTTON, CDIE_N_("Browse..."), (xxwidgets_rect){b.x + b.width - 11, b.y + 13 + i * 2, 11, 1}, &page->browse[i]);
            if (status == XXWIDGETS_OK && !page->callbacks.browse) status = xxwidgets_widget_set_enabled(page->browse[i], 0);
        }
        if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_LABEL, CDIE_N_("DIE database update URL"), (xxwidgets_rect){b.x, b.y + 16, b.width, 1}, NULL);
        if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_EDIT, "", (xxwidgets_rect){b.x, b.y + 17, b.width - 12, 1}, &page->url);
        /* It only shows the address in the browser; "Update database" in the
         * main window downloads and installs from it. */
        if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_BUTTON, CDIE_N_("Open page"), (xxwidgets_rect){b.x + b.width - 11, b.y + 17, 11, 1}, &page->update);
        const char *engine_names[2] = {CDIE_N_("PEiD — unavailable: this build has no PEiD engine"), CDIE_N_("YARA — unavailable: this build has no YARA engine")};
        const char *paths[2] = {page->databases->peid_path, page->databases->yara_path};
        const char *urls[2] = {page->databases->peid_update_url, page->databases->yara_update_url};
        for (int i = 0; status == XXWIDGETS_OK && i < 2; ++i) {
            xxwidgets_widget *path = NULL, *url = NULL;
            status = control(page, XXWIDGETS_LABEL, engine_names[i], (xxwidgets_rect){b.x, b.y + 19 + i * 4, b.width, 1}, NULL);
            if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_EDIT, paths[i], (xxwidgets_rect){b.x, b.y + 20 + i * 4, b.width, 1}, &path);
            if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_EDIT, urls[i], (xxwidgets_rect){b.x, b.y + 21 + i * 4, b.width, 1}, &url);
            if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(path, 0);
            if (status == XXWIDGETS_OK) status = xxwidgets_widget_set_enabled(url, 0);
        }
        if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_LABEL, CDIE_N_("Profiling: total scan duration and detection count in Log."),
            (xxwidgets_rect){b.x, b.y + 27, b.width, 1}, &page->notice);
        if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_LABEL,
            CDIE_N_("Empty folders use defaults. Open page only browses; Update database installs."),
            (xxwidgets_rect){b.x, b.y + 28, b.width, 1}, NULL);
    }
    else if (status == XXWIDGETS_OK) status = control(page, XXWIDGETS_LABEL, CDIE_N_("Profiling: total scan duration and detection count in Log."),
        (xxwidgets_rect){b.x, b.y + 12, b.width, 1}, &page->notice);
    if (status == XXWIDGETS_OK) status = scan_fill(page, values, 0);
    if (status != XXWIDGETS_OK) { scan_destroy(page); return status; }
    *out = page;
    return XXWIDGETS_OK;
}

static xxwidgets_status scan_create(xxwidgets_widget *dialog, xxwidgets_rect b, void *user, void **out)
{ return scan_create_common(dialog, b, user, NULL, out); }
static xxwidgets_status scan_create_ex(xxwidgets_widget *dialog, xxwidgets_rect b, void *user, void **out)
{
    gui_scan_options_page_context *context = user;
    return scan_create_common(dialog, b, context->values, context, out);
}

static void scan_event(void *state, const xxwidgets_event *event)
{
    scan_page *page = state;
    if (event->type != XXWIDGETS_EVENT_CLICK) return;
    if (event->widget == page->colors) page->pending_colors = 1;
    if (event->widget == page->update) page->pending_update = 1;
    for (int i = 0; i < PATH_COUNT; ++i) if (event->widget == page->browse[i]) page->pending_browse = i;
}

static int valid_url(const char *url)
{
    if (xx_rt_strncmp(url, "https://", 8) && xx_rt_strncmp(url, "http://", 7)) return 0;
    const char *host = url + (url[4] == 's' ? 8 : 7);
    if (!*host || *host == '/' || *host == ' ' || *host == '?') return 0;
    for (const unsigned char *p = (const unsigned char *)url; *p; ++p) if (*p <= 32 || *p == 127) return 0;
    return 1;
}

/* A failed optional child dialog must preserve the parent form and its edits.
 * process errors normally terminate the whole shared Options dialog. */
static xxwidgets_status scan_child_error(scan_page *page, const char *action, xxwidgets_status failure)
{
    char message[256];
    xx_rt_snprintf(message, sizeof(message), "%s %s", CDIE_TR(action), gui_error_status_text(failure));
    return xxwidgets_widget_set_text(page->notice, message);
}

static xxwidgets_status scan_process(void *state)
{
    scan_page *page = state;
    if (page->pending_browse >= 0) {
        int index = page->pending_browse, accepted = 0;
        char initial[GUI_PREFERENCE_PATH_MAX], *path = NULL;
        page->pending_browse = -1;
        xxwidgets_status status = xxwidgets_widget_get_text(page->paths[index], initial, sizeof(initial), NULL);
        if (status != XXWIDGETS_OK) return status;
        status = page->callbacks.browse(page->owner, initial, &path, &accepted, page->user);
        if (status == XXWIDGETS_OK && accepted && path) status = xxwidgets_widget_set_text(page->paths[index], path);
        free(path);
        /* The folder can still be typed; Options and its edits stay. */
        if (status != XXWIDGETS_OK) return scan_child_error(page, CDIE_N_("Cannot show the folder chooser."), status);
    }
    if (page->pending_update) {
        char url[GUI_PREFERENCE_URL_MAX];
        page->pending_update = 0;
        xxwidgets_status status = xxwidgets_widget_get_text(page->url, url, sizeof(url), NULL);
        if (status != XXWIDGETS_OK) return status;
        if (valid_url(url)) {
            status = xxwidgets_open_url(page->owner, url);
            return status == XXWIDGETS_OK ? status : scan_child_error(page, CDIE_N_("Cannot open the release page in a browser."), status);
        }
        return xxwidgets_widget_set_text(page->notice, CDIE_TR("Enter a valid HTTP or HTTPS update URL."));
    }
    if (page->pending_colors) {
        gui_scan_page_values edited = page->edited;
        xxwidgets_options_page definition = {0};
        int accepted = 0;
        page->pending_colors = 0;
        definition.title = CDIE_TR("Category colors"); definition.width = 66; definition.height = 24;
        definition.create = palette_create; definition.event = palette_event; definition.reset = palette_reset;
        definition.validate = palette_apply; definition.apply = palette_apply; definition.destroy = palette_destroy; definition.user = &edited;
        xxwidgets_status status = xxwidgets_options_pages_dialog(page->owner, CDIE_TR("Colors"), &definition, 1, NULL, &accepted);
        if (status == XXWIDGETS_OK && accepted) { page->edited.colors = edited.colors; page->edited.dark_colors = edited.dark_colors; }
        return status == XXWIDGETS_OK ? status : scan_child_error(page, CDIE_N_("Cannot open Colors."), status);
    }
    return XXWIDGETS_OK;
}

static xxwidgets_status scan_reset(void *state)
{
    scan_page *page = state;
    gui_scan_page_values defaults = {0};
    xxwidgets_status status = xxwidgets_scan_options_init(&defaults.options);
    defaults.modes = GUI_SCAN_MODES_DEFAULT;
    defaults.scan_after_open = defaults.recursive = defaults.sort = defaults.highlight = 1;
    xx_rt_memcpy(defaults.engine, "die", 4);
    xx_scan_color_options_init(&defaults.colors, 0); xx_scan_color_options_init(&defaults.dark_colors, 1);
    return status == XXWIDGETS_OK ? scan_fill(page, &defaults, 1) : status;
}

static int scan_read(scan_page *page, gui_scan_page_values *values, gui_database_preferences *databases, char *error, size_t capacity)
{
    int controls[CHECK_COUNT], engine = 0;
    *values = page->edited;
    values->options.flags = values->options.databases = values->modes = 0;
    for (int i = 0; i < CHECK_COUNT; ++i) {
        if (xxwidgets_widget_get_value(page->checks[i], &controls[i]) != XXWIDGETS_OK) goto invalid;
        if (controls[i]) {
            if (i < CHECK_CUSTOM) values->options.flags |= 1u << (i - CHECK_FLAGS);
            else if (i == CHECK_CUSTOM) values->options.databases |= XXWIDGETS_SCAN_DATABASE_CUSTOM;
            else if (i < CHECK_BEHAVIORS) values->modes |= 1u << (i - CHECK_MODES);
        }
    }
    values->scan_after_open = controls[CHECK_BEHAVIORS]; values->recursive = controls[CHECK_BEHAVIORS + 1];
    values->profiling = controls[CHECK_BEHAVIORS + 2]; values->sort = controls[CHECK_BEHAVIORS + 3];
    values->use_cache = controls[CHECK_BEHAVIORS + 4]; values->highlight = controls[CHECK_BEHAVIORS + 5];
    if (xxwidgets_widget_get_value(page->engine, &engine) != XXWIDGETS_OK || engine < 0 || (size_t)engine >= gui_backend_engine_count()) goto invalid;
    xx_rt_snprintf(values->engine, sizeof(values->engine), "%s", gui_backend_engine_id((size_t)engine));
    if (page->databases && databases) {
        *databases = *page->databases;
        char *paths[PATH_COUNT] = {databases->main_path, databases->custom_path};
        for (int i = 0; i < PATH_COUNT; ++i)
            if (xxwidgets_widget_get_text(page->paths[i], paths[i], GUI_PREFERENCE_PATH_MAX, NULL) != XXWIDGETS_OK) goto invalid;
        if (xxwidgets_widget_get_text(page->url, databases->main_update_url, sizeof(databases->main_update_url), NULL) != XXWIDGETS_OK) goto invalid;
        if (databases->main_update_url[0] && !valid_url(databases->main_update_url)) {
            if (error && capacity) xx_rt_snprintf(error, capacity, "%s", CDIE_TR("Enter a valid HTTP or HTTPS database update URL."));
            return 0;
        }
    }
    return 1;
invalid:
    if (error && capacity) xx_rt_snprintf(error, capacity, "%s", CDIE_TR("Cannot read the selected scan options, or a database path is too long."));
    return 0;
}

void gui_scan_options_database_error(int slot, int missing, const char *path, char *error, size_t capacity)
{
    static const char *const absent[PATH_COUNT] = {CDIE_N_("The main database folder does not exist: %s"),
        CDIE_N_("The custom database folder does not exist: %s")};
    if (!error || !capacity) return;
    if (missing && slot >= 0 && slot < PATH_COUNT) cdie_translation_snprintf(error, capacity, absent[slot], path ? path : "");
    else if (path && path[0])
        cdie_translation_snprintf(error, capacity, CDIE_N_("No signatures could be loaded from %s. Choose a Detect It Easy db folder."), path);
    else cdie_translation_snprintf(error, capacity, "%s", CDIE_TR("No signatures could be loaded from the default database folder."));
}

/* The backend's own check, before OK leaves this page: a folder other than
 * the loaded one must exist. A failure here keeps Options on this page. */
static int scan_folders_exist(const gui_database_preferences *databases, char *error, size_t capacity)
{
    const char *paths[PATH_COUNT] = {databases->main_path, databases->custom_path};
    const char *current[PATH_COUNT] = {gui_backend_main_db_path(), gui_backend_custom_db_path()};
    for (int i = 0; i < PATH_COUNT; ++i) {
        if (!paths[i][0] || !xx_rt_strcmp(paths[i], current[i]) || xx_fs_is_dir(paths[i]) || xx_fs_is_file(paths[i])) continue;
        gui_scan_options_database_error(i, 1, paths[i], error, capacity);
        return 0;
    }
    return 1;
}

static int scan_validate(void *state, char *error, size_t capacity)
{
    scan_page *page = state;
    gui_scan_page_values values;
    gui_database_preferences databases;
    if (!scan_read(page, &values, &databases, error, capacity)) return 0;
    return !page->databases || scan_folders_exist(&databases, error, capacity);
}
static int scan_apply(void *state, char *error, size_t capacity)
{
    scan_page *page = state;
    gui_scan_page_values values;
    gui_database_preferences databases;
    if (!scan_read(page, &values, &databases, error, capacity)) return 0;
    *page->values = values;
    if (page->databases) *page->databases = databases;
    return 1;
}

xxwidgets_status gui_scan_options_page(xxwidgets_options_page *page, const char *title, gui_scan_page_values *values)
{
    if (!page || !title || !values || (values->modes & ~GUI_SCAN_MODES_MASK)) return XXWIDGETS_INVALID_ARGUMENT;
    xx_rt_memset(page, 0, sizeof(*page));
    page->title = title; page->width = 80; page->height = 13;
    page->create = scan_create; page->event = scan_event; page->process = scan_process;
    page->reset = scan_reset; page->validate = scan_validate; page->apply = scan_apply; page->destroy = scan_destroy; page->user = values;
    return XXWIDGETS_OK;
}
xxwidgets_status gui_scan_options_page_ex(xxwidgets_options_page *page, const char *title, gui_scan_options_page_context *context)
{
    if (!context || !context->values) return XXWIDGETS_INVALID_ARGUMENT;
    xxwidgets_status status = gui_scan_options_page(page, title, context->values);
    if (status == XXWIDGETS_OK) {
        page->create = scan_create_ex; page->user = context; page->height = context->databases ? 29 : 13;
    }
    return status;
}
