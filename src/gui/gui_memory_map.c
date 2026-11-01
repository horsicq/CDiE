/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "app/cdie_translation.h"
#include "gui_memory_map.h"
#include "gui_error.h"
#include <xxfclib/io/xx_io.h>
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/threads/xx_threads.h>
#include <xxwidgets/xxwidgets_process.h>

typedef struct memory_map_presentation {
    xxwidgets_memory_map_data data;
    xxwidgets_memory_map_record records[CDIE_MEMORY_MAP_MAX_RECORDS];
    char names[CDIE_MEMORY_MAP_MAX_RECORDS][512], types[CDIE_MEMORY_MAP_MAX_RECORDS][128];
    char format[32], bitness[16], endianness[8], architecture[32], mode[24];
} memory_map_presentation;
/* Executable names are fixed-width bytes, not necessarily UTF-8. Escape all
 * control/non-ASCII bytes without splitting or trusting their encoding. */
static void display_name(char *out, const char *name)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (size_t i = 0; i + 1 < XX_MEMORY_RECORD_NAME_SIZE && name[i]; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c >= 32 && c < 127) out[n++] = (char)c;
        else {
            out[n++] = '\\';
            out[n++] = 'x';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    out[n] = 0;
}
static void part_name(char *out, xx_file_part_t part)
{
    static const struct {
        xx_file_part_t flag;
        const char *name;
    } parts[] = {{XX_FILE_PART_REGION, "Region"},       {XX_FILE_PART_SECTION, "Section"},   {XX_FILE_PART_SEGMENT, "Segment"}, {XX_FILE_PART_HEADER, "Header"},
                 {XX_FILE_PART_OVERLAY, "Overlay"},     {XX_FILE_PART_RESOURCE, "Resource"}, {XX_FILE_PART_DEBUG, "Debug"},     {XX_FILE_PART_STREAM, "Stream"},
                 {XX_FILE_PART_SIGNATURE, "Signature"}, {XX_FILE_PART_FOOTER, "Footer"},     {XX_FILE_PART_DATA, "Data"},       {XX_FILE_PART_OBJECT, "Object"},
                 {XX_FILE_PART_TABLE, "Table"},         {XX_FILE_PART_VALUE, "Value"}};
    size_t n = 0;
    out[0] = 0;
    for (size_t i = 0; i < sizeof(parts) / sizeof(parts[0]); ++i)
        if (part & parts[i].flag) {
            n += (size_t)xx_rt_snprintf(out + n, 128 - n, "%s%s", n ? ", " : "", parts[i].name);
            if (n >= 127) {
                out[127] = 0;
                return;
            }
            part &= ~parts[i].flag;
        }
    if (part || !n) (void)xx_rt_snprintf(out + n, 128 - n, "%s0x%X", n ? ", " : "", (unsigned)part);
}
static memory_map_presentation *presentation(const char *path, const cdie_memory_map_data *data)
{
    memory_map_presentation *p;
    if (!path || !data || data->count > CDIE_MEMORY_MAP_MAX_RECORDS) return NULL;
    p = xx_rt_calloc(1, sizeof(*p));
    if (!p) return NULL;
    xx_rt_snprintf(p->format, sizeof(p->format), "%s", data->file_type);
    xx_rt_snprintf(p->bitness, sizeof(p->bitness), "%s", data->bitness);
    xx_rt_snprintf(p->endianness, sizeof(p->endianness), "%s", data->endianness);
    xx_rt_snprintf(p->architecture, sizeof(p->architecture), "%s", data->architecture);
    xx_rt_snprintf(p->mode, sizeof(p->mode), "%s", data->mode);
    for (size_t i = 0; i < data->count; ++i) {
        const xx_memory_record *r = data->records + i;
        display_name(p->names[i], r->name);
        part_name(p->types[i], r->file_part);
        p->records[i] = (xxwidgets_memory_map_record){
            r->offset,   r->address,  (uint64_t)r->size,
            p->names[i], p->types[i], (r->is_virtual ? XXWIDGETS_MEMORY_MAP_VIRTUAL : 0) | (r->is_invisible ? XXWIDGETS_MEMORY_MAP_INVISIBLE : 0)};
    }
    p->data =
        (xxwidgets_memory_map_data){path,          p->format,       data->file_size, data->module_address, data->entry_point_address, p->records, data->count, p->bitness,
                                    p->endianness, p->architecture, p->mode,         data->available_modes};
    return p;
}
xxwidgets_status gui_memory_map_set_data(xxwidgets_memory_map *view, const char *path, const cdie_memory_map_data *data)
{
    memory_map_presentation *p;
    xxwidgets_status status;
    if (!view || !path || !data || data->count > CDIE_MEMORY_MAP_MAX_RECORDS) return XXWIDGETS_INVALID_ARGUMENT;
    p = presentation(path, data);
    if (!p) return XXWIDGETS_OUT_OF_MEMORY;
    status = xxwidgets_memory_map_set_data(view, &p->data);
    xx_rt_free(p);
    return status;
}
typedef struct memory_map_session {
    const char *path;
    memory_map_presentation *current;
    xx_io_device *device;
} memory_map_session;

/* A reload keeps the previous snapshot/device alive until both replacements
 * have been accepted. Presentations own row names and display metadata, so no
 * worker result or parser state is retained by the modal or its callbacks. */
