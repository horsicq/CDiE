#include <string.h>
#include <stdio.h>
#include <cdisasm/cdisasm.h> 
#include "core/disasm_backend.h"

#define MAX_SECT 96

typedef struct { uint32_t rva, vsize, raw_off, raw_size; } Sect;

static DasmRead s_rd;
static void* s_user;
static uint64_t s_size, s_base;
static uint32_t s_entry_rva;
static DasmArch s_arch;
static Sect     s_sect[MAX_SECT];
static unsigned s_nsect;
static cdisasm_x86_decode_flags s_flags;

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint64_t rd64(const uint8_t* p) { return rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

int dasm_open(DasmRead rd, void* user, uint64_t file_size)
{
	uint8_t h[0x40], nt[24 + 0xF0], sh[40];
	uint32_t pe;
	unsigned i, nsec, optsize;
	uint16_t machine, magic;
	cdisasm_mode mode;

	dasm_close();
	s_rd = rd; s_user = user; s_size = file_size;

	if (!rd(user, 0, h, sizeof h) || rd16(h) != 0x5A4D) return 0;
	pe = rd32(h + 0x3C);
	if (!rd(user, pe, nt, sizeof nt) || rd32(nt) != 0x00004550) return 0;

	machine = rd16(nt + 4);
	nsec = rd16(nt + 6);
	optsize = rd16(nt + 20);
	magic = rd16(nt + 24);
	s_entry_rva = rd32(nt + 24 + 16);
	s_base = (magic == 0x20B) ? rd64(nt + 24 + 24) : rd32(nt + 24 + 28);

	if (machine == 0x14C) { s_arch = DASM_ARCH_X86_32; mode = CDISASM_MODE_32; }
	else if (machine == 0x8664) { s_arch = DASM_ARCH_X86_64; mode = CDISASM_MODE_64; }
	else return 0;                       /* ARM etc.: later */

	if (nsec > MAX_SECT) nsec = MAX_SECT;
	for (i = 0; i < nsec; ++i) {
		if (!rd(user, (uint64_t)pe + 24 + optsize + i * 40, sh, 40)) break;
		s_sect[s_nsect].vsize = rd32(sh + 8);
		s_sect[s_nsect].rva = rd32(sh + 12);
		s_sect[s_nsect].raw_size = rd32(sh + 16);
		s_sect[s_nsect].raw_off = rd32(sh + 20);
		++s_nsect;
	}

	cdisasm_decode_flags_reset(&s_flags);
	cdisasm_x86_cpu_decode_flag_mask(CDISASM_CPU_X86, mode, &s_flags);
	return s_nsect > 0;
}

void dasm_close(void)
{
	s_rd = NULL; s_user = NULL; s_size = 0; s_base = 0;
	s_entry_rva = 0; s_arch = DASM_ARCH_NONE; s_nsect = 0;
}

DasmArch dasm_arch(void) { return s_arch; }

int dasm_off_to_va(uint64_t off, uint64_t* va)
{
	unsigned i;
	for (i = 0; i < s_nsect; ++i)
		if (off >= s_sect[i].raw_off && off < (uint64_t)s_sect[i].raw_off + s_sect[i].raw_size) {
			*va = s_base + s_sect[i].rva + (off - s_sect[i].raw_off);
			return 1;
		}
	return 0;
}

int dasm_va_to_off(uint64_t va, uint64_t* off)
{
	unsigned i;
	uint64_t rva = va - s_base;
	if (va < s_base) return 0;
	for (i = 0; i < s_nsect; ++i)
		if (rva >= s_sect[i].rva && rva < (uint64_t)s_sect[i].rva + s_sect[i].raw_size) {
			*off = s_sect[i].raw_off + (rva - s_sect[i].rva);
			return 1;
		}
	return 0;
}

uint64_t dasm_entry_offset(void)
{
	uint64_t off = 0;
	dasm_va_to_off(s_base + s_entry_rva, &off);
	return off;
}

/* TODO: needs the name/register string tables from cdisasm_x86_ids.h */
static void format_insn(const cdisasm_x86_instruction* in, DasmLine* out)
{
	snprintf(out->mnemonic, sizeof out->mnemonic, "op_%u", (unsigned)in->name_id);
	out->operands[0] = 0;
	out->target = (in->opcode_groups & (CDISASM_GROUP_JUMP | CDISASM_GROUP_CALL))
		? in->branch_target : 0;
}

int dasm_decode_at(uint64_t off, DasmLine* out)
{
	uint8_t buf[16];
	cdisasm_x86_instruction in;
	size_t avail;
	uint32_t n;
	uint64_t va = 0;

	memset(out, 0, sizeof * out);
	out->file_off = off;
	out->len = 1;
	if (!s_rd || off >= s_size) return 0;
	dasm_off_to_va(off, &va);
	out->va = va;

	avail = (size_t)((s_size - off) < 15 ? (s_size - off) : 15);
	if (!s_rd(s_user, off, buf, avail)) return 0;
	memcpy(out->bytes, buf, avail);

	n = cdisasm_x86_decode(CDISASM_CPU_X86,
		s_arch == DASM_ARCH_X86_64 ? CDISASM_MODE_64 : CDISASM_MODE_32,
		buf, avail, va, &s_flags, &in);
	if (n == 0) {
		snprintf(out->mnemonic, sizeof out->mnemonic, "db");
		snprintf(out->operands, sizeof out->operands, "0x%02X", buf[0]);
		return 1;
	}
	out->valid = 1;
	out->len = n;
	format_insn(&in, out);
	return 1;
}

int dasm_section_of(uint64_t off, uint64_t* lo, uint64_t* hi)
{
	unsigned i;
	for (i = 0; i < s_nsect; ++i)
		if (off >= s_sect[i].raw_off && off < (uint64_t)s_sect[i].raw_off + s_sect[i].raw_size) {
			*lo = s_sect[i].raw_off;
			*hi = (uint64_t)s_sect[i].raw_off + s_sect[i].raw_size;
			if (*hi > s_size) *hi = s_size;
			return 1;
		}
	return 0;
}