/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_file_information.h"
#include "gui_backend.h"
#include "gui_error.h"
#include "app/cdie_translation.h"
#include "../app/cdie_file_info.h"
#include <xxwidgets/xxwidgets_process.h>
#include <xxfclib/strings/xx_string.h>
#include <xxfclib/threads/xx_threads.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define GUI_INFO_MAX_BYTES (64U * 1024U * 1024U)

static char *copy_text(gui_file_information_data *data, const char *text)
{
    size_t size = strlen(text ? text : "") + 1;
    char *copy;
    if (size > GUI_INFO_MAX_BYTES - data->bytes) return NULL;
    copy = malloc(size);
    if (copy) { memcpy(copy, text ? text : "", size); data->bytes += size; }
    return copy;
}
static char *copy_wide(gui_file_information_data *data, const wchar_t *text)
{
    char *utf8 = xx_str_unicode_to_utf8(text ? text : L"");
    char *copy = utf8 ? copy_text(data, utf8) : NULL;
    xx_str_free(utf8);
    return copy;
}
static int collect_file(const char *path, const char *type, int64_t size, void *user)
{
    gui_file_information_data *data = user;
    data->path = copy_text(data, path); data->file_type = copy_text(data, type); data->size = size;
    return data->path && data->file_type;
}
static int collect_structure(const char *name, const xx_data_struct *item, void *user)
{
    gui_file_information_data *data = user;
    xxwidgets_data_struct_item *target;
    if (data->count == data->capacity) {
        size_t capacity = data->capacity ? data->capacity * 2 : 32;
        size_t bytes = (capacity - data->capacity) * sizeof(*data->items);
        xxwidgets_data_struct_item *items;
        if (capacity > 65536 || bytes > GUI_INFO_MAX_BYTES - data->bytes) return 0;
        items = realloc(data->items, capacity * sizeof(*items));
        if (!items) return 0;
        memset(items + data->capacity, 0, bytes);
        data->items = items; data->capacity = capacity; data->bytes += bytes;
    }
    target = &data->items[data->count++];
    target->name = copy_text(data, name); target->data_struct = *item;
    data->field_capacity = 0;
    return target->name != NULL;
}
static int collect_field(const xx_data_struct_record *record, void *user)
{
    gui_file_information_data *data = user;
    xxwidgets_data_struct_item *item;
    xxwidgets_data_struct_field *field;
    if (!data->count) return 0;
    item = &data->items[data->count - 1];
    if (item->field_count == data->field_capacity) {
        /* Most DataStructs (unwind codes, relocations) hold a few fields, and a
         * large PE has tens of thousands: reserve little, the cap counts it. */
        size_t capacity = data->field_capacity ? data->field_capacity * 2 : 4;
        size_t bytes = (capacity - data->field_capacity) * sizeof(*field);
        xxwidgets_data_struct_field *fields;
        if (capacity > 262144 || bytes > GUI_INFO_MAX_BYTES - data->bytes) return 0;
        fields = realloc((void *)item->fields, capacity * sizeof(*fields));
        if (!fields) return 0;
        memset(fields + data->field_capacity, 0, bytes);
        item->fields = fields; data->field_capacity = capacity; data->bytes += bytes;
    }
    field = (xxwidgets_data_struct_field *)item->fields + item->field_count++;
    field->relative_offset = record->offset;
    field->file_offset = record->offset >= 0 && item->data_struct.offset >= 0 &&
        item->data_struct.offset <= INT64_MAX - record->offset ? item->data_struct.offset + record->offset : -1;
    field->size = record->size; field->property = record->property;
    field->name = copy_wide(data, record->name); field->type = copy_wide(data, record->type);
    field->raw = copy_wide(data, record->raw_value); field->value = copy_wide(data, record->display_value);
    field->comment = copy_wide(data, record->comment);
    return field->name && field->type && field->raw && field->value && field->comment;
}
static int end_structure(void *user) { (void)user; return 1; }

void gui_file_information_free(gui_file_information_data *data)
{
    size_t i, j;
    if (!data) return;
    for (i = 0; i < data->count; ++i) {
        xxwidgets_data_struct_item *item = data->items + i;
        free((void *)item->name);
        for (j = 0; j < item->field_count; ++j) {
            const xxwidgets_data_struct_field *field = item->fields + j;
            free((void *)field->name); free((void *)field->type); free((void *)field->raw);
            free((void *)field->value); free((void *)field->comment);
        }
        free((void *)item->fields);
    }
    free(data->items); free(data->path); free(data->file_type); free(data);
}

