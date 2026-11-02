/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "cdie_file_info.h"
#include "../app/cdie_app.h"
#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/formats/executable/pe/xx_pe.h>
#include <xxfclib/formats/executable/elf/xx_elf.h>
#include <xxfclib/formats/executable/macho/xx_macho.h>
#include <xxfclib/formats/executable/machofat/xx_machofat.h>
#include <xxfclib/data/xx_data.h>
#include <xxfclib/strings/xx_string.h>
#include <xxfclib/terminal/xx_terminal.h>
#include <limits.h>

#define INFO_MAX_STRUCTURES 65536U
#define INFO_MAX_FIELDS 262144U
#define INFO_MAX_OUTPUT (64U * 1024U * 1024U)

typedef struct info_reader {
    xx_pe pe;
    xx_elf elf;
    xx_macho macho;
    xx_machofat fat;   /* fat (universal) Mach-O: validates the table... */
    xx_macho slice;    /* ...and reads the opened slice through slice_device */
    xx_io_device *slice_device;
    int fat_ready, slice_ready;
    Abstractformat *format;
    const char *family;
    const char *type;
    uint32_t last_id;
} info_reader;

/* The fat table. die_engine's xxfclib has no MACHOFAT DataStructs, so its
 * fat_header and fat_arch rows are produced here (ids local to this file). */
#define INFO_FAT_HEADER 1U
#define INFO_FAT_ARCH 2U
#define INFO_FAT_ARCH_64 3U
typedef struct info_fat {
    bool big, wide;
    uint32_t magic, count, stride, chosen;
    int64_t slice_offset, slice_size;
} info_fat;

/* One walk over a file's DataStructs. In a fat Mach-O the opened slice's
 * DataStructs take that slice's place; their offsets, relative to the slice,
 * are moved by the slice offset. */
typedef struct info_walk {
    const cdie_file_info_visitor *visitor;
    void *user;
    xx_pd_struct *progress;
    int inventory;
    Abstractformat *selected_format; /* fat: a fat table id when this is fat_tag */
    uint32_t selected_id;
    const char *field_filter;
    Abstractformat *slice, *fat_tag;
    int found;
    size_t structures, visited, field_count, matched_fields;
    const char *message;
} info_walk;

typedef struct info_fat_field {
    const char *name, *type;
    int64_t offset, size;
    uint64_t value;
    int hex;
    xx_data_struct_record_property_t property;
} info_fat_field;

typedef struct info_writer {
    xx_buf_t buffer;
    cdie_output_format output;
    int first_structure;
    int first_field;
    const char *path;
    const char *structure;
} info_writer;

static xxfc_status_t info_sink(void *context, const char *data, size_t size)
{
    xx_buf_t *buffer = context;
    if (size > INFO_MAX_OUTPUT || buffer->size > INFO_MAX_OUTPUT - size) {
        buffer->failed = true;
        return XXFC_ERR_OUT_OF_BOUNDS;
    }
    return xx_buf_append(buffer, data, size) ? XXFC_OK : XXFC_ERR_OUT_OF_MEMORY;
}

static void escaped(info_writer *writer, const char *text)
{
    xx_terminal_writer terminal;
    xx_terminal_writer_init(&terminal, info_sink, &writer->buffer);
    if (writer->output == CDIE_OUTPUT_JSON)
        xx_terminal_writer_json_string(&terminal, text ? text : "");
    else xx_terminal_writer_xml_attribute(&terminal, text ? text : "");
    if (xx_terminal_writer_status(&terminal) != XXFC_OK) writer->buffer.failed = true;
}

static void cell(info_writer *writer, const char *text)
{
    const char *p = text ? text : "";
    xx_buf_append_char(&writer->buffer, '"');
    for (; *p; ++p) {
        if (*p == '"') xx_buf_append_char(&writer->buffer, '"');
        xx_buf_append_char(&writer->buffer, *p);
    }
    xx_buf_append_char(&writer->buffer, '"');
}