static xxwidgets_status reload_memory_map(xxwidgets_memory_map *view, const char *format, const char *mode, void *user)
{
    memory_map_session *session = user;
    cdie_memory_map_data *data = NULL;
    memory_map_presentation *next;
    xx_io_device *device;
    xxwidgets_status status;
    char *error = NULL;
    int64_t size;
    if (!view || !session || !session->current || !session->device) return XXWIDGETS_INVALID_ARGUMENT;
    if (!cdie_memory_map_read_mode(session->path, format, mode, NULL, &data, &error)) {
        if (error) (void)xxwidgets_widget_set_text(xxwidgets_memory_map_control(view, XXWIDGETS_MEMORY_MAP_STATUS), error);
        xx_rt_free(error);
        return XXWIDGETS_UNAVAILABLE;
    }
    device = xx_io_file_open(session->path, "rb");
    size = device ? xx_io_total_size(device) : -1;
    if (size < 0 || (uint64_t)size != data->file_size) {
        xx_rt_free(data);
        if (device) xx_io_close(device);
        return XXWIDGETS_UNAVAILABLE;
    }
    next = presentation(session->path, data);
    xx_rt_free(data);
    if (!next) {
        xx_io_close(device);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    status = xxwidgets_memory_map_set_data_device(view, &next->data, device);
    if (status == XXWIDGETS_OK) {
        xx_io_close(session->device);
        xx_rt_free(session->current);
        session->device = device;
        session->current = next;
    } else {
        xx_io_close(device);
        xx_rt_free(next);
    }
    return status;
}

static xxwidgets_status show_memory_map(xxwidgets_widget *owner, const char *path, const cdie_memory_map_data *data)
{
    memory_map_session session = {0};
    xxwidgets_status status;
    int64_t size;
    session.path = path;
    session.current = presentation(path, data);
    if (!session.current) return XXWIDGETS_OUT_OF_MEMORY;
    session.device = xx_io_file_open(path, "rb");
    size = session.device ? xx_io_total_size(session.device) : -1;
    if (size < 0) status = gui_error_show_file(owner, CDIE_TR("Memory Map"), CDIE_N_("Cannot open %s for the Memory Map."), path, NULL);
    else if ((uint64_t)size != data->file_size)
        status = gui_error_show_file(owner, CDIE_TR("Memory Map"), CDIE_N_("%s changed while its Memory Map was read. Open the Memory Map again."), path, NULL);
    else status = xxwidgets_memory_map_dialog_with_device(owner, CDIE_TR("Memory Map"), &session.current->data, session.device, reload_memory_map, &session);
    /* dialog_with_device destroys the view before returning; its borrowed
     * device remains valid throughout preview reads and modal teardown. */
    if (session.device) xx_io_close(session.device);
    xx_rt_free(session.current);
    return status;
}
typedef struct memory_map_job {
    const char *path;
    cdie_memory_map_data *data;
    char *error;
    xx_mutex_t *lock;
    xx_pd_struct work, snapshot;
    int ok, stop, done;
} memory_map_job;
static bool observe_memory_map(const xx_pd_struct *progress, void *user)
{
    memory_map_job *job = user;
    int stop;
    xx_mutex_lock(job->lock);
    job->snapshot = *progress;
    stop = job->stop;
    xx_mutex_unlock(job->lock);
    return stop != 0;
}
static int memory_map_worker(void *user)
{
    memory_map_job *job = user;
    xx_pd_observer previous = xx_pd_set_observer(&job->work, observe_memory_map, job);
    job->ok = cdie_memory_map_read(job->path, &job->work, &job->data, &job->error);
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(job->lock);
    job->snapshot = job->work;
    job->done = 1;
    xx_mutex_unlock(job->lock);
    return 0;
}
static xxwidgets_status memory_map_progress(void *user, int stop_requested, xx_pd_struct *progress, int *finished)
{
    memory_map_job *job = user;
    xx_mutex_lock(job->lock);
    if (stop_requested) job->stop = 1;
    *progress = job->snapshot;
    *finished = job->done;
    xx_mutex_unlock(job->lock);
    return XXWIDGETS_OK;
}
xxwidgets_status gui_memory_map_show(xxwidgets_widget *owner, const char *path)
{
    memory_map_job job = {0};
    xx_pd_struct progress = xx_pd_init();
    xx_thread_t *thread;
    xxwidgets_status status;
    int joined;
    if (!owner || !path || !*path) return XXWIDGETS_INVALID_ARGUMENT;
    job.path = path;
    job.work = xx_pd_init();
    job.snapshot = job.work;
    job.lock = xx_mutex_create();
    if (!job.lock) return XXWIDGETS_OUT_OF_MEMORY;
    thread = xx_thread_create(memory_map_worker, &job);
    if (!thread) {
        xx_mutex_destroy(job.lock);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    status = xxwidgets_process_dialog(owner, CDIE_TR("Reading Memory Map"), &progress, memory_map_progress, &job);
    if (status != XXWIDGETS_OK) {
        xx_mutex_lock(job.lock);
        job.stop = 1;
        xx_mutex_unlock(job.lock);
    }
    joined = xx_thread_join(thread, NULL);
    xx_mutex_destroy(job.lock);
    if (!joined) status = XXWIDGETS_PLATFORM_ERROR;
    else if (status == XXWIDGETS_OK && job.ok) status = show_memory_map(owner, path, job.data);
    else if (status == XXWIDGETS_OK && !job.work.is_stop)
        status = gui_error_show_file(owner, CDIE_TR("Memory Map"), CDIE_N_("Cannot read the Memory Map of %s."), path, job.error);
    xx_rt_free(job.data);
    xx_rt_free(job.error);
    return status;
}
