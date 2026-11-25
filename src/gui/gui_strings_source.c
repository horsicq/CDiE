/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_strings_source.h"
#include "../app/cdie_memory_map.h"
#include "../app/cdie_strings.h"
#include "../app/cdie_app.h"
#include <xxfclib/strings/xx_strings_scan.h>
#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/regex/xx_regex.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define INDEX_BATCH 512
#define PAGE_TEXT_PREFIX 4096
#define SOURCE_CACHE 65536
typedef struct string_meta {
    uint64_t offset, bytes, characters;
    uint32_t encoding, reserved;
} string_meta;
typedef struct match_range {
    uint64_t first, length, end_count;
} match_range;
typedef struct index_bucket {
    xx_io_device *file;
    string_meta entries[INDEX_BATCH];
    uint64_t total, read;
    size_t used, next;
} index_bucket;
struct gui_strings_source {
    xxwidgets_strings_source model;
    xx_io_device *input, *index, *ranges;
    uint64_t size, range_count, max_bytes;
    int generated, filtered;
    cdie_memory_map_data *map;
    xx_buf_t text[XXWIDGETS_STRINGS_PAGE_SIZE];
    char regions[XXWIDGETS_STRINGS_PAGE_SIZE][512];
    unsigned char cache[SOURCE_CACHE];
    uint64_t cache_offset;
    size_t cache_size;
    string_meta metadata_cache[INDEX_BATCH];
    uint64_t metadata_first;
    size_t metadata_count;
    match_range match_cache;
    int match_cached;
    char *path;
    uint64_t generated_count;
    int configured;
    char format_labels[2][32], map_labels[3][24];
    const char *formats[2], *map_modes[3];
    char error[256];
    uint64_t overlay_offset;
    int has_overlay;
};
static xxwidgets_status source_read(void *, uint64_t, size_t, xxwidgets_string_record *, uint64_t *, size_t *);
static xxwidgets_status source_search(void *, const char *, const char *const[XXWIDGETS_STRINGS_COLUMN_COUNT], xx_pd_struct *, uint64_t *);
static xxwidgets_status source_save(void *, const char *, xx_pd_struct *);
static xxwidgets_status source_save_ex(void *, const char *, xx_pd_struct *, xxwidgets_disk_write_reporter *);
static xxwidgets_status source_search_ex(void *, xxwidgets_strings_search_options *, const char *, const char *const[7], xx_pd_struct *, uint64_t *);
static void repair_model(gui_strings_source *s)
{
    size_t i;
    s->model.user = s;
    s->model.formats = s->formats;
    s->model.map_modes = s->map_modes;
    for (i = 0; i < 2; ++i) s->formats[i] = s->format_labels[i];
    for (i = 0; i < 3; ++i) s->map_modes[i] = s->map_labels[i];
}
static void source_error(gui_strings_source *s, const char *text)
{
    if (s) snprintf(s->error, sizeof(s->error), "%s", text ? text : "");
}
static const char *source_error_text(void *user)
{
    return gui_strings_source_error(user);
}
static int valid_options(const xxwidgets_strings_search_options *o)
{
    return o && o->minimum_length >= 2 && (o->ansi || o->unicode) && (o->ansi == 0 || o->ansi == 1) && (o->unicode == 0 || o->unicode == 1) &&
           (o->null_terminated == 0 || o->null_terminated == 1) && (o->links == 0 || o->links == 1) && (o->regexp == 0 || o->regexp == 1) &&
           memchr(o->expression, 0, sizeof(o->expression));
}
static int write_all(xx_io_device *file, const void *data, size_t bytes)
{
    const unsigned char *p = data;
    while (bytes) {
        ssize_t n = xx_io_write(file, p, bytes);
        if (n <= 0 || (size_t)n > bytes) return 0;
        p += n;
        bytes -= (size_t)n;
    }
    return 1;
}
static int stopped(xx_pd_struct *pd)
{
    if (!xx_pd_is_stopped(pd)) return 0;
    xx_pd_stop(pd);
    return 1;
}
static xxwidgets_status scan_status(xx_strings_scan_status status)
{
    return status == XX_STRINGS_SCAN_OK          ? XXWIDGETS_OK
           : status == XX_STRINGS_SCAN_NO_MEMORY ? XXWIDGETS_OUT_OF_MEMORY
           : status == XX_STRINGS_SCAN_CANCELLED ? XXWIDGETS_UNAVAILABLE
                                                 : XXWIDGETS_PLATFORM_ERROR;
}
static void set_error(char **error, const char *text)
{
    if (error) *error = cdie_strdup(text);
}
static int metadata_at(gui_strings_source *s, uint64_t number, string_meta *m)
{
    if (number >= s->model.total_count) return 0;
    if (s->generated) {
        *m = (string_meta){number * 5, 4, 4, XX_STRINGS_ASCII, 0};
        return 1;
    }
    if (number > INT64_MAX / sizeof(*m)) return 0;
    if (!s->metadata_count || number < s->metadata_first || number - s->metadata_first >= s->metadata_count) {
        size_t count;
        s->metadata_first = number - number % INDEX_BATCH;
        count = s->model.total_count - s->metadata_first < INDEX_BATCH ? (size_t)(s->model.total_count - s->metadata_first) : INDEX_BATCH;
        if (!xx_io_read_at(s->index, (int64_t)(s->metadata_first * sizeof(*m)), s->metadata_cache, count * sizeof(*m))) {
            s->metadata_count = 0;
            return 0;
        }
        s->metadata_count = count;
    }
    *m = s->metadata_cache[(size_t)(number - s->metadata_first)];
    return 1;
}
static int original_at(gui_strings_source *s, uint64_t row, uint64_t *original)
{
    uint64_t low = 0, high = s->range_count;
    match_range range;
    if (row >= s->model.count) return 0;
    if (!s->filtered) {
        *original = row;
        return 1;
    }
    if (s->match_cached && row >= s->match_cache.end_count - s->match_cache.length && row < s->match_cache.end_count) {
        *original = s->match_cache.first + row - (s->match_cache.end_count - s->match_cache.length);
        return 1;
    }
    while (low < high) {
        uint64_t mid = low + (high - low) / 2;
        if (!xx_io_read_at(s->ranges, (int64_t)(mid * sizeof(range)), &range, sizeof(range))) return 0;
        if (range.end_count <= row) low = mid + 1;
        else high = mid;
    }
    if (low == s->range_count || !xx_io_read_at(s->ranges, (int64_t)(low * sizeof(range)), &range, sizeof(range))) return 0;
    *original = range.first + row - (range.end_count - range.length);
    s->match_cache = range;
    s->match_cached = 1;
    return 1;
}
static const xx_memory_record *region_of(const gui_strings_source *s, uint64_t offset)
{
    const xx_memory_record *best = NULL;
    size_t i;
    for (i = 0; s->map && i < s->map->count; ++i) {
        const xx_memory_record *r = s->map->records + i;
        if (r->is_virtual || r->offset < 0 || r->size <= 0 || !r->name[0] || offset < (uint64_t)r->offset || offset - (uint64_t)r->offset >= (uint64_t)r->size) continue;
        if (!best || r->size < best->size) best = r;
    }
    return best;
}
static void escaped_region(char *out, const char *input)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i, n = 0;
    for (i = 0; input && i + 1 < XX_MEMORY_RECORD_NAME_SIZE && input[i] && n + 4 < 512; ++i) {
        unsigned char c = (unsigned char)input[i];
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
static xxwidgets_string_record widget_record(gui_strings_source *s, const string_meta *m, const char *text, char region[512])
{
    const xx_memory_record *r = region_of(s, m->offset);
    xxwidgets_string_record result = {m->offset, m->bytes, m->characters, cdie_strings_encoding_name((cdie_strings_encoding)m->encoding), text, "", 0, 0};
    if (r) {
        escaped_region(region, r->name);
        result.region = region;
        if (r->address != XX_INVALID_ADDRESS && m->offset - (uint64_t)r->offset <= UINT64_MAX - r->address) {
            result.address = r->address + m->offset - (uint64_t)r->offset;
            result.has_address = 1;
        }
    } else if (s->has_overlay && m->offset >= s->overlay_offset && m->offset < s->size) {
        result.region = "Overlay";
    }
    return result;
}
/* The sorted metadata makes nearby short strings share one source read. The
 * large-string path below remains streaming and never grows this cache. */
static const unsigned char *cached_bytes(gui_strings_source *s, const string_meta *m)
{
    uint64_t at;
    size_t bytes;
    if (m->bytes > SOURCE_CACHE / 2 || m->offset > s->size || m->bytes > s->size - m->offset) return NULL;
    if (s->cache_size && m->offset >= s->cache_offset && m->offset - s->cache_offset <= s->cache_size &&
        m->bytes <= s->cache_size - (size_t)(m->offset - s->cache_offset))
        return s->cache + (size_t)(m->offset - s->cache_offset);
    at = m->offset;
    bytes = s->size - at < SOURCE_CACHE ? (size_t)(s->size - at) : SOURCE_CACHE;
    if (!xx_io_read_at(s->input, (int64_t)at, s->cache, bytes)) return NULL;
    s->cache_offset = at;
    s->cache_size = bytes;
    return s->cache;
}
typedef struct text_prefix {
    xx_buf_t *text;
    size_t limit;
    int truncated;
} text_prefix;
static bool collect_prefix(const char *text, size_t bytes, void *user)
{
    text_prefix *p = user;
    size_t room = p->limit - p->text->size;
    if (bytes > room) {
        bytes = room;
        p->truncated = 1;
    }
    if (bytes && !xx_buf_append(p->text, text, bytes)) return false;
    return !p->truncated;
}
static int decode_small(gui_strings_source *s, const string_meta *m, xx_strings_text_sink sink, void *user)
{
    const unsigned char *p = cached_bytes(s, m);
    char output[SOURCE_CACHE];
    size_t at, used = 0;
    if (!p) return 0;
    if (m->encoding & 3) return sink((const char *)p, (size_t)m->bytes, user) ? 1 : -1;
    for (at = 0; at < (size_t)m->bytes; at += 2) {
        uint32_t cp = m->encoding == XX_STRINGS_UTF16LE ? p[at] | ((uint32_t)p[at + 1] << 8) : ((uint32_t)p[at] << 8) | p[at + 1];
        if (cp >= 0xd800 && cp <= 0xdbff && at + 3 < m->bytes) {
            uint32_t low = m->encoding == XX_STRINGS_UTF16LE ? p[at + 2] | ((uint32_t)p[at + 3] << 8) : ((uint32_t)p[at + 2] << 8) | p[at + 3];
            if (low < 0xdc00 || low > 0xdfff) return 0;
            cp = 0x10000 + ((cp - 0xd800) << 10) + low - 0xdc00;
            at += 2;
        }
        if (cp < 0x80) output[used++] = (char)cp;
        else if (cp < 0x800) {
            output[used++] = (char)(0xc0 | (cp >> 6));
            output[used++] = (char)(0x80 | (cp & 63));
        } else if (cp < 0x10000) {
            output[used++] = (char)(0xe0 | (cp >> 12));
            output[used++] = (char)(0x80 | ((cp >> 6) & 63));
            output[used++] = (char)(0x80 | (cp & 63));
        } else {
            output[used++] = (char)(0xf0 | (cp >> 18));
            output[used++] = (char)(0x80 | ((cp >> 12) & 63));
            output[used++] = (char)(0x80 | ((cp >> 6) & 63));
            output[used++] = (char)(0x80 | (cp & 63));
        }
    }
    return sink(output, used, user) ? 1 : -1;
}
static xx_strings_scan_status decode_text(gui_strings_source *s, const string_meta *m, xx_pd_struct *pd, xx_strings_text_sink sink, void *user)
{
    int small;
    if (s->generated) return sink("AAAA", 4, user) ? XX_STRINGS_SCAN_OK : XX_STRINGS_SCAN_SINK;
    small = decode_small(s, m, sink, user);
    if (small) return small > 0 ? XX_STRINGS_SCAN_OK : XX_STRINGS_SCAN_SINK;
    return xx_strings_decode_device_ex(s->input, m->encoding, m->offset, m->bytes, s->configured ? XX_STRINGS_DIE_ALPHABET : 0, pd, sink, user);
}
static xxwidgets_status source_read(void *user, uint64_t first, size_t capacity, xxwidgets_string_record *records, uint64_t *numbers, size_t *received)
{
    gui_strings_source *s = user;
    size_t i, count;
    if (!s || !records || !numbers || !received || capacity > XXWIDGETS_STRINGS_PAGE_SIZE || first > s->model.count) return XXWIDGETS_INVALID_ARGUMENT;
    *received = 0;
    count = s->model.count - first < capacity ? (size_t)(s->model.count - first) : capacity;
    for (i = 0; i < count; ++i) {
        string_meta m;
        text_prefix prefix = {s->text + i, PAGE_TEXT_PREFIX, 0};
        xx_strings_scan_status status;
        if (!original_at(s, first + i, numbers + i) || !metadata_at(s, numbers[i], &m)) return XXWIDGETS_PLATFORM_ERROR;
        xx_buf_clear(s->text + i);
        status = decode_text(s, &m, NULL, collect_prefix, &prefix);
        if (status != XX_STRINGS_SCAN_OK && !(status == XX_STRINGS_SCAN_SINK && prefix.truncated)) return scan_status(status);
        if (prefix.truncated) {
            /* Remove any incomplete final UTF-8 scalar before the explicit
             * display-only prefix label. Search and CSV stream complete text. */
            size_t n = s->text[i].size, start = n;
            while (start && ((unsigned char)s->text[i].data[start - 1] & 0xc0) == 0x80) --start;
            if (start) {
                unsigned char lead = (unsigned char)s->text[i].data[start - 1];
                size_t expected = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
                if (n - (start - 1) < expected) n = start - 1;
            }
            s->text[i].size = n;
            if (s->text[i].data) s->text[i].data[n] = 0;
            if (!xx_buf_append_str(s->text + i, " ... [display prefix]")) return XXWIDGETS_OUT_OF_MEMORY;
        }
        records[i] = widget_record(s, &m, s->text[i].data ? s->text[i].data : "", s->regions[i]);
    }
    *received = count;
    return XXWIDGETS_OK;
}
typedef struct index_build {
    gui_strings_source *source;
    index_bucket buckets[5];
    int failed;
} index_build;
static int bucket_flush(index_bucket *b)
{
    if (b->used && !write_all(b->file, b->entries, b->used * sizeof(*b->entries))) return 0;
    b->used = 0;
    return 1;
}
static bool add_metadata(const xx_strings_scan_record *record, void *user)
{
    index_build *build = user;
    unsigned bucket = record->encoding & 3 ? 0 : record->encoding == XX_STRINGS_UTF16LE ? 1U + (unsigned)(record->offset & 1) : 3U + (unsigned)(record->offset & 1);
    index_bucket *b = build->buckets + bucket;
    if (build->source->configured && build->source->model.options.null_terminated) {
        unsigned char ending[2];
        size_t bytes = record->encoding & 3 ? 1 : 2;
        uint64_t end;
        if (record->offset > build->source->size || record->byte_size > build->source->size - record->offset) {
            build->failed = 1;
            return false;
        }
        end = record->offset + record->byte_size;
        if (bytes > build->source->size - end) return true; /* EOF is not a terminator. */
        if (!xx_io_read_at(build->source->input, (int64_t)end, ending, bytes)) {
            build->failed = 1;
            return false;
        }
        if (ending[0] || (bytes == 2 && ending[1])) return true;
    }
    if (b->total >= INT64_MAX / sizeof(string_meta)) {
        build->failed = 1;
        return false;
    }
    b->entries[b->used++] = (string_meta){record->offset, record->byte_size, record->character_length, record->encoding, 0};
    ++b->total;
    if (record->byte_size > build->source->max_bytes) build->source->max_bytes = record->byte_size;
    if (b->used == INDEX_BATCH && !bucket_flush(b)) {
        build->failed = 1;
        return false;
    }
    return true;
}
static int bucket_next(index_bucket *b, string_meta **out)
{
    if (b->next == b->used) {
        size_t n;
        if (b->read == b->total) {
            *out = NULL;
            return 1;
        }
        n = b->total - b->read < INDEX_BATCH ? (size_t)(b->total - b->read) : INDEX_BATCH;
        if (!xx_io_read_at(b->file, (int64_t)(b->read * sizeof(string_meta)), b->entries, n * sizeof(string_meta))) return 0;
        b->read += n;
        b->used = n;
        b->next = 0;
    }
    *out = b->entries + b->next;
    return 1;
}
static xxwidgets_status build_index(gui_strings_source *s, xx_pd_struct *pd)
{
    index_build *build = xx_rt_calloc(1, sizeof(*build));
    string_meta *heads[5] = {0}, output[INDEX_BATCH];
    xx_strings_scan_options options = xx_strings_scan_defaults();
    xx_strings_scan_stats stats;
    xx_strings_scan_status scan;
    xxwidgets_status status = XXWIDGETS_PLATFORM_ERROR;
    uint64_t done = 0;
    uint64_t accepted = 0;
    size_t used = 0;
    int level = -1;
    unsigned i;
    if (!build) return XXWIDGETS_OUT_OF_MEMORY;
    build->source = s;
    for (i = 0; i < 5; ++i)
        if (!(build->buckets[i].file = xx_io_temp_open())) goto finish;
    options.flags = XX_STRINGS_NO_TEXT;
    if (s->configured) {
        options.minimum_length = s->model.options.minimum_length;
        options.encodings = (s->model.options.ansi ? XX_STRINGS_ASCII : 0) | (s->model.options.unicode ? XX_STRINGS_UTF16LE : 0);
        options.flags |= XX_STRINGS_DIE_ALPHABET;
    }
    scan = xx_strings_scan_device(s->input, &options, pd, add_metadata, build, &stats);
    if (scan != XX_STRINGS_SCAN_OK) {
        status = scan_status(scan);
        goto finish;
    }
    for (i = 0; i < 5; ++i) {
        if (build->buckets[i].total > (uint64_t)INT64_MAX / sizeof(string_meta) - accepted) goto finish;
        accepted += build->buckets[i].total;
    }
    s->model.total_count = s->model.count = accepted;
    s->index = xx_io_temp_open();
    if (!s->index) goto finish;
    for (i = 0; i < 5; ++i) {
        index_bucket *b = build->buckets + i;
        if (!bucket_flush(b) || !bucket_next(b, heads + i)) goto finish;
    }
    level = xx_pd_enter_level(pd, accepted, "Indexing strings");
    while (done < accepted) {
        unsigned best = 5;
        for (i = 0; i < 5; ++i)
            if (heads[i] &&
                (best == 5 || heads[i]->offset < heads[best]->offset || (heads[i]->offset == heads[best]->offset && heads[i]->encoding < heads[best]->encoding)))
                best = i;
        if (best == 5) goto finish;
        output[used++] = *heads[best];
        ++build->buckets[best].next;
        if (!bucket_next(build->buckets + best, heads + best)) goto finish;
        ++done;
        if (used == INDEX_BATCH || done == accepted) {
            if (stopped(pd)) {
                status = XXWIDGETS_UNAVAILABLE;
                goto finish;
            }
            if (!write_all(s->index, output, used * sizeof(*output))) goto finish;
            used = 0;
            xx_pd_set_current(pd, level, done);
        }
    }
    status = stopped(pd) ? XXWIDGETS_UNAVAILABLE : XXWIDGETS_OK;
finish:
    xx_pd_leave_level(pd, level);
    for (i = 0; i < 5; ++i)
        if (build->buckets[i].file) xx_io_close(build->buckets[i].file);
    xx_rt_free(build);
    return status;
}
static unsigned char lower_ascii(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + 'a' - 'A') : c;
}
static int contains(const char *text, const char *filter)
{
    size_t n = filter ? strlen(filter) : 0;
    if (!n) return 1;
    for (; text && *text; ++text) {
        size_t i;
        for (i = 0; i < n && text[i] && lower_ascii((unsigned char)text[i]) == lower_ascii((unsigned char)filter[i]); ++i) {
        }
        if (i == n) return 1;
    }
    return 0;
}
static int contains_hex(uint64_t value, const char *filter)
{
    char short_form[32], long_form[32];
    if (!filter || !*filter) return 1;
    snprintf(short_form, sizeof(short_form), "%08" PRIX64, value);
    snprintf(long_form, sizeof(long_form), "%016" PRIX64, value);
    return contains(short_form, filter) || contains(long_form, filter);
}
typedef struct stream_match {
    const char *pattern;
    size_t length, state, *failure;
    int found;
} stream_match;
static int match_init(stream_match *m, const char *pattern)
{
    size_t i, k = 0;
    memset(m, 0, sizeof(*m));
    m->pattern = pattern ? pattern : "";
    m->length = strlen(m->pattern);
    m->found = !m->length;
    if (!m->length) return 1;
    if (m->length > SIZE_MAX / sizeof(size_t)) return 0;
    m->failure = xx_rt_calloc(m->length, sizeof(size_t));
    if (!m->failure) return 0;
    for (i = 1; i < m->length; ++i) {
        unsigned char c = lower_ascii((unsigned char)m->pattern[i]);
        while (k && c != lower_ascii((unsigned char)m->pattern[k])) k = m->failure[k - 1];
        if (c == lower_ascii((unsigned char)m->pattern[k])) ++k;
        m->failure[i] = k;
    }
    return 1;
}
static void match_feed(stream_match *m, const char *text, size_t bytes)
{
    size_t i;
    if (m->found) return;
    for (i = 0; i < bytes; ++i) {
        unsigned char c = lower_ascii((unsigned char)text[i]);
        while (m->state && c != lower_ascii((unsigned char)m->pattern[m->state])) m->state = m->failure[m->state - 1];
        if (c == lower_ascii((unsigned char)m->pattern[m->state])) ++m->state;
        if (m->state == m->length) {
            m->found = 1;
            break;
        }
    }
}
typedef struct search_context {
    gui_strings_source *source;
    const char *global, *columns[7];
    stream_match global_text, column_text;
    xx_io_device *ranges;
    match_range pending, batch[INDEX_BATCH];
    uint64_t matched, range_count, visited;
    size_t used;
    xx_pd_struct *pd;
    int failed, generated_constant, generated_accept;
    stream_match link_text[4];
    xx_regex *regex;
    int32_t *captures;
    xx_buf_t full_text;
    xxwidgets_status text_error;
} search_context;
static int ranges_flush(search_context *c)
{
    if (c->used && !write_all(c->ranges, c->batch, c->used * sizeof(*c->batch))) return 0;
    c->used = 0;
    return 1;
}
static int range_end(search_context *c)
{
    if (!c->pending.length) return 1;
    if (c->range_count >= INT64_MAX / sizeof(match_range)) return 0;
    c->pending.end_count = c->matched;
    c->batch[c->used++] = c->pending;
    ++c->range_count;
    c->pending.length = 0;
    return c->used < INDEX_BATCH || ranges_flush(c);
}
static int add_match(search_context *c, uint64_t number)
{
    if (c->pending.length && number != c->pending.first + c->pending.length && !range_end(c)) return 0;
    if (!c->pending.length) c->pending.first = number;
    ++c->pending.length;
    ++c->matched;
    return 1;
}
static bool search_text(const char *text, size_t bytes, void *user)
{
    search_context *c = user;
    unsigned i;
    match_feed(&c->global_text, text, bytes);
    match_feed(&c->column_text, text, bytes);
    if (c->source->configured && c->source->model.options.links)
        for (i = 0; i < 4; ++i) match_feed(c->link_text + i, text, bytes);
    if (c->regex) {
        if (bytes > INT32_MAX - c->full_text.size) {
            c->text_error = XXWIDGETS_INVALID_ARGUMENT;
            source_error(c->source, "A complete decoded string exceeds the regular-expression engine's 2 GiB text limit");
            return false;
        }
        if (!xx_buf_append(&c->full_text, text, bytes)) {
            c->text_error = XXWIDGETS_OUT_OF_MEMORY;
            source_error(c->source, "Cannot allocate the complete decoded string for regular-expression search");
            return false;
        }
    }
    return true;
}
static bool abort_regex(void *user)
{
    search_context *c = user;
    return stopped(c->pd) != 0;
}
static int match_metadata(search_context *c, const string_meta *m, uint64_t number)
{
    char region[512], number_text[32];
    xxwidgets_string_record r = widget_record(c->source, m, "", region);
    unsigned i;
    int links = c->source->configured && c->source->model.options.links;
    if (*c->columns[0]) {
        snprintf(number_text, sizeof(number_text), "%" PRIu64, number);
        if (!contains(number_text, c->columns[0])) return 0;
    }
    /* A native page chooses an 8/16-digit width from its rows. Accept either
     * padding for global numeric filters so first/last-page searches agree. */
    if (!contains_hex(m->offset, c->columns[1]) || (r.has_address ? !contains_hex(r.address, c->columns[2]) : !contains("", c->columns[2])) ||
        !contains(r.region, c->columns[3]) || !contains_hex(m->bytes, c->columns[4]) || !contains(r.encoding, c->columns[5]))
        return 0;
    c->global_text.state = c->column_text.state = 0;
    c->global_text.found = contains(r.encoding, c->global);
    c->column_text.found = !c->column_text.length;
    for (i = 0; i < 4; ++i) {
        c->link_text[i].state = 0;
        c->link_text[i].found = 0;
    }
    xx_buf_clear(&c->full_text);
    if (c->regex && m->encoding & 3 && m->bytes > INT32_MAX) {
        c->text_error = XXWIDGETS_INVALID_ARGUMENT;
        source_error(c->source, "A complete decoded string exceeds the regular-expression engine's 2 GiB text limit");
        return -1;
    }
    if (!c->global_text.found || !c->column_text.found || links || c->regex) {
        xx_strings_scan_status status = decode_text(c->source, m, c->pd, search_text, c);
        if (status != XX_STRINGS_SCAN_OK) {
            c->failed = 1;
            return -1;
        }
    }
    if (!c->global_text.found || !c->column_text.found) return 0;
    if (links && !c->link_text[0].found && !c->link_text[1].found && !c->link_text[2].found && !c->link_text[3].found) return 0;
    if (c->regex) {
        xx_regex_exec_status status = xx_regex_exec_ex(c->regex, c->full_text.data ? c->full_text.data : "", c->full_text.size, 0, c->captures, abort_regex, c);
        if (status < 0) {
            c->text_error = status == XX_REGEX_EXEC_CANCELLED   ? XXWIDGETS_UNAVAILABLE
                            : status == XX_REGEX_EXEC_NO_MEMORY ? XXWIDGETS_OUT_OF_MEMORY
                            : status == XX_REGEX_EXEC_INVALID   ? XXWIDGETS_INVALID_ARGUMENT
                                                                : XXWIDGETS_PLATFORM_ERROR;
            source_error(c->source, status == XX_REGEX_EXEC_CANCELLED   ? "Regular-expression search cancelled"
                                    : status == XX_REGEX_EXEC_NO_MEMORY ? "Cannot allocate regular-expression matcher state"
                                    : status == XX_REGEX_EXEC_BUDGET    ? "Regular-expression work limit exceeded; preceding results have been preserved"
                                                                        : "Regular-expression matcher rejected the complete decoded string");
            return -1;
        }
        return status == XX_REGEX_EXEC_MATCH;
    }
    return 1;
}
static bool generated_match(const xx_strings_scan_record *r, void *user)
{
    search_context *c = user;
    string_meta m = {r->offset, r->byte_size, r->character_length, r->encoding, 0};
    int matches;
    if (r->offset != c->visited * 5 || r->byte_size != 4 || r->character_length != 4 || r->encoding != XX_STRINGS_ASCII) return false;
    matches = c->generated_constant ? c->generated_accept : match_metadata(c, &m, c->visited);
    if (matches < 0 || (matches && !add_match(c, c->visited))) {
        c->failed = 1;
        return false;
    }
    ++c->visited;
    return true;
}
static xxwidgets_status source_search(void *user, const char *filter, const char *const columns[7], xx_pd_struct *pd, uint64_t *matched)
{
    gui_strings_source *s = user;
    search_context *c = xx_rt_calloc(1, sizeof(*c));
    xxwidgets_status status = XXWIDGETS_PLATFORM_ERROR;
    uint64_t done = 0;
    int level = -1;
    unsigned i;
    if (!s || !matched) {
        xx_rt_free(c);
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (!c) return XXWIDGETS_OUT_OF_MEMORY;
    source_error(s, "");
    c->source = s;
    c->pd = pd;
    c->global = filter ? filter : "";
    for (i = 0; i < 7; ++i) c->columns[i] = columns && columns[i] ? columns[i] : "";
    if (!match_init(&c->global_text, c->global) || !match_init(&c->column_text, c->columns[6])) {
        status = XXWIDGETS_OUT_OF_MEMORY;
        goto finish;
    }
    if (s->configured && s->model.options.links) {
        static const char *const schemes[4] = {"http://", "https://", "www.", "mailto:"};
        for (i = 0; i < 4; ++i)
            if (!match_init(c->link_text + i, schemes[i])) {
                status = XXWIDGETS_OUT_OF_MEMORY;
                goto finish;
            }
    }
    if (s->configured && s->model.options.regexp) {
        char *error = NULL;
        int groups;
        c->regex = xx_regex_compile(s->model.options.expression, "", &error);
        if (!c->regex) {
            source_error(s, error ? error : "Cannot compile regular expression");
            xx_rt_free(error);
            status = XXWIDGETS_INVALID_ARGUMENT;
            goto finish;
        }
        groups = xx_regex_ngroups(c->regex);
        if (groups < 0 || (size_t)groups + 1 > SIZE_MAX / (2 * sizeof(int32_t))) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            goto finish;
        }
        c->captures = xx_rt_calloc(((size_t)groups + 1) * 2, sizeof(int32_t));
        if (!c->captures) {
            status = XXWIDGETS_OUT_OF_MEMORY;
            goto finish;
        }
    }
    c->ranges = xx_io_temp_open();
    if (!c->ranges) goto finish;
    if (s->generated) {
        xx_strings_scan_options o = xx_strings_scan_defaults();
        xx_strings_scan_stats stats;
        xx_strings_scan_status scan;
        /* Common billion-row searches over the invariant text/type have a
         * constant predicate, while every actual record is still scanned and
         * verified. Avoid formatting a billion unused numeric cells. */
        if (!*c->columns[0] && !*c->columns[1]) {
            string_meta sample = {0, 4, 4, XX_STRINGS_ASCII, 0};
            c->generated_constant = 1;
            c->generated_accept = match_metadata(c, &sample, 0);
            if (c->generated_accept < 0) goto finish;
        }
        o.encodings = XX_STRINGS_ASCII;
        o.flags = XX_STRINGS_NO_TEXT;
        scan = xx_strings_scan_device(s->input, &o, pd, generated_match, c, &stats);
        if (scan != XX_STRINGS_SCAN_OK) {
            status = scan_status(scan);
            goto finish;
        }
        if (c->visited != s->model.total_count || stats.record_count != s->model.total_count) goto finish;
    } else {
        string_meta batch[INDEX_BATCH];
        level = xx_pd_enter_level(pd, s->model.total_count, "Searching strings");
        while (done < s->model.total_count) {
            size_t n = s->model.total_count - done < INDEX_BATCH ? (size_t)(s->model.total_count - done) : INDEX_BATCH;
            size_t at;
            if (stopped(pd)) {
                status = XXWIDGETS_UNAVAILABLE;
                goto finish;
            }
            if (!xx_io_read_at(s->index, (int64_t)(done * sizeof(string_meta)), batch, n * sizeof(string_meta))) goto finish;
            for (at = 0; at < n; ++at) {
                int matches = match_metadata(c, batch + at, done + at);
                if (matches < 0 || (matches && !add_match(c, done + at))) goto finish;
            }
            done += n;
            xx_pd_set_current(pd, level, done);
        }
    }
    if (!range_end(c) || !ranges_flush(c)) goto finish;
    xx_pd_leave_level(pd, level);
    level = -1;
    if (stopped(pd)) {
        status = XXWIDGETS_UNAVAILABLE;
        goto finish;
    }
    if (s->ranges) xx_io_close(s->ranges);
    s->ranges = c->ranges;
    c->ranges = NULL;
    s->range_count = c->range_count;
    s->filtered = 1;
    s->match_cached = 0;
    *matched = c->matched;
    s->model.count = c->matched;
    status = XXWIDGETS_OK;
finish:
    if (c->text_error) status = c->text_error;
    xx_pd_leave_level(pd, level);
    if (c->ranges) xx_io_close(c->ranges);
    xx_rt_free(c->global_text.failure);
    xx_rt_free(c->column_text.failure);
    for (i = 0; i < 4; ++i) xx_rt_free(c->link_text[i].failure);
    xx_regex_free(c->regex);
    xx_rt_free(c->captures);
    xx_buf_free(&c->full_text);
    xx_rt_free(c);
    return status;
}
typedef struct csv_output {
    xx_io_device *file;
    char buffer[65536];
    size_t used;
    uint64_t written;
    xx_pd_struct *progress;
    xxwidgets_disk_write_reporter *reporter;
    int failed;
} csv_output;
static int csv_flush(csv_output *c)
{
    size_t at = 0;
    if (c->failed) return 0;
    while (at < c->used) {
        ssize_t n;
        if (stopped(c->progress)) {
            c->failed = 1;
            return 0;
        }
        n = xx_io_write(c->file, c->buffer + at, c->used - at);
        if (n <= 0 || (size_t)n > c->used - at || (uint64_t)n > UINT64_MAX - c->written) {
            c->failed = 1;
            return 0;
        }
        at += (size_t)n;
        c->written += (uint64_t)n;
        if (c->reporter) (void)xxwidgets_disk_write_set_bytes(c->reporter, c->written, 0, 0);
    }
    c->used = 0;
    return 1;
}
static int csv_bytes(csv_output *c, const char *text, size_t bytes)
{
    while (bytes) {
        size_t room = sizeof(c->buffer) - c->used, n = bytes < room ? bytes : room;
        memcpy(c->buffer + c->used, text, n);
        c->used += n;
        text += n;
        bytes -= n;
        if (c->used == sizeof(c->buffer) && !csv_flush(c)) return 0;
    }
    return 1;
}
static bool csv_text(const char *text, size_t bytes, void *user)
{
    csv_output *c = user;
    size_t at = 0, i;
    for (i = 0; i < bytes; ++i)
        if (text[i] == '"') {
            if (!csv_bytes(c, text + at, i - at + 1) || !csv_bytes(c, "\"", 1)) return false;
            at = i + 1;
        }
    return csv_bytes(c, text + at, bytes - at) != 0;
}
static xxwidgets_status source_save_ex(void *user, const char *path, xx_pd_struct *pd, xxwidgets_disk_write_reporter *reporter)
{
    gui_strings_source *s = user;
    csv_output *csv;
    uint64_t row;
    int level = -1;
    char *temporary = NULL;
    size_t path_length;
    unsigned attempt;
    xxwidgets_status status = XXWIDGETS_PLATFORM_ERROR;
    source_error(s, "");
    if (!s || !path || !*path) {
        source_error(s, "A destination path is required for strings CSV");
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    csv = xx_rt_calloc(1, sizeof(*csv));
    if (!csv) {
        source_error(s, "Cannot allocate strings CSV output buffer");
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    csv->progress = pd;
    csv->reporter = reporter;
    if (reporter) {
        (void)xxwidgets_disk_write_set_file(reporter, path, 0, 1, 1);
        /* Escaping/encoding determines the final CSV size while streaming.
         * Report bytes actually written; rows are never counted as bytes. */
        (void)xxwidgets_disk_write_set_bytes(reporter, 0, 0, 0);
    }
    path_length = strlen(path);
    if (path_length > SIZE_MAX - 96) {
        xx_rt_free(csv);
        source_error(s, "Strings CSV destination path is too long");
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    temporary = xx_rt_malloc(path_length + 96);
    if (!temporary) {
        xx_rt_free(csv);
        source_error(s, "Cannot allocate strings CSV destination path");
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    for (attempt = 0; attempt < 16 && !csv->file; ++attempt) {
        snprintf(temporary, path_length + 96, "%s.cdie-strings-%llx-%llx-%u.tmp", path, (unsigned long long)xx_rt_clock_ms(), (unsigned long long)(uintptr_t)csv,
                 attempt);
        csv->file = xx_io_file_open(temporary, "wbx");
    }
    if (!csv->file) {
        xx_rt_free(temporary);
        xx_rt_free(csv);
        source_error(s, "Cannot create temporary strings CSV beside the destination");
        return XXWIDGETS_PLATFORM_ERROR;
    }
    level = xx_pd_enter_level(pd, s->model.count, "Saving strings CSV");
    if (!csv_bytes(csv, "Offset,Bytes,Characters,Encoding,String\r\n", strlen("Offset,Bytes,Characters,Encoding,String\r\n"))) goto finish;
    for (row = 0; row < s->model.count; ++row) {
        string_meta m;
        uint64_t original;
        char prefix[160];
        int n;
        if (!(row & 511) && stopped(pd)) {
            status = XXWIDGETS_UNAVAILABLE;
            goto finish;
        }
        if (!original_at(s, row, &original) || !metadata_at(s, original, &m)) goto finish;
        n = snprintf(prefix, sizeof(prefix), "0x%016" PRIX64 ",%" PRIu64 ",%" PRIu64 ",%s,\"", m.offset, m.bytes, m.characters,
                     cdie_strings_encoding_name((cdie_strings_encoding)m.encoding));
        if (n < 0 || (size_t)n >= sizeof(prefix) || !csv_bytes(csv, prefix, (size_t)n)) goto finish;
        if (decode_text(s, &m, pd, csv_text, csv) != XX_STRINGS_SCAN_OK || !csv_bytes(csv, "\"\r\n", 3)) goto finish;
        if (!(row & 511)) xx_pd_set_current(pd, level, row + 1);
    }
    if (!csv_flush(csv)) goto finish;
    xx_pd_leave_level(pd, level);
    level = -1;
    if (stopped(pd)) {
        status = XXWIDGETS_UNAVAILABLE;
        goto finish;
    }
    if (xx_io_close(csv->file) != 0) {
        csv->file = NULL;
        goto finish;
    }
    csv->file = NULL;
    if (stopped(pd)) {
        status = XXWIDGETS_UNAVAILABLE;
        goto finish;
    }
    if (!xx_io_file_replace_a(temporary, path, true)) goto finish;
    status = XXWIDGETS_OK;
    if (reporter) {
        (void)xxwidgets_disk_write_set_bytes(reporter, csv->written, csv->written, 1);
        (void)xxwidgets_disk_write_set_file(reporter, path, 1, 1, 1);
    }
finish:
    xx_pd_leave_level(pd, level);
    if (csv->file && xx_io_close(csv->file) != 0) status = XXWIDGETS_PLATFORM_ERROR;
    if (status != XXWIDGETS_OK) (void)xx_io_file_remove_a(temporary);
    xx_rt_free(temporary);
    xx_rt_free(csv);
    if (status != XXWIDGETS_OK) {
        if (stopped(pd)) status = XXWIDGETS_UNAVAILABLE;
        source_error(s, stopped(pd) ? "Saving strings CSV cancelled" : "Cannot write or replace strings CSV destination");
    }
    return status;
}
static xxwidgets_status source_save(void *user, const char *path, xx_pd_struct *pd)
{
    return source_save_ex(user, path, pd, NULL);
}
typedef struct generated_input {
    xx_io_device device;
    uint64_t size, position;
} generated_input;
static ssize_t generated_read(xx_io_device *device, void *buffer, size_t size)
{
    static const unsigned char pattern[5] = {'A', 'A', 'A', 'A', 0};
    generated_input *input = device->priv;
    unsigned char *out = buffer;
    size_t copied, seed = size < 5 ? size : 5, i;
    if ((uint64_t)size > input->size - input->position) {
        size = (size_t)(input->size - input->position);
        seed = size < 5 ? size : 5;
    }
    for (i = 0; i < seed; ++i) out[i] = pattern[(input->position + i) % 5];
    copied = seed;
    while (copied < size) {
        size_t n = size - copied < copied ? size - copied : copied;
        memcpy(out + copied, out, n);
        copied += n;
    }
    input->position += size;
    return (ssize_t)size;
}
static int generated_seek(xx_io_device *device, int64_t offset, int whence)
{
    generated_input *input = device->priv;
    if (whence != SEEK_SET || offset < 0 || (uint64_t)offset > input->size) return -1;
    input->position = (uint64_t)offset;
    return 0;
}
static int64_t generated_tell(xx_io_device *device)
{
    return (int64_t)((generated_input *)device->priv)->position;
}
static int64_t generated_size(xx_io_device *device)
{
    return (int64_t)((generated_input *)device->priv)->size;
}
static int generated_close(xx_io_device *device)
{
    xx_rt_free(device->priv);
    return 0;
}
static bool verify_generated(const xx_strings_scan_record *r, void *user)
{
    uint64_t *count = user;
    if (r->offset != *count * 5 || r->byte_size != 4 || r->character_length != 4 || r->encoding != XX_STRINGS_ASCII) return false;
    ++*count;
    return true;
}
static int attach_generated_input(gui_strings_source *s, uint64_t count)
{
    generated_input *input = xx_rt_calloc(1, sizeof(*input));
    if (!input) return 0;
    input->size = count * 5;
    input->device.priv = input;
    input->device.read = generated_read;
    input->device.seek64 = generated_seek;
    input->device.tell = generated_tell;
    input->device.total_size = generated_size;
    input->device.close = generated_close;
    s->input = &input->device;
    s->size = input->size;
    s->generated = 1;
    s->generated_count = count;
    s->max_bytes = 4;
    s->model.count = s->model.total_count = count;
    return 1;
}
static gui_strings_source *new_source(void)
{
    gui_strings_source *s = xx_rt_calloc(1, sizeof(*s));
    if (s) {
        s->model.read = source_read;
        s->model.search = source_search;
        s->model.save = source_save;
        s->model.save_ex = source_save_ex;
        s->model.search_ex = source_search_ex;
        s->model.error = source_error_text;
        s->model.records_are_previews = 1;
        xxwidgets_strings_search_options_init(&s->model.options);
        snprintf(s->format_labels[0], sizeof(s->format_labels[0]), "Binary");
        snprintf(s->map_labels[0], sizeof(s->map_labels[0]), "Regions");
        s->model.format_count = s->model.map_mode_count = 1;
        repair_model(s);
    }
    return s;
}
/* Selector labels belong to the provider and remain valid after a candidate
 * replaces its internals. Regions is the physical Data map; selecting Binary
 * always normalizes the map selector to Regions. */
static xxwidgets_status configure_map(gui_strings_source *s, const xxwidgets_strings_search_options *requested, xx_pd_struct *pd, char **error)
{
    cdie_memory_map_data *map = NULL;
    char *map_error = NULL;
    xxwidgets_strings_search_options options = s->model.options;
    int native;
    if (!cdie_memory_map_read(s->path, pd, &map, &map_error)) {
        xx_rt_free(map_error);
        map_error = NULL;
        if (stopped(pd)) return XXWIDGETS_UNAVAILABLE;
        if (!cdie_memory_map_read_mode(s->path, "Binary", "Data", pd, &map, &map_error)) {
            if (error) *error = map_error;
            else xx_rt_free(map_error);
            return stopped(pd) ? XXWIDGETS_UNAVAILABLE : XXWIDGETS_PLATFORM_ERROR;
        }
    }
    native = strcmp(map->file_type, "Binary") != 0;
    if (native) {
        snprintf(s->format_labels[1], sizeof(s->format_labels[1]), "%s", map->file_type);
        s->model.format_count = 2;
        if (map->available_modes & CDIE_MEMORY_MAP_MODE_SECTIONS) snprintf(s->map_labels[s->model.map_mode_count++], sizeof(s->map_labels[0]), "Sections");
        if (map->available_modes & CDIE_MEMORY_MAP_MODE_SEGMENTS) snprintf(s->map_labels[s->model.map_mode_count++], sizeof(s->map_labels[0]), "Segments");
        options.format = 1;
        options.map_mode = 1;
    }
    if (requested) options = *requested;
    if (!valid_options(&options) || options.format >= s->model.format_count || options.map_mode >= s->model.map_mode_count) {
        xx_rt_free(map);
        set_error(error, "Invalid strings extraction or reference-map options");
        return XXWIDGETS_INVALID_ARGUMENT;
    }
    if (!options.format) options.map_mode = 0;
    if (!options.format || !options.map_mode || strcmp(map->mode, s->map_labels[options.map_mode])) {
        xx_rt_free(map);
        map = NULL;
        if (!cdie_memory_map_read_mode(s->path, !options.format || !options.map_mode ? "Binary" : s->format_labels[options.format],
                                       !options.map_mode ? "Data" : s->map_labels[options.map_mode], pd, &map, &map_error)) {
            if (error) *error = map_error;
            else xx_rt_free(map_error);
            return stopped(pd) ? XXWIDGETS_UNAVAILABLE : XXWIDGETS_PLATFORM_ERROR;
        }
    }
    s->model.options = options;
    s->map = map;
    /* The native PE section map excludes the physical tail. Label only that
     * tail as Overlay; section gaps remain unmapped and Binary keeps Data. */
    if (s->configured && options.format && options.map_mode && !strncmp(map->file_type, "PE", 2)) {
        uint64_t end = 0;
        size_t i;
        int mapped = 0;
        for (i = 0; i < map->count; ++i) {
            const xx_memory_record *r = map->records + i;
            uint64_t current;
            if (r->is_virtual || r->address == XX_INVALID_ADDRESS || r->offset < 0 || r->size <= 0 || !(r->file_part & (XX_FILE_PART_SECTION | XX_FILE_PART_HEADER)) ||
                (uint64_t)r->offset > s->size || (uint64_t)r->size > s->size - (uint64_t)r->offset)
                continue;
            current = (uint64_t)r->offset + (uint64_t)r->size;
            if (current > end) end = current;
            mapped = 1;
        }
        s->has_overlay = mapped && end < s->size;
        s->overlay_offset = end;
    }
    return XXWIDGETS_OK;
}
static xxwidgets_status open_file(const char *path, const xxwidgets_strings_search_options *options, int configured, int search, xx_pd_struct *pd,
                                  gui_strings_source **out, char **error)
{
    gui_strings_source *s;
    xxwidgets_status status;
    int64_t size;
    if (!out || !error || !path || !*path) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    *error = NULL;
    s = new_source();
    if (!s) return XXWIDGETS_OUT_OF_MEMORY;
    s->configured = configured;
    s->path = cdie_strdup(path);
    if (!s->path) {
        gui_strings_source_free(s);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    s->input = xx_io_file_open(path, "rb");
    size = xx_io_total_size(s->input);
    if (size < 0) {
        set_error(error, "Cannot open strings input");
        gui_strings_source_free(s);
        return XXWIDGETS_PLATFORM_ERROR;
    }
    s->size = (uint64_t)size;
    status = configure_map(s, options, pd, error);
    if (!configured) {
        /* Keep the established UTF-8-aware min-four API for legacy clients.
         * The configurable browser deliberately starts with Qt's min-five. */
        s->model.options.minimum_length = 4;
    }
    if (status == XXWIDGETS_OK) status = build_index(s, pd);
    if (status == XXWIDGETS_OK && configured && search) {
        uint64_t matched;
        status = source_search(s, "", NULL, pd, &matched);
    }
    if (status == XXWIDGETS_OK && stopped(pd)) status = XXWIDGETS_UNAVAILABLE;
    if (status != XXWIDGETS_OK) {
        if (!*error) set_error(error, stopped(pd) ? "Strings extraction cancelled" : s->error[0] ? s->error : "Cannot build temporary strings index");
        gui_strings_source_free(s);
        return status;
    }
    *out = s;
    return XXWIDGETS_OK;
}
xxwidgets_status gui_strings_source_open(const char *path, xx_pd_struct *pd, gui_strings_source **out, char **error)
{
    return open_file(path, NULL, 0, 0, pd, out, error);
}
xxwidgets_status gui_strings_source_open_options(const char *path, const xxwidgets_strings_search_options *options, xx_pd_struct *pd, gui_strings_source **out,
                                                 char **error)
{
    return open_file(path, options, 1, 1, pd, out, error);
}
xxwidgets_status gui_strings_source_generated(uint64_t count, xx_pd_struct *pd, gui_strings_source **out, char **error)
{
    gui_strings_source *s;
    xx_strings_scan_options o = xx_strings_scan_defaults();
    xx_strings_scan_stats stats;
    xx_strings_scan_status scan;
    uint64_t verified = 0;
    if (!out || !error || !count || count > INT64_MAX / 5) return XXWIDGETS_INVALID_ARGUMENT;
    *out = NULL;
    *error = NULL;
    s = new_source();
    if (!s) return XXWIDGETS_OUT_OF_MEMORY;
    if (!attach_generated_input(s, count)) {
        gui_strings_source_free(s);
        return XXWIDGETS_OUT_OF_MEMORY;
    }
    s->model.options.minimum_length = 4;
    s->model.options.unicode = 0;
    o.encodings = XX_STRINGS_ASCII;
    o.flags = XX_STRINGS_NO_TEXT;
    scan = xx_strings_scan_device(s->input, &o, pd, verify_generated, &verified, &stats);
    if (scan != XX_STRINGS_SCAN_OK || verified != count || stats.record_count != count || stats.bytes_scanned != s->size) {
        set_error(error, xx_strings_scan_status_name(scan));
        gui_strings_source_free(s);
        return scan_status(scan == XX_STRINGS_SCAN_OK ? XX_STRINGS_SCAN_IO : scan);
    }
    s->model.count = s->model.total_count = count;
    *out = s;
    return XXWIDGETS_OK;
}
/* This callback is invoked on the browser worker. No component of the live
 * provider is destroyed until both extraction/filtering and the final stop
 * observation have succeeded. Swapping heap objects avoids a result-sized
 * stack allocation and keeps the public model and owner addresses stable. */
static xxwidgets_status source_search_ex(void *user, xxwidgets_strings_search_options *options, const char *filter, const char *const columns[7], xx_pd_struct *pd,
                                         uint64_t *matched)
{
    gui_strings_source *s = user, *candidate = NULL;
    xxwidgets_strings_search_options next;
    xxwidgets_status status;
    char *error = NULL;
    uint64_t result = 0;
    size_t i;
    unsigned char *left, *right;
    if (!s || !matched || !valid_options(options) || options->format >= s->model.format_count || options->map_mode >= s->model.map_mode_count)
        return XXWIDGETS_INVALID_ARGUMENT;
    source_error(s, "");
    next = *options;
    if (!next.format) next.map_mode = 0;
    if (s->generated_count) {
        candidate = new_source();
        status = candidate && attach_generated_input(candidate, s->generated_count) ? XXWIDGETS_OK : XXWIDGETS_OUT_OF_MEMORY;
        if (status == XXWIDGETS_OK) {
            candidate->configured = 1;
            candidate->model.options = next;
            if (!next.ansi || next.unicode || next.minimum_length > 4) {
                candidate->generated = 0;
                status = build_index(candidate, pd);
            }
        }
    } else status = open_file(s->path, &next, 1, 0, pd, &candidate, &error);
    if (status == XXWIDGETS_OK) status = source_search(candidate, filter, columns, pd, &result);
    if (status == XXWIDGETS_OK && stopped(pd)) status = XXWIDGETS_UNAVAILABLE;
    if (status != XXWIDGETS_OK) {
        source_error(s, error                              ? error
                        : candidate && candidate->error[0] ? candidate->error
                        : stopped(pd)                      ? "Strings search cancelled"
                                                           : "Cannot configure strings search");
        xx_rt_free(error);
        gui_strings_source_free(candidate);
        return status;
    }
    left = (unsigned char *)s;
    right = (unsigned char *)candidate;
    for (i = 0; i < sizeof(*s); ++i) {
        unsigned char temporary = left[i];
        left[i] = right[i];
        right[i] = temporary;
    }
    repair_model(s);
    repair_model(candidate);
    gui_strings_source_free(candidate);
    xx_rt_free(error);
    *options = s->model.options;
    *matched = result;
    return XXWIDGETS_OK;
}
const char *gui_strings_source_error(const gui_strings_source *s)
{
    return s ? s->error : "";
}
xxwidgets_strings_source *gui_strings_source_model(gui_strings_source *s)
{
    return s ? &s->model : NULL;
}
uint64_t gui_strings_source_size(const gui_strings_source *s)
{
    return s ? s->size : 0;
}
void gui_strings_source_free(gui_strings_source *s)
{
    size_t i;
    if (!s) return;
    for (i = 0; i < XXWIDGETS_STRINGS_PAGE_SIZE; ++i) xx_buf_free(s->text + i);
    if (s->input) xx_io_close(s->input);
    if (s->index) xx_io_close(s->index);
    if (s->ranges) xx_io_close(s->ranges);
    xx_rt_free(s->map);
    xx_rt_free(s->path);
    xx_rt_free(s);
}
