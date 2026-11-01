/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_extractor.h"
#include "gui_extractor_source.h"
#include "gui_error.h"
#include "app/cdie_translation.h"
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/threads/xx_threads.h>
#include <xxwidgets/xxwidgets_process.h>

/* Opening detects the type and reads the memory map of the whole file, as
 * Memory Map does: on a worker, behind a progress dialog with Cancel. */
typedef struct extractor_job {
    const char *path;
    gui_extractor_source *source;
    char *error;
    xx_mutex_t *lock;
    xx_pd_struct work, snapshot;
    xxwidgets_status status;
    int stop, done;
} extractor_job;
static bool observe_extractor(const xx_pd_struct *progress, void *user)
{
    extractor_job *job = user;
    int stop;
    xx_mutex_lock(job->lock); job->snapshot = *progress; stop = job->stop; xx_mutex_unlock(job->lock);
    return stop != 0;
}
static int extractor_worker(void *user)
{
    extractor_job *job = user;
    xx_pd_observer previous = xx_pd_set_observer(&job->work, observe_extractor, job);
    job->status = gui_extractor_source_open(job->path, &job->work, &job->source, &job->error);
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(job->lock); job->snapshot = job->work; job->done = 1; xx_mutex_unlock(job->lock);
    return 0;
}
static xxwidgets_status extractor_progress(void *user, int stop_requested, xx_pd_struct *progress, int *finished)
{
    extractor_job *job = user;
    xx_mutex_lock(job->lock);
    if (stop_requested) job->stop = 1;
    *progress = job->snapshot; *finished = job->done;
    xx_mutex_unlock(job->lock);
    return XXWIDGETS_OK;
}
xxwidgets_status gui_extractor_show(xxwidgets_widget *owner, const char *path)
{
    extractor_job job = {0};
    xx_pd_struct progress = xx_pd_init();
    xx_thread_t *thread;
    xxwidgets_status status;
    int joined, stopped;
    if (!owner || !path || !*path) return XXWIDGETS_INVALID_ARGUMENT;
    job.path = path; job.work = xx_pd_init(); job.snapshot = job.work; job.status = XXWIDGETS_PLATFORM_ERROR;
    job.lock = xx_mutex_create();
    if (!job.lock) return XXWIDGETS_OUT_OF_MEMORY;
    thread = xx_thread_create(extractor_worker, &job);
    if (!thread) { xx_mutex_destroy(job.lock); return XXWIDGETS_PLATFORM_ERROR; }
    status = xxwidgets_process_dialog(owner, CDIE_TR("Opening Extractor"), &progress, extractor_progress, &job);
    if (status != XXWIDGETS_OK) {
        xx_mutex_lock(job.lock); job.stop = 1; xx_mutex_unlock(job.lock);
        xx_pd_stop(&job.work);
    }
    joined = xx_thread_join(thread, NULL); xx_mutex_destroy(job.lock);
    stopped = job.stop || job.work.is_stop;
    if (!joined) status = XXWIDGETS_PLATFORM_ERROR;
    else if (status == XXWIDGETS_OK && job.status == XXWIDGETS_OK)
        status = xxwidgets_extractor_dialog(owner, CDIE_TR("Extractor"), gui_extractor_source_model(job.source), 1);
    /* The message is the whole outcome; a cancelled open shows nothing. */
    else if (status == XXWIDGETS_OK && !stopped)
        status = gui_error_show_file(owner, CDIE_TR("Extractor"), CDIE_N_("Cannot open %s in the Extractor."), path, job.error);
    gui_extractor_source_free(job.source);
    xx_rt_free(job.error);
    return status;
}
