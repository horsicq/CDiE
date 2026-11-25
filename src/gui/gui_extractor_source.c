/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_extractor_source.h"
#include "../app/cdie_app.h"
#include "../app/cdie_memory_map.h"
#include <xxfclib/extractor/xx_extractor.h>
#include <xxfclib/formats/xx_format.h>
#include <xxfclib/rt/xx_rt.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#define TEXT_PREFIX 4096U
#define TEXT_FIELDS 3U
#define TRANSFER_CHUNK 65536U

/* Pointer members are never serialized. Text lives in a separate temporary
 * stream, so an arbitrary number of results costs bounded resident memory. */
typedef struct extractor_meta {
    xx_extractor_record record;
    uint64_t number, text_offset[TEXT_FIELDS], text_size[TEXT_FIELDS];
} extractor_meta;
typedef struct extractor_index {
    xx_io_device *records, *text;
    uint64_t count, seen, text_size;
    cdie_memory_map_data *map;
} extractor_index;
struct gui_extractor_source {
    xxwidgets_extractor_source model;
    xx_io_device *input;
    char *path;
    uint64_t size;
    xx_file_type_t detected;
    extractor_index index;
    char format_labels[2][32], map_labels[3][24];
    unsigned char format_mode_masks[2];
    const char *formats[2], *map_modes[3], *types[64];
    xx_file_type_t selected_types[64];
    char text[XXWIDGETS_EXTRACTOR_PAGE_SIZE][TEXT_FIELDS][TEXT_PREFIX + 32];
    char regions[XXWIDGETS_EXTRACTOR_PAGE_SIZE][512];
    char methods[XXWIDGETS_EXTRACTOR_PAGE_SIZE][512];
    char error[256];
};
typedef struct scan_context {
    gui_extractor_source *source;
    extractor_index *index;
    const char *const *columns;
    xx_pd_struct *pd;
    xxwidgets_status status;
} scan_context;

static void error_text(gui_extractor_source *s, const char *text)
{
    if (s) snprintf(s->error, sizeof(s->error), "%s", text ? text : "");
}
const char *gui_extractor_source_error(const gui_extractor_source *s)
{
    return s ? s->error : "Invalid Extractor source";
}
static const char *source_error(void *user)
{
    return gui_extractor_source_error(user);
}
static int stopped(xx_pd_struct *pd)
{
    if (!xx_pd_is_stopped(pd)) return 0;
    xx_pd_stop(pd);
    return 1;
}
static xxwidgets_status map_status(xx_extractor_status status)
{
    return status == XX_EXTRACTOR_OK          ? XXWIDGETS_OK
           : status == XX_EXTRACTOR_CANCELLED ? XXWIDGETS_UNAVAILABLE
           : status == XX_EXTRACTOR_NO_MEMORY ? XXWIDGETS_OUT_OF_MEMORY
           : status == XX_EXTRACTOR_INVALID   ? XXWIDGETS_INVALID_ARGUMENT
                                              : XXWIDGETS_PLATFORM_ERROR;
}
static int write_all(xx_io_device *file, const void *data, size_t size, xx_pd_struct *pd)
{
    const unsigned char *p = data;
    while (size) {
        size_t chunk = size < TRANSFER_CHUNK ? size : TRANSFER_CHUNK;
        ssize_t n;
        if (stopped(pd)) return 0;
        n = xx_io_write(file, p, chunk);
        if (n <= 0 || (size_t)n > chunk) return 0;
        p += n;
        size -= (size_t)n;
    }
    return 1;
}
static void index_free(extractor_index *index)
{
    if (index->records) xx_io_close(index->records);
    if (index->text) xx_io_close(index->text);
    xx_rt_free(index->map);
    memset(index, 0, sizeof(*index));
}
static const xx_memory_record *region_at(const extractor_index *index, uint64_t offset)
{
    const xx_memory_record *best = NULL;
    size_t i;
    for (i = 0; index->map && i < index->map->count; ++i) {
        const xx_memory_record *r = index->map->records + i;
        if (r->is_virtual || r->offset < 0 || r->size <= 0 || offset < (uint64_t)r->offset || offset - (uint64_t)r->offset >= (uint64_t)r->size) continue;
        if (!best || r->size < best->size) best = r;
    }
    return best;
}
static const char *method_name(const xx_extractor_record *record)
{
    return record->compression[0] ? record->compression : "Store";
}
static void hex_value(char out[32], uint64_t value)
{
    snprintf(out, 32, value > UINT32_MAX ? "%016" PRIX64 : "%08" PRIX64, value);
}
static void widget_record(const extractor_index *index, const extractor_meta *m, const char *name, const char *info, xxwidgets_extractor_record *out)
{
    const xx_memory_record *region = region_at(index, m->record.offset);
    memset(out, 0, sizeof(*out));
    out->offset = m->record.offset;
    out->size = m->record.size;
    out->number = m->number;
    out->region = region ? region->name : "";
    out->method = method_name(&m->record);
    out->type = xx_format_file_type_to_string(m->record.file_type);
    out->info = info ? info : "";
    out->name = name ? name : "";
    if (region && region->address != XX_INVALID_ADDRESS && m->record.offset - (uint64_t)region->offset <= UINT64_MAX - region->address) {
        out->address = region->address + m->record.offset - (uint64_t)region->offset;
        out->has_address = 1;
    }
}
/* Filters use full metadata, including a name/info string beyond the visible
 * prefix. Poll while scanning long metadata, rather than only between rows. */
