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

typedef uint32_t NtClr;
#ifndef NT_RGB
#define NT_RGB(r, g, b) ((NtClr)(((unsigned)(r) << 16) | ((unsigned)(g) << 8) | (unsigned)(b)))
#endif

#ifndef CLR_NT_ZERO
#define CLR_NT_ZERO         NT_RGB(0x66, 0x6A, 0x8C)  
#define CLR_NT_RAW          NT_RGB(0x7D, 0x83, 0xA6)  
#define CLR_NT_ROW_ALT      NT_RGB(0x2C, 0x2E, 0x3D) 
#define CLR_NT_ROW_SEL      NT_RGB(0x34, 0x38, 0x53)  
#define CLR_NT_COLHDR       NT_RGB(0x23, 0x25, 0x32)  
#define CLR_NT_COLHDR_LINE  NT_RGB(0x32, 0x35, 0x47)
#define CLR_NT_GROUP_LINE   NT_RGB(0x35, 0x38, 0x49)
#define CLR_NT_GREEN        NT_RGB(0x7F, 0xD6, 0xA4)  
#define CLR_NT_AMBER        NT_RGB(0xE6, 0xC2, 0x7A)  
#define CLR_NT_CHIP_BG      NT_RGB(0x30, 0x33, 0x4A)
#define CLR_NT_CHIP_BORDER  NT_RGB(0x5B, 0x5F, 0x7B)
#define CLR_NT_SB_THUMB     NT_RGB(0x45, 0x49, 0x66)
#define CLR_NT_SB_HOT       NT_RGB(0x5B, 0x5F, 0x7B)
#endif

#define NT_MAX_ROWS 96
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
	char sz[4];
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
	NtRow     rows[NT_MAX_ROWS];
	int       nrows;
	char      info[NT_GROUPS][56];
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
	if (r) snprintf(r->name, sizeof r->name, "%s", s_titles[g]);
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

static void build(NtView* v, NtRead rd, void* u, uint64_t size)
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
	const char* s, NtClr dim, NtClr hi, int all_dim)
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

static void sb_pill(PlCanvas* c, int l, int t, int r, int b, NtClr col)
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
				NtClr col = r->kind == M_ENUM ? CLR_NT_GREEN
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