static void row(info_writer *writer, const char *structure, int64_t offset, int64_t size, uint64_t count,
    const char *field, const char *type, const char *raw, const char *value)
{
    char separator = writer->output == CDIE_OUTPUT_CSV ? ';' : '\t';
    cell(writer, writer->path); xx_buf_append_char(&writer->buffer, separator);
    cell(writer, structure); xx_buf_append_char(&writer->buffer, separator);
    xx_buf_appendf(&writer->buffer, "%lld%c%lld%c", (long long)offset, separator, (long long)size, separator);
    if (count) xx_buf_appendf(&writer->buffer, "%llu", (unsigned long long)count);
    xx_buf_append_char(&writer->buffer, separator);
    cell(writer, field); xx_buf_append_char(&writer->buffer, separator);
    cell(writer, type); xx_buf_append_char(&writer->buffer, separator);
    cell(writer, raw); xx_buf_append_char(&writer->buffer, separator);
    cell(writer, value); xx_buf_append_char(&writer->buffer, '\n');
}

static void begin_document(info_writer *writer, const char *path, const char *type, int64_t size)
{
    xx_buf_t *buffer = &writer->buffer;
    writer->first_structure = 1;
    writer->path = path;
    if (writer->output == CDIE_OUTPUT_JSON) {
        xx_buf_append_str(buffer, "{\"path\":"); escaped(writer, path);
        xx_buf_append_str(buffer, ",\"filetype\":"); escaped(writer, type);
        xx_buf_appendf(buffer, ",\"size\":%lld,\"structures\":[", (long long)size);
    } else if (writer->output == CDIE_OUTPUT_XML) {
        xx_buf_append_str(buffer, "<file-info path=\""); escaped(writer, path);
        xx_buf_append_str(buffer, "\" filetype=\""); escaped(writer, type);
        xx_buf_appendf(buffer, "\" size=\"%lld\">\n", (long long)size);
    } else if (writer->output == CDIE_OUTPUT_CSV || writer->output == CDIE_OUTPUT_TSV) {
        char separator = writer->output == CDIE_OUTPUT_CSV ? ';' : '\t';
        xx_buf_appendf(buffer, "Path%cStructure%cOffset%cSize%cCount%cField%cType%cRaw%cValue\n",
            separator, separator, separator, separator, separator, separator, separator, separator);
    } else {
        xx_buf_appendf(buffer, "%s\n    File: %s\n    Size: %lld\n", type, path ? path : "", (long long)size);
    }
}

static void begin_structure(info_writer *writer, const char *name, const xx_data_struct *item)
{
    xx_buf_t *buffer = &writer->buffer;
    writer->first_field = 1;
    if (writer->output == CDIE_OUTPUT_JSON) {
        if (!writer->first_structure) xx_buf_append_char(buffer, ',');
        xx_buf_append_str(buffer, "{\"name\":"); escaped(writer, name);
        xx_buf_appendf(buffer, ",\"offset\":%lld,\"size\":%lld,\"count\":%llu,\"fields\":[",
            (long long)item->offset, (long long)item->total_size, (unsigned long long)item->count);
    } else if (writer->output == CDIE_OUTPUT_XML) {
        xx_buf_append_str(buffer, "  <structure name=\""); escaped(writer, name);
        xx_buf_appendf(buffer, "\" offset=\"%lld\" size=\"%lld\" count=\"%llu\">\n",
            (long long)item->offset, (long long)item->total_size, (unsigned long long)item->count);
    } else if (writer->output == CDIE_OUTPUT_CSV || writer->output == CDIE_OUTPUT_TSV) {
        row(writer, name, item->offset, item->total_size, item->count, "", "", "", "");
    } else {
        xx_buf_appendf(buffer, "    %s [Offset=0x%llx, Size=0x%llx, Count=%llu]\n", name,
            (unsigned long long)item->offset, (unsigned long long)item->total_size, (unsigned long long)item->count);
    }
    writer->first_structure = 0;
}

static void end_structure(info_writer *writer)
{
    if (writer->output == CDIE_OUTPUT_JSON) xx_buf_append_str(&writer->buffer, "]}");
    else if (writer->output == CDIE_OUTPUT_XML) xx_buf_append_str(&writer->buffer, "  </structure>\n");
}

