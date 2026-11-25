/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "cdie_imports.h"
#include "cdie_app.h"
#include <xxfclib/formats/executable/elf/xx_elf.h>
#include <xxfclib/formats/executable/macho/xx_macho.h>
#include <xxfclib/formats/executable/pe/xx_pe.h>
#include <xxfclib/rt/xx_rt.h>
void cdie_imports_free(cdie_imports_data *data)
{
    if (!data) return;
    for (size_t i = 0; i < data->count; ++i) {
        xx_rt_free(data->records[i].library_name);
        xx_rt_free(data->records[i].name);
    }
    xx_rt_free(data->records);
    xx_rt_free(data);
}
static char *copy_name(cdie_imports_data *data, const char *name)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t size = 0, pos = 0;
    char *out;
    if (!name) name = "";
    for (size_t i = 0; name[i]; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (i >= CDIE_IMPORTS_MAX_NAME) return NULL;
        size += c >= 32 && c < 127 ? 1 : 4;
        if (size > CDIE_IMPORTS_MAX_NAME) return NULL;
    }
    if (size + 1 > CDIE_IMPORTS_MAX_TEXT - data->text_size) return NULL;
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
static int append(cdie_imports_data *data, const xx_import_record *record)
{
    xx_import_record copy;
    if (data->count == CDIE_IMPORTS_MAX_RECORDS || record->offset < -1 || (record->offset >= 0 && (uint64_t)record->offset >= data->file_size)) return 0;
    if (data->count == data->capacity) {
        size_t capacity = data->capacity ? data->capacity * 2 : 64;
        xx_import_record *rows;
        if (capacity > CDIE_IMPORTS_MAX_RECORDS) capacity = CDIE_IMPORTS_MAX_RECORDS;
        rows = xx_rt_realloc(data->records, capacity * sizeof(*rows));
        if (!rows) return 0;
        data->records = rows;
        data->capacity = capacity;
    }
    copy = *record;
    copy.library_name = copy_name(data, record->library_name);
    copy.name = copy_name(data, record->name);
    if (!copy.library_name || !copy.name) {
        xx_rt_free(copy.library_name);
        xx_rt_free(copy.name);
        return 0;
    }
    data->records[data->count++] = copy;
    return 1;
}
static int read_pe(xx_pe *pe, cdie_imports_data *data, xx_pd_struct *pd, int level)
{
    const StreamImport *api = xx_format_get_stream_import(&pe->format);
    xx_import_state *state;
    const xx_import_record *record;
    int ok = 1;
    if (!api || !api->create_imports_reading || !api->get_current_import || !api->import_move_to_next || !api->free_imports_reading) return 0;
    state = api->create_imports_reading(&pe->format, pd);
    if (!state) return 0;
    while ((record = api->get_current_import(&pe->format, state)) != NULL) {
        if (xx_pd_is_stopped(pd) || !append(data, record)) {
            ok = 0;
            break;
        }
        xx_pd_set_current(pd, level, data->count);
        if (!api->import_move_to_next(&pe->format, state, pd)) break;
    }
    if (state->failed) ok = 0;
    api->free_imports_reading(&pe->format, state);
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
static int read_elf(xx_elf *elf, cdie_imports_data *data, xx_pd_struct *pd, int level)
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
        if (symbol->is_undefined && symbol->name && *symbol->name && (symbol->binding == XX_SYMBOL_BINDING_GLOBAL || symbol->binding == XX_SYMBOL_BINDING_WEAK) &&
            (elf->type == XX_ELF_TYPE_REL || dynamic_symbol(elf, symbol))) {
            xx_import_record r = {0};
            r.name = symbol->name;
            r.offset = -1;
            r.address = UINT64_MAX;
            if (!append(data, &r)) {
                ok = 0;
                break;
            }
            xx_pd_set_current(pd, level, data->count);
        }
        if (!xx_elf_symbol_move_to_next(&elf->format, state, pd)) break;
    }
    if (state->failed) ok = 0;
    xx_elf_free_symbols_reading(&elf->format, state);
    return ok;
}
static int read_macho(xx_macho *macho, cdie_imports_data *data, xx_pd_struct *pd, int level)
{
    xx_macho_inspection inspection = {0};
    int ok = xx_macho_inspect_parse(macho, &inspection, pd);
    for (int i = 0; ok && i < inspection.nSymbolCount; ++i) {
        const xx_macho_inspect_symbol *s = inspection.pSymbols + i;
        if (xx_pd_is_stopped(pd)) {
            ok = 0;
            break;
        }
        if (s->bFromSymbolTable && s->bExternal && s->bUndefined && !(s->nType & 0xe0U) && (!s->nValue || (s->nType & 0x0eU) == 0x0cU) && s->pName && *s->pName) {
            xx_import_record r = {0};
            unsigned ordinal = s->nDescription >> 8;
            r.name = s->pName;
            r.offset = -1;
            r.address = UINT64_MAX;
            if (ordinal > 0 && ordinal < 0xfeU && ordinal <= (unsigned)inspection.nLibraryCount) r.library_name = inspection.pLibraries[ordinal - 1].pName;
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
int cdie_imports_read(const char *path, xx_pd_struct *pd, cdie_imports_data **out, char **error)
{
    xx_io_device *device = NULL;
    cdie_imports_data *data = NULL;
    xx_pe pe;
    xx_elf elf;
    xx_macho macho;
    Abstractformat *formats[3], *format = NULL;
    const char *message = "Cannot read imports: invalid table, read failure or result limit";
    int initialized = 0, ok = 0, level = -1;
    int64_t size;
    if (!out || !error) return 0;
    *out = NULL;
    *error = NULL;
    if (!path || !*path) {
        message = "Imports requires a target";
        goto done;
    }
    if (xx_pd_is_stopped(pd)) goto done;
    device = xx_io_file_open(path, "rb");
    if (!device) {
        message = "Cannot open file for Imports";
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
        message = "Imports supports PE, ELF and thin Mach-O files";
        goto done;
    }
    if (!xx_format_handle_base_info(format, pd)) goto done;
    data = xx_rt_calloc(1, sizeof(*data));
    if (!data) {
        message = "Cannot allocate imports";
        goto done;
    }
    data->file_size = (uint64_t)size;
    xx_rt_snprintf(data->file_type, sizeof(data->file_type), "%s", xx_format_file_type_to_string(format->file_type));
    level = xx_pd_enter_level(pd, CDIE_IMPORTS_MAX_RECORDS, "Reading Imports");
    ok = format == &pe.format ? read_pe(&pe, data, pd, level) : format == &elf.format ? read_elf(&elf, data, pd, level) : read_macho(&macho, data, pd, level);
done:
    xx_pd_leave_level(pd, level);
    if (xx_pd_is_stopped(pd)) {
        xx_pd_stop(pd);
        ok = 0;
        message = "Imports cancelled";
    }
    if (initialized) {
        xx_pe_destroy(&pe);
        xx_elf_destroy(&elf);
        xx_macho_destroy(&macho);
    }
    if (device) xx_io_close(device);
    if (ok) *out = data;
    else {
        cdie_imports_free(data);
        *error = cdie_strdup(message);
    }
    return ok;
}