static int contains(const char *text, const char *query, xx_pd_struct *pd)
{
    size_t at, n;
    if (!query || !*query) return 1;
    if (!text) return 0;
    n = strlen(query);
    for (at = 0; text[at]; ++at) {
        size_t i;
        if (!(at & 4095U) && stopped(pd)) return -1;
        for (i = 0; i < n && text[at + i]; ++i) {
            unsigned char a = (unsigned char)text[at + i], b = (unsigned char)query[i];
            if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
            if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
            if (a != b) break;
        }
        if (i == n) return 1;
    }
    return 0;
}
static int matches(scan_context *c, const extractor_meta *meta, const xx_extractor_record *record)
{
    xxwidgets_extractor_record row;
    char number[32], offset[32], address[32] = "", size[32];
    const char *fields[9];
    size_t i;
    widget_record(c->index, meta, record->name, record->info, &row);
    snprintf(number, sizeof(number), "%" PRIu64, row.number);
    hex_value(offset, row.offset);
    hex_value(size, row.size);
    if (row.has_address) hex_value(address, row.address);
    fields[0] = number;
    fields[1] = offset;
    fields[2] = address;
    fields[3] = row.region;
    fields[4] = size;
    fields[5] = row.method;
    fields[6] = row.type;
    fields[7] = row.info;
    fields[8] = row.name;
    for (i = 0; c->columns && i < 9; ++i) {
        int result = contains(fields[i], c->columns[i], c->pd);
        if (result <= 0) return result;
    }
    return 1;
}
static bool collect_record(const xx_extractor_record *record, void *user)
{
    scan_context *c = user;
    extractor_meta meta;
    const char *fields[TEXT_FIELDS];
    size_t i;
    int match;
    memset(&meta, 0, sizeof(meta));
    if (stopped(c->pd)) {
        c->status = XXWIDGETS_UNAVAILABLE;
        return false;
    }
    meta.record = *record;
    meta.record.name = meta.record.info = meta.record.extension = NULL;
    meta.number = c->index->seen++;
    if (record->offset > c->source->size || record->size > c->source->size - record->offset) {
        c->status = XXWIDGETS_PLATFORM_ERROR;
        return false;
    }
    match = matches(c, &meta, record);
    if (match < 0) {
        c->status = XXWIDGETS_UNAVAILABLE;
        return false;
    }
    if (!match) return true;
    fields[0] = record->name;
    fields[1] = record->info;
    fields[2] = record->extension;
    for (i = 0; i < TEXT_FIELDS; ++i) {
        size_t bytes = fields[i] ? strlen(fields[i]) : 0;
        if (bytes > INT64_MAX || c->index->text_size > (uint64_t)INT64_MAX - bytes) goto io_failure;
        meta.text_offset[i] = c->index->text_size;
        meta.text_size[i] = bytes;
        if (!write_all(c->index->text, fields[i], bytes, c->pd)) goto io_failure;
        c->index->text_size += bytes;
    }
    if (c->index->count >= (uint64_t)INT64_MAX / sizeof(meta) || !write_all(c->index->records, &meta, sizeof(meta), c->pd)) goto io_failure;
    ++c->index->count;
    return true;
io_failure:
    c->status = stopped(c->pd) ? XXWIDGETS_UNAVAILABLE : XXWIDGETS_PLATFORM_ERROR;
    return false;
}
static int metadata_at(gui_extractor_source *s, uint64_t row, extractor_meta *meta)
{
    return row < s->index.count && row <= (uint64_t)INT64_MAX / sizeof(*meta) && xx_io_read_at(s->index.records, (int64_t)(row * sizeof(*meta)), meta, sizeof(*meta));
}
/* Validate a complete UTF-8 scalar. Escape a malformed byte or an ASCII
 * control independently, so arbitrary archive/section names remain visible. */