static int emit_field(info_writer *writer, const char *structure, const xx_data_struct_record *record)
{
    char *name = xx_str_unicode_to_utf8(record->name ? record->name : L"");
    char *type = xx_str_unicode_to_utf8(record->type ? record->type : L"");
    char *raw = xx_str_unicode_to_utf8(record->raw_value ? record->raw_value : L"");
    char *value = xx_str_unicode_to_utf8(record->display_value ? record->display_value : L"");
    xx_buf_t *buffer = &writer->buffer;
    int ok = name && type && raw && value;
    if (!ok) goto done;
    if (writer->output == CDIE_OUTPUT_JSON) {
        if (!writer->first_field) xx_buf_append_char(buffer, ',');
        xx_buf_append_str(buffer, "{\"name\":"); escaped(writer, name);
        xx_buf_append_str(buffer, ",\"type\":"); escaped(writer, type);
        xx_buf_appendf(buffer, ",\"offset\":%lld,\"size\":%lld,\"raw\":", (long long)record->offset, (long long)record->size);
        escaped(writer, raw); xx_buf_append_str(buffer, ",\"value\":"); escaped(writer, value);
        xx_buf_append_char(buffer, '}');
    } else if (writer->output == CDIE_OUTPUT_XML) {
        xx_buf_append_str(buffer, "    <field name=\""); escaped(writer, name);
        xx_buf_append_str(buffer, "\" type=\""); escaped(writer, type);
        xx_buf_appendf(buffer, "\" offset=\"%lld\" size=\"%lld\" raw=\"", (long long)record->offset, (long long)record->size);
        escaped(writer, raw); xx_buf_append_str(buffer, "\" value=\""); escaped(writer, value);
        xx_buf_append_str(buffer, "\"/>\n");
    } else if (writer->output == CDIE_OUTPUT_CSV || writer->output == CDIE_OUTPUT_TSV) {
        row(writer, structure, record->offset, record->size, 0, name, type, raw, value);
    } else xx_buf_appendf(buffer, "        %s: %s [%s, +0x%llx, %lld bytes, raw=%s]\n",
        name, value, type, (unsigned long long)record->offset, (long long)record->size, raw);
    writer->first_field = 0;
    ok = xx_buf_ok(buffer) && buffer->size <= INFO_MAX_OUTPUT;
done:
    xx_str_free(name); xx_str_free(type); xx_str_free(raw); xx_str_free(value);
    return ok;
}

static void select_family(info_reader *reader, int family)
{
    reader->format = family == 0 ? &reader->pe.format : family == 1 ? &reader->elf.format : &reader->macho.format;
    reader->family = family == 0 ? "PE" : family == 1 ? "ELF" : "MACHO";
    reader->last_id = family == 0 ? XX_PE_DATA_STRUCT_LAST : family == 1 ? XX_ELF_DATA_STRUCT_LAST : XX_MACHO_DATA_STRUCT_LAST;
}

/* fat_arch cputype of the architecture this build runs as; 0 when none. */
static uint32_t host_cpu_type(void)
{
#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
    return UINT32_C(0x01000007);
#elif defined(__aarch64__) || defined(_M_ARM64)
    return UINT32_C(0x0100000c);
#elif defined(__i386__) || defined(_M_IX86)
    return 7U;
#elif defined(__arm__) || defined(_M_ARM)
    return 12U;
#elif defined(__powerpc64__) || defined(__ppc64__)
    return UINT32_C(0x01000012);
#elif defined(__powerpc__) || defined(__ppc__)
    return 18U;
#else
    return 0U;
#endif
}

/* One fat_arch entry: cputype, offset and size. */
static int fat_entry(xx_io_device *device, const info_fat *fat, uint32_t index, uint8_t *entry,
    uint32_t *cpu, int64_t *offset, int64_t *size)
{
    if (!xx_io_read_at(device, 8 + (int64_t)index * fat->stride, entry, fat->stride)) return 0;
    *cpu = xx_data_get_u32(entry, fat->stride, 0, fat->big);
    *offset = (int64_t)(fat->wide ? xx_data_get_u64(entry, fat->stride, 8, fat->big) : xx_data_get_u32(entry, fat->stride, 8, fat->big));
    *size = (int64_t)(fat->wide ? xx_data_get_u64(entry, fat->stride, 16, fat->big) : xx_data_get_u32(entry, fat->stride, 12, fat->big));
    return *offset >= 0 && *size > 0;
}

/* Reads the table and picks the slice to open: the requested index, else the
 * first slice built for the host architecture, else the first. The caller has
 * run xx_machofat_check_is_valid (1..4096 entries, ranges inside the file). */
