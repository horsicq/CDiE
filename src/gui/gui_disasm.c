/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "app/cdie_translation.h"
#include "gui_disasm.h"
#include "gui_error.h"
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/threads/xx_threads.h>
#include <xxwidgets/xxwidgets_process.h>

static xxwidgets_disasm_options g_options;
static int g_options_set;
void gui_disasm_get_options(xxwidgets_disasm_options *options)
{
    if (!options) return;
    if (!g_options_set) { xxwidgets_disasm_options_init(&g_options); g_options_set = 1; }
    *options = g_options;
}
void gui_disasm_set_options(const xxwidgets_disasm_options *options)
{
    if (xxwidgets_disasm_options_validate(options) != XXWIDGETS_OK) return;
    g_options = *options;
    g_options_set = 1;
}
static xxwidgets_status load(void *user, uint64_t offset, size_t mode, xxwidgets_disasm_page *out)
{
    gui_disasm_source *s = user;
    const cdie_disasm_page *page;
    cdie_disasm_status status = cdie_disasm_load(s->context, offset, mode, &page);
    size_t i;
    if (status != CDIE_DISASM_OK)
        return status == CDIE_DISASM_UNSUPPORTED ? XXWIDGETS_UNAVAILABLE
               : status == CDIE_DISASM_NO_MEMORY ? XXWIDGETS_OUT_OF_MEMORY
               : status == CDIE_DISASM_INVALID   ? XXWIDGETS_INVALID_ARGUMENT
                                                 : XXWIDGETS_PLATFORM_ERROR;
    for (i = 0; i < page->count; ++i) {
        const cdie_disasm_row *r = page->rows + i;
        s->records[i] = (xxwidgets_disasm_record){r->offset, r->address, r->size, r->bytes, r->instruction, r->comment};
    }
    *out = (xxwidgets_disasm_page){page->file_size, page->offset, page->next_offset, s->records, page->count,
        (mode ? mode : cdie_disasm_get_info(s->context)->detected_mode) >= CDIE_DISASM_ARM};
    return XXWIDGETS_OK;
}
void gui_disasm_source_init(gui_disasm_source *s, cdie_disasm_context *context, const char *path)
{
    const cdie_disasm_info *info = cdie_disasm_get_info(context);
    size_t i, count = cdie_disasm_mode_count();
    xx_rt_memset(s, 0, sizeof(*s));
    s->context = context;
    { xxwidgets_disasm_options options; gui_disasm_get_options(&options); cdie_disasm_set_syntax(context, options.syntax); }
    for (i = 0; i < count; ++i)
        s->names[i] = cdie_disasm_mode_name(i);
    s->source = (xxwidgets_disasm_source){path, info->description, info->file_size, info->initial_offset, s->names, count, 0, load, s};
}
static xxwidgets_status show_disasm(xxwidgets_widget *owner, const char *path, cdie_disasm_context *context)
{
    gui_disasm_source source;
    xxwidgets_disasm_options options;
    gui_disasm_get_options(&options);
    { xxwidgets_status status = xxwidgets_app_set_disasm_options(xxwidgets_widget_app(owner), &options); if (status != XXWIDGETS_OK) return status; }
    gui_disasm_source_init(&source, context, path);
    return xxwidgets_disasm_dialog(owner, CDIE_TR("Disasm"), &source.source);
}
typedef struct disasm_job {
    const char *path;
    cdie_disasm_context *data;
    char *error;
    xx_mutex_t *lock;
    xx_pd_struct work, snapshot;
    int ok, stop, done;
} disasm_job;
static bool observe_disasm(const xx_pd_struct *progress, void *user)
{
    disasm_job *job = user;
    int stop;
    xx_mutex_lock(job->lock);
    job->snapshot = *progress;
    stop = job->stop;
    xx_mutex_unlock(job->lock);
    return stop != 0;
}
static int disasm_worker(void *user)
{
    disasm_job *job = user;
    xx_pd_observer previous = xx_pd_set_observer(&job->work, observe_disasm, job);
    job->ok = cdie_disasm_open(job->path, &job->work, &job->data, &job->error);
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(job->lock);
    job->snapshot = job->work;
    job->done = 1;
    xx_mutex_unlock(job->lock);
    return 0;
}
static xxwidgets_status disasm_progress(void *user, int stop_requested, xx_pd_struct *progress, int *finished)
{
    disasm_job *job = user;
    xx_mutex_lock(job->lock);
    if (stop_requested)
        job->stop = 1;
    *progress = job->snapshot;
    *finished = job->done;
    xx_mutex_unlock(job->lock);
    return XXWIDGETS_OK;
}
xxwidgets_status gui_disasm_show(xxwidgets_widget *owner, const char *path)
{
    disasm_job job = {0};
    xx_pd_struct progress = xx_pd_init();
    xx_thread_t *thread;
    xxwidgets_status status;
    int joined;
    if (!owner || !path || !*path)
        return XXWIDGETS_INVALID_ARGUMENT;
    job.path = path;
    job.work = xx_pd_init();
    job.snapshot = job.work;
    job.lock = xx_mutex_create();
    if (!job.lock)
        return XXWIDGETS_OUT_OF_MEMORY;
    thread = xx_thread_create(disasm_worker, &job);
    if (!thread) {
        xx_mutex_destroy(job.lock);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    status = xxwidgets_process_dialog(owner, CDIE_TR("Opening Disasm"), &progress, disasm_progress, &job);
    if (status != XXWIDGETS_OK) {
        xx_mutex_lock(job.lock);
        job.stop = 1;
        xx_mutex_unlock(job.lock);
    }
    joined = xx_thread_join(thread, NULL);
    xx_mutex_destroy(job.lock);
    if (!joined)
        status = XXWIDGETS_PLATFORM_ERROR;
    else if (status == XXWIDGETS_OK && job.ok)
        status = show_disasm(owner, path, job.data);
    else if (status == XXWIDGETS_OK && !job.work.is_stop)
        status = gui_error_show_file(owner, CDIE_TR("Disasm"), CDIE_N_("Cannot disassemble %s."), path, job.error);
    cdie_disasm_close(job.data);
    xx_rt_free(job.error);
    return status;
}