static size_t safe_token(const unsigned char *p, size_t available, char out[4], size_t *bytes)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0, i;
    unsigned char c = p[0];
    if (c >= 32 && c < 127) n = 1;
    else if (c >= 0xc2 && c <= 0xdf) n = 2;
    else if (c >= 0xe0 && c <= 0xef) n = 3;
    else if (c >= 0xf0 && c <= 0xf4) n = 4;
    if (n > available) n = 0;
    for (i = 1; i < n; ++i)
        if ((p[i] & 0xc0) != 0x80) {
            n = 0;
            break;
        }
    if ((n == 3 && ((c == 0xe0 && p[1] < 0xa0) || (c == 0xed && p[1] >= 0xa0))) || (n == 4 && ((c == 0xf0 && p[1] < 0x90) || (c == 0xf4 && p[1] >= 0x90)))) n = 0;
    if (n) {
        memcpy(out, p, n);
        *bytes = n;
        return n;
    }
    out[0] = '\\';
    out[1] = 'x';
    out[2] = hex[c >> 4];
    out[3] = hex[c & 15];
    *bytes = 4;
    return 1;
}
static size_t safe_prefix(char *out, size_t capacity, const unsigned char *input, size_t available, size_t limit)
{
    size_t at = 0, used = 0;
    while (at < available && at < limit) {
        char token[4];
        size_t bytes, consumed = safe_token(input + at, available - at, token, &bytes);
        if (bytes + 1 > capacity - used) break;
        memcpy(out + used, token, bytes);
        used += bytes;
        at += consumed;
    }
    out[used] = 0;
    return at;
}
static int prefix_at(gui_extractor_source *s, const extractor_meta *meta, size_t field, char *out, size_t capacity)
{
    unsigned char input[TEXT_PREFIX + 3];
    size_t n, consumed, used;
    if (!capacity || meta->text_offset[field] > s->index.text_size || meta->text_size[field] > s->index.text_size - meta->text_offset[field]) return 0;
    n = meta->text_size[field] < sizeof(input) ? (size_t)meta->text_size[field] : sizeof(input);
    if (capacity < 32 || (n && !xx_io_read_at(s->index.text, (int64_t)meta->text_offset[field], input, n))) return 0;
    consumed = safe_prefix(out, capacity - 24, input, n, TEXT_PREFIX);
    used = strlen(out);
    if (consumed < meta->text_size[field]) snprintf(out + used, capacity - used, " ... [display prefix]");
    return 1;
}
static xxwidgets_status source_read(void *user, uint64_t first, size_t capacity, xxwidgets_extractor_record *records, size_t *received)
{
    gui_extractor_source *s = user;
    size_t i;
    if (received) *received = 0;
    if (!s || !received || (!records && capacity) || capacity > XXWIDGETS_EXTRACTOR_PAGE_SIZE || first > s->index.count || capacity > s->index.count - first)
        return XXWIDGETS_INVALID_ARGUMENT;
    for (i = 0; i < capacity; ++i) {
        extractor_meta meta;
        if (!metadata_at(s, first + i, &meta) || !prefix_at(s, &meta, 0, s->text[i][0], sizeof(s->text[i][0])) ||
            !prefix_at(s, &meta, 1, s->text[i][1], sizeof(s->text[i][1]))) {
            error_text(s, "Cannot read the Extractor result index");
            return XXWIDGETS_PLATFORM_ERROR;
        }
        widget_record(&s->index, &meta, s->text[i][0], s->text[i][1], records + i);
        safe_prefix(s->regions[i], sizeof(s->regions[i]), (const unsigned char *)records[i].region, strlen(records[i].region), SIZE_MAX);
        records[i].region = s->regions[i];
        safe_prefix(s->methods[i], sizeof(s->methods[i]), (const unsigned char *)records[i].method, strlen(records[i].method), SIZE_MAX);
        records[i].method = s->methods[i];
    }
    *received = capacity;
    return XXWIDGETS_OK;
}
static xxwidgets_status make_map(gui_extractor_source *s, const xxwidgets_extractor_options *options, xx_pd_struct *pd, cdie_memory_map_data **out)
{
    char *error = NULL;
    const char *format = s->formats[options->format], *mode = s->map_modes[options->map_mode];
    int binary = !strcmp(format, "Binary") || !strcmp(mode, "Data");
    if (!cdie_memory_map_read_mode(s->path, binary ? "Binary" : NULL, binary ? "Data" : mode, pd, out, &error)) {
        error_text(s, error ? error : "Cannot build the Extractor memory map");
        xx_rt_free(error);
        return stopped(pd) ? XXWIDGETS_UNAVAILABLE : XXWIDGETS_PLATFORM_ERROR;
    }
    if ((*out)->file_size != s->size) {
        xx_rt_free(*out);
        *out = NULL;
        error_text(s, "The input size changed; reopen Extractor before scanning");
        return XXWIDGETS_PLATFORM_ERROR;
    }
    return XXWIDGETS_OK;
}
static xxwidgets_status source_scan(void *user, xxwidgets_extractor_options *requested, const char *const columns[9], xx_pd_struct *pd, uint64_t *count)
{
    gui_extractor_source *s = user;
    xxwidgets_extractor_options options;
    xx_extractor_options native = xx_extractor_defaults();
    xx_extractor_stats stats;
    xx_file_type_t selected[64];
    extractor_index candidate = {0}, old;
    scan_context context;
    xxwidgets_status status;
    xx_extractor_status result;
    size_t i, selected_count = 0;
    error_text(s, "");
    if (!s || !requested || !count || requested->format >= s->model.format_count || requested->map_mode >= s->model.map_mode_count ||
        requested->mode < XXWIDGETS_EXTRACTOR_HEURISTIC || requested->mode > XXWIDGETS_EXTRACTOR_RAW || (requested->all_types != 0 && requested->all_types != 1) ||
        (requested->deep_scan != 0 && requested->deep_scan != 1) || (s->model.type_count < 64 && (requested->type_mask >> s->model.type_count)))
        return XXWIDGETS_INVALID_ARGUMENT;
    options = *requested;
    if (!strcmp(s->formats[options.format], "Binary")) {
        for (i = 0; i < s->model.map_mode_count; ++i)
            if (!strcmp(s->map_modes[i], "Data")) options.map_mode = i;
    }
    if (stopped(pd)) {
        error_text(s, "Extractor scan cancelled");
        return XXWIDGETS_UNAVAILABLE;
    }
    if (xx_io_total_size(s->input) != (int64_t)s->size) {
        error_text(s, "The input size changed; reopen Extractor before scanning");
        return XXWIDGETS_PLATFORM_ERROR;
    }
    status = make_map(s, &options, pd, &candidate.map);
    if (status != XXWIDGETS_OK) return status;
    candidate.records = xx_io_temp_open();
    candidate.text = xx_io_temp_open();
    if (!candidate.records || !candidate.text) {
        error_text(s, "Cannot create temporary Extractor indexes");
        status = XXWIDGETS_PLATFORM_ERROR;
        goto finish;
    }
    for (i = 0; i < s->model.type_count; ++i)
        if (options.type_mask & (UINT64_C(1) << i)) selected[selected_count++] = s->selected_types[i];
    native.mode = (xx_extractor_mode)options.mode;
    /* Binary is a raw-byte interpretation. An explicit Format request is
     * rejected by the core; Heuristic correctly resolves to Raw. */
    native.file_type = !strcmp(s->formats[options.format], "Binary") ? XX_FILE_TYPE_BINARY : s->detected;
    native.types = selected;
    native.type_count = selected_count;
    native.all_types = options.all_types != 0;
    native.deep_scan = options.deep_scan != 0;
    context = (scan_context){s, &candidate, columns, pd, XXWIDGETS_OK};
    result = xx_extractor_scan_device(s->input, &native, pd, collect_record, &context, &stats);
    status = context.status != XXWIDGETS_OK ? context.status : map_status(result);
    if (status != XXWIDGETS_OK || stopped(pd)) {
        if (stopped(pd)) status = XXWIDGETS_UNAVAILABLE;
        error_text(s, status == XXWIDGETS_UNAVAILABLE ? "Extractor scan cancelled" : xx_extractor_status_name(result));
        goto finish;
    }
    /* Flush both indexes through a seek/read before publication, so late
     * storage errors cannot replace a valid preceding result. */
    if (candidate.count) {
        extractor_meta last;
        if (!xx_io_read_at(candidate.records, (int64_t)((candidate.count - 1) * sizeof(last)), &last, sizeof(last))) {
            error_text(s, "Cannot finalize the Extractor index");
            status = XXWIDGETS_PLATFORM_ERROR;
            goto finish;
        }
    }
    if (stopped(pd)) {
        status = XXWIDGETS_UNAVAILABLE;
        error_text(s, "Extractor scan cancelled");
        goto finish;
    }
    old = s->index;
    s->index = candidate;
    memset(&candidate, 0, sizeof(candidate));
    s->model.count = s->index.count;
    s->model.options = options;
    *requested = options;
    *count = s->index.count;
    index_free(&old);
finish:
    index_free(&candidate);
    return status;
}