static int fat_read(xx_io_device *device, int requested, info_fat *fat)
{
    uint8_t header[8], entry[32];
    uint32_t i, cpu, host = host_cpu_type();
    int64_t offset, size;
    int chosen = requested;
    if (!xx_io_read_at(device, 0, header, sizeof(header))) return 0;
    fat->magic = xx_data_get_u32(header, sizeof(header), 0, true);
    fat->big = fat->magic == UINT32_C(0xcafebabe) || fat->magic == UINT32_C(0xcafebabf);
    fat->wide = fat->magic == UINT32_C(0xcafebabf) || fat->magic == UINT32_C(0xbfbafeca);
    fat->magic = xx_data_get_u32(header, sizeof(header), 0, fat->big);
    fat->count = xx_data_get_u32(header, sizeof(header), 4, fat->big);
    fat->stride = fat->wide ? 32U : 20U;
    if (!fat->count || fat->count > 4096U || (requested >= 0 && (uint32_t)requested >= fat->count)) return 0;
    for (i = 0; chosen < 0 && host && i < fat->count; ++i) {
        if (!fat_entry(device, fat, i, entry, &cpu, &offset, &size)) return 0;
        if (cpu == host) chosen = (int)i;
    }
    fat->chosen = chosen < 0 ? 0U : (uint32_t)chosen;
    return fat_entry(device, fat, fat->chosen, entry, &cpu, &fat->slice_offset, &fat->slice_size);
}

/* A fat table row, shaped like the native DataStructs: hex identifiers and
 * offsets, decimal counts and sizes, the raw value in fixed-width hex. */
static int fat_structure(info_walk *walk, const char *name, uint32_t id, const xx_data_struct *item,
    const info_fat_field *fields, size_t count)
{
    const cdie_file_info_visitor *visitor = walk->visitor;
    size_t i, k;
    if (++walk->visited > INFO_MAX_STRUCTURES) { walk->message = "DataStructs structure limit exceeded"; return 0; }
    if (walk->selected_id && (walk->selected_format != walk->fat_tag || id != walk->selected_id)) return 1;
    walk->found = 1;
    if (++walk->structures > INFO_MAX_STRUCTURES) { walk->message = "DataStructs structure limit exceeded"; return 0; }
    if (!visitor->begin_structure(name, item, walk->user)) goto collect_failure;
    for (i = 0; !walk->inventory && i < count; ++i) {
        const info_fat_field *field = fields + i;
        char raw[24], value[24];
        wchar_t wide_name[16], wide_type[16], wide_raw[24], wide_value[24];
        xx_data_struct_record record;
        if (walk->field_filter && xx_rt_strcmp(field->name, walk->field_filter)) continue;
        if (++walk->field_count > INFO_MAX_FIELDS) { walk->message = "DataStructs field limit exceeded"; return 0; }
        if (field->size == 8) xx_rt_snprintf(raw, sizeof(raw), "0x%016llX", (unsigned long long)field->value);
        else xx_rt_snprintf(raw, sizeof(raw), "0x%08X", (unsigned)field->value);
        if (field->hex) xx_rt_snprintf(value, sizeof(value), "%s", raw);
        else xx_rt_snprintf(value, sizeof(value), "%llu", (unsigned long long)field->value);
        for (k = 0; k < 15 && field->name[k]; ++k) wide_name[k] = (wchar_t)field->name[k];
        wide_name[k] = 0;
        for (k = 0; k < 15 && field->type[k]; ++k) wide_type[k] = (wchar_t)field->type[k];
        wide_type[k] = 0;
        for (k = 0; k < 23 && raw[k]; ++k) wide_raw[k] = (wchar_t)raw[k];
        wide_raw[k] = 0;
        for (k = 0; k < 23 && value[k]; ++k) wide_value[k] = (wchar_t)value[k];
        wide_value[k] = 0;
        xx_rt_memset(&record, 0, sizeof(record));
        record.name = wide_name; record.type = wide_type; record.raw_value = wide_raw; record.display_value = wide_value;
        record.offset = field->offset; record.size = field->size; record.property = field->property;
        ++walk->matched_fields;
        if (!visitor->field(&record, walk->user)) goto collect_failure;
    }
    if (!visitor->end_structure(walk->user)) goto collect_failure;
    return 1;
collect_failure:
    walk->message = "File Information collector failed";
    return 0;
}

static int walk_structures(info_walk *walk, Abstractformat *format, int64_t bias);

/* fat_header, the fat_arch table (one row per entry, or one table row in an
 * inventory), then each slice in table order: the opened one as its own
 * DataStructs, the others as RAW_DATA. */
