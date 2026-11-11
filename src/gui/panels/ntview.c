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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "controls/hexviewer.h"
#include "panels/ntview.h"
#include "theme/theme.h"
#include "core/i18n.h"

#ifndef NT_TEXT_W
#define NT_TEXT_W(f, s) pl_text_width((f), (s))
#endif


#define NT_MAX_ROWS 320
#define NT_GROUPS   4
#define ROW_H       26
#define HDR_H       32
#define COLH_H      24
#define CHIP_H      16
#define NONE        0xFFFF
#define SB_W        3      
#define SB_GAP      3      
#define SB_HIT      14     
#define SB_MIN      24    
#define SB_PAD      8     
#define WHEEL_STEP  (ROW_H * 3)

#define COL_OFF     16
#define COL_SIZE    108
#define COL_NAME    148
#define COL_VAL     328
#define COL_RAW     480
#define COL_MEAN    616

enum { M_NONE = 0, M_ENUM, M_TIME, M_FLAGS };

typedef struct {
	int  group, hdr, dim, kind, idx;
	uint64_t foff;
	int  fsize;
	char off[16];
	char name[40];
	char sz[8];
	char raw[28];
	char hex[24];
	char note[128];
} NtRow;

struct NtView {
	PlWidget* w;
	int       width, height, hovered, selected, gcount;
	int       scroll, sb_drag, sb_drag_off, sb_hot;
	NtGotoFn  on_goto;
	void* goto_user;
	int       open[NT_GROUPS];
	int       fmt;
	const char* titles[NT_GROUPS];
	NtRow     rows[NT_MAX_ROWS];
	int       nrows;
	char      info[NT_GROUPS][128];
	char      status[96];
	PlFont* font, * bold, * fnt_s, * fnt_sb;
};

typedef struct { const char* name; unsigned short off, size; } NtField;
typedef struct {
	const char* name;
	unsigned short off32, off64;
	unsigned char size32, size64, kind;
} NtOpt;

typedef struct { unsigned bit; const char* name; } NtFlag;

static const char* s_titles[NT_GROUPS] = {
	"DOS HEADER", "NT HEADERS", "OPTIONAL HEADER", "DATA DIRECTORIES"
};

static const NtField s_dos[] = {
	{ "e_magic", 0, 2 },{ "e_cblp", 2, 2 },{ "e_cp", 4, 2 },
	{ "e_crlf", 6, 2 },{ "e_cparhdr", 8, 2 },{ "e_minalloc", 10, 2 },
	{ "e_maxalloc", 12, 2 },{ "e_ss", 14, 2 },{ "e_sp", 16, 2 },
	{ "e_csum", 18, 2 },{ "e_ip", 20, 2 },{ "e_cs", 22, 2 },
	{ "e_lfarlc", 24, 2 },{ "e_ovno", 26, 2 },{ "e_oemid", 36, 2 },
	{ "e_oeminfo", 38, 2 },{ "e_lfanew", 60, 4 }
};

static const NtOpt s_opt[] = {
	{ "Magic", 0, 0, 2, 2, 4 },
	{ "MajorLinkerVersion", 2, 2, 1, 1, 5 },
	{ "MinorLinkerVersion", 3, 3, 1, 1, 5 },
	{ "SizeOfCode", 4, 4, 4, 4, 3 },
	{ "SizeOfInitializedData", 8, 8, 4, 4, 3 },
	{ "SizeOfUninitializedData", 12, 12, 4, 4, 3 },
	{ "AddressOfEntryPoint", 16, 16, 4, 4, 0 },
	{ "BaseOfCode", 20, 20, 4, 4, 0 },
	{ "BaseOfData", 24, NONE, 4, 0, 0 },
	{ "ImageBase", 28, 24, 4, 8, 0 },
	{ "SectionAlignment", 32, 32, 4, 4, 3 },
	{ "FileAlignment", 36, 36, 4, 4, 3 },
	{ "MajorOSVersion", 40, 40, 2, 2, 5 },
	{ "MinorOSVersion", 42, 42, 2, 2, 5 },
	{ "MajorImageVersion", 44, 44, 2, 2, 5 },
	{ "MinorImageVersion", 46, 46, 2, 2, 5 },
	{ "MajorSubsystemVersion", 48, 48, 2, 2, 5 },
	{ "MinorSubsystemVersion", 50, 50, 2, 2, 5 },
	{ "Win32VersionValue", 52, 52, 4, 4, 0 },
	{ "SizeOfImage", 56, 56, 4, 4, 3 },
	{ "SizeOfHeaders", 60, 60, 4, 4, 3 },
	{ "CheckSum", 64, 64, 4, 4, 0 },
	{ "Subsystem", 68, 68, 2, 2, 1 },
	{ "DllCharacteristics", 70, 70, 2, 2, 2 },
	{ "SizeOfStackReserve", 72, 72, 4, 8, 3 },
	{ "SizeOfStackCommit", 76, 80, 4, 8, 3 },
	{ "SizeOfHeapReserve", 80, 88, 4, 8, 3 },
	{ "SizeOfHeapCommit", 84, 96, 4, 8, 3 },
	{ "LoaderFlags", 88, 104, 4, 4, 0 },
	{ "NumberOfRvaAndSizes", 92, 108, 4, 4, 5 }
};

static const char* s_dirs[16] = {
	"Export", "Import", "Resource", "Exception", "Security", "Base Relocation",
	"Debug", "Architecture", "Global Ptr", "TLS", "Load Config", "Bound Import",
	"IAT", "Delay Import", "COM Descriptor", "Reserved"
};

static const char* s_dir_desc[16] = {
	"Exported functions (DLLs)",
	"Imported DLLs and functions",
	"Resources: icons, dialogs, version info",
	"Exception unwind data (x64 .pdata)",
	"Authenticode signature (value is a file offset)",
	"Relocations for loading at another address",
	"Debug info, e.g. PDB path",
	"Reserved (architecture specific)",
	"Global pointer register value (IA64)",
	"Thread-local storage",
	"Load config: security cookie, CFG",
	"Bound imports (precomputed addresses)",
	"Import address table",
	"Delay-loaded imports",
	".NET (CLR) runtime header",
	"Reserved, must be zero"
};

static const NtFlag s_char_flags[] = {
	{ 0x0001, "RELOCS_STRIPPED" },{ 0x0002, "EXECUTABLE" },
	{ 0x0004, "LINE_NUMS_STRIPPED" },{ 0x0008, "LOCAL_SYMS_STRIPPED" },
	{ 0x0020, "LARGE_ADDRESS_AWARE" },{ 0x0100, "32BIT_MACHINE" },
	{ 0x0200, "DEBUG_STRIPPED" },{ 0x1000, "SYSTEM" },{ 0x2000, "DLL" }
};