/* Reject the input itself, including hard links/alternate spellings, before
 * replacing any chosen destination. Paths are UTF-8 at the native boundary. */
static int same_input(const char *source, const char *destination)
{
#ifdef _WIN32
    wchar_t *a = xx_rt_utf8_to_utf16(source), *b = xx_rt_utf8_to_utf16(destination);
    HANDLE one = INVALID_HANDLE_VALUE, two = INVALID_HANDLE_VALUE;
    BY_HANDLE_FILE_INFORMATION x, y;
    int equal = 0;
    if (a && b) {
        one = CreateFileW(a, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        two = CreateFileW(b, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        if (one != INVALID_HANDLE_VALUE && two != INVALID_HANDLE_VALUE && GetFileInformationByHandle(one, &x) && GetFileInformationByHandle(two, &y))
            equal = x.dwVolumeSerialNumber == y.dwVolumeSerialNumber && x.nFileIndexHigh == y.nFileIndexHigh && x.nFileIndexLow == y.nFileIndexLow;
    }
    if (one != INVALID_HANDLE_VALUE) CloseHandle(one);
    if (two != INVALID_HANDLE_VALUE) CloseHandle(two);
    xx_rt_free(a);
    xx_rt_free(b);
    return equal;
#else
    struct stat a, b;
    return stat(source, &a) == 0 && stat(destination, &b) == 0 && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
#endif
}
typedef struct dump_progress {
    xx_pd_observer previous;
    xxwidgets_disk_write_reporter *reporter;
    uint64_t base, total;
} dump_progress;
/* Chain the managed disk observer instead of replacing its cancellation
 * channel. CRC validation is a read phase; only the core copy level counts
 * bytes written, and its local counter is offset by preceding output files. */
static bool observe_dump(const xx_pd_struct *pd, void *user)
{
    dump_progress *p = user;
    bool stop = p->previous.progress == pd && p->previous.callback ? p->previous.callback(pd, p->previous.user_data) : false;
    size_t i;
    for (i = 0; i < XX_PD_LEVELS; ++i)
        if (pd->records[i].is_busy && !strcmp(pd->records[i].status, "Copying extracted data") && pd->records[i].current <= p->total - p->base) {
            (void)xxwidgets_disk_write_set_bytes(p->reporter, p->base + pd->records[i].current, p->total, 1);
            break;
        }
    return stop;
}
static xxwidgets_status source_dump_ex(void *user, const char *destination, int directory, const uint64_t *indices, size_t count, xx_pd_struct *pd,
                                       xxwidgets_disk_write_reporter *reporter)
{
    gui_extractor_source *s = user;
    uint64_t row, total, total_bytes = 0, written_bytes = 0;
    int level;
    xxwidgets_status status = XXWIDGETS_OK;
    error_text(s, "");
    if (!s || !destination || !*destination || (directory != 0 && directory != 1) || (!directory && (!indices || count != 1)) || (directory && (indices || count)))
        return XXWIDGETS_INVALID_ARGUMENT;
    total = directory ? s->index.count : 1;
    if (reporter) {
        (void)xxwidgets_disk_write_set_file(reporter, destination, 0, total, 1);
        (void)xxwidgets_disk_write_set_bytes(reporter, 0, 0, 0);
        for (row = 0; row < total; ++row) {
            extractor_meta meta;
            if (stopped(pd)) return XXWIDGETS_UNAVAILABLE;
            if (!metadata_at(s, directory ? row : indices[0], &meta) || meta.record.size > UINT64_MAX - total_bytes) {
                error_text(s, "Cannot determine the complete dump size");
                return XXWIDGETS_PLATFORM_ERROR;
            }
            total_bytes += meta.record.size;
        }
        (void)xxwidgets_disk_write_set_bytes(reporter, 0, total_bytes, 1);
    }
    if (directory && !xx_io_create_dirs_a(destination, true)) {
        error_text(s, "Cannot open the Dump-all destination directory");
        return XXWIDGETS_PLATFORM_ERROR;
    }
    level = xx_pd_enter_level(pd, total, "Dumping Extractor results");
    for (row = 0; row < total; ++row) {
        extractor_meta meta;
        xx_extractor_record record;
        uint64_t at = directory ? row : indices[0];
        char name[1024], full_name[1200], member_name[TEXT_PREFIX + 32], extension[TEXT_PREFIX + 32];
        char *path = NULL;
        const char *target = destination;
        xx_extractor_status result;
        size_t n;
        if (stopped(pd)) {
            status = XXWIDGETS_UNAVAILABLE;
            break;
        }
        if (!metadata_at(s, at, &meta)) {
            status = XXWIDGETS_INVALID_ARGUMENT;
            break;
        }
        record = meta.record;
        /* Dump means the displayed physical Offset+Size, matching Qt. The
         * generic core also offers decoded extraction for other consumers. */
        if (record.method != XX_EXTRACTOR_STORE) record.has_crc32 = false;
        record.method = XX_EXTRACTOR_STORE;
        record.unpacked_size = record.size;
        record.container_offset = record.container_size = record.archive_index = 0;
        record.container_type = XX_FILE_TYPE_UNKNOWN;
        if (!prefix_at(s, &meta, 0, member_name, sizeof(member_name)) || !prefix_at(s, &meta, 2, extension, sizeof(extension))) {
            status = XXWIDGETS_PLATFORM_ERROR;
            break;
        }
        record.name = member_name;
        record.extension = extension;
        if (directory) {
            if (!xx_extractor_record_filename(&record, name, sizeof(name))) {
                status = XXWIDGETS_PLATFORM_ERROR;
                break;
            }
            snprintf(full_name, sizeof(full_name), "%016" PRIX64 "_%s", meta.number, name);
            n = strlen(destination);
            if (n > SIZE_MAX - strlen(full_name) - 2 || !(path = xx_rt_malloc(n + strlen(full_name) + 2))) {
                status = XXWIDGETS_OUT_OF_MEMORY;
                break;
            }
            snprintf(path, n + strlen(full_name) + 2, "%s/%s", destination, full_name);
            target = path;
        }
        if (same_input(s->path, target)) {
            error_text(s, "Dump destination must differ from the input file");
            status = XXWIDGETS_INVALID_ARGUMENT;
            xx_rt_free(path);
            break;
        }
        if (reporter) {
            dump_progress tracking = {{0}, reporter, written_bytes, total_bytes};
            (void)xxwidgets_disk_write_set_file(reporter, target, row, total, 1);
            tracking.previous = xx_pd_set_observer(pd, observe_dump, &tracking);
            result = xx_extractor_extract_file(s->input, &record, target, directory == 0, pd);
            xx_pd_set_observer(tracking.previous.progress, tracking.previous.callback, tracking.previous.user_data);
        } else result = xx_extractor_extract_file(s->input, &record, target, directory == 0, pd);
        status = map_status(result);
        if (status != XXWIDGETS_OK) {
            error_text(s, xx_extractor_status_name(result));
            xx_rt_free(path);
            break;
        }
        if (reporter) {
            written_bytes += record.size;
            (void)xxwidgets_disk_write_set_bytes(reporter, written_bytes, total_bytes, 1);
            (void)xxwidgets_disk_write_set_file(reporter, target, row + 1, total, 1);
        }
        xx_rt_free(path);
        xx_pd_set_current(pd, level, row + 1);
    }
    xx_pd_leave_level(pd, level);
    if (status == XXWIDGETS_UNAVAILABLE) xx_pd_stop(pd);
    if (status != XXWIDGETS_OK && !*s->error) error_text(s, status == XXWIDGETS_UNAVAILABLE ? "Extractor dump cancelled" : "Cannot dump Extractor results");
    return status;
}
static xxwidgets_status source_dump(void *user, const char *destination, int directory, const uint64_t *indices, size_t count, xx_pd_struct *pd)
{
    return source_dump_ex(user, destination, directory, indices, count, pd, NULL);
}

typedef struct csv_counter {
    xx_io_device *file;
    xxwidgets_disk_write_reporter *reporter;
    uint64_t written, reported;
} csv_counter;
static ssize_t count_csv_write(xx_io_device *device, const void *data, size_t size)
{
    csv_counter *c = device->priv;
    ssize_t n;
    if (size > UINT64_MAX - c->written) return -1;
    n = xx_io_write(c->file, data, size);
    if (n > 0 && (size_t)n <= size) {
        c->written += (uint64_t)n;
        if (c->reporter && c->written - c->reported >= TRANSFER_CHUNK) {
            (void)xxwidgets_disk_write_set_bytes(c->reporter, c->written, 0, 0);
            c->reported = c->written;
        }
    }
    return n;
}

static int csv_text(xx_io_device *file, const char *text, size_t bytes, xx_pd_struct *pd)
{
    size_t i, at = 0;
    for (i = 0; i < bytes; ++i)
        if (text[i] == '"') {
            if (!write_all(file, text + at, i - at + 1, pd) || !write_all(file, "\"", 1, pd)) return 0;
            at = i + 1;
        }
    return write_all(file, text + at, bytes - at, pd);
}
static int csv_field(xx_io_device *file, const char *text, xx_pd_struct *pd)
{
    size_t at = 0, n = text ? strlen(text) : 0;
    if (!write_all(file, "\"", 1, pd)) return 0;
    while (at < n) {
        char token[4];
        size_t bytes, consumed = safe_token((const unsigned char *)text + at, n - at, token, &bytes);
        if (!csv_text(file, token, bytes, pd)) return 0;
        at += consumed;
    }
    return write_all(file, "\"", 1, pd);
}
static int csv_stored(gui_extractor_source *s, xx_io_device *file, const extractor_meta *meta, size_t field, xx_pd_struct *pd)
{
    unsigned char buffer[TRANSFER_CHUNK + 3];
    char output[TRANSFER_CHUNK];
    uint64_t at = 0, bytes = meta->text_size[field];
    if (!write_all(file, "\"", 1, pd)) return 0;
    while (at < bytes) {
        size_t n = bytes - at < sizeof(buffer) ? (size_t)(bytes - at) : sizeof(buffer), consumed = 0, used = 0;
        if (stopped(pd) || !xx_io_read_at(s->index.text, (int64_t)(meta->text_offset[field] + at), buffer, n)) return 0;
        while (consumed < n && consumed < TRANSFER_CHUNK) {
            char token[4];
            size_t length, step = safe_token(buffer + consumed, n - consumed, token, &length);
            if (used + length > sizeof(output)) {
                if (!csv_text(file, output, used, pd)) return 0;
                used = 0;
            }
            memcpy(output + used, token, length);
            used += length;
            consumed += step;
        }
        if (!csv_text(file, output, used, pd)) return 0;
        at += consumed;
    }
    return write_all(file, "\"", 1, pd);
}
static xxwidgets_status source_save_ex(void *user, const char *path, xx_pd_struct *pd, xxwidgets_disk_write_reporter *reporter)
{
    gui_extractor_source *s = user;
    xx_io_device *file = NULL;
    xx_io_device output = {0};
    csv_counter counter = {0};
    char *temporary = NULL;
    size_t length;
    uint64_t row;
    unsigned attempt;
    int level = -1, created = 0;
    xxwidgets_status status = XXWIDGETS_PLATFORM_ERROR;
    error_text(s, "");
    if (!s || !path || !*path || same_input(s->path, path)) {
        error_text(s, "Save destination must differ from the input file");
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    length = strlen(path);
    if (length > SIZE_MAX - 96) return XXWIDGETS_INVALID_ARGUMENT;
    temporary = xx_rt_malloc(length + 96);
    if (!temporary) return XXWIDGETS_OUT_OF_MEMORY;
    if (reporter) {
        (void)xxwidgets_disk_write_set_file(reporter, path, 0, 1, 1);
        (void)xxwidgets_disk_write_set_bytes(reporter, 0, 0, 0);
    }
    for (attempt = 0; attempt < 16 && !file; ++attempt) {
        snprintf(temporary, length + 96, "%s.cdie-extractor-%llx-%llx-%u.tmp", path, (unsigned long long)xx_rt_clock_ms(), (unsigned long long)(uintptr_t)s, attempt);
        file = xx_io_file_open(temporary, "wbx");
        if (file) created = 1;
    }
    if (!file) goto finish;
    counter.file = file;
    counter.reporter = reporter;
    output.write = count_csv_write;
    output.priv = &counter;
    level = xx_pd_enter_level(pd, s->index.count, "Saving Extractor CSV");
    {
        static const char header[] = "#,Offset,Address,Region,Size,Method,Type,Info,Name\r\n";
        if (!write_all(&output, header, sizeof(header) - 1, pd)) goto finish;
    }
    for (row = 0; row < s->index.count; ++row) {
        extractor_meta meta;
        xxwidgets_extractor_record record;
        char number[32], offset[32], address[32] = "", size[32];
        const char *fields[7];
        size_t i;
        if (stopped(pd)) {
            status = XXWIDGETS_UNAVAILABLE;
            goto finish;
        }
        if (!metadata_at(s, row, &meta)) goto finish;
        widget_record(&s->index, &meta, "", "", &record);
        snprintf(number, sizeof(number), "%" PRIu64, meta.number);
        hex_value(offset, record.offset);
        hex_value(size, record.size);
        if (record.has_address) hex_value(address, record.address);
        fields[0] = number;
        fields[1] = offset;
        fields[2] = address;
        fields[3] = record.region;
        fields[4] = size;
        fields[5] = record.method;
        fields[6] = record.type;
        for (i = 0; i < 7; ++i)
            if ((i && !write_all(&output, ",", 1, pd)) || !csv_field(&output, fields[i], pd)) goto finish;
        if (!write_all(&output, ",", 1, pd) || !csv_stored(s, &output, &meta, 1, pd) || !write_all(&output, ",", 1, pd) || !csv_stored(s, &output, &meta, 0, pd) ||
            !write_all(&output, "\r\n", 2, pd))
            goto finish;
        xx_pd_set_current(pd, level, row + 1);
    }
    xx_pd_leave_level(pd, level);
    level = -1;
    if (stopped(pd)) {
        status = XXWIDGETS_UNAVAILABLE;
        goto finish;
    }
    if (xx_io_close(file) != 0) {
        file = NULL;
        goto finish;
    }
    file = NULL;
    if (stopped(pd)) {
        status = XXWIDGETS_UNAVAILABLE;
        goto finish;
    }
    if (!xx_io_file_replace_a(temporary, path, true)) goto finish;
    status = XXWIDGETS_OK;
    if (reporter) {
        (void)xxwidgets_disk_write_set_bytes(reporter, counter.written, counter.written, 1);
        (void)xxwidgets_disk_write_set_file(reporter, path, 1, 1, 1);
    }
finish:
    xx_pd_leave_level(pd, level);
    if (file) xx_io_close(file);
    if (status != XXWIDGETS_OK && created) (void)xx_io_file_remove_a(temporary);
    xx_rt_free(temporary);
    if (status != XXWIDGETS_OK) {
        if (stopped(pd)) status = XXWIDGETS_UNAVAILABLE;
        error_text(s, status == XXWIDGETS_UNAVAILABLE ? "Saving Extractor CSV cancelled" : "Cannot write or replace Extractor CSV");
    }
    return status;
}
static xxwidgets_status source_save(void *user, const char *path, xx_pd_struct *pd)
{
    return source_save_ex(user, path, pd, NULL);
}

xxwidgets_status gui_extractor_source_open(const char *path, xx_pd_struct *pd, gui_extractor_source **out, char **error)
{
    gui_extractor_source *s;
    cdie_memory_map_data *map = NULL;
    char *map_error = NULL;
    int64_t size;
    size_t i;
    xxwidgets_status status = XXWIDGETS_PLATFORM_ERROR;
    if (!out || !error || !path || !*path) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    *error = NULL;
    s = xx_rt_calloc(1, sizeof(*s));
    if (!s) return XXWIDGETS_OUT_OF_MEMORY;
    s->path = cdie_strdup(path);
    if (!s->path) {
        status = XXWIDGETS_OUT_OF_MEMORY;
        goto fail;
    }
    if (stopped(pd)) {
        status = XXWIDGETS_UNAVAILABLE;
        goto fail;
    }
    s->input = xx_io_file_open(path, "rb");
    size = xx_io_total_size(s->input);
    if (size < 0) {
        error_text(s, "Cannot open Extractor input");
        goto fail;
    }
    s->size = (uint64_t)size;
    s->detected = xx_extractor_detect_device(s->input, pd);
    if (stopped(pd)) {
        status = XXWIDGETS_UNAVAILABLE;
        goto fail;
    }
    if (!cdie_memory_map_read(path, pd, &map, &map_error)) {
        xx_rt_free(map_error);
        map_error = NULL;
        if (stopped(pd)) {
            status = XXWIDGETS_UNAVAILABLE;
            goto fail;
        }
        if (!cdie_memory_map_read_mode(path, "Binary", "Data", pd, &map, &map_error)) {
            error_text(s, map_error ? map_error : "Cannot read Extractor map");
            goto fail;
        }
    }
    if (s->detected == XX_FILE_TYPE_UNKNOWN) s->detected = XX_FILE_TYPE_BINARY;
    s->model.format_count = 1;
    snprintf(s->format_labels[0], sizeof(s->format_labels[0]), "%s", xx_format_file_type_to_string(s->detected));
    if (s->detected != XX_FILE_TYPE_BINARY) {
        snprintf(s->format_labels[1], sizeof(s->format_labels[1]), "Binary");
        s->model.format_count = 2;
    }
    snprintf(s->map_labels[0], sizeof(s->map_labels[0]), "%s", map->mode);
    s->model.map_mode_count = 1;
    if ((map->available_modes & CDIE_MEMORY_MAP_MODE_SECTIONS) && strcmp(map->mode, "Sections"))
        snprintf(s->map_labels[s->model.map_mode_count++], sizeof(s->map_labels[0]), "Sections");
    if ((map->available_modes & CDIE_MEMORY_MAP_MODE_SEGMENTS) && strcmp(map->mode, "Segments"))
        snprintf(s->map_labels[s->model.map_mode_count++], sizeof(s->map_labels[0]), "Segments");
    if (strcmp(map->mode, "Data")) snprintf(s->map_labels[s->model.map_mode_count++], sizeof(s->map_labels[0]), "Data");
    s->index.map = map;
    map = NULL;
    s->model.type_count = xx_extractor_type_count();
    if (s->model.type_count > 64) {
        error_text(s, "Too many Extractor types for the native selector");
        goto fail;
    }
    s->model.options.deep_scan = 1;
    for (i = 0; i < s->model.type_count; ++i) {
        const xx_extractor_type *type = xx_extractor_type_at(i);
        s->types[i] = type->name;
        s->selected_types[i] = type->file_type;
        if (type->enabled_by_default) s->model.options.type_mask |= UINT64_C(1) << i;
    }
    for (i = 0; i < 2; ++i) s->formats[i] = s->format_labels[i];
    for (i = 0; i < s->model.format_count; ++i)
        s->format_mode_masks[i] = (unsigned char)(5U | (strcmp(s->formats[i], "Binary") && xx_extractor_format_available(s->detected) ? 2U : 0U));
    for (i = 0; i < 3; ++i) s->map_modes[i] = s->map_labels[i];
    s->model.formats = s->formats;
    s->model.map_modes = s->map_modes;
    s->model.types = s->types;
    s->model.format_mode_masks = s->format_mode_masks;
    s->model.read = source_read;
    s->model.scan = source_scan;
    s->model.dump = source_dump;
    s->model.save = source_save;
    s->model.error = source_error;
    s->model.user = s;
    s->model.save_ex = source_save_ex;
    s->model.dump_ex = source_dump_ex;
    s->model.device = s->input;
    s->model.size = s->size;
    *out = s;
    xx_rt_free(map_error);
    return XXWIDGETS_OK;
fail:
    xx_rt_free(map);
    xx_rt_free(map_error);
    *error = cdie_strdup(status == XXWIDGETS_UNAVAILABLE ? "Opening Extractor cancelled" : s->error[0] ? s->error : "Cannot initialize Extractor");
    gui_extractor_source_free(s);
    return status;
}
xxwidgets_extractor_source *gui_extractor_source_model(gui_extractor_source *s)
{
    return s ? &s->model : NULL;
}
void gui_extractor_source_free(gui_extractor_source *s)
{
    if (!s) return;
    index_free(&s->index);
    if (s->input) xx_io_close(s->input);
    xx_rt_free(s->path);
    xx_rt_free(s);
}
