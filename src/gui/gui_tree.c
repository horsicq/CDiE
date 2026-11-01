/* Copyright (c) 2026 hors<horsicq@gmail.com> · SPDX-License-Identifier: MIT */
#include "gui_tree.h"
#include "app/cdie_translation.h"
#include "gui_backend.h"
#include "gui_error.h"
#include <xxwidgets/die_engine/xxwidgets_die_signature_editor.h>
#include <xxwidgets/xxwidgets_process.h>
#include <xxwidgets/xxwidgets_scan_directory.h>
#include <xxfclib/rt/xx_rt.h>
#include <stdio.h>
#include <xxfclib/threads/xx_threads.h>
#include <stdlib.h>
#include <string.h>

enum { MARK_SIGNATURE = 0, MARK_INFO, MARK_COUNT };

typedef struct tree_buffer {
    xxwidgets_tree_node *nodes;
    xxwidgets_tree_color *colors;
    char **names, **signatures; /* per node; NULL on group rows */
    unsigned int *marks;
    xx_scan_color_category *categories;
    size_t count, capacity, detections;
} tree_buffer;

static gui_scan_page_values g_tree_preferences;
static int g_tree_preferences_ready;

static xxwidgets_tree_color category_color(xx_scan_color_category category, const gui_scan_page_values *preferences)
{
    if (!preferences->highlight || category < XX_SCAN_COLOR_UNKNOWN || category >= XX_SCAN_COLOR_COUNT)
        return (xxwidgets_tree_color){UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};
    xx_scan_color_record light = preferences->colors.records[category], dark = preferences->dark_colors.records[category];
    return (xxwidgets_tree_color){light.foreground, light.background, dark.foreground, dark.background};
}

static void tree_preferences_init(void)
{
    if (g_tree_preferences_ready) return;
    memset(&g_tree_preferences, 0, sizeof(g_tree_preferences));
    g_tree_preferences.highlight = 1;
    xx_scan_color_options_init(&g_tree_preferences.colors, 0);
    xx_scan_color_options_init(&g_tree_preferences.dark_colors, 1);
    g_tree_preferences_ready = 1;
}

enum { PENDING_NONE = 0, PENDING_MARK, PENDING_SIGNATURES, PENDING_DIRECTORY, PENDING_LOG };

/* The rows of the tree on screen, the last scan's file and log, and what a
 * button press waits on. */
static struct {
    tree_buffer rows;
    char *path, *log;
    int type_id;
    const xxwidgets_widget *tree;
    xxwidgets_scan_panel *panel;
    size_t pending_node;
    int pending_mark, pending;
} g_marks = {{0}, NULL, NULL, 0, NULL, NULL, 0, -1, PENDING_NONE};

static void free_rows(tree_buffer *rows)
{
    for (size_t i = 0; i < rows->count; ++i) {
        free((void *)rows->nodes[i].text);
        free(rows->names[i]); free(rows->signatures[i]);
    }
    free(rows->nodes); free(rows->colors); free(rows->names); free(rows->signatures); free(rows->marks); free(rows->categories);
    memset(rows, 0, sizeof(*rows));
}

static char *copy_text(const char *text)
{
    char *copy = text ? malloc(strlen(text) + 1) : NULL;
    if (copy) strcpy(copy, text);
    return copy;
}

