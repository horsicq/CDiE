/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "cdie_resources.h"
#include "cdie_app.h"
#include <xxfclib/formats/executable/elf/xx_elf.h>
#include <xxfclib/formats/executable/pe/xx_pe.h>
#include <xxfclib/rt/xx_rt.h>
void cdie_resources_free(cdie_resources_data *data)
{
    if (!data) return;
    for (size_t i = 0; i < data->count; ++i) {
        xx_rt_free(data->records[i].type_name);
        xx_rt_free(data->records[i].name);
        xx_rt_free(data->records[i].language_name);
    }
    xx_rt_free(data->records);
    xx_rt_free(data);
}
static size_t utf8_width(const unsigned char *p, size_t remaining)
{
    size_t n = p[0] >= 0xc2 && p[0] <= 0xdf ? 2 : p[0] >= 0xe0 && p[0] <= 0xef ? 3 : p[0] >= 0xf0 && p[0] <= 0xf4 ? 4 : 0;
    uint32_t value = n ? p[0] & (n == 2 ? 31U : n == 3 ? 15U : 7U) : 0;
    if (!n || n > remaining) return 0;
    for (size_t j = 1; j < n; ++j) {
        if ((p[j] & 0xc0U) != 0x80U) return 0;
        value = (value << 6) | (p[j] & 63U);
    }
    return value < (n == 2 ? 0x80U : n == 3 ? 0x800U : 0x10000U) || value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU) ? 0 : n;
}
static char *copy_text(cdie_resources_data *data, const char *text)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    size_t length = 0, size = 0, pos = 0;
    char *out;
    while (p[length]) {
        if (length == CDIE_RESOURCES_MAX_NAME) return NULL;
        ++length;
    }
    for (size_t i = 0; i < length;) {
        size_t width = p[i] >= 128 ? utf8_width(p + i, length - i) : p[i] >= 32 && p[i] != 127 ? 1 : 0;
        size += width ? width : 4;
        if (size > CDIE_RESOURCES_MAX_NAME) return NULL;
        i += width ? width : 1;
    }
    if (size + 1 > CDIE_RESOURCES_MAX_TEXT - data->text_size) return NULL;
    out = xx_rt_malloc(size + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < length;) {
        size_t width = p[i] >= 128 ? utf8_width(p + i, length - i) : p[i] >= 32 && p[i] != 127 ? 1 : 0;
        if (width) {
            xx_rt_memcpy(out + pos, p + i, width);
            pos += width;
            i += width;
        } else {
            out[pos++] = '\\';
            out[pos++] = 'x';
            out[pos++] = hex[p[i] >> 4];
            out[pos++] = hex[p[i] & 15];
            ++i;
        }
    }
    out[pos] = 0;
    data->text_size += size + 1;
    return out;
}
static int append(cdie_resources_data *data, const xx_resource_record *record)
{
    xx_resource_record copy;
    if (data->count == CDIE_RESOURCES_MAX_RECORDS || record->size < 0 || record->offset < -1 || (record->offset < 0 && record->size > 0) ||
        (record->offset >= 0 && ((uint64_t)record->offset > data->file_size || (uint64_t)record->size > data->file_size - (uint64_t)record->offset)))
        return 0;
    if (data->count == data->capacity) {
        size_t capacity = data->capacity ? data->capacity * 2 : 64;
        xx_resource_record *rows;
        if (capacity > CDIE_RESOURCES_MAX_RECORDS) capacity = CDIE_RESOURCES_MAX_RECORDS;
        rows = xx_rt_realloc(data->records, capacity * sizeof(*rows));
        if (!rows) return 0;
        data->records = rows;
        data->capacity = capacity;
    }
    copy = *record;
    copy.type_name = copy_text(data, record->type_name);
    copy.name = copy_text(data, record->name);
    copy.language_name = copy_text(data, record->language_name);
    if (!copy.type_name || !copy.name || !copy.language_name) {
        xx_rt_free(copy.type_name);
        xx_rt_free(copy.name);
        xx_rt_free(copy.language_name);
        return 0;
    }
    data->records[data->count++] = copy;
    return 1;
}
int cdie_resources_read(const char *path, xx_pd_struct *pd, cdie_resources_data **out, char **error)
{
    xx_io_device *device = NULL;
    cdie_resources_data *data = NULL;
    xx_pe pe;
    xx_elf elf;
    Abstractformat *formats[2], *format = NULL;
    const StreamResources *api = NULL;
    xx_resource_state *state = NULL;
    const xx_resource_record *record;
    const char *message = "Cannot read resources: invalid table, read failure or result limit";
    int initialized = 0, ok = 0, level = -1;
    int64_t size;
    if (!out || !error) return 0;
    *out = NULL;
    *error = NULL;
    if (!path || !*path) {
        message = "Resources requires a target";
        goto done;
    }
    if (xx_pd_is_stopped(pd)) goto done;
    device = xx_io_file_open(path, "rb");
    if (!device) {
        message = "Cannot open file for Resources";
        goto done;
    }
    size = xx_io_total_size(device);
    if (size < 0) goto done;
    xx_pe_init(&pe, device, 0);
    xx_elf_init(&elf, device, 0);
    initialized = 1;
    formats[0] = &pe.format;
    formats[1] = &elf.format;
    for (size_t i = 0; i < 2 && !xx_pd_is_stopped(pd); ++i)
        if (formats[i]->check_is_valid(formats[i], pd)) {
            format = formats[i];
            break;
        }
    if (!format) {
        message = "Resources supports PE and ELF GResource files";
        goto done;
    }
    if (!xx_format_handle_base_info(format, pd)) goto done;
    api = xx_format_get_stream_resources(format);
    if (!api || !api->create_resources_reading || !api->get_current_resource || !api->resource_move_to_next || !api->free_resources_reading) goto done;
    data = xx_rt_calloc(1, sizeof(*data));
    if (!data) {
        message = "Cannot allocate resources";
        goto done;
    }
    data->file_size = (uint64_t)size;
    xx_rt_snprintf(data->file_type, sizeof(data->file_type), "%s", xx_format_file_type_to_string(format->file_type));
    level = xx_pd_enter_level(pd, CDIE_RESOURCES_MAX_RECORDS, "Reading Resources");
    state = api->create_resources_reading(format, pd);
    if (!state) goto done;
    ok = 1;
    while ((record = api->get_current_resource(format, state)) != NULL) {
        if (xx_pd_is_stopped(pd) || !append(data, record)) {
            ok = 0;
            break;
        }
        xx_pd_set_current(pd, level, data->count);
        if (!api->resource_move_to_next(format, state, pd)) break;
    }
    if (state->failed) ok = 0;
done:
    if (state) api->free_resources_reading(format, state);
    xx_pd_leave_level(pd, level);
    if (xx_pd_is_stopped(pd)) {
        xx_pd_stop(pd);
        ok = 0;
        message = "Resources cancelled";
    }
    if (initialized) {
        xx_pe_destroy(&pe);
        xx_elf_destroy(&elf);
    }
    if (device) xx_io_close(device);
    if (ok) *out = data;
    else {
        cdie_resources_free(data);
        *error = cdie_strdup(message);
    }
    return ok;
}
