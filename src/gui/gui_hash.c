/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "app/cdie_translation.h"
#include "gui_hash.h"
#include "gui_error.h"
#include <xxfclib/rt/xx_rt.h>
#include <xxwidgets/xxwidgets_process.h>
#include <xxfclib/threads/xx_threads.h>

static void entries(const cdie_hash_data *data, xxwidgets_hash_entry *rows)
{
    size_t i;
    for (i = 0; i < CDIE_HASH_COUNT; ++i)
        rows[i] = (xxwidgets_hash_entry){data->entries[i].algorithm, data->entries[i].value};
}
xxwidgets_status gui_hash_set_data(xxwidgets_hash *view, const char *path, const cdie_hash_data *data)
{
    xxwidgets_hash_entry rows[CDIE_HASH_COUNT];
    if (!view || !data || !path) return XXWIDGETS_INVALID_ARGUMENT;
    entries(data, rows);
    return xxwidgets_hash_set_data(view, path, data->size, rows, CDIE_HASH_COUNT);
}
static xxwidgets_status show_hash(xxwidgets_widget *owner, const char *path, const cdie_hash_data *data)
{
    xxwidgets_hash_entry rows[CDIE_HASH_COUNT];
    entries(data, rows);
    return xxwidgets_hash_dialog(owner, CDIE_TR("Hash"), path, data->size, rows, CDIE_HASH_COUNT);
}
typedef struct hash_job {
    const char *path;
    cdie_hash_data *data;
    char *error;
    xx_mutex_t *lock;
    xx_pd_struct work, snapshot;
    int ok, stop, done;
} hash_job;
static bool observe_hash(const xx_pd_struct *progress, void *user)
{
    hash_job *job = user;
    int stop;
    xx_mutex_lock(job->lock); job->snapshot = *progress; stop = job->stop; xx_mutex_unlock(job->lock);
    return stop != 0;
}
static int hash_worker(void *user)
{
    hash_job *job = user;
    xx_pd_observer previous = xx_pd_set_observer(&job->work, observe_hash, job);
    job->ok = cdie_hash_read(job->path, &job->work, &job->data, &job->error);
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(job->lock); job->snapshot = job->work; job->done = 1; xx_mutex_unlock(job->lock);
    return 0;
}
static xxwidgets_status hash_progress(void *user, int stop_requested, xx_pd_struct *progress, int *finished)
{
    hash_job *job = user;
    xx_mutex_lock(job->lock);
    if (stop_requested) job->stop = 1;
    *progress = job->snapshot; *finished = job->done;
    xx_mutex_unlock(job->lock);
    return XXWIDGETS_OK;
}
xxwidgets_status gui_hash_show(xxwidgets_widget *owner, const char *path)
{
    hash_job job = {0};
    xx_pd_struct progress = xx_pd_init();
    xx_thread_t *thread;
    xxwidgets_status status;
    int joined;
    if (!owner || !path || !*path) return XXWIDGETS_INVALID_ARGUMENT;
    job.path = path; job.work = xx_pd_init(); job.snapshot = job.work; job.lock = xx_mutex_create();
    if (!job.lock) return XXWIDGETS_OUT_OF_MEMORY;
    thread = xx_thread_create(hash_worker, &job);
    if (!thread) { xx_mutex_destroy(job.lock); return XXWIDGETS_PLATFORM_ERROR; }
    status = xxwidgets_process_dialog(owner, CDIE_TR("Calculating Hash"), &progress, hash_progress, &job);
    if (status != XXWIDGETS_OK) {
        xx_mutex_lock(job.lock); job.stop = 1; xx_mutex_unlock(job.lock);
    }
    joined = xx_thread_join(thread, NULL); xx_mutex_destroy(job.lock);
    if (!joined) status = XXWIDGETS_PLATFORM_ERROR;
    else if (status == XXWIDGETS_OK && job.ok)
        status = show_hash(owner, path, job.data);
    else if (status == XXWIDGETS_OK && !job.work.is_stop)
        status = gui_error_show_file(owner, CDIE_TR("Hash"), CDIE_N_("Cannot calculate the hashes of %s."), path, job.error);
    xx_rt_free(job.data); xx_rt_free(job.error);
    return status;
}
