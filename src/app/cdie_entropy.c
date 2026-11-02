/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "cdie_entropy.h"
#include "cdie_app.h"
#include <xxfclib/formats/executable/pe/xx_pe.h>
#include <xxfclib/formats/executable/elf/xx_elf.h>
#include <xxfclib/formats/executable/macho/xx_macho.h>
#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/terminal/xx_terminal.h>
#include <xxfclib/global/xx_global.h>

static double histogram_entropy(const uint64_t counts[256], uint64_t size)
{
    double sum = 0, result;
    size_t i;
    if (!size) return 0;
    for (i = 0; i < 256; ++i) if (counts[i]) {
        double p = (double)counts[i] / (double)size;
        sum -= p * xx_rt_log(p) * 1.44269504088896340736;
    }
    result = sum < 0 ? 0 : sum > 8 ? 8 : sum;
    return result;
}
static int cancelled(xx_pd_struct *progress)
{
    if (!xx_pd_is_stopped(progress)) return 0;
    xx_pd_stop(progress); /* Observer cancellation stays sticky after leaving a level. */
    return 1;
}
/* Each read is the general buffer size (diec --buffersize, the GUI's
 * Feature/ReadBufferSize), no larger than the input. The default size is used
 * when that much memory is not available, so the setting never changes a value. */
static unsigned char *read_buffer(uint64_t size, size_t *capacity)
{
    size_t wanted = xx_get_buffer_size();
    unsigned char *buffer;
    if ((uint64_t)wanted > size) wanted = size ? (size_t)size : 1;
    buffer = xx_rt_malloc(wanted);
    if (!buffer && wanted > XX_DEFAULT_BUFFER_SIZE) buffer = xx_rt_malloc(wanted = XX_DEFAULT_BUFFER_SIZE);
    *capacity = buffer ? wanted : 0;
    return buffer;
}
static int read_histogram(xx_io_device *device, uint64_t offset, uint64_t size,
    uint64_t counts[256], unsigned char *buffer, size_t capacity, xx_pd_struct *progress, int level)
{
    uint64_t done = 0;
    xx_rt_memset(counts, 0, 256 * sizeof(*counts));
    while (done < size) {
        size_t i, chunk = size - done < capacity ? (size_t)(size - done) : capacity;
        if (cancelled(progress)) return 0;
        if (!xx_io_read_at(device, (int64_t)(offset + done), buffer, chunk)) return 0;
        for (i = 0; i < chunk; ++i) ++counts[buffer[i]];
        done += chunk;
        xx_pd_set_current(progress, level, done);
    }
    return !cancelled(progress);
}
/* Executable names are fixed-width byte strings, not guaranteed UTF-8.
 * Preserve unusual bytes as visible escapes in both terminal and GUI reports. */