xxwidgets_status gui_tree_set_scan_preferences(const gui_scan_page_values *values)
{
    xxwidgets_tree_color *colors = NULL;
    xxwidgets_status status = XXWIDGETS_OK;
    static const char *const labels[MARK_COUNT] = {"S", "?"};
    int selected = -1;
    if (!values || (values->highlight != 0 && values->highlight != 1)) return XXWIDGETS_INVALID_ARGUMENT;
    for (size_t i = 0; i < XX_SCAN_COLOR_COUNT; ++i) {
        const xx_scan_color_record records[2] = {values->colors.records[i], values->dark_colors.records[i]};
        for (size_t j = 0; j < 2; ++j)
            if ((records[j].foreground > UINT32_C(0xffffff) && records[j].foreground != UINT32_MAX) ||
                (records[j].background > UINT32_C(0xffffff) && records[j].background != UINT32_MAX)) return XXWIDGETS_INVALID_ARGUMENT;
    }
    tree_preferences_init();
    if (g_marks.tree && g_marks.rows.count) {
        xxwidgets_widget *tree = (xxwidgets_widget *)g_marks.tree;
        colors = malloc(g_marks.rows.count * sizeof(*colors));
        if (!colors) return XXWIDGETS_OUT_OF_MEMORY;
        (void)xxwidgets_widget_get_value(tree, &selected);
        for (size_t i = 0; i < g_marks.rows.count; ++i) {
            colors[i] = category_color(g_marks.rows.categories[i], values);
            (void)xxwidgets_treeview_get_expanded(tree, i, &g_marks.rows.nodes[i].expanded);
        }
        status = xxwidgets_treeview_set_colored_nodes(tree, g_marks.rows.nodes, colors, g_marks.rows.count);
        if (status == XXWIDGETS_OK) status = xxwidgets_treeview_set_marks(tree, labels, MARK_COUNT, g_marks.rows.marks, g_marks.rows.count);
        if (status == XXWIDGETS_OK) {
            memcpy(g_marks.rows.colors, colors, g_marks.rows.count * sizeof(*colors));
            (void)xxwidgets_widget_set_value(tree, selected);
        } else {
            (void)xxwidgets_treeview_set_colored_nodes(tree, g_marks.rows.nodes, g_marks.rows.colors, g_marks.rows.count);
            (void)xxwidgets_treeview_set_marks(tree, labels, MARK_COUNT, g_marks.rows.marks, g_marks.rows.count);
            (void)xxwidgets_widget_set_value(tree, selected);
        }
        free(colors);
    }
    if (status == XXWIDGETS_OK) g_tree_preferences = *values;
    return status;
}

/* The worker scans with its own progress object, work. xx_pd_struct has plain
 * counters, so the UI never reads it: an observer bound on the worker copies
 * it into snapshot under lock and returns the UI's stop request, as
 * xxwidgets_process_dialog expects. Everything below lock is shared. */
typedef struct scan_job {
    const char *path;
    int type_id;
    tree_buffer buffer;
    char *report, *type;
    gui_backend_scan_info info;
    int ok;
    xx_pd_struct work;
    xx_mutex_t *lock;
    xx_pd_struct snapshot;
    int stop, done;
} scan_job;

static int grow(void **array, size_t size, size_t capacity)
{
    void *grown = realloc(*array, capacity * size);
    if (!grown) return 0;
    *array = grown;
    return 1;
}

static int collect_node(size_t parent, const char *text, const char *category, const gui_backend_detection *detection, void *user)
{
    tree_buffer *buffer = user;
    char *copy, *name = NULL, *signature = NULL;
    if (buffer->count == buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity * 2 : 16;
        if (capacity < buffer->capacity || capacity > SIZE_MAX / sizeof(xxwidgets_tree_node)) return 0;
        if (!grow((void **)&buffer->nodes, sizeof(*buffer->nodes), capacity) ||
            !grow((void **)&buffer->colors, sizeof(*buffer->colors), capacity) ||
            !grow((void **)&buffer->names, sizeof(*buffer->names), capacity) ||
            !grow((void **)&buffer->signatures, sizeof(*buffer->signatures), capacity) ||
            !grow((void **)&buffer->categories, sizeof(*buffer->categories), capacity) ||
            !grow((void **)&buffer->marks, sizeof(*buffer->marks), capacity)) return 0;
        buffer->capacity = capacity;
    }
    copy = copy_text(text);
    if (detection) {
        name = copy_text(detection->name);
        signature = detection->signature ? copy_text(detection->signature) : NULL;
    }
    if (!copy || (detection && (!name || (detection->signature && !signature)))) {
        free(copy); free(name); free(signature);
        return 0;
    }
    buffer->nodes[buffer->count] = (xxwidgets_tree_node){parent, copy, 1};
    buffer->categories[buffer->count] = category[0] ? xx_scan_type_to_color_category(category) : XX_SCAN_COLOR_COUNT;
    buffer->colors[buffer->count] = category_color(buffer->categories[buffer->count], &g_tree_preferences);
    buffer->names[buffer->count] = name;
    buffer->signatures[buffer->count] = signature;
    buffer->marks[buffer->count++] = (signature ? 1u << MARK_SIGNATURE : 0) | (name ? 1u << MARK_INFO : 0);
    if (category[0]) ++buffer->detections;
    return 1;
}

/* Worker thread, after each progress update and at each stop check. */
static bool observe_progress(const xx_pd_struct *progress, void *user)
{
    scan_job *job = user;
    int stop;
    xx_mutex_lock(job->lock);
    job->snapshot = *progress;
    stop = job->stop;
    xx_mutex_unlock(job->lock);
    return stop != 0;
}

