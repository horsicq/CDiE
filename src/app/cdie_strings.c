/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "cdie_strings.h"
#include "cdie_app.h"
#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/terminal/xx_terminal.h>
#include <xxfclib/strings/xx_strings_scan.h>

#define STRINGS_MAX_OUTPUT (32U * 1024U * 1024U)
cdie_strings_options cdie_strings_defaults(void)
{
    cdie_strings_options o = {4, CDIE_STRINGS_ALL};
    return o;
}
const char *cdie_strings_encoding_name(cdie_strings_encoding encoding)
{
    return encoding == CDIE_STRINGS_ASCII ? "ASCII" : encoding == CDIE_STRINGS_UTF8 ? "UTF-8" : encoding == CDIE_STRINGS_UTF16LE ? "UTF-16LE" : "UTF-16BE";
}
unsigned cdie_strings_parse_encoding(const char *name)
{
    if (!name) return 0;
    if (!xx_rt_strcmp(name, "ascii")) return CDIE_STRINGS_ASCII;
    if (!xx_rt_strcmp(name, "utf8")) return CDIE_STRINGS_UTF8;
    if (!xx_rt_strcmp(name, "utf16le")) return CDIE_STRINGS_UTF16LE;
    if (!xx_rt_strcmp(name, "utf16be")) return CDIE_STRINGS_UTF16BE;
    if (!xx_rt_strcmp(name, "all")) return CDIE_STRINGS_ALL;
    return 0;
}
int cdie_strings_options_valid(const cdie_strings_options *o)
{
    return o && o->minimum_length >= 1 && o->minimum_length <= 1024 && o->encodings && !(o->encodings & ~15U) && (o->encodings & 3) != 3;
}
void cdie_strings_free(cdie_strings_data *data)
{
    size_t i;
    if (!data) return;
    for (i = 0; i < data->count; ++i) xx_rt_free(data->records[i].text);
    xx_rt_free(data->records);
    xx_rt_free(data);
}
/* The command-line report retains its explicit output limits. Large GUI
 * sources use the same scanner with disk-backed metadata instead. */
