/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "cdie_symbols.h"
#include "cdie_app.h"
#include <limits.h>
#include <xxfclib/formats/executable/elf/xx_elf.h>
#include <xxfclib/formats/executable/macho/xx_macho.h>
#include <xxfclib/formats/executable/pe/xx_pe.h>
#include <xxfclib/rt/xx_rt.h>
void cdie_symbols_free(cdie_symbols_data *data)
{
    if (!data) return;
    for (size_t i = 0; i < data->count; ++i) {
        xx_rt_free(data->records[i].file_name);
        xx_rt_free(data->records[i].name);
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
static char *copy_text(cdie_symbols_data *data, const char *text)
{
    static const char hex[] = "0123456789ABCDEF";
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    size_t length = 0, size = 0, pos = 0;
    char *out;
    while (p[length]) {
        if (length == CDIE_SYMBOLS_MAX_NAME) return NULL;
        ++length;
    }
    for (size_t i = 0; i < length;) {
        size_t width = p[i] >= 128 ? utf8_width(p + i, length - i) : p[i] >= 32 && p[i] != 127 ? 1 : 0;
        size += width ? width : 4;
        if (size > CDIE_SYMBOLS_MAX_NAME) return NULL;
        i += width ? width : 1;
    }
    if (size + 1 > CDIE_SYMBOLS_MAX_TEXT - data->text_size) return NULL;
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
static int append(cdie_symbols_data *data, const xx_symbol_record *record)
{
    xx_symbol_record copy;
    if (data->count == CDIE_SYMBOLS_MAX_RECORDS || record->offset < -1 || record->record_offset < -1 ||
        (record->offset >= 0 && (uint64_t)record->offset > data->file_size) || (record->record_offset >= 0 && (uint64_t)record->record_offset >= data->file_size))
        return 0;
    if (data->count == data->capacity) {
        size_t capacity = data->capacity ? data->capacity * 2 : 64;
        xx_symbol_record *rows;
        if (capacity > CDIE_SYMBOLS_MAX_RECORDS) capacity = CDIE_SYMBOLS_MAX_RECORDS;
        rows = xx_rt_realloc(data->records, capacity * sizeof(*rows));
        if (!rows) return 0;
        data->records = rows;
        data->capacity = capacity;
    }
    copy = *record;
    copy.name = copy_text(data, record->name);
    copy.file_name = copy_text(data, record->file_name);
    if (!copy.name || !copy.file_name) {
        xx_rt_free(copy.name);
        xx_rt_free(copy.file_name);
        return 0;
    }
    data->records[data->count++] = copy;
    return 1;
}
static int64_t macho_offset(const xx_macho *macho, uint64_t address)
{
    if (address == UINT64_MAX) return -1;
    for (uint32_t i = 0; i < macho->segment_count; ++i) {
        const xx_macho_segment *segment = macho->segments + i;
        uint64_t delta;
        if (address < segment->virtual_address) continue;
        delta = address - segment->virtual_address;
        if (delta < segment->file_size && segment->file_offset <= (uint64_t)INT64_MAX - delta) return (int64_t)(segment->file_offset + delta);
    }
    return -1;
}
static int read_macho(xx_macho *macho, cdie_symbols_data *data, xx_pd_struct *pd, int level)
{
    xx_macho_inspection inspection = {0};
    int ok = xx_macho_inspect_parse(macho, &inspection, pd);
    for (int i = 0; ok && i < inspection.nSymbolCount; ++i) {
        const xx_macho_inspect_symbol *s = inspection.pSymbols + i;
        xx_symbol_record r = {0};
        if (xx_pd_is_stopped(pd)) {
            ok = 0;
            break;
        }
        r.name = s->pName;
        r.value = s->nValue;
        r.table_index = s->nTableIndex;
        r.record_offset = s->nRecordOffset;
        r.section_number = s->nSection;
        r.native_type = s->nType;
        r.native_storage_class = s->nDescription;
        r.is_debug = (s->nType & 0xe0U) != 0;
        r.is_undefined = s->bUndefined != 0;
        r.is_absolute =
            !r.is_debug && ((s->bFromSymbolTable && (s->nType & 0x0eU) == 2U) || (!s->bFromSymbolTable && s->bFromExportTrie && (s->nExportFlags & 3U) == 2U));
        r.address = s->bFromSymbolTable && !s->bUndefined && ((s->nType & 0x0eU) == 0x0eU || r.is_absolute) ? s->nValue : s->nExportAddress;
        if (r.is_debug) r.address = UINT64_MAX;
        r.offset = r.is_debug || r.is_absolute ? -1 : macho_offset(macho, r.address);
        r.binding = s->nDescription & 0xc0U ? XX_SYMBOL_BINDING_WEAK : s->bExternal ? XX_SYMBOL_BINDING_GLOBAL : XX_SYMBOL_BINDING_LOCAL;
        if (r.is_debug && (s->nType == 0x64U || s->nType == 0x66U)) r.kind = XX_SYMBOL_KIND_FILE;
        if (!append(data, &r)) {
            ok = 0;
            break;
        }
        xx_pd_set_current(pd, level, data->count);
    }
    xx_macho_inspect_free(&inspection);
    return ok;
}
int cdie_symbols_read(const char *path, xx_pd_struct *pd, cdie_symbols_data **out, char **error)
{
    xx_io_device *device = NULL;
    cdie_symbols_data *data = NULL;
    xx_pe pe;
    xx_elf elf;
    xx_macho macho;
    Abstractformat *formats[3], *format = NULL;
    const StreamSymbols *api = NULL;
    xx_symbol_state *state = NULL;
    const xx_symbol_record *record;
    const char *message = "Cannot read symbols: invalid table, read failure or result limit";
    int initialized = 0, ok = 0, level = -1;
    int64_t size;
    if (!out || !error) return 0;
    *out = NULL;
    *error = NULL;
    if (!path || !*path) {
        message = "Symbols requires a target";
        goto done;
    }
    if (xx_pd_is_stopped(pd)) goto done;
    device = xx_io_file_open(path, "rb");
    if (!device) {
        message = "Cannot open file for Symbols";
        goto done;
    }
    size = xx_io_total_size(device);
    if (size < 0) goto done;
    xx_pe_init(&pe, device, 0);
    xx_elf_init(&elf, device, 0);
    xx_macho_init(&macho, device, 0);
    initialized = 1;
    formats[0] = &pe.format;
    formats[1] = &elf.format;
    formats[2] = &macho.format;
    for (size_t i = 0; i < 3 && !xx_pd_is_stopped(pd); ++i)
        if (formats[i]->check_is_valid(formats[i], pd)) {
            format = formats[i];
            break;
        }
    if (!format) {
        message = "Symbols supports PE, ELF and thin Mach-O files";
        goto done;
    }
    if (!xx_format_handle_base_info(format, pd)) goto done;
    data = xx_rt_calloc(1, sizeof(*data));
    if (!data) {
        message = "Cannot allocate symbols";
        goto done;
    }
    data->file_size = (uint64_t)size;
    xx_rt_snprintf(data->file_type, sizeof(data->file_type), "%s", xx_format_file_type_to_string(format->file_type));
    level = xx_pd_enter_level(pd, CDIE_SYMBOLS_MAX_RECORDS, "Reading Symbols");
    if (format == &macho.format) {
        ok = read_macho(&macho, data, pd, level);
        goto done;
    }
    api = xx_format_get_stream_symbols(format);
    if (!api || !api->create_symbols_reading || !api->get_current_symbol || !api->symbol_move_to_next || !api->free_symbols_reading) goto done;
    state = api->create_symbols_reading(format, pd);
    if (!state) goto done;
    ok = 1;
    while ((record = api->get_current_symbol(format, state)) != NULL) {
        xx_symbol_record normalized = *record;
        if (normalized.is_absolute || normalized.is_debug) normalized.offset = -1;
        if (xx_pd_is_stopped(pd) || !append(data, &normalized)) {
            ok = 0;
            break;
        }
        xx_pd_set_current(pd, level, data->count);
        if (!api->symbol_move_to_next(format, state, pd)) break;
    }
    if (state->failed) ok = 0;
done:
    if (state) api->free_symbols_reading(format, state);
    xx_pd_leave_level(pd, level);
    if (xx_pd_is_stopped(pd)) {
        xx_pd_stop(pd);
        ok = 0;
        message = "Symbols cancelled";
    }
    if (initialized) {
        xx_pe_destroy(&pe);
        xx_elf_destroy(&elf);
        xx_macho_destroy(&macho);
    }
    if (device) xx_io_close(device);
    if (ok) *out = data;
    else {
        cdie_symbols_free(data);
        *error = cdie_strdup(message);
    }
    return ok;
}