static int scan_worker(void *arg)
{
    scan_job *job = arg;
    xx_pd_observer previous = xx_pd_set_observer(&job->work, observe_progress, job);
    job->ok = gui_backend_scan_tree(job->path, job->type_id, collect_node, &job->buffer,
                                    &job->report, &job->type, &job->info, &job->work);
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(job->lock);
    job->snapshot = job->work;
    job->done = 1;
    xx_mutex_unlock(job->lock);
    return 0;
}

/* UI thread: an xxwidgets_process_update_fn. */
static xxwidgets_status update_progress(void *user, int stop_requested, xx_pd_struct *progress, int *finished)
{
    scan_job *job = user;
    xx_mutex_lock(job->lock);
    if (stop_requested) job->stop = 1;
    *progress = job->snapshot;
    *finished = job->done;
    xx_mutex_unlock(job->lock);
    return XXWIDGETS_OK;
}

int gui_tree_scan(xxwidgets_widget *owner, xxwidgets_scan_panel *panel, const char *path, int type_id,
                  char **type, int *cancelled)
{
    scan_job job = {0};
    xx_pd_struct progress = xx_pd_init();
    xx_thread_t *thread;
    long long started = xx_rt_clock_ms();
    int ok;
    tree_preferences_init();
    if (type) *type = NULL;
    if (cancelled) *cancelled = 0;
    job.path = path; job.type_id = type_id;
    job.work = xx_pd_init(); job.snapshot = job.work;
    job.lock = xx_mutex_create();
    if (!job.lock) return 0;
    /* The script interpreter recurses. macOS gives secondary threads only
     * 512 KiB, so ask for at least what a main thread gets (Windows reserves). */
    thread = xx_thread_create_ex(scan_worker, &job, 8u * 1024u * 1024u);
    if (!thread) { xx_mutex_destroy(job.lock); return 0; }
    /* BUSY (inside an input callback or another modal form) returns before
     * starting, so the scan simply runs to its end; any other failure has
     * already passed the worker a stop request. Either way, join it. */
    if (owner) (void)xxwidgets_process_dialog(owner, CDIE_TR("Scanning"), &progress, update_progress, &job);
    ok = xx_thread_join(thread, NULL) && job.ok;
    xx_mutex_destroy(job.lock);
    if (cancelled) *cancelled = job.work.is_stop;
    if (ok && panel) ok = xxwidgets_scan_panel_set_tree(panel, job.buffer.nodes, job.buffer.colors,
        job.buffer.count, job.buffer.detections, job.report) == XXWIDGETS_OK;
    if (ok && panel) {
        /* The facts and time DIE shows around the tree, and the log behind Log. */
        (void)xxwidgets_scan_panel_set_file_info(panel, job.info.endianness, job.info.mode, job.info.architecture, job.info.type);
        (void)xxwidgets_scan_panel_set_elapsed(panel, xx_rt_clock_ms() - started);
    }
    if (ok && panel) {
        /* Labels as DIE draws them; the rows stay for the button presses. */
        static const char *const labels[MARK_COUNT] = {"S", "?"};
        xxwidgets_widget *tree = xxwidgets_scan_panel_control(panel, XXWIDGETS_SCAN_PANEL_RESULTS);
        char *copy = copy_text(path);
        if (copy && xxwidgets_treeview_set_marks(tree, labels, MARK_COUNT, job.buffer.marks, job.buffer.count) == XXWIDGETS_OK) {
            gui_tree_reset();
            g_marks.rows = job.buffer;
            memset(&job.buffer, 0, sizeof(job.buffer));
            g_marks.path = copy;
            g_marks.type_id = type_id;
            g_marks.tree = tree;
            g_marks.log = copy_text(job.info.log);
        } else free(copy);
    }
    if (ok && type) { *type = job.type; job.type = NULL; }
    free_rows(&job.buffer);
    gui_backend_free(job.report); gui_backend_free(job.type); gui_backend_free(job.info.log);
    return ok;
}

void gui_tree_reset(void)
{
    free_rows(&g_marks.rows);
    free(g_marks.path);
    free(g_marks.log);
    g_marks.path = g_marks.log = NULL;
    g_marks.tree = NULL;
    g_marks.pending = PENDING_NONE;
}

