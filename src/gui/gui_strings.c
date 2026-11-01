/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "app/cdie_translation.h"
#include "gui_strings.h"
#include "gui_strings_source.h"
#include "gui_error.h"
#include <xxfclib/rt/xx_rt.h>
#include <xxwidgets/xxwidgets_process.h>
#include <xxfclib/threads/xx_threads.h>
#include <stdlib.h>

/* The smallest file region holding offset: a section rather than the segment
 * around it. NULL without a map or outside every region. */
static const xx_memory_record *region_of(const cdie_memory_map_data *map, uint64_t offset)
{
    const xx_memory_record *best = NULL;
    size_t i;
    for (i = 0; map && i < map->count; ++i) {
        const xx_memory_record *r = map->records + i;
        if (r->is_virtual || r->offset < 0 || r->size <= 0 || !r->name[0] || offset < (uint64_t)r->offset || offset - (uint64_t)r->offset >= (uint64_t)r->size) continue;
        if (!best || r->size < best->size) best = r;
    }
    return best;
}
static xxwidgets_string_record *records(const cdie_strings_data *data, const cdie_memory_map_data *map)
{
    size_t i;
    xxwidgets_string_record *rows;
    if (data->count > CDIE_STRINGS_MAX_RECORDS) return NULL;
    rows = calloc(data->count ? data->count : 1, sizeof(*rows));
    if (!rows) return NULL;
    for (i = 0; i < data->count; ++i) {
        const cdie_string_record *r = data->records + i;
        const xx_memory_record *region = region_of(map, r->offset);
        rows[i] = (xxwidgets_string_record){r->offset, r->byte_size, r->character_length, cdie_strings_encoding_name(r->encoding), r->text, NULL, 0, 0};
        if (region) {
            rows[i].region = region->name;
            if (region->address != XX_INVALID_ADDRESS) {
                rows[i].address = region->address + (r->offset - (uint64_t)region->offset);
                rows[i].has_address = 1;
            }
        }
    }
    return rows;
}
xxwidgets_status gui_strings_set_data(xxwidgets_strings *view, const cdie_strings_data *data, const cdie_memory_map_data *map)
{
    xxwidgets_string_record *rows;
    xxwidgets_status status;
    if (!view || !data) return XXWIDGETS_INVALID_ARGUMENT;
    rows = records(data, map);
    if (!rows) return XXWIDGETS_OUT_OF_MEMORY;
    status = xxwidgets_strings_set_records(view, rows, data->count);
    free(rows);
    return status;
}
xxwidgets_status gui_strings_save_csv(const char *path, const char *text, size_t length, void *user)
{
    xx_io_device *device;
    size_t written = 0;
    int ok = 1;
    (void)user;
    if (!path || !text) return XXWIDGETS_INVALID_ARGUMENT;
    device = xx_io_file_open(path, "wb");
    if (!device) return XXWIDGETS_PLATFORM_ERROR;
    while (written < length) {
        size_t chunk = length - written < 65536 ? length - written : 65536;
        ssize_t count = xx_io_write(device, text + written, chunk);
        if (count <= 0 || (size_t)count > chunk) {
            ok = 0;
            break;
        }
        written += (size_t)count;
    }
    if (xx_io_close(device) != 0) ok = 0;
    return ok ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
}
typedef struct strings_job {
    const char *path;
    uint64_t generated_count;
    gui_strings_source *source;
    char *error;
    xx_mutex_t *lock;
    xx_pd_struct work, snapshot;
    int ok, stop, done;
} strings_job;
static bool observe_strings(const xx_pd_struct *progress, void *user)
{
    strings_job *job = user;
    int stop;
    xx_mutex_lock(job->lock);
    job->snapshot = *progress;
    stop = job->stop;
    xx_mutex_unlock(job->lock);
    return stop != 0;
}
static int strings_worker(void *user)
{
    strings_job *job = user;
    xx_pd_observer previous = xx_pd_set_observer(&job->work, observe_strings, job);
    job->ok = (job->generated_count ? gui_strings_source_generated(job->generated_count, &job->work, &job->source, &job->error)
                                    : gui_strings_source_open_options(job->path, NULL, &job->work, &job->source, &job->error)) == XXWIDGETS_OK;
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(job->lock);
    job->snapshot = job->work;
    job->done = 1;
    xx_mutex_unlock(job->lock);
    return 0;
}
static xxwidgets_status strings_progress(void *user, int stop_requested, xx_pd_struct *progress, int *finished)
{
    strings_job *job = user;
    xx_mutex_lock(job->lock);
    if (stop_requested) job->stop = 1;
    *progress = job->snapshot;
    *finished = job->done;
    xx_mutex_unlock(job->lock);
    return XXWIDGETS_OK;
}
static xxwidgets_status strings_show_job(xxwidgets_widget *owner, const char *path, uint64_t generated_count)
{
    strings_job job = {0};
    xx_pd_struct progress = xx_pd_init();
    xx_thread_t *thread;
    xxwidgets_status status;
    int joined;
    if (!owner || !path || !*path) return XXWIDGETS_INVALID_ARGUMENT;
    job.path = path;
    job.generated_count = generated_count;
    job.work = xx_pd_init();
    job.snapshot = job.work;
    job.lock = xx_mutex_create();
    if (!job.lock) return XXWIDGETS_OUT_OF_MEMORY;
    thread = xx_thread_create(strings_worker, &job);
    if (!thread) {
        xx_mutex_destroy(job.lock);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    status = xxwidgets_process_dialog(owner, CDIE_TR("Extracting Strings"), &progress, strings_progress, &job);
    if (status != XXWIDGETS_OK) {
        xx_mutex_lock(job.lock);
        job.stop = 1;
        xx_mutex_unlock(job.lock);
    }
    joined = xx_thread_join(thread, NULL);
    xx_mutex_destroy(job.lock);
    if (!joined) status = XXWIDGETS_PLATFORM_ERROR;
    else if (status == XXWIDGETS_OK && job.ok)
        status = xxwidgets_strings_source_dialog(owner, CDIE_TR("Strings"), path, gui_strings_source_size(job.source), gui_strings_source_model(job.source));
    else if (status == XXWIDGETS_OK && !job.work.is_stop && generated_count)
        status = xxwidgets_text_dialog(owner, CDIE_TR("Strings"), job.error ? job.error : CDIE_TR("Cannot generate the strings."));
    else if (status == XXWIDGETS_OK && !job.work.is_stop)
        status = gui_error_show_file(owner, CDIE_TR("Strings"), CDIE_N_("Cannot extract the strings of %s."), path, job.error);
    gui_strings_source_free(job.source);
    xx_rt_free(job.error);
    return status;
}
xxwidgets_status gui_strings_show(xxwidgets_widget *owner, const char *path)
{
    return strings_show_job(owner, path, 0);
}
xxwidgets_status gui_strings_show_generated(xxwidgets_widget *owner, uint64_t count)
{
    if (!count) return XXWIDGETS_INVALID_ARGUMENT;
    return strings_show_job(owner, "Generated AAAA + NUL strings (5 bytes each)", count);
}