int gui_file_information_read(const char *path, xx_pd_struct *progress, gui_file_information_data **out, char **error)
{
    return gui_file_information_read_slice(path, CDIE_FILE_INFO_SLICE_AUTO, progress, out, error);
}

int gui_file_information_read_slice(const char *path, int slice, xx_pd_struct *progress,
    gui_file_information_data **out, char **error)
{
    static const cdie_file_info_visitor visitor = {collect_file, collect_structure, collect_field, end_structure};
    gui_file_information_data *data;
    if (!out || !error) return 0;
    *out = NULL; *error = NULL;
    data = calloc(1, sizeof(*data));
    if (!data) return 0;
    if (!cdie_file_info_visit_slice(path, slice, NULL, 0, &visitor, data, progress, error)) {
        gui_file_information_free(data); return 0;
    }
    *out = data;
    return 1;
}

typedef struct information_job {
    const char *path;
    gui_file_information_data *data;
    char *error;
    xx_mutex_t *lock;
    xx_pd_struct work, snapshot;
    int slice, ok, stop, done;
} information_job;
static bool observe_information(const xx_pd_struct *progress, void *user)
{
    information_job *job = user;
    int stop;
    xx_mutex_lock(job->lock); job->snapshot = *progress; stop = job->stop; xx_mutex_unlock(job->lock);
    return stop != 0;
}
static int information_worker(void *user)
{
    information_job *job = user;
    xx_pd_observer previous = xx_pd_set_observer(&job->work, observe_information, job);
    job->ok = gui_file_information_read_slice(job->path, job->slice, &job->work, &job->data, &job->error);
    xx_pd_set_observer(previous.progress, previous.callback, previous.user_data);
    xx_mutex_lock(job->lock); job->snapshot = job->work; job->done = 1; xx_mutex_unlock(job->lock);
    return 0;
}
static xxwidgets_status information_progress(void *user, int stop_requested, xx_pd_struct *progress, int *finished)
{
    information_job *job = user;
    xx_mutex_lock(job->lock);
    if (stop_requested) job->stop = 1;
    *progress = job->snapshot; *finished = job->done;
    xx_mutex_unlock(job->lock);
    return XXWIDGETS_OK;
}
xxwidgets_status gui_file_information_show(xxwidgets_widget *owner, const char *path)
{
    return gui_file_information_show_slice(owner, path, CDIE_FILE_INFO_SLICE_AUTO);
}

static xxwidgets_status show_information(xxwidgets_widget *owner, const char *path, int slice, const char *selector);

xxwidgets_status gui_file_information_show_slice(xxwidgets_widget *owner, const char *path, int slice)
{
    return show_information(owner, path, slice, NULL);
}

xxwidgets_status gui_file_information_show_at(xxwidgets_widget *owner, const char *path, const char *selector)
{
    return show_information(owner, path, CDIE_FILE_INFO_SLICE_AUTO, selector);
}

static xxwidgets_status show_information(xxwidgets_widget *owner, const char *path, int slice, const char *selector)
{
    information_job job = {0};
    xx_pd_struct progress = xx_pd_init();
    xx_thread_t *thread;
    xxwidgets_status status;
    int joined;
    if (!owner || !path || !*path || slice < CDIE_FILE_INFO_SLICE_AUTO) return XXWIDGETS_INVALID_ARGUMENT;
    job.path = path; job.slice = slice; job.work = xx_pd_init(); job.snapshot = job.work; job.lock = xx_mutex_create();
    if (!job.lock) return XXWIDGETS_OUT_OF_MEMORY;
    thread = xx_thread_create(information_worker, &job);
    if (!thread) { xx_mutex_destroy(job.lock); return XXWIDGETS_PLATFORM_ERROR; }
    status = xxwidgets_process_dialog(owner, CDIE_TR("Loading File Information"), &progress, information_progress, &job);
    if (status != XXWIDGETS_OK) {
        xx_mutex_lock(job.lock); job.stop = 1; xx_mutex_unlock(job.lock);
    }
    joined = xx_thread_join(thread, NULL); xx_mutex_destroy(job.lock);
    if (!joined) status = XXWIDGETS_PLATFORM_ERROR;
    else if (status == XXWIDGETS_OK && job.ok)
        status = xxwidgets_file_information_dialog_at(owner, CDIE_TR("File Information"), job.data->path, job.data->file_type,
            job.data->size, job.data->items, job.data->count, selector);
    else if (status == XXWIDGETS_OK && !job.work.is_stop)
        status = gui_error_show_file(owner, CDIE_TR("File Information"), CDIE_N_("Cannot read the file information of %s."), path, job.error);
    gui_file_information_free(job.data); gui_backend_free(job.error);
    return status;
}