static void display_name(char *out, size_t capacity, const char *name)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    const unsigned char *p = (const unsigned char *)name;
    while (*p && n + 1 < capacity) {
        unsigned char c = *p++;
        if (c >= 32 && c < 127) out[n++] = (char)c;
        else {
            if (n + 4 >= capacity) break;
            out[n++] = '\\'; out[n++] = 'x'; out[n++] = hex[c >> 4]; out[n++] = hex[c & 15];
        }
    }
    out[n] = 0;
}
int cdie_entropy_read(const char *path, xx_pd_struct *progress, cdie_entropy_data **out, char **error)
{
    xx_io_device *device = NULL;
    cdie_entropy_data *data = NULL;
    unsigned char *buffer = NULL;
    xx_pe pe; xx_elf elf; xx_macho macho;
    Abstractformat *formats[3], *format = NULL;
    const xx_memory_map *map;
    const char *message = "Cannot read entropy data";
    uint64_t counts[256];
    int64_t size;
    size_t i, j, capacity = 0;
    int ok = 0, initialized = 0, level = -1;
    if (!out || !error) return 0;
    *out = NULL; *error = NULL;
    if (!path || !*path) { message = "Entropy requires a target"; goto finish; }
    if (cancelled(progress)) goto finish;
    device = xx_io_file_open(path, "rb");
    if (!device) { message = "Cannot open file"; goto finish; }
    size = xx_io_total_size(device);
    if (size < 0) { message = "Cannot determine file size"; goto finish; }
    data = xx_rt_calloc(1, sizeof(*data)); buffer = read_buffer((uint64_t)size, &capacity);
    if (!data || !buffer) { message = "Cannot allocate entropy data"; goto finish; }
    data->size = (uint64_t)size;
    xx_rt_snprintf(data->file_type, sizeof(data->file_type), "Binary");
    xx_pe_init(&pe, device, 0); xx_elf_init(&elf, device, 0); xx_macho_init(&macho, device, 0); initialized = 1;
    formats[0] = &pe.format; formats[1] = &elf.format; formats[2] = &macho.format;
    for (i = 0; i < 3; ++i) if (formats[i]->check_is_valid(formats[i], progress)) { format = formats[i]; break; }
    if (format) {
        if (!xx_format_handle_base_info(format, progress)) goto finish;
        xx_rt_snprintf(data->file_type, sizeof(data->file_type), "%s", xx_format_file_type_to_string(format->file_type));
        map = xx_format_get_memory_map(format, XX_MEMORY_MAP_MODE_UNKNOWN, progress);
        if (!map && !xx_pd_is_stopped(progress)) { message = "Cannot read entropy regions"; goto finish; }
        if (map) for (i = 0; i < map->record_count; ++i) {
            const xx_memory_record *record = &map->records[i];
            cdie_entropy_region *region;
            if (record->is_virtual || record->is_invisible || record->offset < 0 || record->size <= 0 || record->offset >= size) continue;
            if (data->region_count == CDIE_ENTROPY_MAX_REGIONS) { message = "Too many entropy regions"; goto finish; }
            region = &data->regions[data->region_count++];
            region->offset = (uint64_t)record->offset;
            region->size = (uint64_t)record->size;
            if (region->size > data->size - region->offset) region->size = data->size - region->offset;
            {
                char name[192];
                if (record->file_part & XX_FILE_PART_SECTION) {
                    const char *label = xx_rt_strchr(record->name, '[');
                    xx_rt_snprintf(name, sizeof(name), "Section (%d)%s%s", record->file_part_number,
                        label ? " " : "", label ? label : "");
                } else xx_rt_snprintf(name, sizeof(name), "%s", record->name[0] ? record->name : "Data");
                display_name(region->name, sizeof(region->name), name);
            }
        }
    }
    if (!data->region_count) {
        data->region_count = 1;
        data->regions[0].size = data->size;
        xx_rt_snprintf(data->regions[0].name, sizeof(data->regions[0].name), "Data");
    }
    data->sample_count = data->size / 512 > CDIE_ENTROPY_MAX_SAMPLES ? CDIE_ENTROPY_MAX_SAMPLES : (size_t)(data->size / 512);
    if (size && !data->sample_count) data->sample_count = 1;
    /* Blocks cover the complete file, including the final remainder. Combine
     * their histograms for the total; no second whole-file read is needed. */
    level = xx_pd_enter_level(progress, data->sample_count + data->region_count, "Calculating entropy");
    for (i = 0; i < data->sample_count; ++i) {
        cdie_entropy_sample *sample = &data->samples[i];
        int bytes_level;
        sample->offset = i * (data->size / data->sample_count);
        sample->size = i + 1 == data->sample_count ? data->size - sample->offset : data->size / data->sample_count;
        bytes_level = xx_pd_enter_level(progress, sample->size, "Reading blocks");
        ok = read_histogram(device, sample->offset, sample->size, counts, buffer, capacity, progress, bytes_level);
        xx_pd_leave_level(progress, bytes_level);
        if (!ok) goto finish;
        sample->entropy = histogram_entropy(counts, sample->size);
        for (j = 0; j < 256; ++j) data->counts[j] += counts[j];
        xx_pd_set_current(progress, level, i + 1);
    }
    ok = 0;
    data->total = histogram_entropy(data->counts, data->size);
    for (i = 0; i < data->region_count; ++i) {
        cdie_entropy_region *region = &data->regions[i];
        int bytes_level, read;
        if (!region->offset && region->size == data->size) {
            region->entropy = data->total;
            xx_pd_set_current(progress, level, data->sample_count + i + 1);
            continue;
        }
        bytes_level = xx_pd_enter_level(progress, region->size, "Reading regions");
        read = read_histogram(device, region->offset, region->size, counts, buffer, capacity, progress, bytes_level);
        xx_pd_leave_level(progress, bytes_level);
        if (!read) goto finish;
        region->entropy = histogram_entropy(counts, region->size);
        xx_pd_set_current(progress, level, data->sample_count + i + 1);
    }
    ok = 1;
finish:
    xx_pd_leave_level(progress, level);
    if (xx_pd_is_stopped(progress)) { xx_pd_stop(progress); ok = 0; message = "Entropy cancelled"; }
    if (initialized) { xx_pe_destroy(&pe); xx_elf_destroy(&elf); xx_macho_destroy(&macho); }
    if (device) xx_io_close(device);
    xx_rt_free(buffer);
    if (ok) *out = data;
    else { xx_rt_free(data); *error = cdie_strdup(message); }
    return ok;
}
static const char *packed(double entropy) { return entropy >= 6.5 ? "packed" : "not packed"; }
static xxfc_status_t sink(void *context, const char *text, size_t size)
{
    return xx_buf_append(context, text, size) ? XXFC_OK : XXFC_ERR_OUT_OF_MEMORY;
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
    for (; *text; ++text) {
        if (*text == '"') xx_buf_append_char(buffer, '"');
        xx_buf_append_char(buffer, *text);
    }
    xx_buf_append_char(buffer, '"');
}
int cdie_entropy_report(const char *path, cdie_output_format format, char **text, char **error)
{
    cdie_entropy_data *data;
    xx_buf_t buffer;
    char total[64], entropy[64];
    size_t i;
    if (!text || !error) return 0;
    *text = NULL;
    if (!cdie_entropy_read(path, NULL, &data, error)) return 0;
    xx_buf_init(&buffer);
    xx_rt_dtoa_shortest(data->total, total, sizeof(total));
    if (format == CDIE_OUTPUT_JSON) xx_buf_append_str(&buffer, "{\"records\":[");
    else if (format == CDIE_OUTPUT_XML) xx_buf_appendf(&buffer, "<fileentropy total=\"%s\" status=\"%s\">\n", total, packed(data->total));
    else if (format != CDIE_OUTPUT_CSV && format != CDIE_OUTPUT_TSV) {
        xx_rt_dtoa_precision(data->total, 6, total, sizeof(total));
        xx_buf_appendf(&buffer, "Total %s: %s\n", total, packed(data->total));
    }
    for (i = 0; i < data->region_count; ++i) {
        const cdie_entropy_region *r = &data->regions[i];
        xx_rt_dtoa_shortest(r->entropy, entropy, sizeof(entropy));
        if (format == CDIE_OUTPUT_JSON) {
            if (i) xx_buf_append_char(&buffer, ',');
            xx_buf_append_str(&buffer, "{\"name\":"); escaped(&buffer, r->name, 1);
            xx_buf_appendf(&buffer, ",\"offset\":%llu,\"size\":%llu,\"entropy\":%s,\"status\":\"%s\"}",
                (unsigned long long)r->offset, (unsigned long long)r->size, entropy, packed(r->entropy));
        } else if (format == CDIE_OUTPUT_XML) {
            xx_buf_append_str(&buffer, "  <record name=\""); escaped(&buffer, r->name, 0);
            xx_buf_appendf(&buffer, "\" offset=\"%llu\" size=\"%llu\" entropy=\"%s\" status=\"%s\"/>\n",
                (unsigned long long)r->offset, (unsigned long long)r->size, entropy, packed(r->entropy));
        } else {
            int table = format == CDIE_OUTPUT_CSV || format == CDIE_OUTPUT_TSV;
            char separator = format == CDIE_OUTPUT_CSV ? ';' : format == CDIE_OUTPUT_TSV ? '\t' : '|';
            xx_rt_dtoa_precision(r->entropy, 6, entropy, sizeof(entropy));
            xx_buf_appendf(&buffer, "%s%llu%c", table ? "" : "  ", (unsigned long long)i, separator);
            if (table) cell(&buffer, r->name); else xx_buf_append_str(&buffer, r->name);
            xx_buf_appendf(&buffer, "%c%llu%c%llu%c%s%s%s\n", separator, (unsigned long long)r->offset,
                separator, (unsigned long long)r->size, separator, entropy, table ? (format == CDIE_OUTPUT_CSV ? ";" : "\t") : ": ", packed(r->entropy));
        }
    }
    if (format == CDIE_OUTPUT_JSON) xx_buf_appendf(&buffer, "],\"status\":\"%s\",\"total\":%s}\n", packed(data->total), total);
    else if (format == CDIE_OUTPUT_XML) xx_buf_append_str(&buffer, "</fileentropy>\n");
    *text = xx_buf_detach(&buffer, NULL); xx_buf_free(&buffer); xx_rt_free(data);
    if (!*text) *error = cdie_strdup("Cannot allocate entropy report");
    return *text != NULL;
}
