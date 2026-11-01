/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "app/cdie_translation.h"
#include "gui_resources.h"
#include "gui_error.h"
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/threads/xx_threads.h>
#include <xxwidgets/xxwidgets_process.h>
xxwidgets_status gui_resources_set_data(xxwidgets_resources *view, const char *path, const cdie_resources_data *data)
{
    xxwidgets_resources_data snapshot;
    if (!view || !path || !data)
        return XXWIDGETS_INVALID_ARGUMENT;
    snapshot = (xxwidgets_resources_data){path, data->file_type, data->file_size, data->records, data->count};
    return xxwidgets_resources_set_data(view, &snapshot);
}
static xxwidgets_status show_resources(xxwidgets_widget *owner, const char *path, const cdie_resources_data *data)
{
    xxwidgets_resources_data snapshot = {path, data->file_type, data->file_size, data->records, data->count};
    return xxwidgets_resources_dialog(owner, CDIE_TR("Resources"), &snapshot);
}
typedef struct resources_job {
    const char *path;
    cdie_resources_data *data;
    char *error;
    xx_mutex_t *lock;
    xx_pd_struct work, snapshot;
    int ok, stop, done;
} resources_job;
static bool observe_resources(const xx_pd_struct *progress, void *user)
{
    resources_job *job = user;
    int stop;
    xx_mutex_lock(job->lock);
    job->snapshot = *progress;
    stop = job->stop;
    xx_mutex_unlock(job->lock);
    return stop != 0;
}
static int resources_worker(void *user)
{
    resources_job *job = user;
    xx_pd_observer previous = xx_pd_set_observer(&job->work, observe_resources, job);
    job->ok = cdie_resources_read(job->path, &job->work, &job->data, &job->error);
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(job->lock);
    job->snapshot = job->work;
    job->done = 1;
    xx_mutex_unlock(job->lock);
    return 0;
}
static xxwidgets_status resources_progress(void *user, int stop_requested, xx_pd_struct *progress, int *finished)
{
    resources_job *job = user;
    xx_mutex_lock(job->lock);
    if (stop_requested)
        job->stop = 1;
    *progress = job->snapshot;
    *finished = job->done;
    xx_mutex_unlock(job->lock);
    return XXWIDGETS_OK;
}
xxwidgets_status gui_resources_show(xxwidgets_widget *owner, const char *path)
{
    resources_job job = {0};
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
    thread = xx_thread_create(resources_worker, &job);
    if (!thread) {
        xx_mutex_destroy(job.lock);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    status = xxwidgets_process_dialog(owner, CDIE_TR("Reading Resources"), &progress, resources_progress, &job);
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
        status = show_resources(owner, path, job.data);
    else if (status == XXWIDGETS_OK && !job.work.is_stop)
        status = gui_error_show_file(owner, CDIE_TR("Resources"), CDIE_N_("Cannot read the resources of %s."), path, job.error);
    cdie_resources_free(job.data);
    xx_rt_free(job.error);
    return status;
}
