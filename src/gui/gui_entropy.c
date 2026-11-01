/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "app/cdie_translation.h"
#include "gui_entropy.h"
#include "gui_error.h"
#include <xxfclib/rt/xx_rt.h>
#include <xxwidgets/xxwidgets_process.h>
#include <xxfclib/threads/xx_threads.h>

/* Keep transport separate from xxwidgets' public model, avoiding layout casts. */
static void statistics(const cdie_entropy_data *data, xxwidgets_entropy_statistics *s,
    xxwidgets_entropy_region *regions, double *values, xxwidgets_entropy_sample *samples)
{
    size_t i;
    *s = (xxwidgets_entropy_statistics){data->size, 0, data->file_type, data->total, data->counts,
        regions, values, data->region_count, samples, data->sample_count};
    for (i = 0; i < data->region_count; ++i) {
        regions[i] = (xxwidgets_entropy_region){data->regions[i].offset, data->regions[i].size, data->regions[i].name};
        values[i] = data->regions[i].entropy;
    }
    for (i = 0; i < data->sample_count; ++i)
        samples[i] = (xxwidgets_entropy_sample){data->samples[i].offset, data->samples[i].size, data->samples[i].entropy};
}
xxwidgets_status gui_entropy_set_data(xxwidgets_entropy *view, const cdie_entropy_data *data)
{
    xxwidgets_entropy_statistics s;
    xxwidgets_entropy_region regions[CDIE_ENTROPY_MAX_REGIONS];
    double values[CDIE_ENTROPY_MAX_REGIONS];
    xxwidgets_entropy_sample samples[CDIE_ENTROPY_MAX_SAMPLES];
    if (!view || !data || data->region_count > CDIE_ENTROPY_MAX_REGIONS || data->sample_count > CDIE_ENTROPY_MAX_SAMPLES)
        return XXWIDGETS_INVALID_ARGUMENT;
    statistics(data, &s, regions, values, samples);
    return xxwidgets_entropy_set_statistics(view, &s);
}
static xxwidgets_status show_entropy(xxwidgets_widget *owner, const cdie_entropy_data *data)
{
    xxwidgets_entropy_statistics s;
    xxwidgets_entropy_region regions[CDIE_ENTROPY_MAX_REGIONS];
    double values[CDIE_ENTROPY_MAX_REGIONS];
    xxwidgets_entropy_sample samples[CDIE_ENTROPY_MAX_SAMPLES];
    statistics(data, &s, regions, values, samples);
    return xxwidgets_entropy_dialog(owner, CDIE_TR("Entropy"), &s);
}
typedef struct entropy_job {
    const char *path;
    cdie_entropy_data *data;
    char *error;
    xx_mutex_t *lock;
    xx_pd_struct work, snapshot;
    int ok, stop, done;
} entropy_job;
static bool observe_entropy(const xx_pd_struct *progress, void *user)
{
    entropy_job *job = user;
    int stop;
    xx_mutex_lock(job->lock); job->snapshot = *progress; stop = job->stop; xx_mutex_unlock(job->lock);
    return stop != 0;
}
static int entropy_worker(void *user)
{
    entropy_job *job = user;
    xx_pd_observer previous = xx_pd_set_observer(&job->work, observe_entropy, job);
    job->ok = cdie_entropy_read(job->path, &job->work, &job->data, &job->error);
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(job->lock); job->snapshot = job->work; job->done = 1; xx_mutex_unlock(job->lock);
    return 0;
}
static xxwidgets_status entropy_progress(void *user, int stop_requested, xx_pd_struct *progress, int *finished)
{
    entropy_job *job = user;
    xx_mutex_lock(job->lock);
    if (stop_requested) job->stop = 1;
    *progress = job->snapshot; *finished = job->done;
    xx_mutex_unlock(job->lock);
    return XXWIDGETS_OK;
}
xxwidgets_status gui_entropy_show(xxwidgets_widget *owner, const char *path)
{
    entropy_job job = {0};
    xx_pd_struct progress = xx_pd_init();
    xx_thread_t *thread;
    xxwidgets_status status;
    int joined;
    if (!owner || !path || !*path) return XXWIDGETS_INVALID_ARGUMENT;
    job.path = path; job.work = xx_pd_init(); job.snapshot = job.work; job.lock = xx_mutex_create();
    if (!job.lock) return XXWIDGETS_OUT_OF_MEMORY;
    thread = xx_thread_create(entropy_worker, &job);
    if (!thread) { xx_mutex_destroy(job.lock); return XXWIDGETS_PLATFORM_ERROR; }
    status = xxwidgets_process_dialog(owner, CDIE_TR("Calculating Entropy"), &progress, entropy_progress, &job);
    if (status != XXWIDGETS_OK) {
        xx_mutex_lock(job.lock); job.stop = 1; xx_mutex_unlock(job.lock);
    }
    joined = xx_thread_join(thread, NULL); xx_mutex_destroy(job.lock);
    if (!joined) status = XXWIDGETS_PLATFORM_ERROR;
    else if (status == XXWIDGETS_OK && job.ok)
        status = show_entropy(owner, job.data);
    else if (status == XXWIDGETS_OK && !job.work.is_stop)
        status = gui_error_show_file(owner, CDIE_TR("Entropy"), CDIE_N_("Cannot calculate the entropy of %s."), path, job.error);
    xx_rt_free(job.data); xx_rt_free(job.error);
    return status;
}