static int walk_fat(info_walk *walk, xx_io_device *device, const info_fat *fat)
{
    const char *arch = fat->wide ? "fat_arch_64" : "fat_arch";
    uint32_t arch_id = fat->wide ? INFO_FAT_ARCH_64 : INFO_FAT_ARCH, i, cpu;
    uint8_t entry[32];
    int64_t offset, size;
    xx_data_struct item;
    info_fat_field fields[6] = {
        {"magic", "uint32", 0, 4, 0, 1, XX_DATA_STRUCT_RECORD_PROPERTY_SIGNATURE},
        {"nfat_arch", "uint32", 4, 4, 0, 0, XX_DATA_STRUCT_RECORD_PROPERTY_COUNT}
    };
    xx_rt_memset(&item, 0, sizeof(item));
    item.address = -1; item.type = XX_DATA_STRUCT_TYPE_STRUCT;
    item.entry_size = item.total_size = 8; item.count = 1;
    fields[0].value = fat->magic; fields[1].value = fat->count;
    if (!fat_structure(walk, "fat_header", INFO_FAT_HEADER, &item, fields, 2)) return 0;
    item.offset = 8; item.entry_size = fat->stride; item.count = walk->inventory ? fat->count : 1;
    item.total_size = (int64_t)fat->stride * (int64_t)item.count;
    if (walk->inventory && !fat_structure(walk, arch, arch_id, &item, NULL, 0)) return 0;
    /* fat_arch: cputype, cpusubtype, offset, size, align (+ reserved in fat_arch_64);
     * offset and size are 32-bit, or 64-bit in fat_arch_64. */
    fields[0].name = "cputype"; fields[0].hex = 1; fields[0].property = XX_DATA_STRUCT_RECORD_PROPERTY_ID;
    fields[1].name = "cpusubtype"; fields[1].offset = 4; fields[1].hex = 1; fields[1].property = XX_DATA_STRUCT_RECORD_PROPERTY_ID;
    for (i = 2; i < 6; ++i) {
        fields[i].type = i < 4 && fat->wide ? "uint64" : "uint32";
        fields[i].size = i < 4 && fat->wide ? 8 : 4;
        fields[i].offset = fields[i - 1].offset + fields[i - 1].size;
    }
    fields[2].name = "offset"; fields[2].hex = 1; fields[2].property = XX_DATA_STRUCT_RECORD_PROPERTY_OFFSET;
    fields[3].name = "size"; fields[3].property = XX_DATA_STRUCT_RECORD_PROPERTY_SIZE;
    fields[4].name = "align"; fields[4].property = XX_DATA_STRUCT_RECORD_PROPERTY_NONE;
    fields[5].name = "reserved"; fields[5].property = XX_DATA_STRUCT_RECORD_PROPERTY_RESERVED;
    for (i = 0; !walk->inventory && i < fat->count; ++i) {
        if (!fat_entry(device, fat, i, entry, &cpu, &offset, &size)) { walk->message = "Cannot read the fat_arch table"; return 0; }
        fields[0].value = cpu;
        fields[1].value = xx_data_get_u32(entry, fat->stride, 4, fat->big);
        fields[2].value = (uint64_t)offset; fields[3].value = (uint64_t)size;
        fields[4].value = xx_data_get_u32(entry, fat->stride, (size_t)fields[4].offset, fat->big);
        fields[5].value = fat->wide ? xx_data_get_u32(entry, fat->stride, 28, fat->big) : 0;
        item.offset = 8 + (int64_t)i * fat->stride;
        if (!fat_structure(walk, arch, arch_id, &item, fields, fat->wide ? 6 : 5)) return 0;
    }
    for (i = 0; i < fat->count; ++i) {
        if (!fat_entry(device, fat, i, entry, &cpu, &offset, &size)) { walk->message = "Cannot read the fat_arch table"; return 0; }
        if (i == fat->chosen && walk->slice) {
            if (!walk_structures(walk, walk->slice, offset)) return 0;
            continue;
        }
        item.id = XX_DATA_STRUCT_ID_RAW_DATA; item.type = XX_DATA_STRUCT_TYPE_RAW_DATA;
        item.offset = offset; item.entry_size = item.total_size = size; item.count = 1;
        if (!fat_structure(walk, "RAW_DATA", XX_DATA_STRUCT_ID_RAW_DATA, &item, NULL, 0)) return 0;
    }
    return 1;
}