int gui_tree_event(xxwidgets_scan_panel *panel, const xxwidgets_event *event)
{
    const xxwidgets_widget *tree = panel ? xxwidgets_scan_panel_control(panel, XXWIDGETS_SCAN_PANEL_RESULTS) : NULL;
    static const struct { xxwidgets_scan_panel_control_id control; int pending; } buttons[] = {
        {XXWIDGETS_SCAN_PANEL_SIGNATURES, PENDING_SIGNATURES}, {XXWIDGETS_SCAN_PANEL_DIRECTORY, PENDING_DIRECTORY},
        {XXWIDGETS_SCAN_PANEL_LOG, PENDING_LOG}};
    size_t i;
    if (!event || event->type != XXWIDGETS_EVENT_CLICK || !tree) return 0;
    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); ++i)
        if (event->widget == xxwidgets_scan_panel_control(panel, buttons[i].control)) {
            g_marks.panel = panel;
            g_marks.pending = buttons[i].pending;
            return 1;
        }
    if (event->widget != tree) return 0;
    /* Only presses on the rows this module put there, in this tree. */
    if (tree == g_marks.tree && event->value >= 0 && (size_t)event->value < g_marks.rows.count &&
        event->x >= 0 && event->x < MARK_COUNT && (g_marks.rows.marks[event->value] >> event->x & 1u)) {
        g_marks.pending_node = (size_t)event->value;
        g_marks.pending_mark = event->x;
        g_marks.pending = PENDING_MARK;
    }
    return 1;
}

typedef struct run_target {
    const char *path;
    int type_id;
} run_target;

/* The signature editor's Run, for the signature at path: its result text
 * belongs to the editor's heap. */
static xxwidgets_status run_edited(const char *source, const char *path, char **result, void *user)
{
    const run_target *target = user;
    char *text = NULL;
    int ok = 0;
    if (target->path) ok = gui_backend_run_signature(target->path, target->type_id, path, source, &text);
    *result = copy_text(!target->path ? CDIE_TR("Scan a file first: Run executes the signature on it.") :
        text && text[0] ? text : ok ? CDIE_TR("Nothing detected.") : CDIE_TR("Cannot scan the file."));
    gui_backend_free(text);
    return *result ? XXWIDGETS_OK : XXWIDGETS_OUT_OF_MEMORY;
}

/* Entry index of the Signatures window: its source, or a note where a
 * compiled database has none. */
static xxwidgets_status load_signature(size_t index, char **source, const char **path, void *user)
{
    char *text;
    (void)user;
    if (!gui_backend_signature_entry((int)index, NULL, path)) return XXWIDGETS_INVALID_ARGUMENT;
    text = gui_backend_signature_source(*path);
    *source = copy_text(text ? text : CDIE_TR("// The source of this signature is not available: its database is compiled.\n"));
    gui_backend_free(text);
    return *source ? XXWIDGETS_OK : XXWIDGETS_OUT_OF_MEMORY;
}

/* Every loaded signature, signature (a database path) or the first shown. */
static xxwidgets_status signatures_window(xxwidgets_widget *owner, const char *signature)
{
    int count = gui_backend_signature_count(), i;
    size_t selected = 0;
    const char **names;
    run_target target = {g_marks.path, g_marks.type_id};
    xxwidgets_status status;
    if (count <= 0) return XXWIDGETS_UNAVAILABLE;
    names = malloc((size_t)count * sizeof(*names));
    if (!names) return XXWIDGETS_OUT_OF_MEMORY;
    for (i = 0; i < count; ++i) {
        const char *path = NULL;
        if (!gui_backend_signature_entry(i, &names[i], &path)) { free(names); return XXWIDGETS_PLATFORM_ERROR; }
        if (signature && path && !strcmp(path, signature)) selected = (size_t)i;
    }
    status = xxwidgets_die_signatures_dialog(owner, CDIE_TR("Signatures"), names, (size_t)count, selected,
        load_signature, run_edited, &target);
    free(names);
    return status;
}

typedef struct directory_out {
    xxwidgets_scan_directory_task *task;
    xxwidgets_status status;
} directory_out;

static void emit_detection(const char *type, const char *name, const char *version, const char *info, void *user)
{
    directory_out *out = user;
    xxwidgets_scan_result detection = {type, name, version, info};
    if (out->status == XXWIDGETS_OK) out->status = xxwidgets_scan_directory_emit(out->task, &detection);
}

/* One file of a directory scan, on the dialog's worker thread. */
static xxwidgets_status scan_directory_file(xxwidgets_scan_directory_task *task, const char *path, const xxwidgets_scan_options *options, void *user)
{
    directory_out out = {task, XXWIDGETS_OK};
    (void)user;
    if (xxwidgets_scan_directory_cancelled(task)) return XXWIDGETS_OK;
    if (!gui_backend_scan_detections(path, options->flags, options->databases, emit_detection, &out) && out.status == XXWIDGETS_OK)
        return XXWIDGETS_PLATFORM_ERROR;
    return out.status;
}