typedef struct strings_collect {
    cdie_strings_data *data;
    const char *error;
} strings_collect;
static int stopped(xx_pd_struct *pd)
{
    if (!xx_pd_is_stopped(pd)) return 0;
    xx_pd_stop(pd);
    return 1;
}
static bool collect_string(const xx_strings_scan_record *input, void *user)
{
    strings_collect *collect = user;
    cdie_strings_data *data = collect->data;
    cdie_string_record *record;
    if ((input->flags & XX_STRINGS_TEXT_TRUNCATED) || data->count == CDIE_STRINGS_MAX_RECORDS || input->text_size + 1 > CDIE_STRINGS_MAX_TEXT - data->text_bytes) {
        collect->error = "Strings result limit exceeded";
        return false;
    }
    if (data->count == data->capacity) {
        size_t capacity = data->capacity ? data->capacity * 2 : 64;
        void *records;
        if (capacity > CDIE_STRINGS_MAX_RECORDS) capacity = CDIE_STRINGS_MAX_RECORDS;
        records = xx_rt_realloc(data->records, capacity * sizeof(*data->records));
        if (!records) {
            collect->error = "Cannot allocate strings";
            return false;
        }
        data->records = records;
        data->capacity = capacity;
    }
    record = data->records + data->count;
    record->text = cdie_strdup(input->text ? input->text : "");
    if (!record->text) {
        collect->error = "Cannot allocate strings";
        return false;
    }
    record->offset = input->offset;
    record->byte_size = input->byte_size;
    record->character_length = input->character_length;
    record->encoding = (cdie_strings_encoding)input->encoding;
    data->text_bytes += input->text_size + 1;
    ++data->count;
    return true;
}
static int compare_records(const void *left, const void *right)
{
    const cdie_string_record *a = left, *b = right;
    if (a->offset != b->offset) return a->offset < b->offset ? -1 : 1;
    return (int)a->encoding - (int)b->encoding;
}
int cdie_strings_read_device(xx_io_device *device, const cdie_strings_options *options, xx_pd_struct *pd, cdie_strings_data **out, char **error)
{
    cdie_strings_options selected = options ? *options : cdie_strings_defaults();
    xx_strings_scan_options scan_options = xx_strings_scan_defaults();
    xx_strings_scan_status status;
    strings_collect collect = {NULL, NULL};
    int64_t size;
    if (!out || !error) return 0;
    *out = NULL;
    *error = NULL;
    if (!cdie_strings_options_valid(&selected)) {
        *error = cdie_strdup("Invalid strings options");
        return 0;
    }
    if (stopped(pd)) {
        *error = cdie_strdup("Strings extraction cancelled");
        return 0;
    }
    if (!device || (size = xx_io_total_size(device)) < 0) {
        *error = cdie_strdup("Cannot determine file size");
        return 0;
    }
    collect.data = xx_rt_calloc(1, sizeof(*collect.data));
    if (!collect.data) {
        *error = cdie_strdup("Cannot allocate strings");
        return 0;
    }
    collect.data->size = (uint64_t)size;
    scan_options.minimum_length = selected.minimum_length;
    scan_options.encodings = selected.encodings;
    scan_options.max_text_bytes = CDIE_STRINGS_MAX_STRING;
    status = xx_strings_scan_device(device, &scan_options, pd, collect_string, &collect, NULL);
    if (status == XX_STRINGS_SCAN_OK) {
        if (collect.data->count > 1) xx_rt_qsort(collect.data->records, collect.data->count, sizeof(*collect.data->records), compare_records);
        *out = collect.data;
        return 1;
    }
    *error = cdie_strdup(collect.error ? collect.error : xx_strings_scan_status_name(status));
    cdie_strings_free(collect.data);
    return 0;
}
int cdie_strings_read(const char *path, const cdie_strings_options *options, xx_pd_struct *pd, cdie_strings_data **out, char **error)
{
    xx_io_device *device;
    int ok;
    if (!out || !error) return 0;
    *out = NULL;
    *error = NULL;
    if (stopped(pd)) {
        *error = cdie_strdup("Strings extraction cancelled");
        return 0;
    }
    device = path && *path ? xx_io_file_open(path, "rb") : NULL;
    if (!device) {
        *error = cdie_strdup("Cannot open file");
        return 0;
    }
    ok = cdie_strings_read_device(device, options, pd, out, error);
    xx_io_close(device);
    return ok;
}
static xxfc_status_t sink(void *user, const char *text, size_t size)
{
    xx_buf_t *buffer = user;
    if (size > STRINGS_MAX_OUTPUT || buffer->size > STRINGS_MAX_OUTPUT - size) {
        buffer->failed = true;
        return XXFC_ERR_OUT_OF_BOUNDS;
    }
    return xx_buf_append(buffer, text, size) ? XXFC_OK : XXFC_ERR_OUT_OF_MEMORY;
}
static void escaped(xx_buf_t *buffer, const char *text, int json)
{
    xx_terminal_writer writer;
    xx_terminal_writer_init(&writer, sink, buffer);
    if (json) xx_terminal_writer_json_string(&writer, text);
    else xx_terminal_writer_xml_attribute(&writer, text);
    if (xx_terminal_writer_status(&writer) != XXFC_OK) buffer->failed = true;
}
static void cell(xx_buf_t *buffer, const char *text)
{
    xx_buf_append_char(buffer, '"');
    for (; *text && xx_buf_ok(buffer); ++text) {
        if (buffer->size > STRINGS_MAX_OUTPUT) {
            buffer->failed = true;
            break;
        }
        if (*text == '"') xx_buf_append_char(buffer, '"');
        xx_buf_append_char(buffer, *text);
    }
    xx_buf_append_char(buffer, '"');
}
int cdie_strings_report(const char *path, const cdie_strings_options *options, cdie_output_format format, char **text, char **error)
{
    cdie_strings_data *data;
    xx_buf_t buffer;
    size_t i;
    if (!text || !error) return 0;
    *text = NULL;
    *error = NULL;
    if (!cdie_strings_read(path, options, NULL, &data, error)) return 0;
    xx_buf_init(&buffer);
    if (format == CDIE_OUTPUT_JSON) {
        xx_buf_append_str(&buffer, "{\"path\":");
        escaped(&buffer, path, 1);
        xx_buf_appendf(&buffer, ",\"size\":%llu,\"strings\":[", (unsigned long long)data->size);
    } else if (format == CDIE_OUTPUT_XML) {
        xx_buf_append_str(&buffer, "<file-strings path=\"");
        escaped(&buffer, path, 0);
        xx_buf_appendf(&buffer, "\" size=\"%llu\">\n", (unsigned long long)data->size);
    } else if (format == CDIE_OUTPUT_CSV || format == CDIE_OUTPUT_TSV) {
        char sep = format == CDIE_OUTPUT_CSV ? ';' : '\t';
        xx_buf_appendf(&buffer, "Path%cOffset%cBytes%cCharacters%cEncoding%cString\n", sep, sep, sep, sep, sep);
    } else xx_buf_appendf(&buffer, "Strings: %s\n", path);
    for (i = 0; i < data->count && xx_buf_ok(&buffer); ++i) {
        const cdie_string_record *r = data->records + i;
        const char *name = cdie_strings_encoding_name(r->encoding);
        if (format == CDIE_OUTPUT_JSON) {
            xx_buf_appendf(&buffer, "%s{\"offset\":%llu,\"byte_size\":%llu,\"character_length\":%llu,\"encoding\":\"%s\",\"text\":", i ? "," : "",
                           (unsigned long long)r->offset, (unsigned long long)r->byte_size, (unsigned long long)r->character_length, name);
            escaped(&buffer, r->text, 1);
            xx_buf_append_char(&buffer, '}');
        } else if (format == CDIE_OUTPUT_XML) {
            xx_buf_appendf(&buffer, "  <string offset=\"%llu\" byte_size=\"%llu\" character_length=\"%llu\" encoding=\"%s\">", (unsigned long long)r->offset,
                           (unsigned long long)r->byte_size, (unsigned long long)r->character_length, name);
            escaped(&buffer, r->text, 0);
            xx_buf_append_str(&buffer, "</string>\n");
        } else if (format == CDIE_OUTPUT_CSV || format == CDIE_OUTPUT_TSV) {
            char sep = format == CDIE_OUTPUT_CSV ? ';' : '\t';
            cell(&buffer, path);
            xx_buf_appendf(&buffer, "%c%llu%c%llu%c%llu%c%s%c", sep, (unsigned long long)r->offset, sep, (unsigned long long)r->byte_size, sep,
                           (unsigned long long)r->character_length, sep, name, sep);
            cell(&buffer, r->text);
            xx_buf_append_char(&buffer, '\n');
        } else
            xx_buf_appendf(&buffer, "0x%016llx [%s, %llu chars, %llu bytes] %s\n", (unsigned long long)r->offset, name, (unsigned long long)r->character_length,
                           (unsigned long long)r->byte_size, r->text);
        if (buffer.size > STRINGS_MAX_OUTPUT) buffer.failed = true;
    }
    if (format == CDIE_OUTPUT_JSON) xx_buf_append_str(&buffer, "]}\n");
    else if (format == CDIE_OUTPUT_XML) xx_buf_append_str(&buffer, "</file-strings>\n");
    *text = xx_buf_detach(&buffer, NULL);
    xx_buf_free(&buffer);
    cdie_strings_free(data);
    if (!*text) *error = cdie_strdup("Strings report limit or allocation failure");
    return *text != NULL;
}
