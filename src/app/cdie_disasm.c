/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "cdie_disasm.h"
#include "cdie_app.h"
#include <cdisasm/cdisasm.h>
#include <cdisasm/cdisasm_format.h>
#include <xxfclib/formats/executable/elf/xx_elf.h>
#include <xxfclib/formats/executable/macho/xx_macho.h>
#include <xxfclib/formats/executable/pe/xx_pe.h>
#include <xxfclib/rt/xx_rt.h>

struct cdie_disasm_context {
    xx_io_device *device;
    xx_pe pe;
    xx_elf elf;
    xx_macho macho;
    Abstractformat *format;
    const xx_memory_map *map;
    cdie_disasm_info info;
    cdie_disasm_page *page;
    int syntax;
};
int cdie_disasm_set_syntax(cdie_disasm_context *context, int syntax)
{
    if (!context || (syntax != 0 && syntax != 1)) return 0;
    context->syntax = syntax;
    return 1;
}
static const char *const modes[] = {"Auto (file)", "x86 16-bit", "x86 32-bit", "x86 64-bit", "ARM32 LE", "Thumb LE", "ARM64 LE", "ARM32 BE", "Thumb BE", "ARM64 BE"};
size_t cdie_disasm_mode_count(void)
{
    return USE_ARCH_ARM ? 10 : 4;
}
const char *cdie_disasm_mode_name(size_t mode)
{
    return mode < cdie_disasm_mode_count() ? modes[mode] : NULL;
}
const cdie_disasm_info *cdie_disasm_get_info(const cdie_disasm_context *c)
{
    return c ? &c->info : NULL;
}
void cdie_disasm_close(cdie_disasm_context *c)
{
    if (!c) return;
    xx_pe_destroy(&c->pe);
    xx_elf_destroy(&c->elf);
    xx_macho_destroy(&c->macho);
    if (c->device) xx_io_close(c->device);
    xx_rt_free(c->page);
    xx_rt_free(c);
}
static cdie_disasm_mode native_mode(cdie_disasm_context *c)
{
    if (!c->format) return CDIE_DISASM_AUTO;
    switch (c->format->arch) {
        case XX_ARCH_X86_16: return CDIE_DISASM_X86_16;
        case XX_ARCH_X86: return CDIE_DISASM_X86_32;
        case XX_ARCH_X86_64: return CDIE_DISASM_X86_64;
#if USE_ARCH_ARM
        case XX_ARCH_ARM:
            if (c->format->endian == XX_ENDIAN_BIG) return CDIE_DISASM_AUTO;
            if ((c->map && (c->map->entry_point_address & 1) && c->map->entry_point_address != XX_INVALID_ADDRESS) ||
                (c->format == &c->pe.format && c->pe.machine == 0x1c4))
                return CDIE_DISASM_THUMB;
            return CDIE_DISASM_ARM;
        case XX_ARCH_ARM64: return c->format->endian == XX_ENDIAN_BIG ? CDIE_DISASM_AUTO : CDIE_DISASM_ARM64;
#endif
        default: return CDIE_DISASM_AUTO;
    }
}
int cdie_disasm_open(const char *path, xx_pd_struct *pd, cdie_disasm_context **out, char **error)
{
    cdie_disasm_context *c;
    Abstractformat *formats[3];
    int64_t size, entry = -1;
    uint64_t address;
    size_t i;
    const char *type = "Binary";
    if (!out || !error) return 0;
    *out = NULL;
    *error = NULL;
    c = xx_rt_calloc(1, sizeof(*c));
    if (!c) {
        *error = cdie_strdup("Cannot allocate disassembler");
        return 0;
    }
    c->device = path && *path ? xx_io_file_open(path, "rb") : NULL;
    xx_pe_init(&c->pe, c->device, 0);
    xx_elf_init(&c->elf, c->device, 0);
    xx_macho_init(&c->macho, c->device, 0);
    if (!c->device || (size = xx_io_total_size(c->device)) < 0) {
        *error = cdie_strdup("Cannot open file for Disasm");
        cdie_disasm_close(c);
        return 0;
    }
    c->info.file_size = (uint64_t)size;
    formats[0] = &c->pe.format;
    formats[1] = &c->elf.format;
    formats[2] = &c->macho.format;
    for (i = 0; i < 3 && !xx_pd_is_stopped(pd); ++i)
        if (formats[i]->check_is_valid(formats[i], pd)) {
            if (xx_format_handle_base_info(formats[i], pd)) {
                c->format = formats[i];
                type = xx_format_file_type_to_string(c->format->file_type);
                c->map = xx_format_get_memory_map(c->format, XX_MEMORY_MAP_MODE_UNKNOWN, pd);
            }
            break;
        }
    if (xx_pd_is_stopped(pd)) {
        xx_pd_stop(pd);
        *error = cdie_strdup("Disasm opening cancelled");
        cdie_disasm_close(c);
        return 0;
    }
    c->info.detected_mode = native_mode(c);
    if (c->map) {
        address = c->map->entry_point_address;
        if (c->format->arch == XX_ARCH_ARM && address != XX_INVALID_ADDRESS) address &= ~UINT64_C(1);
        if (address && address != XX_INVALID_ADDRESS && !(c->format == &c->pe.format && !c->pe.entry_point_rva)) entry = xx_memory_map_address_to_offset(c->map, address);
        if ((entry < 0 || entry >= size) && c->map->code_base >= 0) entry = xx_memory_map_address_to_offset(c->map, (uint64_t)c->map->code_base);
    }
    if (entry >= 0 && entry < size) c->info.initial_offset = (uint64_t)entry;
    xx_rt_snprintf(c->info.description, sizeof(c->info.description), "%s | Auto: %s | VA when mapped; file offsets otherwise", type,
                   c->info.detected_mode ? modes[c->info.detected_mode] : "Select instruction mode");
    *out = c;
    return 1;
}
static unsigned decode_one(int syntax, size_t mode, const unsigned char *bytes, size_t size, uint64_t address, char *text, size_t capacity)
{
    uint32_t length = 0;
    size_t formatted = 0;
    if (mode >= CDIE_DISASM_X86_16 && mode <= CDIE_DISASM_X86_64) {
        cdisasm_x86_instruction instruction;
        cdisasm_x86_decode_flags flags;
        cdisasm_x86_mode bits = mode == CDIE_DISASM_X86_16 ? CDISASM_X86_MODE_16 : mode == CDIE_DISASM_X86_32 ? CDISASM_X86_MODE_32 : CDISASM_X86_MODE_64;
        if (cdisasm_x86_cpu_decode_flag_mask(CDISASM_CPU_X86, bits, &flags) != CDISASM_STATUS_OK) return 0;
        length = cdisasm_x86_decode(CDISASM_CPU_X86, bits, bytes, size, address, &flags, &instruction);
        if (length) formatted = cdisasm_x86_format_mode(&instruction, bits, syntax ? CDISASM_FORMAT_SYNTAX_X86_ATT : CDISASM_FORMAT_SYNTAX_X86_INTEL, text, capacity);
    }
#if USE_ARCH_ARM
    else if (mode >= CDIE_DISASM_ARM && mode <= CDIE_DISASM_ARM64_BE) {
        cdisasm_arm_instruction instruction;
        cdisasm_arm_decode_flags flags = CDISASM_DECODE_FLAGS_NONE_INITIALIZER;
        size_t local = mode >= CDIE_DISASM_ARM_BE ? mode - 3 : mode;
        cdisasm_arm_mode arm_mode = local == CDIE_DISASM_ARM ? CDISASM_ARM_MODE_A32 : local == CDIE_DISASM_THUMB ? CDISASM_ARM_MODE_T32 : CDISASM_ARM_MODE_A64;
        if (mode >= CDIE_DISASM_ARM_BE) cdisasm_decode_flags_set_bit(&flags, CDISASM_ARM_DECODE_BIT_BIG_ENDIAN);
        length = cdisasm_arm_decode(CDISASM_ARM_CPU_ANY, arm_mode, bytes, size, address, &flags, &instruction);
        if (length) formatted = cdisasm_arm_format(&instruction, CDISASM_FORMAT_SYNTAX_ARM_CANONICAL, text, capacity);
    }
#endif
    return length && length <= size && length <= 15 && formatted && formatted < capacity ? length : 0;
}
cdie_disasm_status cdie_disasm_load(cdie_disasm_context *c, uint64_t offset, size_t mode, const cdie_disasm_page **out)
{
    unsigned char bytes[CDIE_DISASM_PAGE_BYTES + 15];
    cdie_disasm_page *page;
    int64_t size;
    size_t available, pos = 0;
    if (!out) return CDIE_DISASM_INVALID;
    *out = NULL;
    if (!c || mode >= cdie_disasm_mode_count()) return CDIE_DISASM_INVALID;
    size = xx_io_total_size(c->device);
    if (size < 0) return CDIE_DISASM_IO_ERROR;
    if (offset > (uint64_t)size) return CDIE_DISASM_INVALID;
    if (!mode) mode = c->info.detected_mode;
    if (offset < (uint64_t)size && !mode) return CDIE_DISASM_UNSUPPORTED;
    available = (uint64_t)size - offset < sizeof(bytes) ? (size_t)((uint64_t)size - offset) : sizeof(bytes);
    if (available && !xx_io_read_at(c->device, (int64_t)offset, bytes, available)) return CDIE_DISASM_IO_ERROR;
    page = xx_rt_calloc(1, sizeof(*page));
    if (!page) return CDIE_DISASM_NO_MEMORY;
    page->file_size = (uint64_t)size;
    page->offset = offset;
    while (pos < available && pos < CDIE_DISASM_PAGE_BYTES && page->count < CDIE_DISASM_MAX_ROWS) {
        cdie_disasm_row *r = page->rows + page->count;
        uint64_t address = c->map ? xx_memory_map_offset_to_address(c->map, (int64_t)(offset + pos)) : XX_INVALID_ADDRESS;
        size_t span = available - pos, j, used;
        int mapped = address != XX_INVALID_ADDRESS;
        r->offset = offset + pos;
        r->address = mapped ? address : r->offset;
        if (c->map && mapped)
            for (j = 0; j < c->map->record_count; ++j) {
                const xx_memory_record *m = c->map->records + j;
                if (!m->is_virtual && m->offset >= 0 && m->size > 0 && r->offset >= (uint64_t)m->offset && r->offset - (uint64_t)m->offset < (uint64_t)m->size &&
                    (uint64_t)m->size - (r->offset - (uint64_t)m->offset) < span)
                    span = (size_t)((uint64_t)m->size - (r->offset - (uint64_t)m->offset));
            }
        r->size = decode_one(c->syntax, mode, bytes + pos, span, r->address, r->instruction, sizeof(r->instruction));
        if (!r->size) {
            r->size = 1;
            xx_rt_snprintf(r->instruction, sizeof(r->instruction), "db 0x%02X", bytes[pos]);
            xx_rt_snprintf(r->comment, sizeof(r->comment), "File +0x%llX | invalid, unsupported or truncated", (unsigned long long)r->offset);
        } else xx_rt_snprintf(r->comment, sizeof(r->comment), "File +0x%llX%s", (unsigned long long)r->offset, mapped ? "" : " | unmapped");
        used = 0;
        for (j = 0; j < r->size; ++j) used += (size_t)xx_rt_snprintf(r->bytes + used, sizeof(r->bytes) - used, "%s%02X", j ? " " : "", bytes[pos + j]);
        pos += r->size;
        ++page->count;
    }
    page->next_offset = offset + pos;
    xx_rt_free(c->page);
    c->page = page;
    *out = page;
    return CDIE_DISASM_OK;
}
