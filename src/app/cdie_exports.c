/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "cdie_exports.h"
#include "cdie_app.h"
#include <limits.h>
#include <xxfclib/formats/executable/elf/xx_elf.h>
#include <xxfclib/formats/executable/macho/xx_macho.h>
#include <xxfclib/formats/executable/pe/xx_pe.h>
#include <xxfclib/rt/xx_rt.h>
void cdie_exports_free(cdie_exports_data *data)
{
    if (!data) return;
    for (size_t i = 0; i < data->count; ++i) {
        xx_rt_free(data->records[i].forwarder);
        xx_rt_free(data->records[i].name);
    }
    xx_rt_free(data->records);
    xx_rt_free(data);
}
static char *copy_name(cdie_exports_data *data, const char *name)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t size = 0, pos = 0;
    char *out;
    if (!name) name = "";
    for (size_t i = 0; name[i]; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (i >= CDIE_EXPORTS_MAX_NAME) return NULL;
        size += c >= 32 && c < 127 ? 1 : 4;
        if (size > CDIE_EXPORTS_MAX_NAME) return NULL;
    }
    if (size + 1 > CDIE_EXPORTS_MAX_TEXT - data->text_size) return NULL;
    out = xx_rt_malloc(size + 1);
    if (!out) return NULL;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (*p >= 32 && *p < 127) out[pos++] = (char)*p;
        else {
            out[pos++] = '\\';
            out[pos++] = 'x';
            out[pos++] = hex[*p >> 4];
            out[pos++] = hex[*p & 15];
        }
    }
    out[pos] = 0;
    data->text_size += size + 1;
    return out;
}
static int append(cdie_exports_data *data, const xx_export_record *record)
{
    xx_export_record copy;
    if (data->count == CDIE_EXPORTS_MAX_RECORDS || record->offset < -1 || (record->offset >= 0 && (uint64_t)record->offset >= data->file_size)) return 0;
    if (data->count == data->capacity) {
        size_t capacity = data->capacity ? data->capacity * 2 : 64;
        xx_export_record *rows;
        if (capacity > CDIE_EXPORTS_MAX_RECORDS) capacity = CDIE_EXPORTS_MAX_RECORDS;
        rows = xx_rt_realloc(data->records, capacity * sizeof(*rows));
        if (!rows) return 0;
        data->records = rows;
        data->capacity = capacity;
    }
    copy = *record;
    copy.forwarder = copy_name(data, record->forwarder);
    copy.name = copy_name(data, record->name);
    if (!copy.forwarder || !copy.name) {
        xx_rt_free(copy.forwarder);
        xx_rt_free(copy.name);
        return 0;
    }
    data->records[data->count++] = copy;
    return 1;
}
static int read_pe(xx_pe *pe, cdie_exports_data *data, xx_pd_struct *pd, int level)
{
    const StreamExport *api = xx_format_get_stream_export(&pe->format);
    xx_export_state *state;
    const xx_export_record *record;
    int ok = 1;
    if (!api || !api->create_exports_reading || !api->get_current_export || !api->export_move_to_next || !api->free_exports_reading) return 0;
    state = api->create_exports_reading(&pe->format, pd);
    if (!state) return 0;
    while ((record = api->get_current_export(&pe->format, state)) != NULL) {
        if (xx_pd_is_stopped(pd) || !append(data, record)) {
            ok = 0;
            break;
        }
        xx_pd_set_current(pd, level, data->count);
        if (!api->export_move_to_next(&pe->format, state, pd)) break;
    }
    if (state->failed) ok = 0;
    api->free_exports_reading(&pe->format, state);
    return ok;
}
static int dynamic_symbol(const xx_elf *elf, const xx_symbol_record *symbol)
{
    uint64_t offset;
    if (symbol->record_offset < elf->format.base_address) return 0;
    offset = (uint64_t)(symbol->record_offset - elf->format.base_address);
    for (uint64_t i = 0; i < elf->section_header_count; ++i) {
        const xx_elf_section_header *s = elf->section_headers + i;
        if (s->type == XX_ELF_SECTION_DYNSYM && offset >= s->offset && offset - s->offset < s->size) return 1;
    }
    return 0;
}
/* Native symbol metadata supplies table/record locations. st_other visibility
 * is the only extra byte read; it is not exposed in xx_symbol_record. */