static int walk_structures(info_walk *walk, Abstractformat *format, int64_t bias)
{
    const cdie_file_info_visitor *visitor = walk->visitor;
    xx_data_struct_state *state;
    xx_data_struct_record_state *fields = NULL;
    const xx_data_struct *item;
    const xx_data_struct_record *record;
    int ok = 0;
    state = xx_format_create_data_structs_reading(format, walk->progress);
    if (!state) return 0;
    while ((item = xx_format_get_current_data_struct(format, state)) != NULL) {
        const char *name;
        if (++walk->visited > INFO_MAX_STRUCTURES) { walk->message = "DataStructs structure limit exceeded"; goto done; }
        if (!walk->selected_id || (format == walk->selected_format && item->id == walk->selected_id)) {
            uint64_t occurrence, occurrences = 1;
            name = xx_format_data_struct_id_to_string(format, item->id);
            if (!name) name = "Unknown";
            walk->found = 1;
            if (!walk->inventory && item->type != XX_DATA_STRUCT_TYPE_RAW_DATA && item->count > 1 && item->entry_size > 0) {
                if (item->total_size <= 0 || item->count > (uint64_t)(item->total_size / item->entry_size)) {
                    walk->message = "Invalid DataStructs table extent"; goto done;
                }
                occurrences = item->count;
            }
            for (occurrence = 0; occurrence < occurrences; ++occurrence) {
                xx_data_struct entry = *item, shown;
                if (++walk->structures > INFO_MAX_STRUCTURES) { walk->message = "DataStructs structure limit exceeded"; goto done; }
                if (occurrences > 1) {
                    int64_t delta = (int64_t)occurrence * item->entry_size;
                    if (item->offset < 0 || item->offset > INT64_MAX - delta) {
                        walk->message = "DataStructs table offset overflow"; goto done;
                    }
                    entry.offset += delta;
                    entry.address = item->address >= 0 && item->address <= INT64_MAX - delta ? item->address + delta : -1;
                    entry.total_size = entry.entry_size;
                    entry.count = 1;
                }
                shown = entry;
                if (bias) {
                    if (entry.offset < 0 || entry.offset > INT64_MAX - bias) { walk->message = "DataStructs table offset overflow"; goto done; }
                    shown.offset += bias;
                }
                if (!visitor->begin_structure(name, &shown, walk->user)) goto collect_failure;
                /* Native RAW_DATA descriptors intentionally have no named-field
                 * schema (DOS stubs, padding, overlays and opaque payloads). */
                if (!walk->inventory && item->type != XX_DATA_STRUCT_TYPE_RAW_DATA) {
                    fields = xx_format_create_data_struct_records_reading(format, &entry, walk->progress);
                    if (!fields) goto done;
                    while ((record = xx_format_get_current_data_struct_record(format, fields)) != NULL) {
                        char *field_name;
                        int match;
                        if (++walk->field_count > INFO_MAX_FIELDS) { walk->message = "DataStructs field limit exceeded"; goto done; }
                        field_name = xx_str_unicode_to_utf8(record->name ? record->name : L"");
                        if (!field_name) { walk->message = "Out of memory"; goto done; }
                        match = !walk->field_filter || !xx_rt_strcmp(field_name, walk->field_filter);
                        xx_str_free(field_name);
                        if (match) {
                            ++walk->matched_fields;
                            if (!visitor->field(record, walk->user)) goto collect_failure;
                        }
                        if (!xx_format_data_struct_record_move_to_next(format, fields, walk->progress)) break;
                    }
                    xx_format_free_data_struct_records_reading(format, fields); fields = NULL;
                }
                if (!visitor->end_structure(walk->user)) goto collect_failure;
            }
        }
        if (!xx_format_data_struct_move_to_next(format, state, walk->progress)) break;
    }
    ok = 1;
    goto done;
collect_failure:
    walk->message = "File Information collector failed";
done:
    if (fields) xx_format_free_data_struct_records_reading(format, fields);
    xx_format_free_data_structs_reading(format, state);
    return ok;
}

int cdie_file_info_visit(const char *path, const char *selector, int inventory,
    const cdie_file_info_visitor *visitor, void *user, xx_pd_struct *progress, char **error)
{
    return cdie_file_info_visit_slice(path, CDIE_FILE_INFO_SLICE_AUTO, selector, inventory, visitor, user, progress, error);
}

