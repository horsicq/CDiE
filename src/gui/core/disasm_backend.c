/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <string.h>
#include <stdio.h>
#include <cdisasm/cdisasm.h> 
#include "core/disasm_backend.h"

#define MAX_SECT 96
#define MAX_CODE 256

/* one mapped range: file offset <-> virtual address */
typedef struct { uint64_t va, off, size; } Sect;

static DasmRead s_rd;
static void* s_user;
static uint64_t s_size, s_entry_va;
static DasmArch s_arch;
static Sect     s_seg[MAX_SECT];     /* used for va <-> offset (PE sections / ELF PT_LOAD) */
static unsigned s_nseg;
static Sect     s_code[MAX_CODE];    /* used to bound scrolling (code ranges) */
static unsigned s_ncode;
static cdisasm_x86_decode_flags s_flags;

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint64_t rd64(const uint8_t* p) { return rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

static void add_seg(uint64_t va, uint64_t off, uint64_t size)
{
	if (s_nseg < MAX_SECT && size) {
		s_seg[s_nseg].va = va; s_seg[s_nseg].off = off; s_seg[s_nseg].size = size;
		++s_nseg;
	}
}

static void add_code(uint64_t va, uint64_t off, uint64_t size)
{
	if (s_ncode < MAX_CODE && size) {
		s_code[s_ncode].va = va; s_code[s_ncode].off = off; s_code[s_ncode].size = size;
		++s_ncode;
	}
}

/* ---------------- PE ---------------- */

static int open_pe(DasmRead rd, void* user, const uint8_t* h)
{
	uint8_t nt[24 + 0xF0], sh[40];
	uint32_t pe;
	unsigned i, nsec, optsize;
	uint16_t machine, magic;
	uint64_t base;
	cdisasm_mode mode;

	pe = rd32(h + 0x3C);
	if (!rd(user, pe, nt, sizeof nt) || rd32(nt) != 0x00004550) return 0;

	machine = rd16(nt + 4);
	nsec = rd16(nt + 6);
	optsize = rd16(nt + 20);
	magic = rd16(nt + 24);
	base = (magic == 0x20B) ? rd64(nt + 24 + 24) : rd32(nt + 24 + 28);
	s_entry_va = base + rd32(nt + 24 + 16);

	if (machine == 0x14C) { s_arch = DASM_ARCH_X86_32; mode = CDISASM_MODE_32; }
	else if (machine == 0x8664) { s_arch = DASM_ARCH_X86_64; mode = CDISASM_MODE_64; }
	else return 0;

	if (nsec > MAX_SECT) nsec = MAX_SECT;
	for (i = 0; i < nsec; ++i) {
		uint64_t va, off, sz;
		if (!rd(user, (uint64_t)pe + 24 + optsize + i * 40, sh, 40)) break;
		va = base + rd32(sh + 12);
		sz = rd32(sh + 16);
		off = rd32(sh + 20);
		add_seg(va, off, sz);
		add_code(va, off, sz);
	}

	cdisasm_decode_flags_reset(&s_flags);
	cdisasm_x86_cpu_decode_flag_mask(CDISASM_CPU_X86, mode, &s_flags);
	return s_nseg > 0;
}

/* ---------------- ELF ---------------- */

static int open_elf(DasmRead rd, void* user, const uint8_t* h)
{
	uint8_t ph[56], sh[64];
	int is64;
	unsigned machine, phentsize, phnum, shentsize, shnum, i;
	uint64_t phoff, shoff;
	cdisasm_mode mode;
	unsigned phneed, shneed;

	if (h[4] != 1 && h[4] != 2) return 0;
	if (h[5] != 1) return 0;                          /* x86 is little-endian only */
	is64 = h[4] == 2;
	machine = rd16(h + 18);

	if (machine == 3) { s_arch = DASM_ARCH_X86_32; mode = CDISASM_MODE_32; }
	else if (machine == 62) { s_arch = DASM_ARCH_X86_64; mode = CDISASM_MODE_64; }
	else return 0;

	if (is64) {
		s_entry_va = rd64(h + 24);
		phoff = rd64(h + 32);  shoff = rd64(h + 40);
		phentsize = rd16(h + 54); phnum = rd16(h + 56);
		shentsize = rd16(h + 58); shnum = rd16(h + 60);
	}
	else {
		s_entry_va = rd32(h + 24);
		phoff = rd32(h + 28);  shoff = rd32(h + 32);
		phentsize = rd16(h + 42); phnum = rd16(h + 44);
		shentsize = rd16(h + 46); shnum = rd16(h + 48);
	}
	phneed = is64 ? 56 : 32;
	shneed = is64 ? 64 : 40;

	/* PT_LOAD segments give the va <-> file offset map (works on stripped files) */
	if (!phoff || phentsize < phneed) return 0;
	for (i = 0; i < phnum && s_nseg < MAX_SECT; ++i) {
		uint32_t type, flags;
		uint64_t off, va, filesz;
		if (!rd(user, phoff + (uint64_t)i * phentsize, ph, phneed)) break;
		type = rd32(ph);
		if (is64) {
			flags = rd32(ph + 4); off = rd64(ph + 8); va = rd64(ph + 16); filesz = rd64(ph + 32);
		}
		else {
			off = rd32(ph + 4); va = rd32(ph + 8); filesz = rd32(ph + 16); flags = rd32(ph + 24);
		}
		if (type != 1) continue;                      /* PT_LOAD */
		add_seg(va, off, filesz);
		(void)flags;
	}
	if (s_nseg == 0) return 0;

	/* code ranges: executable sections (.text, .plt, .init ...) */
	if (shoff && shentsize >= shneed) {
		for (i = 1; i < shnum && i < 4096; ++i) {
			uint64_t flags, addr, off, size;
			uint32_t type;
			if (!rd(user, shoff + (uint64_t)i * shentsize, sh, shneed)) break;
			type = rd32(sh + 4);
			if (is64) {
				flags = rd64(sh + 8); addr = rd64(sh + 16); off = rd64(sh + 24); size = rd64(sh + 32);
			}
			else {
				flags = rd32(sh + 8); addr = rd32(sh + 12); off = rd32(sh + 16); size = rd32(sh + 20);
			}
			if ((flags & 4) && type != 8) add_code(addr, off, size);   /* SHF_EXECINSTR, not NOBITS */
		}
	}
	/* fallback: executable segments (PF_X) when there are no section headers */
	if (s_ncode == 0) {
		for (i = 0; i < phnum; ++i) {
			uint32_t type, flags;
			uint64_t off, va, filesz;
			if (!rd(user, phoff + (uint64_t)i * phentsize, ph, phneed)) break;
			type = rd32(ph);
			if (is64) {
				flags = rd32(ph + 4); off = rd64(ph + 8); va = rd64(ph + 16); filesz = rd64(ph + 32);
			}
			else {
				off = rd32(ph + 4); va = rd32(ph + 8); filesz = rd32(ph + 16); flags = rd32(ph + 24);
			}
			if (type == 1 && (flags & 1)) add_code(va, off, filesz);
		}
	}

	cdisasm_decode_flags_reset(&s_flags);
	cdisasm_x86_cpu_decode_flag_mask(CDISASM_CPU_X86, mode, &s_flags);
	return 1;
}

/* ---------------- public API ---------------- */

int dasm_open(DasmRead rd, void* user, uint64_t file_size)
{
	uint8_t h[0x40];
	int ok = 0;

	dasm_close();
	s_rd = rd; s_user = user; s_size = file_size;

	if (!rd(user, 0, h, sizeof h)) return 0;
	if (rd16(h) == 0x5A4D) ok = open_pe(rd, user, h);
	else if (h[0] == 0x7F && h[1] == 'E' && h[2] == 'L' && h[3] == 'F') ok = open_elf(rd, user, h);

	if (!ok) dasm_close();
	return ok;
}

void dasm_close(void)
{
	s_rd = NULL; s_user = NULL; s_size = 0;
	s_entry_va = 0; s_arch = DASM_ARCH_NONE; s_nseg = 0; s_ncode = 0;
}

DasmArch dasm_arch(void) { return s_arch; }

int dasm_off_to_va(uint64_t off, uint64_t* va)
{
	unsigned i;
	for (i = 0; i < s_nseg; ++i)
		if (off >= s_seg[i].off && off - s_seg[i].off < s_seg[i].size) {
			*va = s_seg[i].va + (off - s_seg[i].off);
			return 1;
		}
	return 0;
}

int dasm_va_to_off(uint64_t va, uint64_t* off)
{
	unsigned i;
	for (i = 0; i < s_nseg; ++i)
		if (va >= s_seg[i].va && va - s_seg[i].va < s_seg[i].size) {
			*off = s_seg[i].off + (va - s_seg[i].va);
			return 1;
		}
	return 0;
}

uint64_t dasm_entry_offset(void)
{
	uint64_t off = 0;
	dasm_va_to_off(s_entry_va, &off);
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
	for (i = 0; i < s_ncode; ++i)
		if (off >= s_code[i].off && off - s_code[i].off < s_code[i].size) {
			*lo = s_code[i].off;
			*hi = s_code[i].off + s_code[i].size;
			if (*hi > s_size) *hi = s_size;
			return 1;
		}
	return 0;
}