static const NtFlag s_dll_flags[] = {
	{ 0x0020, "HIGH_ENTROPY_VA" },{ 0x0040, "DYNAMIC_BASE" },
	{ 0x0080, "FORCE_INTEGRITY" },{ 0x0100, "NX_COMPAT" },
	{ 0x0200, "NO_ISOLATION" },{ 0x0400, "NO_SEH" },{ 0x0800, "NO_BIND" },
	{ 0x1000, "APPCONTAINER" },{ 0x2000, "WDM_DRIVER" },
	{ 0x4000, "GUARD_CF" },{ 0x8000, "TERMINAL_SERVER_AWARE" }
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static uint64_t rd_n(const unsigned char* p, int n)
{
	uint64_t x = 0;
	int i;
	for (i = n - 1; i >= 0; --i) x = (x << 8) | p[i];
	return x;
}

static const char* machine_name(unsigned m)
{
	switch (m) {
	case 0x014C: return "x86";
	case 0x8664: return "x64";
	case 0x01C0: return "ARM";
	case 0x01C4: return "ARMNT";
	case 0xAA64: return "ARM64";
	case 0x0200: return "IA64";
	default:     return "Unknown";
	}
}

static const char* subsystem_name(unsigned s)
{
	switch (s) {
	case 1:  return "Native";
	case 2:  return "Windows GUI";
	case 3:  return "Windows console";
	case 5:  return "OS/2 console";
	case 7:  return "POSIX console";
	case 9:  return "Windows CE GUI";
	case 10: return "EFI application";
	case 11: return "EFI boot driver";
	case 12: return "EFI runtime driver";
	case 14: return "Xbox";
	case 16: return "Windows boot application";
	default: return "Unknown";
	}
}

static void fmt_time(char* out, size_t cap, uint32_t t)
{
	long z = (long)(t / 86400u) + 719468L;
	long era = z / 146097L;
	long doe = z - era * 146097L;
	long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long y = yoe + era * 400;
	long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	long mp = (5 * doy + 2) / 153;
	long d = doy - (153 * mp + 2) / 5 + 1;
	long m = mp < 10 ? mp + 3 : mp - 9;
	unsigned s = t % 86400u;
	if (m <= 2) ++y;
	snprintf(out, cap, "%04ld-%02ld-%02ld %02u:%02u:%02u UTC",
		y, m, d, s / 3600, (s / 60) % 60, s % 60);
}

static void flags_text(char* out, size_t cap, uint64_t val, const NtFlag* t, int n)
{
	size_t len = 0;
	int i;
	out[0] = 0;
	for (i = 0; i < n; ++i) {
		size_t add;
		if (!(val & t[i].bit)) continue;
		add = strlen(t[i].name) + (len ? 3 : 0);
		if (len + add + 1 > cap) break;
		len += (size_t)snprintf(out + len, cap - len, "%s%s",
			len ? " | " : "", t[i].name);
	}
}

static void fmt_size(char* out, size_t cap, uint64_t n)
{
	if (n >= 1048576u)    snprintf(out, cap, "%.2f MiB", (double)n / 1048576.0);
	else if (n >= 1024u)  snprintf(out, cap, "%.2f KiB", (double)n / 1024.0);
	else                  snprintf(out, cap, "%u bytes", (unsigned)n);
}

static void raw_text(char* out, size_t cap, const unsigned char* p, int n)
{
	size_t len = 0;
	int i;
	out[0] = 0;
	if (!p || n < 1 || n > 8) return;
	for (i = 0; i < n && len + 3 < cap; ++i)
		len += (size_t)snprintf(out + len, cap - len, "%s%02X", i ? " " : "", p[i]);
}

typedef struct { const char* name; const char* text; } NtDesc;

static const NtDesc s_desc[] = {
	/* DOS header */
	{ "e_cblp",     "Bytes on the last page of the file" },
	{ "e_cp",       "Number of 512-byte pages in the file" },
	{ "e_crlf",     "Number of DOS relocation entries" },
	{ "e_cparhdr",  "Header size in 16-byte paragraphs" },
	{ "e_minalloc", "Minimum extra paragraphs needed" },
	{ "e_maxalloc", "Maximum extra paragraphs wanted" },
	{ "e_ss",       "Initial stack segment (SS)" },
	{ "e_sp",       "Initial stack pointer (SP)" },
	{ "e_csum",     "DOS checksum, usually 0" },
	{ "e_ip",       "Initial instruction pointer (IP)" },
	{ "e_cs",       "Initial code segment (CS)" },
	{ "e_lfarlc",   "File offset of the DOS relocation table" },
	{ "e_ovno",     "Overlay number, 0 for the main program" },
	{ "e_oemid",    "OEM identifier" },
	{ "e_oeminfo",  "OEM information" },
	/* NT headers */
	{ "PointerToSymbolTable", "File offset of the COFF symbol table, 0 if none" },
	{ "NumberOfSymbols",      "Number of COFF symbols, 0 if none" },
	/* Optional header */
	{ "AddressOfEntryPoint", "RVA where code execution starts" },
	{ "BaseOfCode",          "RVA of the start of the code section" },
	{ "BaseOfData",          "RVA of the start of the data section (PE32 only)" },
	{ "ImageBase",           "Preferred load address in memory" },
	{ "Win32VersionValue",   "Reserved, should be 0" },
	{ "CheckSum",            "Image checksum, 0 if not computed" },
	{ "LoaderFlags",         "Obsolete, should be 0" },
	{ "SizeOfCode",              "Total size of all code sections" },
	{ "SizeOfInitializedData",   "Total size of initialized data sections" },
	{ "SizeOfUninitializedData", "Total size of uninitialized data (.bss)" },
	{ "MajorOSVersion",          "Minimum OS major version required" },
	{ "MinorOSVersion",          "Minimum OS minor version required" },
	{ "MajorImageVersion",       "Major version of this image" },
	{ "MinorImageVersion",       "Minor version of this image" },
	{ "MajorSubsystemVersion",   "Minimum subsystem major version" },
	{ "MinorSubsystemVersion",   "Minimum subsystem minor version" },
	{ "SizeOfStackReserve",      "Stack memory reserved for the main thread" },
	{ "SizeOfStackCommit",       "Stack memory committed at startup" },
	{ "SizeOfHeapReserve",       "Heap memory reserved for the process" },
	{ "SizeOfHeapCommit",        "Heap memory committed at startup" }
};

static const char* field_desc(const char* name)
{
	int i;
	for (i = 0; i < COUNT(s_desc); ++i)
		if (!strcmp(s_desc[i].name, name)) return s_desc[i].text;
	return NULL;
}

static NtRow* add_row(NtView* v, int group, int hdr)
{
	NtRow* r;
	if (v->nrows >= NT_MAX_ROWS) return NULL;
	r = &v->rows[v->nrows++];
	memset(r, 0, sizeof * r);
	r->group = group;
	r->hdr = hdr;
	return r;
}

static void add_hdr(NtView* v, int g)
{
	NtRow* r = add_row(v, g, 1);
	if (r) snprintf(r->name, sizeof r->name, "%s", v->titles[g] ? v->titles[g] : "");
	v->gcount = 0;
}

static void add_cols(NtView* v, int g)
{
	add_row(v, g, 2);
}

static void add_val(NtView* v, int g, uint64_t off, const char* name, int size,
	const unsigned char* raw, const char* hex, const char* note, int kind, int dim)
{
	NtRow* r = add_row(v, g, 0);
	if (!r) return;
	r->idx = v->gcount++;
	r->foff = off;
	r->fsize = size;
	snprintf(r->off, sizeof r->off, "0x%08X", (unsigned)off);
	snprintf(r->name, sizeof r->name, "%s", name);
	snprintf(r->sz, sizeof r->sz, "%d", size);
	raw_text(r->raw, sizeof r->raw, raw, size);
	snprintf(r->hex, sizeof r->hex, "%s", hex);
	snprintf(r->note, sizeof r->note, "%s", note ? note : "");
	r->kind = kind;
	r->dim = dim;
	if (!r->note[0] && kind == M_NONE && g < 3) {
		const char* d = field_desc(name);
		if (d) snprintf(r->note, sizeof r->note, "%s", d);
	}
}

static void add_hex(NtView* v, int g, uint64_t off, const char* name,
	const unsigned char* p, int bytes)
{
	char t[32];
	uint64_t val = rd_n(p, bytes);
	snprintf(t, sizeof t, "0x%0*llX", bytes * 2, (unsigned long long)val);
	add_val(v, g, off, name, bytes, p, t, "", M_NONE, val == 0);
}

static int group_count(const NtView* v, int g)
{
	int i, n = 0;
	for (i = 0; i < v->nrows; ++i)
		if (v->rows[i].group == g && v->rows[i].hdr == 0) ++n;
	return n;
}

static void set_status(NtView* v, const char* text)
{
	snprintf(v->status, sizeof v->status, "%s", text);
}

static void set_info(NtView* v, int g, const char* text)
{
	snprintf(v->info[g], sizeof v->info[g], "%s", text);
}

static void build_pe(NtView* v, NtRead rd, void* u, uint64_t size)
{
	unsigned char dos[64], nt[512];
	uint64_t pe, n;
	unsigned avail, magic, plus, dir, count, i, optsz;
	char t[128], h[32], note[128];

	v->nrows = 0;
	v->selected = -1;
	v->scroll = 0;
	memset(v->info, 0, sizeof v->info);
	v->status[0] = 0;

	if (size < 64 || !rd(u, 0, dos, 64) || rd_n(dos, 2) != 0x5A4D) {
		set_status(v, "Not a PE file (no MZ header).");
		return;
	}

	add_hdr(v, 0);
	add_cols(v, 0);
	for (i = 0; i < (unsigned)COUNT(s_dos); ++i) {
		const NtField* f = &s_dos[i];
		add_hex(v, 0, f->off, f->name, dos + f->off, f->size);
		if (v->nrows > 0) {
			NtRow* r = &v->rows[v->nrows - 1];
			if (f->off == 0) {
				snprintf(r->note, sizeof r->note, "MZ");
				r->kind = M_ENUM;
			}
			else if (f->off == 60) {
				snprintf(r->note, sizeof r->note, "PE header at 0x%X",
					(unsigned)rd_n(dos + 60, 4));
			}
		}
	}
	snprintf(t, sizeof t, "%d fields", group_count(v, 0));
	set_info(v, 0, t);

	pe = rd_n(dos + 60, 4);
	if (pe + 24 > size) {
		set_status(v, "Invalid e_lfanew: the NT headers are outside the file.");
		return;
	}
	n = size - pe;
	if (n > sizeof nt) n = sizeof nt;
	if (!rd(u, pe, nt, (size_t)n) || rd_n(nt, 4) != 0x4550) {
		set_status(v, "PE signature not found.");
		return;
	}

	add_hdr(v, 1);
	add_cols(v, 1);
	add_val(v, 1, pe, "Signature", 4, nt, "0x00004550", "PE", M_ENUM, 0);

	snprintf(h, sizeof h, "0x%04X", (unsigned)rd_n(nt + 4, 2));
	add_val(v, 1, pe + 4, "Machine", 2, nt + 4, h,
		machine_name((unsigned)rd_n(nt + 4, 2)), M_ENUM, 0);

	i = (unsigned)rd_n(nt + 6, 2);
	snprintf(h, sizeof h, "0x%04X", i);
	snprintf(note, sizeof note, "%u section%s", i, i == 1 ? "" : "s");
	add_val(v, 1, pe + 6, "NumberOfSections", 2, nt + 6, h, note, M_NONE, 0);

	i = (unsigned)rd_n(nt + 8, 4);
	snprintf(h, sizeof h, "0x%08X", i);
	fmt_time(note, sizeof note, (uint32_t)i);
	add_val(v, 1, pe + 8, "TimeDateStamp", 4, nt + 8, h, note, M_TIME, i == 0);

	add_hex(v, 1, pe + 12, "PointerToSymbolTable", nt + 12, 4);
	add_hex(v, 1, pe + 16, "NumberOfSymbols", nt + 16, 4);

	optsz = (unsigned)rd_n(nt + 20, 2);
	snprintf(h, sizeof h, "0x%04X", optsz);
	snprintf(note, sizeof note, "%u bytes", optsz);
	add_val(v, 1, pe + 20, "SizeOfOptionalHeader", 2, nt + 20, h, note, M_NONE, optsz == 0);

	i = (unsigned)rd_n(nt + 22, 2);
	snprintf(h, sizeof h, "0x%04X", i);
	flags_text(note, sizeof note, i, s_char_flags, COUNT(s_char_flags));
	add_val(v, 1, pe + 22, "Characteristics", 2, nt + 22, h, note, M_FLAGS, i == 0);

	snprintf(t, sizeof t, "0x%X-0x%X, %d fields",
		(unsigned)pe, (unsigned)(pe + 23), group_count(v, 1));
	set_info(v, 1, t);

	avail = (unsigned)(n - 24);
	if (avail < 2) return;
	magic = (unsigned)rd_n(nt + 24, 2);
	if (magic != 0x10B && magic != 0x20B) {
		set_status(v, "Unknown optional header magic.");
		return;
	}
	plus = magic == 0x20B;

	add_hdr(v, 2);
	add_cols(v, 2);
	for (i = 0; i < (unsigned)COUNT(s_opt); ++i) {
		const NtOpt* e = &s_opt[i];
		unsigned off = plus ? e->off64 : e->off32;
		unsigned sz = plus ? e->size64 : e->size32;
		const unsigned char* p;
		uint64_t val;
		if (off == NONE) continue;
		if (off + sz > avail) break;
		p = nt + 24 + off;
		val = rd_n(p, (int)sz);
		snprintf(h, sizeof h, "0x%0*llX", (int)sz * 2, (unsigned long long)val);
		switch (e->kind) {
		case 1:   /* subsystem */
			add_val(v, 2, pe + 24 + off, e->name, (int)sz, p, h,
				subsystem_name((unsigned)val), M_ENUM, 0);
			break;
		case 2:   /* dll characteristics -> chips */
			flags_text(note, sizeof note, val, s_dll_flags, COUNT(s_dll_flags));
			add_val(v, 2, pe + 24 + off, e->name, (int)sz, p, h, note, M_FLAGS, val == 0);
			break;
		case 3:   /* byte sizes -> "3.18 MiB" */
			note[0] = 0;
			if (val) fmt_size(note, sizeof note, val);
			add_val(v, 2, pe + 24 + off, e->name, (int)sz, p, h, note, M_NONE, val == 0);
			break;
		case 4:   /* magic */
			add_val(v, 2, pe + 24 + off, e->name, (int)sz, p, h,
				plus ? "PE32+" : "PE32", M_ENUM, 0);
			break;
		case 5:   /* version numbers: hex + decimal */
			snprintf(note, sizeof note, "%u", (unsigned)val);
			add_val(v, 2, pe + 24 + off, e->name, (int)sz, p, h, note, M_NONE, val == 0);
			break;
		default:
			add_hex(v, 2, pe + 24 + off, e->name, p, (int)sz);
			break;
		}
	}
	if (optsz)
		snprintf(t, sizeof t, "%s, 0x%X-0x%X", plus ? "PE32+" : "PE32",
			(unsigned)(pe + 24), (unsigned)(pe + 24 + optsz - 1));
	else
		snprintf(t, sizeof t, "%s", plus ? "PE32+" : "PE32");
	set_info(v, 2, t);

	dir = plus ? 112 : 96;
	if (avail < dir) return;
	count = (unsigned)rd_n(nt + 24 + (plus ? 108 : 92), 4);
	if (count > 16) count = 16;
	if (!count) return;

	add_hdr(v, 3);
	add_cols(v, 3);
	for (i = 0; i < count; ++i) {
		uint64_t rva, sz;
		if (dir + 8 * i + 8 > avail) break;
		rva = rd_n(nt + 24 + dir + 8 * i, 4);
		sz = rd_n(nt + 24 + dir + 8 * i + 4, 4);
		snprintf(h, sizeof h, "0x%08X", (unsigned)rva);
		if (rva == 0 && sz == 0) {
			snprintf(note, sizeof note, "%s", s_dir_desc[i]);
		}
		else {
			char sbuf[32];
			fmt_size(sbuf, sizeof sbuf, sz);
			snprintf(note, sizeof note, "%s - %s (0x%X)", s_dir_desc[i], sbuf, (unsigned)sz);
		}
		add_val(v, 3, pe + 24 + dir + 8 * i, s_dirs[i], 8, nt + 24 + dir + 8 * i, h, note,
			M_NONE, rva == 0 && sz == 0);
	}
	snprintf(t, sizeof t, "%d entries", group_count(v, 3));
	set_info(v, 3, t);
}

static uint64_t rd_e(const unsigned char* p, int n, int be)
{
	uint64_t x = 0;
	int i;
	if (be) { for (i = 0; i < n; ++i) x = (x << 8) | p[i]; }
	else { for (i = n - 1; i >= 0; --i) x = (x << 8) | p[i]; }
	return x;
}

static void add_hex_e(NtView* v, int g, uint64_t off, const char* name,
	const unsigned char* p, int bytes, int be, const char* note, int kind)
{
	char t[32];
	uint64_t val = rd_e(p, bytes, be);
	snprintf(t, sizeof t, "0x%0*llX", bytes * 2, (unsigned long long)val);
	add_val(v, g, off, name, bytes, p, t, note, kind, val == 0);
}

static void rd_cstr(NtRead rd, void* u, uint64_t size, uint64_t off, char* out, size_t cap)
{
	size_t n = cap - 1;
	out[0] = 0;
	if (off >= size) return;
	if (off + n > size) n = (size_t)(size - off);
	if (!n || !rd(u, off, out, n)) { out[0] = 0; return; }
	out[n] = 0;
}

static void set_titles(NtView* v, const char* a, const char* b, const char* c, const char* d)
{
	v->titles[0] = a; v->titles[1] = b; v->titles[2] = c; v->titles[3] = d;
}

static const char* elf_type_name(unsigned t)
{
	switch (t) {
	case 0: return "None";
	case 1: return "Relocatable (REL)";
	case 2: return "Executable (EXEC)";
	case 3: return "Shared object / PIE (DYN)";
	case 4: return "Core dump (CORE)";
	default: return "Unknown";
	}
}

static const char* elf_machine_name(unsigned m)
{
	switch (m) {
	case 0x02:  return "SPARC";
	case 0x03:  return "x86";
	case 0x08:  return "MIPS";
	case 0x14:  return "PowerPC";
	case 0x15:  return "PowerPC64";
	case 0x16:  return "S390";
	case 0x28:  return "ARM";
	case 0x2B:  return "SPARC V9";
	case 0x32:  return "IA-64";
	case 0x3E:  return "x86-64";
	case 0xB7:  return "AArch64";
	case 0xF3:  return "RISC-V";
	case 0x102: return "LoongArch";
	default:    return "Unknown";
	}
}

static const char* elf_osabi_name(unsigned a)
{
	switch (a) {
	case 0:    return "UNIX System V";
	case 2:    return "NetBSD";
	case 3:    return "Linux (GNU)";
	case 6:    return "Solaris";
	case 9:    return "FreeBSD";
	case 12:   return "OpenBSD";
	case 0x61: return "ARM EABI";
	case 0xFF: return "Standalone";
	default:   return "Unknown";
	}
}

static const char* elf_pt_name(unsigned t)
{
	switch (t) {
	case 0: return "PT_NULL";
	case 1: return "PT_LOAD";
	case 2: return "PT_DYNAMIC";
	case 3: return "PT_INTERP";
	case 4: return "PT_NOTE";
	case 5: return "PT_SHLIB";
	case 6: return "PT_PHDR";
	case 7: return "PT_TLS";
	case 0x6474E550u: return "PT_GNU_EH_FRAME";
	case 0x6474E551u: return "PT_GNU_STACK";
	case 0x6474E552u: return "PT_GNU_RELRO";
	case 0x6474E553u: return "PT_GNU_PROPERTY";
	default: return "PT_OTHER";
	}
}

static const char* elf_sht_name(unsigned t)
{
	switch (t) {
	case 0: return "NULL";
	case 1: return "PROGBITS";
	case 2: return "SYMTAB";
	case 3: return "STRTAB";
	case 4: return "RELA";
	case 5: return "HASH";
	case 6: return "DYNAMIC";
	case 7: return "NOTE";
	case 8: return "NOBITS";
	case 9: return "REL";
	case 11: return "DYNSYM";
	case 14: return "INIT_ARRAY";
	case 15: return "FINI_ARRAY";
	case 0x6FFFFFF6u: return "GNU_HASH";
	case 0x6FFFFFFDu: return "GNU_VERDEF";
	case 0x6FFFFFFEu: return "GNU_VERNEED";
	case 0x6FFFFFFFu: return "GNU_VERSYM";
	default: return "OTHER";
	}
}

static void build_elf(NtView* v, NtRead rd, void* u, uint64_t size)
{
	unsigned char eh[64], ph[64], sh[64];
	int is64, be, trunc = 0;
	unsigned n, a, o_ph, o_sh, o_fl, o_h, i;
	unsigned phent, phnum, shent, shnum, shstr;
	uint64_t phoff, shoff, val, stroff = 0, strsz = 0;
	char t[128], h[32], note[128], nm[40], fs[8], sb[32];

	n = (unsigned)(size < 64 ? size : 64);
	if (n < 52 || !rd(u, 0, eh, n)) { set_status(v, "Truncated ELF header."); return; }
	if ((eh[4] != 1 && eh[4] != 2) || (eh[5] != 1 && eh[5] != 2)) {
		set_status(v, "Invalid ELF identification bytes.");
		return;
	}
	is64 = eh[4] == 2;
	be = eh[5] == 2;
	if (is64 && n < 64) { set_status(v, "Truncated ELF header."); return; }

	a = is64 ? 8 : 4;
	o_ph = is64 ? 32 : 28;
	o_sh = is64 ? 40 : 32;
	o_fl = is64 ? 48 : 36;
	o_h = is64 ? 52 : 40;

	set_titles(v, "ELF HEADER", "PROGRAM HEADERS", "SECTION HEADERS", NULL);

	add_hdr(v, 0);
	add_cols(v, 0);
	add_val(v, 0, 0, "EI_MAGIC", 4, eh, "0x7F454C46", "ELF", M_ENUM, 0);
	add_val(v, 0, 4, "EI_CLASS", 1, eh + 4, is64 ? "0x02" : "0x01",
		is64 ? "64-bit" : "32-bit", M_ENUM, 0);
	add_val(v, 0, 5, "EI_DATA", 1, eh + 5, be ? "0x02" : "0x01",
		be ? "Big-endian" : "Little-endian", M_ENUM, 0);
	snprintf(h, sizeof h, "0x%02X", eh[6]);
	add_val(v, 0, 6, "EI_VERSION", 1, eh + 6, h, "", M_NONE, eh[6] == 0);
	snprintf(h, sizeof h, "0x%02X", eh[7]);
	add_val(v, 0, 7, "EI_OSABI", 1, eh + 7, h, elf_osabi_name(eh[7]), M_ENUM, 0);
	snprintf(h, sizeof h, "0x%02X", eh[8]);
	add_val(v, 0, 8, "EI_ABIVERSION", 1, eh + 8, h, "", M_NONE, eh[8] == 0);

	val = rd_e(eh + 16, 2, be);
	add_hex_e(v, 0, 16, "e_type", eh + 16, 2, be, elf_type_name((unsigned)val), M_ENUM);
	val = rd_e(eh + 18, 2, be);
	add_hex_e(v, 0, 18, "e_machine", eh + 18, 2, be, elf_machine_name((unsigned)val), M_ENUM);
	add_hex_e(v, 0, 20, "e_version", eh + 20, 4, be, "", M_NONE);
	add_hex_e(v, 0, 24, "e_entry", eh + 24, (int)a, be, "Entry point virtual address", M_NONE);
	add_hex_e(v, 0, o_ph, "e_phoff", eh + o_ph, (int)a, be, "File offset of the program header table", M_NONE);
	add_hex_e(v, 0, o_sh, "e_shoff", eh + o_sh, (int)a, be, "File offset of the section header table", M_NONE);
	add_hex_e(v, 0, o_fl, "e_flags", eh + o_fl, 4, be, "", M_NONE);
	add_hex_e(v, 0, o_h, "e_ehsize", eh + o_h, 2, be, "Size of this header", M_NONE);
	add_hex_e(v, 0, o_h + 2, "e_phentsize", eh + o_h + 2, 2, be, "Size of one program header", M_NONE);
	add_hex_e(v, 0, o_h + 4, "e_phnum", eh + o_h + 4, 2, be, "Number of program headers", M_NONE);
	add_hex_e(v, 0, o_h + 6, "e_shentsize", eh + o_h + 6, 2, be, "Size of one section header", M_NONE);
	add_hex_e(v, 0, o_h + 8, "e_shnum", eh + o_h + 8, 2, be, "Number of section headers", M_NONE);
	add_hex_e(v, 0, o_h + 10, "e_shstrndx", eh + o_h + 10, 2, be, "Section that holds the section names", M_NONE);

	snprintf(t, sizeof t, "%s, %s, %d fields", is64 ? "ELF64" : "ELF32",
		be ? "big-endian" : "little-endian", group_count(v, 0));
	set_info(v, 0, t);

	phoff = rd_e(eh + o_ph, (int)a, be);
	shoff = rd_e(eh + o_sh, (int)a, be);
	phent = (unsigned)rd_e(eh + o_h + 2, 2, be);
	phnum = (unsigned)rd_e(eh + o_h + 4, 2, be);
	shent = (unsigned)rd_e(eh + o_h + 6, 2, be);
	shnum = (unsigned)rd_e(eh + o_h + 8, 2, be);
	shstr = (unsigned)rd_e(eh + o_h + 10, 2, be);

	if (phoff && phnum && phent >= (is64 ? 56u : 32u)) {
		unsigned need = is64 ? 56 : 32;
		add_hdr(v, 1);
		add_cols(v, 1);
		for (i = 0; i < phnum; ++i) {
			uint64_t po = phoff + (uint64_t)i * phent, off, vaddr, fsz;
			unsigned type, fl;
			if (i >= 64) { trunc = 1; break; }
			if (po + need > size || !rd(u, po, ph, need)) break;
			type = (unsigned)rd_e(ph, 4, be);
			if (is64) {
				fl = (unsigned)rd_e(ph + 4, 4, be);
				off = rd_e(ph + 8, 8, be);
				vaddr = rd_e(ph + 16, 8, be);
				fsz = rd_e(ph + 32, 8, be);
			}
			else {
				off = rd_e(ph + 4, 4, be);
				vaddr = rd_e(ph + 8, 4, be);
				fsz = rd_e(ph + 16, 4, be);
				fl = (unsigned)rd_e(ph + 24, 4, be);
			}
			snprintf(fs, sizeof fs, "%c%c%c", (fl & 4) ? 'R' : '-',
				(fl & 2) ? 'W' : '-', (fl & 1) ? 'X' : '-');
			snprintf(nm, sizeof nm, "%s [%u]", elf_pt_name(type), i);
			snprintf(h, sizeof h, "0x%08llX", (unsigned long long)vaddr);
			snprintf(note, sizeof note, "%s  offset 0x%llX  filesz 0x%llX",
				fs, (unsigned long long)off, (unsigned long long)fsz);
			add_val(v, 1, po, nm, (int)phent, NULL, h, note, M_NONE, 0);
		}
		snprintf(t, sizeof t, "%d entries", group_count(v, 1));
		set_info(v, 1, t);
	}

	if (shoff && shnum && shent >= (is64 ? 64u : 40u)) {
		unsigned need = is64 ? 64 : 40;
		uint64_t so0 = shoff + (uint64_t)shstr * shent;
		if (shstr < shnum && so0 + need <= size && rd(u, so0, sh, need)) {
			stroff = rd_e(sh + (is64 ? 24 : 16), (int)a, be);
			strsz = rd_e(sh + (is64 ? 32 : 20), (int)a, be);
		}
		add_hdr(v, 2);
		add_cols(v, 2);
		for (i = 0; i < shnum; ++i) {
			uint64_t so = shoff + (uint64_t)i * shent, fl, addr, off, sz;
			unsigned type, nameoff;
			if (i >= 200) { trunc = 1; break; }
			if (so + need > size || !rd(u, so, sh, need)) break;
			nameoff = (unsigned)rd_e(sh, 4, be);
			type = (unsigned)rd_e(sh + 4, 4, be);
			if (is64) {
				fl = rd_e(sh + 8, 8, be);
				addr = rd_e(sh + 16, 8, be);
				off = rd_e(sh + 24, 8, be);
				sz = rd_e(sh + 32, 8, be);
			}
			else {
				fl = rd_e(sh + 8, 4, be);
				addr = rd_e(sh + 12, 4, be);
				off = rd_e(sh + 16, 4, be);
				sz = rd_e(sh + 20, 4, be);
			}
			nm[0] = 0;
			if (strsz && nameoff < strsz)
				rd_cstr(rd, u, size, stroff + nameoff, nm, sizeof nm);
			if (!nm[0]) snprintf(nm, sizeof nm, "%s", i ? "(unnamed)" : "(null)");
			snprintf(fs, sizeof fs, "%c%c%c", (fl & 1) ? 'W' : '-',
				(fl & 2) ? 'A' : '-', (fl & 4) ? 'X' : '-');
			fmt_size(sb, sizeof sb, sz);
			snprintf(h, sizeof h, "0x%08llX", (unsigned long long)addr);
			snprintf(note, sizeof note, "%s  %s  offset 0x%llX  size %s",
				elf_sht_name(type), fs, (unsigned long long)off, sb);
			add_val(v, 2, so, nm, (int)shent, NULL, h, note, M_NONE, type == 0);
		}
		snprintf(t, sizeof t, "%d entries", group_count(v, 2));
		set_info(v, 2, t);
	}

	if (trunc)
		set_status(v, "List truncated: first 64 program headers and 200 sections shown.");
}

static int is_macho_magic(const unsigned char* m)
{
	uint32_t x = (uint32_t)rd_e(m, 4, 1);
	return x == 0xFEEDFACEu || x == 0xFEEDFACFu || x == 0xCEFAEDFEu ||
		x == 0xCFFAEDFEu || x == 0xCAFEBABEu || x == 0xCAFEBABFu;
}

static const char* macho_cpu_name(unsigned c)
{
	switch (c) {
	case 7:          return "x86";
	case 0x01000007: return "x86-64";
	case 12:         return "ARM";
	case 0x0100000C: return "ARM64";
	case 0x0200000C: return "ARM64_32";
	case 18:         return "PowerPC";
	case 0x01000012: return "PowerPC64";
	default:         return "Unknown";
	}
}

static const char* macho_type_name(unsigned t)
{
	switch (t) {
	case 1:  return "Object (MH_OBJECT)";
	case 2:  return "Executable (MH_EXECUTE)";
	case 3:  return "Fixed VM library";
	case 4:  return "Core dump (MH_CORE)";
	case 5:  return "Preloaded executable";
	case 6:  return "Dynamic library (MH_DYLIB)";
	case 7:  return "Dynamic linker (MH_DYLINKER)";
	case 8:  return "Bundle (MH_BUNDLE)";
	case 9:  return "Dylib stub";
	case 10: return "Debug symbols (MH_DSYM)";
	case 11: return "Kernel extension (MH_KEXT_BUNDLE)";
	default: return "Unknown";
	}
}

static const char* macho_lc_name(unsigned c)
{
	switch (c) {
	case 0x1:        return "LC_SEGMENT";
	case 0x2:        return "LC_SYMTAB";
	case 0x4:        return "LC_THREAD";
	case 0x5:        return "LC_UNIXTHREAD";
	case 0xB:        return "LC_DYSYMTAB";
	case 0xC:        return "LC_LOAD_DYLIB";
	case 0xD:        return "LC_ID_DYLIB";
	case 0xE:        return "LC_LOAD_DYLINKER";
	case 0xF:        return "LC_ID_DYLINKER";
	case 0x19:       return "LC_SEGMENT_64";
	case 0x1B:       return "LC_UUID";
	case 0x1D:       return "LC_CODE_SIGNATURE";
	case 0x1E:       return "LC_SEGMENT_SPLIT_INFO";
	case 0x21:       return "LC_ENCRYPTION_INFO";
	case 0x22:       return "LC_DYLD_INFO";
	case 0x24:       return "LC_VERSION_MIN_MACOSX";
	case 0x25:       return "LC_VERSION_MIN_IPHONEOS";
	case 0x26:       return "LC_FUNCTION_STARTS";
	case 0x27:       return "LC_DYLD_ENVIRONMENT";
	case 0x29:       return "LC_DATA_IN_CODE";
	case 0x2A:       return "LC_SOURCE_VERSION";
	case 0x2C:       return "LC_ENCRYPTION_INFO_64";
	case 0x32:       return "LC_BUILD_VERSION";
	case 0x80000018u: return "LC_LOAD_WEAK_DYLIB";
	case 0x8000001Cu: return "LC_RPATH";
	case 0x8000001Fu: return "LC_REEXPORT_DYLIB";
	case 0x80000022u: return "LC_DYLD_INFO_ONLY";
	case 0x80000028u: return "LC_MAIN";
	case 0x80000033u: return "LC_DYLD_EXPORTS_TRIE";
	case 0x80000034u: return "LC_DYLD_CHAINED_FIXUPS";
	default:         return "LC_UNKNOWN";
	}
}

static const NtFlag s_mh_flags[] = {
	{ 0x1, "NOUNDEFS" },{ 0x4, "DYLDLINK" },{ 0x80, "TWOLEVEL" },
	{ 0x2000, "SUBSECTIONS_VIA_SYMBOLS" },{ 0x8000, "WEAK_DEFINES" },
	{ 0x10000, "BINDS_TO_WEAK" },{ 0x20000, "ALLOW_STACK_EXECUTION" },
	{ 0x200000, "PIE" },{ 0x800000, "HAS_TLV_DESCRIPTORS" },
	{ 0x1000000, "NO_HEAP_EXECUTION" },{ 0x2000000, "APP_EXTENSION_SAFE" }
};

static void build_macho(NtView* v, NtRead rd, void* u, uint64_t size)
{
	unsigned char b[32], lc[80];
	uint64_t base = 0, co, end, val;
	uint32_t magic;
	int be, is64, fat = 0, trunc = 0;
	unsigned hsz, n, ncmds, sizeofcmds, i, k;
	char t[128], h[32], note[128], nm[40], sb[32], seg[17];

	if (size < 8 || !rd(u, 0, b, 8)) { set_status(v, "Truncated Mach-O header."); return; }
	magic = (uint32_t)rd_e(b, 4, 1);

	set_titles(v, "FAT HEADER", "MACH HEADER", "LOAD COMMANDS", NULL);

	if (magic == 0xCAFEBABEu || magic == 0xCAFEBABFu) {
		int f64 = magic == 0xCAFEBABFu;
		unsigned es = f64 ? 32 : 20;
		unsigned nfat = (unsigned)rd_e(b + 4, 4, 1);
		if (nfat == 0 || nfat >= 20) {
			set_status(v, "Not a valid universal (fat) Mach-O file.");
			return;
		}
		fat = 1;
		add_hdr(v, 0);
		add_cols(v, 0);
		add_val(v, 0, 0, "magic", 4, b, f64 ? "0xCAFEBABF" : "0xCAFEBABE",
			f64 ? "FAT_MAGIC_64" : "FAT_MAGIC", M_ENUM, 0);
		add_hex_e(v, 0, 4, "nfat_arch", b + 4, 4, 1, "Architectures in this file", M_NONE);
		for (k = 0; k < nfat && k < 8; ++k) {
			unsigned char fa[32];
			uint64_t fo = 8 + (uint64_t)k * es, soff, ssz;
			unsigned cpu;
			if (fo + es > size || !rd(u, fo, fa, es)) break;
			cpu = (unsigned)rd_e(fa, 4, 1);
			if (f64) { soff = rd_e(fa + 8, 8, 1); ssz = rd_e(fa + 16, 8, 1); }
			else { soff = rd_e(fa + 8, 4, 1); ssz = rd_e(fa + 12, 4, 1); }
			if (k == 0) base = soff;
			fmt_size(sb, sizeof sb, ssz);
			snprintf(nm, sizeof nm, "slice [%u]", k);
			snprintf(h, sizeof h, "0x%08llX", (unsigned long long)soff);
			snprintf(note, sizeof note, "%s, %s", macho_cpu_name(cpu), sb);
			add_val(v, 0, fo, nm, (int)es, NULL, h, note, M_ENUM, 0);
		}
		snprintf(t, sizeof t, "%u architectures", nfat);
		set_info(v, 0, t);
	}

	if (base + 28 > size) { set_status(v, "Slice offset is outside the file."); return; }
	n = (unsigned)(base + 32 <= size ? 32 : size - base);
	if (!rd(u, base, b, n)) { set_status(v, "Cannot read the Mach-O header."); return; }
	magic = (uint32_t)rd_e(b, 4, 1);
	switch (magic) {
	case 0xFEEDFACEu: be = 1; is64 = 0; break;
	case 0xFEEDFACFu: be = 1; is64 = 1; break;
	case 0xCEFAEDFEu: be = 0; is64 = 0; break;
	case 0xCFFAEDFEu: be = 0; is64 = 1; break;
	default: set_status(v, "The slice is not a Mach-O image."); return;
	}
	hsz = is64 ? 32 : 28;
	if (n < hsz) { set_status(v, "Truncated Mach-O header."); return; }

	add_hdr(v, 1);
	add_cols(v, 1);
	snprintf(h, sizeof h, "0x%08X", (unsigned)rd_e(b, 4, be));
	snprintf(note, sizeof note, "%s (%s)", is64 ? "MH_MAGIC_64" : "MH_MAGIC",
		be ? "big-endian" : "little-endian");
	add_val(v, 1, base, "magic", 4, b, h, note, M_ENUM, 0);
	val = rd_e(b + 4, 4, be);
	add_hex_e(v, 1, base + 4, "cputype", b + 4, 4, be, macho_cpu_name((unsigned)val), M_ENUM);
	add_hex_e(v, 1, base + 8, "cpusubtype", b + 8, 4, be, "", M_NONE);
	val = rd_e(b + 12, 4, be);
	add_hex_e(v, 1, base + 12, "filetype", b + 12, 4, be, macho_type_name((unsigned)val), M_ENUM);
	ncmds = (unsigned)rd_e(b + 16, 4, be);
	sizeofcmds = (unsigned)rd_e(b + 20, 4, be);
	snprintf(note, sizeof note, "%u load command%s", ncmds, ncmds == 1 ? "" : "s");
	add_hex_e(v, 1, base + 16, "ncmds", b + 16, 4, be, note, M_NONE);
	fmt_size(sb, sizeof sb, sizeofcmds);
	add_hex_e(v, 1, base + 20, "sizeofcmds", b + 20, 4, be, sb, M_NONE);
	val = rd_e(b + 24, 4, be);
	snprintf(h, sizeof h, "0x%08X", (unsigned)val);
	flags_text(note, sizeof note, val, s_mh_flags, COUNT(s_mh_flags));
	add_val(v, 1, base + 24, "flags", 4, b + 24, h, note, M_FLAGS, val == 0);
	if (is64) add_hex_e(v, 1, base + 28, "reserved", b + 28, 4, be, "", M_NONE);
	snprintf(t, sizeof t, "%s, %s, %s", is64 ? "64-bit" : "32-bit",
		be ? "big-endian" : "little-endian",
		macho_cpu_name((unsigned)rd_e(b + 4, 4, be)));
	set_info(v, 1, t);

	if (ncmds) {
		add_hdr(v, 2);
		add_cols(v, 2);
		co = base + hsz;
		end = co + sizeofcmds;
		if (end > size) end = size;
		for (i = 0; i < ncmds; ++i) {
			unsigned cmd, csz, rl;
			if (i >= 250) { trunc = 1; break; }
			if (co + 8 > end || !rd(u, co, lc, 8)) break;
			cmd = (unsigned)rd_e(lc, 4, be);
			csz = (unsigned)rd_e(lc + 4, 4, be);
			if (csz < 8 || co + csz > end) break;
			rl = csz < 72 ? csz : 72;
			if (rl > 8) rd(u, co, lc, rl);
			note[0] = 0;

			if (cmd == 0x19 && rl >= 40) {
				memcpy(seg, lc + 8, 16); seg[16] = 0;
				snprintf(note, sizeof note, "%s  vmaddr 0x%llX  vmsize 0x%llX", seg,
					(unsigned long long)rd_e(lc + 24, 8, be),
					(unsigned long long)rd_e(lc + 32, 8, be));
			}
			else if (cmd == 0x1 && rl >= 32) {
				memcpy(seg, lc + 8, 16); seg[16] = 0;
				snprintf(note, sizeof note, "%s  vmaddr 0x%llX  vmsize 0x%llX", seg,
					(unsigned long long)rd_e(lc + 24, 4, be),
					(unsigned long long)rd_e(lc + 28, 4, be));
			}
			else if (cmd == 0x80000028u && rl >= 16) {
				snprintf(note, sizeof note, "entry offset 0x%llX",
					(unsigned long long)rd_e(lc + 8, 8, be));
			}
			else if (cmd == 0x1B && rl >= 24) {
				snprintf(note, sizeof note,
					"%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
					lc[8], lc[9], lc[10], lc[11], lc[12], lc[13], lc[14], lc[15],
					lc[16], lc[17], lc[18], lc[19], lc[20], lc[21], lc[22], lc[23]);
			}
			else if ((cmd == 0xC || cmd == 0xD || cmd == 0x80000018u || cmd == 0x8000001Fu ||
				cmd == 0x8000001Cu || cmd == 0xE || cmd == 0xF || cmd == 0x27) && rl >= 12) {
				unsigned so = (unsigned)rd_e(lc + 8, 4, be);
				if (so < csz) rd_cstr(rd, u, size, co + so, note, sizeof note);
			}

			snprintf(h, sizeof h, "0x%08X", cmd);
			add_val(v, 2, co, macho_lc_name(cmd), (int)csz, NULL, h, note, M_NONE, 0);
			co += csz;
		}
		snprintf(t, sizeof t, "%u commands", ncmds);
		set_info(v, 2, t);
	}

	if (trunc) set_status(v, "List truncated: first 250 load commands shown.");
	else if (fat) set_status(v, "Universal binary: showing the first slice.");
}

enum { F_NONE = 0, F_PE, F_ELF, F_MACHO };

static void build(NtView* v, NtRead rd, void* u, uint64_t size)
{
	unsigned char m[4];
	int f;

	v->nrows = 0;
	v->selected = -1;
	v->scroll = 0;
	memset(v->info, 0, sizeof v->info);
	v->status[0] = 0;

	if (size < 4 || !rd(u, 0, m, 4)) {
		set_status(v, "File too small to have a header.");
		return;
	}
	if (m[0] == 'M' && m[1] == 'Z')            f = F_PE;
	else if (!memcmp(m, "\x7f" "ELF", 4))      f = F_ELF;
	else if (is_macho_magic(m))                f = F_MACHO;
	else {
		set_status(v, "No PE, ELF or Mach-O header found.");
		return;
	}

	if (v->fmt != f) {
		memset(v->open, 0, sizeof v->open);
		if (f == F_PE) { v->open[1] = 1; v->open[2] = 1; }
		else if (f == F_ELF) { v->open[0] = 1; v->open[1] = 1; }
		else { v->open[0] = 1; v->open[1] = 1; v->open[2] = 1; }
		v->fmt = f;
	}

	if (f == F_PE) {
		set_titles(v, s_titles[0], s_titles[1], s_titles[2], s_titles[3]);
		build_pe(v, rd, u, size);
	}
	else if (f == F_ELF)  build_elf(v, rd, u, size);
	else                  build_macho(v, rd, u, size);
}

static int row_h(const NtView* d, int i)
{
	const NtRow* r = &d->rows[i];
	if (r->hdr == 1) return HDR_H;
	if (!d->open[r->group]) return 0;
	return r->hdr == 2 ? COLH_H : ROW_H;
}

static int content_h(const NtView* d)
{
	int i, h = 0;
	for (i = 0; i < d->nrows; ++i) h += row_h(d, i);
	return h + 8;
}

static int max_scroll(const NtView* d)
{
	int m = content_h(d) - d->height;
	return m > 0 ? m : 0;
}

static void clamp_scroll(NtView* d)
{
	int m = max_scroll(d);
	if (d->scroll > m) d->scroll = m;
	if (d->scroll < 0) d->scroll = 0;
}

static int sb_thumb(const NtView* d, int* ty, int* th)
{
	int ch = content_h(d), trk = d->height - 2 * SB_PAD, m = ch - d->height;
	if (m <= 0 || trk <= 0) return 0;
	*th = (int)((long long)trk * d->height / ch);
	if (*th < SB_MIN) *th = SB_MIN;
	if (*th > trk) *th = trk;
	*ty = SB_PAD + (int)((long long)d->scroll * (trk - *th) / m);
	return 1;
}

static int hit_row(const NtView* d, int my)
{
	int i, y = -d->scroll;
	for (i = 0; i < d->nrows; ++i) {
		int h = row_h(d, i);
		if (my >= y && my < y + h) return i;
		y += h;
	}
	return -1;
}

static int hit_group(const NtView* d, int my)
{
	int i = hit_row(d, my);
	return (i >= 0 && d->rows[i].hdr == 1) ? d->rows[i].group : -1;
}

static void draw_split(PlCanvas* c, PlFont* f, int l, int top, int bot, int r,
	const char* s, PlColor dim, PlColor hi, int all_dim)
{
	char pre[32];
	size_t k = 0;
	PlRect rp = { l, top, r, bot };
	if (!s[0]) return;
	if (all_dim) {
		k = strlen(s);
	}
	else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		k = 2;
		while (s[k] == '0') ++k;
		if (!s[k]) k = strlen(s);
	}
	if (k >= sizeof pre) k = sizeof pre - 1;
	memcpy(pre, s, k);
	pre[k] = 0;
	if (k) pl_text(c, f, rp, pre, dim, PL_TEXT_ELLIPSIS);
	if (s[k]) {
		PlRect rs = { l + NT_TEXT_W(f, pre), top, r, bot };
		if (rs.l < rs.r) pl_text(c, f, rs, s + k, hi, PL_TEXT_ELLIPSIS);
	}
}