static xxwidgets_status directory_window(xxwidgets_widget *owner)
{
    xxwidgets_scan_options options = {XXWIDGETS_SCAN_DEEP | XXWIDGETS_SCAN_HEURISTIC | XXWIDGETS_SCAN_VERBOSE,
        XXWIDGETS_SCAN_DATABASE_CUSTOM};
    char *folder = copy_text(g_marks.path ? g_marks.path : "");
    xxwidgets_status status;
    if (!folder) return XXWIDGETS_OUT_OF_MEMORY;
    if (g_marks.panel) {
        (void)xxwidgets_scan_panel_get_flags(g_marks.panel, &options.flags);
        (void)xxwidgets_scan_panel_get_databases(g_marks.panel, &options.databases);
    }
    {
        /* Start in the folder of the scanned file, if there is one. */
        char *cut = NULL;
        for (char *s = folder; *s; ++s) if (*s == '/' || *s == '\\') cut = s;
        if (cut) *cut = 0; else folder[0] = 0;
    }
    status = xxwidgets_scan_directory_dialog(owner, CDIE_TR("Scan directory"), folder, &options, scan_directory_file, NULL);
    free(folder);
    return status;
}

/* "Name" for the search, percent-encoded as a query value. */
static char *search_url(const char *name)
{
    static const char prefix[] = "https://www.google.com/search?q=", hex[] = "0123456789ABCDEF";
    char *url = malloc(sizeof(prefix) + strlen(name) * 3), *out;
    if (!url) return NULL;
    memcpy(url, prefix, sizeof(prefix) - 1);
    out = url + sizeof(prefix) - 1;
    for (const unsigned char *s = (const unsigned char *)name; *s; ++s) {
        if ((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || strchr("-_.~", *s)) *out++ = (char)*s;
        else { *out++ = '%'; *out++ = hex[*s >> 4]; *out++ = hex[*s & 15]; }
    }
    *out = 0;
    return url;
}

/* The status bar line for a window that did not open: a sentence, not the
 * status name ("operation unavailable during callback or polling"). */
static const char *window_failed(char *message, size_t capacity, const char *window, xxwidgets_status status)
{
    if (status == XXWIDGETS_OK) return NULL;
    cdie_translation_snprintf(message, capacity, CDIE_N_("Cannot open %s. %s"), window, gui_error_status_text(status));
    return message;
}

const char *gui_tree_process(xxwidgets_widget *owner)
{
    static char message[512];
    size_t node = g_marks.pending_node;
    int mark = g_marks.pending_mark, pending = g_marks.pending;
    xxwidgets_status status;
    g_marks.pending = PENDING_NONE;
    if (pending == PENDING_SIGNATURES || pending == PENDING_DIRECTORY || pending == PENDING_LOG) {
        const char *window = pending == PENDING_SIGNATURES ? CDIE_TR("Signatures") : pending == PENDING_DIRECTORY ? CDIE_TR("Scan directory") : CDIE_TR("Log");
        if (pending == PENDING_SIGNATURES) status = signatures_window(owner, NULL);
        else if (pending == PENDING_DIRECTORY) status = directory_window(owner);
        else status = xxwidgets_text_dialog(owner, CDIE_TR("Log"), g_marks.log && g_marks.log[0] ? g_marks.log : CDIE_TR("No messages."));
        if (status == XXWIDGETS_UNAVAILABLE && pending == PENDING_SIGNATURES) return CDIE_TR("No signatures loaded.");
        return window_failed(message, sizeof(message), window, status);
    }
    if (pending != PENDING_MARK || mark < 0 || node >= g_marks.rows.count) return NULL;
    if (mark == MARK_SIGNATURE) {
        /* As DIE: the Signatures window, on the script that found the row. */
        status = signatures_window(owner, g_marks.rows.signatures[node]);
        return window_failed(message, sizeof(message), CDIE_TR("Signatures"), status);
    }
    {
        const char *name = g_marks.rows.names[node];
        char *info = gui_backend_info_text(name), *url;
        if (info) {
            status = xxwidgets_text_dialog(owner, name, info);
            gui_backend_free(info);
            return window_failed(message, sizeof(message), name, status);
        }
        url = search_url(name);
        if (!url) return gui_error_status_text(XXWIDGETS_OUT_OF_MEMORY);
        status = xxwidgets_open_url(owner, url);
        /* Without a browser, say where to look instead. */
        if (status == XXWIDGETS_OK) cdie_translation_snprintf(message, sizeof(message), CDIE_N_("No description of %s is bundled; searching the web for it."), name);
        else cdie_translation_snprintf(message, sizeof(message), CDIE_N_("No description of %s is bundled. Search the web: %s"), name, url);
        free(url);
        return message;
    }
}