int cdie_file_info_visit_slice(const char *path, int slice, const char *selector, int inventory,
    const cdie_file_info_visitor *visitor, void *user, xx_pd_struct *external_progress, char **error)
{
    /* The fat table's names, indexed by INFO_FAT_* - 1. */
    static const char *const fat_names[] = {"fat_header", "fat_arch", "fat_arch_64"};
    info_reader reader;
    info_walk walk;
    info_fat fat_table;
    xx_io_device *device = NULL;
    xx_pd_struct private_progress = {0};
    xx_pd_struct *progress = external_progress ? external_progress : &private_progress;
    const char *message = "Cannot read DataStructs";
    char *selection = NULL, *qualified, *hash;
    int family, found = 0, fat = 0, ok = 0;
    int64_t size = -1;
    if (!error) return 0;
    *error = NULL;
    if (!visitor || !visitor->begin_file || !visitor->begin_structure || !visitor->field || !visitor->end_structure ||
        slice < CDIE_FILE_INFO_SLICE_AUTO) {
        *error = cdie_strdup("Invalid File Information visitor"); return 0;
    }
    xx_rt_memset(&reader, 0, sizeof(reader));
    xx_rt_memset(&walk, 0, sizeof(walk));
    xx_rt_memset(&fat_table, 0, sizeof(fat_table));
    walk.visitor = visitor; walk.user = user; walk.progress = progress; walk.inventory = inventory;
    walk.fat_tag = &reader.fat.format;
    if (xx_pd_is_stopped(progress)) { message = "File Information cancelled"; goto finish; }
    if (path) {
        device = xx_io_file_open(path, "rb");
        if (!device) { message = "Cannot open file"; goto finish; }
        size = xx_io_total_size(device);
        if (size < 0) { message = "Cannot determine file size"; goto finish; }
    } else if (!inventory) { message = "File information requires a target"; goto finish; }
    xx_pe_init(&reader.pe, device, 0);
    xx_elf_init(&reader.elf, device, 0);
    xx_macho_init(&reader.macho, device, 0);
    xx_machofat_init(&reader.fat, device, 0);
    reader.fat_ready = 1;
    if (!path) {
        if (!visitor->begin_file("", "Supported DataStructs", -1, user)) goto collect_failure;
        for (family = 0; family < 4; ++family) {
            uint32_t id, last;
            select_family(&reader, family < 3 ? family : 2);
            last = family < 3 ? reader.last_id : (uint32_t)(sizeof(fat_names) / sizeof(fat_names[0]));
            for (id = 1; id <= last; ++id) {
                const char *name = family < 3 ? xx_format_data_struct_id_to_string(reader.format, id) : fat_names[id - 1];
                char qualified_name[256];
                xx_data_struct catalog = {0};
                if (!name || !*name) continue;
                catalog.offset = catalog.address = catalog.total_size = -1;
                xx_rt_snprintf(qualified_name, sizeof(qualified_name), "%s::%s", reader.family, name);
                if (!visitor->begin_structure(qualified_name, &catalog, user) || !visitor->end_structure(user)) goto collect_failure;
            }
        }
        ok = 1; goto finish;
    }
    for (family = 0; family < 3; ++family) {
        select_family(&reader, family);
        if (reader.format->check_is_valid(reader.format, NULL)) { found = 1; break; }
    }
    if (!found && reader.fat.format.check_is_valid(&reader.fat.format, NULL)) {
        /* Selectors stay MACHO:: for the fat table and the slice alike. */
        select_family(&reader, 2);
        found = fat = 1;
    }
    if (!found) { message = "File information supports PE, ELF and Mach-O only"; goto finish; }
    if (fat) {
        if (!fat_read(device, slice, &fat_table)) { message = "Mach-O slice is not present in this file"; goto finish; }
        reader.slice_device = xx_io_sub_open_ro(device, fat_table.slice_offset, fat_table.slice_size);
        if (!reader.slice_device) { message = "Out of memory"; goto finish; }
        xx_macho_init(&reader.slice, reader.slice_device, 0);
        reader.slice_ready = 1;
        /* A slice that is not a Mach-O (a static library's archive) stays RAW_DATA. */
        if (reader.slice.format.check_is_valid(&reader.slice.format, NULL)) {
            if (!xx_format_handle_base_info(&reader.slice.format, progress) || !xx_format_has_data_structs(&reader.slice.format)) goto finish;
            walk.slice = &reader.slice.format;
        }
        reader.type = xx_format_file_type_to_string(XX_FILE_TYPE_MACHOFAT);
    } else {
        if (!xx_format_handle_base_info(reader.format, progress) || !xx_format_has_data_structs(reader.format)) goto finish;
        reader.type = xx_format_file_type_to_string(reader.format->file_type);
    }
    if (selector && xx_rt_strcmp(selector, "Info")) {
        selection = cdie_strdup(selector);
        if (!selection) { message = "Out of memory"; goto finish; }
        qualified = xx_rt_strstr(selection, "::");
        if (qualified) {
            *qualified = 0;
            if (xx_rt_strcmp(selection, reader.family)) { message = "Structure belongs to a different format"; goto finish; }
            qualified += 2;
        } else qualified = selection;
        hash = xx_rt_strchr(qualified, '#');
        if (hash) { *hash = 0; walk.field_filter = hash + 1; }
        walk.selected_format = walk.slice ? walk.slice : reader.format;
        walk.selected_id = xx_format_data_struct_string_to_id(walk.selected_format, qualified);
        for (family = 0; fat && !walk.selected_id && family < 3; ++family) {
            if (!xx_rt_strcmp(qualified, fat_names[family])) { walk.selected_format = walk.fat_tag; walk.selected_id = (uint32_t)family + 1U; }
        }
        if (!walk.selected_id || (walk.field_filter && !*walk.field_filter)) { message = "Unknown structure or empty field selector"; goto finish; }
    }
    if (!visitor->begin_file(path, reader.type, size, user)) goto collect_failure;
    if (!(fat ? walk_fat(&walk, device, &fat_table) : walk_structures(&walk, reader.format, 0))) {
        if (walk.message) message = walk.message;
        goto finish;
    }
    if (progress->last_error || progress->is_stop) goto finish;
    if (walk.selected_id && !walk.found) { message = "Structure is not present in this file"; goto finish; }
    if (walk.field_filter && !walk.matched_fields) { message = "Field is not present in the selected structure"; goto finish; }
    ok = 1;
    goto finish;
collect_failure:
    message = "File Information collector failed";
finish:
    /* A stop requested by an observer is returned by is_stopped without
     * necessarily setting the sticky flag. Preserve it for GUI callers. */
    if (xx_pd_is_stopped(progress)) {
        xx_pd_stop(progress);
        ok = 0;
        message = "File Information cancelled";
    }
    xx_pe_destroy(&reader.pe); xx_elf_destroy(&reader.elf); xx_macho_destroy(&reader.macho);
    if (reader.slice_ready) xx_macho_destroy(&reader.slice);
    if (reader.fat_ready) xx_machofat_destroy(&reader.fat);
    if (reader.slice_device) xx_io_close(reader.slice_device);
    if (device) xx_io_close(device);
    xx_rt_free(selection);
    if (!ok) *error = cdie_strdup(progress->error_string[0] ? progress->error_string : message);
    return ok;
}