static int read_elf(xx_elf *elf, cdie_exports_data *data, xx_pd_struct *pd, int level)
{
    xx_symbol_state *state = xx_elf_create_symbols_reading(&elf->format, pd);
    const xx_symbol_record *symbol;
    int ok = 1;
    if (!state) return 0;
    while ((symbol = xx_elf_get_current_symbol(&elf->format, state)) != NULL) {
        if (xx_pd_is_stopped(pd)) {
            ok = 0;
            break;
        }
        if (!symbol->is_undefined && symbol->name && *symbol->name && (symbol->binding == XX_SYMBOL_BINDING_GLOBAL || symbol->binding == XX_SYMBOL_BINDING_WEAK) &&
            symbol->kind != XX_SYMBOL_KIND_FILE && symbol->kind != XX_SYMBOL_KIND_SECTION && (elf->type == XX_ELF_TYPE_REL || dynamic_symbol(elf, symbol))) {
            uint8_t other;
            int64_t delta = elf->elf_class == XX_ELF_CLASS_64 ? 5 : 13;
            if (symbol->record_offset < 0 || symbol->record_offset > INT64_MAX - delta || !xx_io_read_at(elf->format.device, symbol->record_offset + delta, &other, 1)) {
                ok = 0;
                break;
            }
            if ((other & 3U) == 0 || (other & 3U) == 3) {
                xx_export_record r = {0};
                r.name = symbol->name;
                r.ordinal = UINT64_MAX;
                /* SHN_ABS carries a value, not a physical byte location. */
                r.offset = symbol->is_absolute ? -1 : symbol->offset;
                r.address = symbol->address;
                if (!append(data, &r)) {
                    ok = 0;
                    break;
                }
                xx_pd_set_current(pd, level, data->count);
            }
        }
        if (!xx_elf_symbol_move_to_next(&elf->format, state, pd)) break;
    }
    if (state->failed) ok = 0;
    xx_elf_free_symbols_reading(&elf->format, state);
    return ok;
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
static int read_macho(xx_macho *macho, cdie_exports_data *data, xx_pd_struct *pd, int level)
{
    xx_macho_inspection inspection = {0};
    int ok = xx_macho_inspect_parse(macho, &inspection, pd);
    for (int i = 0; ok && i < inspection.nSymbolCount; ++i) {
        const xx_macho_inspect_symbol *s = inspection.pSymbols + i;
        if (xx_pd_is_stopped(pd)) {
            ok = 0;
            break;
        }
        if (s->bExported && s->pName && *s->pName) {
            xx_export_record r = {0};
            r.name = s->pName;
            r.forwarder = s->pForwarder;
            r.ordinal = UINT64_MAX;
            r.address = s->nExportAddress;
            r.offset = (s->nExportFlags & 3U) == 2U ? -1 : macho_offset(macho, r.address);
            if (!append(data, &r)) {
                ok = 0;
                break;
            }
            xx_pd_set_current(pd, level, data->count);
        }
    }
    xx_macho_inspect_free(&inspection);
    return ok;
}
int cdie_exports_read(const char *path, xx_pd_struct *pd, cdie_exports_data **out, char **error)
{
    xx_io_device *device = NULL;
    cdie_exports_data *data = NULL;
    xx_pe pe;
    xx_elf elf;
    xx_macho macho;
    Abstractformat *formats[3], *format = NULL;
    const char *message = "Cannot read exports: invalid table, read failure or result limit";
    int initialized = 0, ok = 0, level = -1;
    int64_t size;
    if (!out || !error) return 0;
    *out = NULL;
    *error = NULL;
    if (!path || !*path) {
        message = "Exports requires a target";
        goto done;
    }
    if (xx_pd_is_stopped(pd)) goto done;
    device = xx_io_file_open(path, "rb");
    if (!device) {
        message = "Cannot open file for Exports";
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
        message = "Exports supports PE, ELF and thin Mach-O files";
        goto done;
    }
    if (!xx_format_handle_base_info(format, pd)) goto done;
    data = xx_rt_calloc(1, sizeof(*data));
    if (!data) {
        message = "Cannot allocate exports";
        goto done;
    }
    data->file_size = (uint64_t)size;
    xx_rt_snprintf(data->file_type, sizeof(data->file_type), "%s", xx_format_file_type_to_string(format->file_type));
    level = xx_pd_enter_level(pd, CDIE_EXPORTS_MAX_RECORDS, "Reading Exports");
    ok = format == &pe.format ? read_pe(&pe, data, pd, level) : format == &elf.format ? read_elf(&elf, data, pd, level) : read_macho(&macho, data, pd, level);
done:
    xx_pd_leave_level(pd, level);
    if (xx_pd_is_stopped(pd)) {
        xx_pd_stop(pd);
        ok = 0;
        message = "Exports cancelled";
    }
    if (initialized) {
        xx_pe_destroy(&pe);
        xx_elf_destroy(&elf);
        xx_macho_destroy(&macho);
    }
    if (device) xx_io_close(device);
    if (ok) *out = data;
    else {
        cdie_exports_free(data);
        *error = cdie_strdup(message);
    }
    return ok;
}