static int next_flag(const char** p, char* tok, size_t cap)
{
	const char* s = *p;
	size_t n = 0;
	if (!*s) return 0;
	while (*s && !(s[0] == ' ' && s[1] == '|' && s[2] == ' ')) {
		if (n + 1 < cap) tok[n++] = *s;
		++s;
	}
	tok[n] = 0;
	if (*s) s += 3;
	*p = s;
	return 1;
}

static void draw_chips(NtView* d, PlCanvas* c, int x, int y, int rh, int right,
	const char* flags)
{
	char tok[48];
	const char* p = flags;
	int top = y + (rh - CHIP_H) / 2;
	while (next_flag(&p, tok, sizeof tok)) {
		int cw = NT_TEXT_W(d->fnt_s, tok) + 14;
		if (x + cw > right) {
			int more = 1;
			char buf[16];
			PlRect rm = { x, y, right, y + rh };
			while (next_flag(&p, tok, sizeof tok)) ++more;
			snprintf(buf, sizeof buf, "+%d", more);
			pl_text(c, d->fnt_s, rm, buf, CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
			return;
		}
		{
			PlRect rb = { x + 7, top, x + cw - 7, top + CHIP_H };
			pl_fill(c, x, top, x + cw, top + CHIP_H, CLR_NT_CHIP_BORDER);
			pl_fill(c, x + 1, top + 1, x + cw - 1, top + CHIP_H - 1, CLR_NT_CHIP_BG);
			pl_text(c, d->fnt_s, rb, tok, CLR_NT_AMBER, PL_TEXT_ELLIPSIS);
		}
		x += cw + 6;
	}
}

static void sb_pill(PlCanvas* c, int l, int t, int r, int b, PlColor col)
{
	static const int inset[4] = { 2, 1, 0, 0 };
	int i;
	for (i = 0; i < 4; ++i) {
		pl_fill(c, l + inset[i], t + i, r - inset[i], t + i + 1, col);
		pl_fill(c, l + inset[i], b - i - 1, r - inset[i], b - i, col);
	}
	pl_fill(c, l, t + 4, r, b - 4, col);
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
	NtView* d = (NtView*)u;
	const char* msg;
	int i, y;
	int ty, th;

	d->width = w; d->height = h;
	clamp_scroll(d);
	y = -d->scroll;
	pl_fill(c, 0, 0, w, h, CLR_PANEL_BODY);

	for (i = 0; i < d->nrows && y < h; ++i) {
		const NtRow* r = &d->rows[i];
		int rh = row_h(d, i);
		if (!rh) continue;
		if (y + rh <= 0) { y += rh; continue; }

		if (r->hdr == 1) {
			PlRect t = { 16, y, w - 40, y + rh };
			int cx = w - 20, cy = y + rh / 2;
			pl_fill(c, 0, y, w, y + rh,
				d->hovered == r->group ? CLR_BTN_GHOST_HOVER : CLR_PANEL_HEADER);
			if (i > 0) pl_line(c, 0, y, w, y, CLR_NT_GROUP_LINE);
			pl_text(c, d->bold, t, translate(r->name), CLR_TEXT_PRIMARY, PL_TEXT_ELLIPSIS);
			if (d->info[r->group][0]) {
				int tw = NT_TEXT_W(d->fnt_s, d->info[r->group]);
				PlRect ri = { cx - 20 - tw, y, cx - 20, y + rh };
				if (ri.l > 160)
					pl_text(c, d->fnt_s, ri, d->info[r->group],
						CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
			}
			if (d->open[r->group]) {
				pl_line(c, cx - 4, cy - 2, cx, cy + 2, CLR_TEXT_SECONDARY);
				pl_line(c, cx, cy + 2, cx + 5, cy - 3, CLR_TEXT_SECONDARY);
			}
			else {
				pl_line(c, cx - 2, cy - 4, cx + 2, cy, CLR_TEXT_SECONDARY);
				pl_line(c, cx + 2, cy, cx - 3, cy + 5, CLR_TEXT_SECONDARY);
			}
		}
		else if (r->hdr == 2) {
			PlRect r0 = { COL_OFF,  y, COL_SIZE, y + rh };
			PlRect r1 = { COL_SIZE, y, COL_NAME, y + rh };
			PlRect r2 = { COL_NAME, y, COL_VAL,  y + rh };
			PlRect r3 = { COL_VAL,  y, COL_RAW,  y + rh };
			PlRect r4 = { COL_RAW,  y, COL_MEAN, y + rh };
			PlRect r5 = { COL_MEAN, y, w - 16,   y + rh };
			if (r5.r < r5.l + 8) r5.r = r5.l + 8;
			pl_fill(c, 0, y, w, y + rh, CLR_NT_COLHDR);
			pl_line(c, 0, y + rh - 1, w, y + rh - 1, CLR_NT_COLHDR_LINE);
			pl_text(c, d->fnt_sb, r0, translate("OFFSET"), CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
			pl_text(c, d->fnt_sb, r1, translate("SIZE"), CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
			pl_text(c, d->fnt_sb, r2, translate("FIELD"), CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
			pl_text(c, d->fnt_sb, r3, translate(r->group == 3 ? "RVA" : "VALUE"), CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
			pl_text(c, d->fnt_sb, r4, translate("RAW"), CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
			pl_text(c, d->fnt_sb, r5, translate("MEANING"), CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
		}
		else {
			PlRect rs = { COL_SIZE, y, COL_NAME, y + rh };
			PlRect rn = { COL_NAME, y, COL_VAL,  y + rh };
			PlRect rr = { COL_RAW,  y, COL_MEAN, y + rh };
			PlRect rm = { COL_MEAN, y, w - 16,   y + rh };
			if (rm.r < rm.l + 8) rm.r = rm.l + 8;

			if (i == d->selected) {
				pl_fill(c, 0, y, w, y + rh, CLR_NT_ROW_SEL);
				pl_fill(c, 0, y, 2, y + rh, CLR_TEXT_ACCENT);
			}
			else if (r->idx & 1) {
				pl_fill(c, 0, y, w, y + rh, CLR_NT_ROW_ALT);
			}

			draw_split(c, d->font, COL_OFF, y, y + rh, COL_SIZE, r->off,
				CLR_NT_ZERO, CLR_TEXT_CODE, 0);
			pl_text(c, d->font, rs, r->sz, CLR_TEXT_CODE, PL_TEXT_ELLIPSIS);
			pl_text(c, d->font, rn, r->name, CLR_TEXT_PRIMARY, PL_TEXT_ELLIPSIS);
			draw_split(c, d->font, COL_VAL, y, y + rh, COL_RAW, r->hex,
				CLR_NT_ZERO, CLR_TEXT_ACCENT, r->dim);
			if (r->raw[0])
				pl_text(c, d->fnt_s, rr, r->raw,
					r->dim ? CLR_NT_ZERO : CLR_NT_RAW, PL_TEXT_ELLIPSIS);
			if (r->kind == M_FLAGS) {
				draw_chips(d, c, COL_MEAN, y, rh, w - 16, r->note);
			}
			else if (r->note[0]) {
				PlColor col = r->kind == M_ENUM ? CLR_NT_GREEN
					: r->kind == M_TIME ? CLR_NT_AMBER
					: r->dim ? CLR_NT_ZERO
					: CLR_TEXT_SECONDARY;
				pl_text(c, d->font, rm, r->note, col, PL_TEXT_ELLIPSIS);
			}
		}
		y += rh;
	}

	msg = d->status[0] ? d->status
		: (d->nrows ? NULL : "Scan a PE file to see its headers.");
	if (msg) {
		PlRect r = { 16, y + 10, w > 40 ? w - 16 : 24, y + 34 };
		pl_text(c, d->font, r, translate(msg), CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
	}

	if (sb_thumb(d, &ty, &th)) {
		PlRect tr = { w - SB_GAP - SB_W, ty, w - SB_GAP, ty + th };
		pl_round_rect(c, tr, CLR_BTN_GHOST_BD, CLR_BTN_GHOST_BD, 1);
	}

	pl_frame_corners(c, w, h, 12, CLR_BG, CLR_PANEL_BORDER);
}

static void on_resize(void* u, int w, int h)
{
	NtView* d = (NtView*)u;
	d->width = w; d->height = h;
	clamp_scroll(d);
	pl_invalidate(d->w, NULL);
}

static void on_mouse_move(void* u, int x, int y)
{
	NtView* d = (NtView*)u;
	int hit, ty, th, hot = 0;

#ifdef _WIN32
	if (d->sb_drag && !(GetKeyState(VK_LBUTTON) & 0x8000)) {
		d->sb_drag = 0;
		pl_invalidate(d->w, NULL);
	}
#endif
	if (d->sb_drag && sb_thumb(d, &ty, &th)) {
		int track = d->height - 2 * SB_PAD - th;
		if (track > 0) {
			d->scroll = (int)((long long)(y - d->sb_drag_off - SB_PAD) * max_scroll(d) / track);
			clamp_scroll(d);
			pl_invalidate(d->w, NULL);
		}
		return;
	}

	if (sb_thumb(d, &ty, &th) && x >= d->width - SB_HIT && y >= ty && y < ty + th)
		hot = 1;
	hit = hit_group(d, y);
	if (d->hovered != hit || d->sb_hot != hot) {
		d->hovered = hit;
		d->sb_hot = hot;
		pl_invalidate(d->w, NULL);
	}
}

static void on_mouse_leave(void* u)
{
	NtView* d = (NtView*)u;
	if (d->hovered != -1 || d->sb_hot) {
		d->hovered = -1;
		d->sb_hot = 0;
		pl_invalidate(d->w, NULL);
	}
}

static void on_mouse_up(void* u, int x, int y)
{
	NtView* d = (NtView*)u;
	(void)x; (void)y;
	if (d->sb_drag) { d->sb_drag = 0; pl_invalidate(d->w, NULL); }
}

static void on_wheel(void* u, int x, int y, int delta, int mods)
{
	NtView* d = (NtView*)u;
	(void)x; (void)y; (void)mods;
	d->scroll -= delta * WHEEL_STEP;
	clamp_scroll(d);
	pl_invalidate(d->w, NULL);
}

static void on_mouse_down(void* u, int x, int y)
{
	NtView* d = (NtView*)u;
	int i, ty, th;

	if (sb_thumb(d, &ty, &th) && x >= d->width - SB_HIT) {
		if (y >= ty && y < ty + th) {
			d->sb_drag = 1;
			d->sb_drag_off = y - ty;
		}
		else {
			d->scroll += (y < ty ? -d->height : d->height);   /* page up / down */
			clamp_scroll(d);
		}
		pl_invalidate(d->w, NULL);
		return;
	}

	i = hit_row(d, y);
	if (i < 0) return;
	if (d->rows[i].hdr == 0 && d->on_goto && x >= COL_OFF && x < COL_SIZE) {
		d->selected = i;
		pl_invalidate(d->w, NULL);
		d->on_goto(d->goto_user, d->rows[i].foff, (uint64_t)d->rows[i].fsize);
		return;
	}
	if (d->rows[i].hdr == 1)
		d->open[d->rows[i].group] = !d->open[d->rows[i].group];
	else if (d->rows[i].hdr == 0)
		d->selected = (d->selected == i) ? -1 : i;
	else
		return;
	clamp_scroll(d);
	pl_invalidate(d->w, NULL);
}

static int on_hand(void* u, int x, int y)
{
	NtView* d = (NtView*)u;
	int i;
	if (!d->on_goto || x < COL_OFF || x >= COL_SIZE) return 0;
	i = hit_row(d, y);
	return i >= 0 && d->rows[i].hdr == 0;
}

static void on_destroy(void* u)
{
	NtView* d = (NtView*)u;
	pl_font_destroy(d->font);
	pl_font_destroy(d->bold);
	pl_font_destroy(d->fnt_s);
	pl_font_destroy(d->fnt_sb);
	free(d);
}

static const PlWidgetOps s_ops = {
	on_paint, on_mouse_move, on_mouse_leave, on_mouse_down, on_resize,
	on_hand, on_destroy, NULL, on_wheel, on_mouse_up
};

NtView* ntview_create(void* parent_native, int x, int y, int w, int h)
{
	NtView* v = (NtView*)calloc(1, sizeof * v);
	if (!v) return NULL;
	v->hovered = -1;
	v->selected = -1;
	v->open[1] = 1;
	v->open[2] = 1;
	v->width = w; v->height = h;
	v->font = pl_font_create(FONT_UI_PT, 0);
	v->bold = pl_font_create(FONT_UI_PT, 1);
	v->fnt_s = pl_font_create(FONT_UI_PT - 1, 0);
	v->fnt_sb = pl_font_create(FONT_UI_PT - 1, 1);
	v->w = pl_widget_create(parent_native, &s_ops, v, x, y, w, h);
	if (!v->w) {
		pl_font_destroy(v->font);
		pl_font_destroy(v->bold);
		pl_font_destroy(v->fnt_s);
		pl_font_destroy(v->fnt_sb);
		free(v);
		return NULL;
	}
	return v;
}

PlWidget* ntview_widget(const NtView* v) { return v->w; }

void ntview_set_callbacks(NtView* v, NtGotoFn on_goto, void* user)
{
	if (!v) return;
	v->on_goto = on_goto;
	v->goto_user = user;
}

void ntview_set_rect(NtView* v, int x, int y, int w, int h)
{
	pl_widget_set_rect(v->w, x, y, w, h);
}

void ntview_open(NtView* v, NtRead rd, void* user, uint64_t size)
{
	build(v, rd, user, size);
	pl_invalidate(v->w, NULL);
}

void ntview_close(NtView* v)
{
	v->nrows = 0;
	v->selected = -1;
	v->scroll = 0;
	memset(v->info, 0, sizeof v->info);
	v->status[0] = 0;
	pl_invalidate(v->w, NULL);
}