static int format_begin_file(const char *path, const char *type, int64_t size, void *user)
{
    info_writer *writer = user;
    begin_document(writer, path, type, size);
    return xx_buf_ok(&writer->buffer);
}
static int format_begin_structure(const char *name, const xx_data_struct *item, void *user)
{
    info_writer *writer = user;
    writer->structure = name;
    begin_structure(writer, name, item);
    return xx_buf_ok(&writer->buffer) && writer->buffer.size <= INFO_MAX_OUTPUT;
}
static int format_field(const xx_data_struct_record *record, void *user)
{
    info_writer *writer = user;
    return emit_field(writer, writer->structure, record);
}
static int format_end_structure(void *user)
{
    info_writer *writer = user;
    end_structure(writer);
    return xx_buf_ok(&writer->buffer) && writer->buffer.size <= INFO_MAX_OUTPUT;
}

int cdie_file_info(const char *path, const char *selector, int inventory,
    cdie_output_format output, char **text, char **error)
{
    static const cdie_file_info_visitor visitor = {
        format_begin_file, format_begin_structure, format_field, format_end_structure
    };
    info_writer writer;
    int ok;
    if (!text || !error) return 0;
    *text = NULL;
    xx_buf_init(&writer.buffer); writer.output = output;
    ok = cdie_file_info_visit(path, selector, inventory, &visitor, &writer, NULL, error);
    if (ok) {
        if (output == CDIE_OUTPUT_JSON) xx_buf_append_str(&writer.buffer, "]}\n");
        else if (output == CDIE_OUTPUT_XML) xx_buf_append_str(&writer.buffer, "</file-info>\n");
        *text = xx_buf_detach(&writer.buffer, NULL);
        ok = *text != NULL;
        if (!ok) *error = cdie_strdup("Cannot allocate File Information report");
    }
    xx_buf_free(&writer.buffer);
    return ok;
}
