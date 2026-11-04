#ifndef _WIN32
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#endif

#include "panels/sectionsview.h"
#include "core/i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>        

#ifdef _WIN32
#include <windows.h>
#endif

#define SEC_MAX     256                 
#define SEC_ENT_CAP (8u << 20)        

enum { SS_NOFILE, SS_OK, SS_ERROR };

typedef struct {
	char name[32];                     
	uint64_t va, vsize, rawptr, rawsize;
	uint32_t chars;                   
	double entropy;
	int elf, nobits;
} SecInfo;

struct SecView {
	PlWidget* w;
	PlFont* fTitle, * fText;
	int width, height;
	int state;
	char path[2048];
	char err[128];
	char arch[32];                     /* e.g. "PE32+ (x64)" */
	SecInfo sec[SEC_MAX];
	int count;
	int sel;

	/* hit areas from the last paint */
	PlRect seg_rc[SEC_MAX], row_rc[SEC_MAX], leg_rc[SEC_MAX];
	int nseg, nrow;
	PlRect btn_entropy;
	int has_entropy;
	int hover_row, hover_btn, hover_seg;
	int scroll, content_h;             /* page scroll offset and total page height */
	PlRect sb_track, sb_thumb;
	int has_sb;

	void (*entropy_cb)(void*);
	void* entropy_user;
};

/* ================= PE parsing ================= */

static FILE* open_utf8(const char* path)
{
#ifdef _WIN32
	int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
	wchar_t* w;
	FILE* f;
	if (n <= 0) return NULL;
	w = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
	if (!w) return NULL;
	MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n);
	f = _wfopen(w, L"rb");
	free(w);
	return f;
#else
	return fopen(path, "rb");
#endif
}

static int seek64(FILE* f, uint64_t off)
{
#ifdef _WIN32
	return _fseeki64(f, (__int64)off, SEEK_SET) == 0;
#else
	return fseeko(f, (off_t)off, SEEK_SET) == 0;
#endif
}

static uint64_t file_size(FILE* f)
{
	uint64_t n;
#ifdef _WIN32
	_fseeki64(f, 0, SEEK_END);
	n = (uint64_t)_ftelli64(f);
#else
	fseeko(f, 0, SEEK_END);
	n = (uint64_t)ftello(f);
#endif
	return n;
}

static uint32_t rd16(const unsigned char* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const unsigned char* p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static double section_entropy(FILE* f, uint64_t off, uint64_t len)
{
	uint64_t cnt[256] = { 0 }, done = 0;
	unsigned char buf[65536];
	double e = 0.0;
	int i;
	if (len > SEC_ENT_CAP) len = SEC_ENT_CAP;
	if (!len || !seek64(f, off)) return 0.0;
	while (done < len) {
		size_t want = (len - done) > sizeof(buf) ? sizeof(buf) : (size_t)(len - done);
		size_t got = fread(buf, 1, want, f), k;
		if (!got) break;
		for (k = 0; k < got; ++k) cnt[buf[k]]++;
		done += got;
	}
	if (!done) return 0.0;
	for (i = 0; i < 256; ++i) {
		if (cnt[i]) {
			double p = (double)cnt[i] / (double)done;
			e -= p * log(p) / log(2.0);
		}
	}
	return e;
}

static const char* machine_name(uint32_t m)
{
	switch (m) {
	case 0x014c: return "x86";
	case 0x8664: return "x64";
	case 0xAA64: return "ARM64";
	case 0x01c4: return "ARM";
	default:     return "unknown";
	}
}

static void set_err(SecView* v, const char* msg)
{
	v->state = SS_ERROR;
	snprintf(v->err, sizeof(v->err), "%s", msg);
}

static void parse_pe(SecView* v)
{
	unsigned char dos[64], hdr[24], mg[2], sh[40];
	FILE* f;
	uint64_t fsz, lfa, table;
	uint32_t machine, nsec, optsz, magic = 0;
	unsigned i;
	int n = 0;

	v->count = 0;
	f = open_utf8(v->path);
	if (!f) { set_err(v, "Cannot open the file."); return; }
	fsz = file_size(f);

	if (!seek64(f, 0) || fread(dos, 1, 64, f) != 64 || dos[0] != 'M' || dos[1] != 'Z') {
		fclose(f); set_err(v, "Unsupported format (expected a PE or ELF file)."); return;
	}
	lfa = rd32(dos + 0x3C);
	if (lfa + 24 > fsz || !seek64(f, lfa) || fread(hdr, 1, 24, f) != 24 ||
		hdr[0] != 'P' || hdr[1] != 'E' || hdr[2] != 0 || hdr[3] != 0) {
		fclose(f); set_err(v, "Not a PE file (missing PE signature)."); return;
	}
	machine = rd16(hdr + 4);
	nsec = rd16(hdr + 6);
	optsz = rd16(hdr + 20);
	if (optsz >= 2 && seek64(f, lfa + 24) && fread(mg, 1, 2, f) == 2) magic = rd16(mg);
	snprintf(v->arch, sizeof(v->arch), "%s (%s)",
		magic == 0x20b ? "PE32+" : (magic == 0x10b ? "PE32" : "PE"), machine_name(machine));

	if (nsec == 0) { fclose(f); set_err(v, "This file has no sections."); return; }
	if (nsec > SEC_MAX) nsec = SEC_MAX;

	table = lfa + 24 + optsz;
	for (i = 0; i < nsec; ++i) {
		SecInfo* s = &v->sec[n];
		uint64_t avail = 0;
		memset(s, 0, sizeof * s);
		int k;
		if (!seek64(f, table + (uint64_t)i * 40) || fread(sh, 1, 40, f) != 40) break;
		memcpy(s->name, sh, 8);
		s->name[8] = 0;
		for (k = 0; k < 8 && s->name[k]; ++k)
			if ((unsigned char)s->name[k] < 32 || (unsigned char)s->name[k] > 126) s->name[k] = '?';
		s->vsize = rd32(sh + 8);
		s->va = rd32(sh + 12);
		s->rawsize = rd32(sh + 16);
		s->rawptr = rd32(sh + 20);
		s->chars = rd32(sh + 36);
		if (s->rawptr < fsz) {
			avail = fsz - s->rawptr;
			if (avail > s->rawsize) avail = s->rawsize;
		}
		s->entropy = section_entropy(f, s->rawptr, avail);
		++n;
	}
	fclose(f);

	if (n == 0) { set_err(v, "The section table is truncated."); return; }
	v->count = n;
	v->sel = 0;
	v->state = SS_OK;
}

static uint64_t rdn(const unsigned char* p, int n, int be)
{
	uint64_t r = 0;
	int i;
	for (i = 0; i < n; ++i) r |= (uint64_t)p[be ? n - 1 - i : i] << (8 * i);
	return r;
}

static const char* elf_machine_name(unsigned m)
{
	switch (m) {
	case 3:   return "x86";
	case 62:  return "x86-64";
	case 40:  return "ARM";
	case 183: return "AArch64";
	case 8:   return "MIPS";
	case 20:  return "PowerPC";
	case 21:  return "PowerPC64";
	case 243: return "RISC-V";
	default:  return "unknown";
	}
}

static int is_elf(const char* path)
{
	unsigned char m[4];
	FILE* f = open_utf8(path);
	int ok;
	if (!f) return 0;
	ok = fread(m, 1, 4, f) == 4 && m[0] == 0x7F && m[1] == 'E' && m[2] == 'L' && m[3] == 'F';
	fclose(f);
	return ok;
}

static void parse_elf(SecView* v)
{
	unsigned char eh[64], sh[64], nm[32];
	FILE* f;
	uint64_t fsz, shoff, stroff = 0, strsz = 0;
	unsigned shentsize, shnum, shstrndx, machine, i;
	int is64, be, hsz, n = 0, w64;
	size_t got;

	v->count = 0;
	f = open_utf8(v->path);
	if (!f) { set_err(v, "Cannot open the file."); return; }
	fsz = file_size(f);

	got = seek64(f, 0) ? fread(eh, 1, 64, f) : 0;
	if (got < 52 || (eh[4] != 1 && eh[4] != 2) || (eh[5] != 1 && eh[5] != 2)) {
		fclose(f); set_err(v, "Invalid ELF header."); return;
	}
	is64 = eh[4] == 2;
	be = eh[5] == 2;
	if (is64 && got < 64) { fclose(f); set_err(v, "Invalid ELF header."); return; }
	w64 = is64 ? 8 : 4;                                  /* width of address/offset/size fields */

	machine = (unsigned)rdn(eh + 18, 2, be);
	shoff = rdn(eh + (is64 ? 40 : 32), w64, be);
	shentsize = (unsigned)rdn(eh + (is64 ? 58 : 46), 2, be);
	shnum = (unsigned)rdn(eh + (is64 ? 60 : 48), 2, be);
	shstrndx = (unsigned)rdn(eh + (is64 ? 62 : 50), 2, be);
	hsz = is64 ? 64 : 40;

	snprintf(v->arch, sizeof(v->arch), "ELF%d (%s)%s", is64 ? 64 : 32,
		elf_machine_name(machine), be ? " BE" : "");

	if (!shoff || shentsize < (unsigned)hsz) {
		fclose(f);
		set_err(v, "No section headers (the file is stripped or packed).");
		return;
	}

	if ((shnum == 0 || shstrndx == 0xFFFF) &&
		seek64(f, shoff) && fread(sh, 1, hsz, f) == (size_t)hsz) {
		if (shnum == 0) shnum = (unsigned)rdn(sh + (is64 ? 32 : 20), w64, be);
		if (shstrndx == 0xFFFF) shstrndx = (unsigned)rdn(sh + (is64 ? 40 : 24), 4, be);
	}
	if (shnum == 0) { fclose(f); set_err(v, "This file has no sections."); return; }

	if (shstrndx < shnum && seek64(f, shoff + (uint64_t)shstrndx * shentsize) &&
		fread(sh, 1, hsz, f) == (size_t)hsz) {
		stroff = rdn(sh + (is64 ? 24 : 16), w64, be);
		strsz = rdn(sh + (is64 ? 32 : 20), w64, be);
	}

	for (i = 1; i < shnum && n < SEC_MAX; ++i) {          /* index 0 is the null section */
		SecInfo* s = &v->sec[n];
		uint32_t type, nameoff;
		uint64_t flags, addr, off, size, avail = 0;
		int k;

		if (!seek64(f, shoff + (uint64_t)i * shentsize) || fread(sh, 1, hsz, f) != (size_t)hsz) break;
		nameoff = (uint32_t)rdn(sh, 4, be);
		type = (uint32_t)rdn(sh + 4, 4, be);
		flags = rdn(sh + 8, w64, be);
		addr = rdn(sh + (is64 ? 16 : 12), w64, be);
		off = rdn(sh + (is64 ? 24 : 16), w64, be);
		size = rdn(sh + (is64 ? 32 : 20), w64, be);

		memset(s, 0, sizeof * s);
		s->elf = 1;
		s->nobits = (type == 8);                          /* SHT_NOBITS, e.g. .bss */

		if (strsz && nameoff < strsz) {
			uint64_t want = strsz - nameoff;
			if (want > sizeof(nm) - 1) want = sizeof(nm) - 1;
			if (seek64(f, stroff + nameoff)) {
				got = fread(nm, 1, (size_t)want, f);
				nm[got] = 0;
				snprintf(s->name, sizeof(s->name), "%s", (char*)nm);
			}
		}
		for (k = 0; s->name[k]; ++k)
			if ((unsigned char)s->name[k] < 32 || (unsigned char)s->name[k] > 126) s->name[k] = '?';
		if (!s->name[0]) snprintf(s->name, sizeof(s->name), "[%u]", i);

		if (flags & 2) { s->va = addr; s->vsize = size; }  /* SHF_ALLOC: occupies memory */
		s->rawptr = off;
		s->rawsize = s->nobits ? 0 : size;
		s->chars = ((flags & 2) ? 0x40000000u : 0) |       /* R = allocated */
			((flags & 1) ? 0x80000000u : 0) |              /* W = SHF_WRITE */
			((flags & 4) ? 0x20000000u : 0);               /* X = SHF_EXECINSTR */

		if (!s->nobits && off < fsz) {
			avail = fsz - off;
			if (avail > s->rawsize) avail = s->rawsize;
		}
		s->entropy = avail ? section_entropy(f, off, avail) : 0.0;
		++n;
	}
	fclose(f);

	if (n == 0) { set_err(v, "The section table is truncated."); return; }
	v->count = n;
	v->sel = 0;
	v->state = SS_OK;
}

static const char* s_std_names[] = {
	".text", ".rdata", ".data", ".rsrc", ".reloc", ".pdata",
	".idata", ".edata", ".tls", ".bss", ".CRT", ".didat", NULL
};

static int is_std_name(const char* n)
{
	int i;
	for (i = 0; s_std_names[i]; ++i) if (strcmp(n, s_std_names[i]) == 0) return 1;
	return 0;
}

/* writes a comma-separated list of structural warnings, returns how many */
static int build_flags(const SecInfo* s, char* out, size_t cap)
{
	int n = 0;
	size_t len = 0;
	const char* items[3];
	int k;
	out[0] = 0;
	if ((s->chars & 0x80000000u) && (s->chars & 0x20000000u)) items[n++] = translate("Writable + executable");
	if (s->elf ? s->name[0] != '.' : !is_std_name(s->name)) items[n++] = translate("Non-standard name");
	if (!s->nobits && s->rawsize == 0 && s->vsize > 0) items[n++] = translate("Empty on disk");
	for (k = 0; k < n; ++k) {
		int w = snprintf(out + len, cap - len, "%s%s", k ? ", " : "", items[k]);
		if (w < 0 || (size_t)w >= cap - len) break;
		len += (size_t)w;
	}
	return n;
}

static const char* describe(const char* n)
{
	if (!strcmp(n, ".text"))  return translate("Executable code. This is where the program's instructions live.");
	if (!strcmp(n, ".rdata")) return translate("Read-only data: import tables, constants and string literals.");
	if (!strcmp(n, ".data"))  return translate("Initialised global variables.");
	if (!strcmp(n, ".bss"))   return translate("Uninitialised data, zero-filled when loaded.");
	if (!strcmp(n, ".rsrc"))  return translate("Resources such as icons, dialogs and the manifest.");
	if (!strcmp(n, ".reloc")) return translate("Base relocation table, used when the image is loaded at a different address.");
	if (!strcmp(n, ".pdata")) return translate("Exception handling data (x64).");
	if (!strcmp(n, ".idata")) return translate("Import table.");
	if (!strcmp(n, ".edata")) return translate("Export table.");
	if (!strcmp(n, ".tls"))   return translate("Thread-local storage data.");
	if (!strcmp(n, ".rodata"))   return translate("Read-only data: constants and string literals.");
	if (!strcmp(n, ".plt") || !strcmp(n, ".plt.got") || !strcmp(n, ".plt.sec"))
		return translate("Procedure linkage table: stubs for calling shared library functions.");
	if (!strcmp(n, ".got") || !strcmp(n, ".got.plt"))
		return translate("Global offset table: resolved addresses of imported symbols.");
	if (!strcmp(n, ".init") || !strcmp(n, ".fini"))
		return translate("Startup / shutdown code run before and after main.");
	if (!strcmp(n, ".init_array") || !strcmp(n, ".fini_array"))
		return translate("Arrays of constructor / destructor function pointers.");
	if (!strcmp(n, ".dynsym") || !strcmp(n, ".dynstr"))
		return translate("Dynamic symbol table and names used for imports and exports.");
	if (!strcmp(n, ".symtab") || !strcmp(n, ".strtab"))
		return translate("Static symbol table and names (removed by strip).");
	if (!strcmp(n, ".dynamic")) return translate("Dynamic linking information.");
	if (!strcmp(n, ".interp"))  return translate("Path of the dynamic loader.");
	if (!strcmp(n, ".eh_frame") || !strcmp(n, ".eh_frame_hdr"))
		return translate("Stack unwinding / exception handling data.");
	if (!strcmp(n, ".comment")) return translate("Compiler version string.");
	if (!strcmp(n, ".shstrtab")) return translate("Names of the sections.");
	if (!strncmp(n, ".note", 5)) return translate("Note section with build or ABI metadata.");
	if (!strncmp(n, ".rela", 5) || !strncmp(n, ".rel.", 5))
		return translate("Relocation entries.");
	if (!strncmp(n, ".debug", 6)) return translate("DWARF debug information.");
	if (!strncmp(n, "UPX", 3)) return translate("Section name used by the UPX packer. Code is probably unpacked at runtime.");
	return translate("Custom section. Check what it contains in the Hex viewer.");
}

static void fmt_size(char* out, size_t cap, uint64_t n)
{
	if (n >= 1048576) snprintf(out, cap, "%.2f MB", (double)n / 1048576.0);
	else snprintf(out, cap, "%.1f KB", (double)n / 1024.0);
}

static const char* base_name(const char* path)
{
	const char* name = path, * p;
	for (p = path; *p; ++p) if (*p == '\\' || *p == '/') name = p + 1;
	return name;
}

/* ================= painting ================= */

#define SC_RED    ((PlColor)0xD9534F)
#define SC_AMBER  ((PlColor)0xF0AD4E)
#define SC_GREEN  ((PlColor)0x5CB85C)
#define SC_WHITE  ((PlColor)0xFFFFFF)

static const PlColor s_pal[6] = {
	(PlColor)0x4C82F5, (PlColor)0x2FA6A0, (PlColor)0x8E6BD8,
	(PlColor)0xD8843A, (PlColor)0x6C7A94, (PlColor)0xD9534F
};

static void put_text(PlCanvas* c, PlFont* f, int x, int y, int w, int h,
	const char* s, PlColor col)
{
	PlRect r = { x, y, x + w, y + h };
	pl_text(c, f, r, s, col, PL_TEXT_LEFT);
}

static void draw_card(PlCanvas* c, int x, int y, int w, int h)
{
	PlRect r = { x, y, x + w, y + h };
	pl_round_rect(c, r, CLR_PANEL, CLR_PANEL_BORDER, 8);
}

static void draw_button(SecView* v, PlCanvas* c, PlRect r, const char* label, int hovered)
{
	PlRect tr = { r.l + 14, r.t, r.r - 8, r.b };
	pl_round_rect(c, r, CLR_BG, hovered ? CLR_ACCENT : CLR_PANEL_BORDER, 6);
	pl_text(c, v->fText, tr, label, CLR_TEXT_PRIMARY, PL_TEXT_LEFT);
}

static int draw_message(SecView* v, PlCanvas* c, int x, int y, int w,
	const char* l1, PlColor c1, const char* l2)
{
	int h = l2 ? 68 : 46;
	draw_card(c, x, y, w, h);
	put_text(c, v->fText, x + 16, y + 12, w - 32, 20, l1, c1);
	if (l2) put_text(c, v->fText, x + 16, y + 36, w - 32, 20, l2, CLR_TEXT_SECONDARY);
	return h;
}

/* R / W / X badges */
static void draw_access(SecView* v, PlCanvas* c, int x, int y, const SecInfo* s)
{
	static const char* letters[3] = { "R", "W", "X" };
	int on[3], k, bad;
	on[0] = (s->chars & 0x40000000u) != 0;
	on[1] = (s->chars & 0x80000000u) != 0;
	on[2] = (s->chars & 0x20000000u) != 0;
	bad = on[1] && on[2];
	for (k = 0; k < 3; ++k) {
		PlRect r = { x + k * 23, y, x + k * 23 + 20, y + 20 };
		PlRect tr = { r.l + 6, r.t, r.r, r.b };
		PlColor fill = !on[k] ? CLR_BG
			: bad ? SC_RED
			: (k == 1 ? SC_AMBER : (k == 2 ? CLR_ACCENT : SC_GREEN));
		pl_round_rect(c, r, fill, on[k] ? fill : CLR_PANEL_BORDER, 4);
		pl_text(c, v->fText, tr, letters[k], on[k] ? SC_WHITE : CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
	}
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
	SecView* v = (SecView*)u;
	int x = 30, y, tw = w - 2 * x, i, maxs;
	char line[256];

	v->width = w; v->height = h;
	v->nseg = v->nrow = 0;
	v->has_entropy = 0;
	v->has_sb = 0;
	maxs = v->content_h - h;
	if (maxs < 0) maxs = 0;
	if (v->scroll > maxs) v->scroll = maxs;
	if (v->scroll < 0) v->scroll = 0;
	y = 14 - v->scroll;
	pl_fill(c, 0, 0, w, h, CLR_BG);
	{
		PlRect frame = { 0, 0, w - 1, h - 1 };
		pl_round_rect(c, frame, CLR_PANEL_BODY, CLR_PANEL_BORDER, 12);
	}
	if (tw < 40) return;

	/* ---- header ---- */
	put_text(c, v->fTitle, x, y, tw, 24, translate("Sections"), CLR_TEXT_PRIMARY);
	y += 24;
	if (v->state == SS_OK) snprintf(line, sizeof(line), "%s  -  %s", base_name(v->path), v->arch);
	else snprintf(line, sizeof(line), "%s", translate("PE section table"));
	put_text(c, v->fText, x, y, tw, 20, line, CLR_TEXT_SECONDARY);
	y += 30;
	pl_fill(c, x, y, x + tw, y + 1, CLR_PANEL_BORDER);
	y += 16;

	if (v->state == SS_NOFILE) {
		draw_message(v, c, x, y, tw,
			translate("No file to inspect."), CLR_TEXT_PRIMARY,
			translate("Scan a file first (Ctrl+O or drag one here)."));
		return;
	}
	if (v->state == SS_ERROR) {
		draw_message(v, c, x, y, tw, v->err, SC_RED, NULL);
		return;
	}

	/* ---- summary cards ---- */
	{
		uint64_t tr = 0, tv = 0;
		int warn = 0, gap = 12, cw = (tw - 3 * gap) / 4, k;
		char vals[4][32], tmp[96];
		const char* labels[4];
		PlColor vcol[4];

		for (i = 0; i < v->count; ++i) {
			tr += v->sec[i].rawsize;
			tv += v->sec[i].vsize;
			if (build_flags(&v->sec[i], tmp, sizeof(tmp))) ++warn;
		}
		snprintf(vals[0], sizeof(vals[0]), "%d", v->count);
		fmt_size(vals[1], sizeof(vals[1]), tr);
		fmt_size(vals[2], sizeof(vals[2]), tv);
		snprintf(vals[3], sizeof(vals[3]), "%d", warn);
		labels[0] = translate("Sections");
		labels[1] = translate("Total raw size");
		labels[2] = translate("Total virtual size");
		labels[3] = translate("With warnings");
		for (k = 0; k < 4; ++k) vcol[k] = CLR_TEXT_PRIMARY;
		if (warn > 0) vcol[3] = SC_RED;

		for (k = 0; k < 4; ++k) {
			int cx = x + k * (cw + gap);
			draw_card(c, cx, y, cw, 58);
			put_text(c, v->fTitle, cx + 14, y + 8, cw - 28, 24, vals[k], vcol[k]);
			put_text(c, v->fText, cx + 14, y + 34, cw - 28, 20, labels[k], CLR_TEXT_SECONDARY);
		}
		y += 58 + 16;
	}

	/* ---- image layout map ---- */
	{
		int n = v->count, gap = 3, rgap = 4, rowh = 28, bx = x + 16, bw = tw - 32;
		int minw[SEC_MAX], wd[SEC_MAX], rowof[SEC_MAX];
		uint64_t vs[SEC_MAX];
		int k, s, e, rows = 0, barh, ch, cx = bx, ay;
		const SecInfo* last = &v->sec[n - 1];

		for (k = 0; k < n; ++k) {
			vs[k] = v->sec[k].vsize ? v->sec[k].vsize : 1;
			minw[k] = (int)strlen(v->sec[k].name) * 7 + 16;   /* name always fits */
			if (minw[k] > bw) minw[k] = bw;
		}

		/* pack segments into rows, then share each row's spare width by size */
		s = 0;
		while (s < n) {
			int used = 0, extra, sum = 0, big, rem;
			uint64_t sumv = 0;
			for (e = s; e < n; ++e) {
				int need = minw[e] + (e > s ? gap : 0);
				if (e > s && used + need > bw) break;
				used += need;
			}
			extra = bw - used;
			if (extra < 0) extra = 0;
			big = s;
			for (k = s; k < e; ++k) {
				sumv += vs[k];
				if (vs[k] > vs[big]) big = k;
			}
			for (k = s; k < e; ++k) {
				wd[k] = minw[k] + (int)((uint64_t)extra * vs[k] / sumv);
				sum += wd[k];
				rowof[k] = rows;
			}
			rem = bw - (sum + gap * (e - s - 1));      /* rounding leftovers */
			if (rem > 0) wd[big] += rem;
			++rows;
			s = e;
		}

		barh = rows * rowh + (rows - 1) * rgap;
		ch = 32 + barh + 34;
		draw_card(c, x, y, tw, ch);
		put_text(c, v->fText, x + 16, y + 8, tw - 32, 20,
			translate("Image layout"), CLR_TEXT_SECONDARY);

		for (k = 0; k < n; ++k) {
			PlRect r, tr;
			PlColor fc = s_pal[k % 6];
			int ry = y + 32 + rowof[k] * (rowh + rgap);
			if (k == 0 || rowof[k] != rowof[k - 1]) cx = bx;
			r.l = cx; r.t = ry; r.r = cx + wd[k]; r.b = ry + rowh;
			pl_round_rect(c, r, fc,
				(k == v->sel || k == v->hover_seg) ? CLR_TEXT_PRIMARY : fc, 5);
			tr.l = r.l + 6; tr.t = r.t; tr.r = r.r - 2; tr.b = r.b;
			pl_text(c, v->fText, tr, v->sec[k].name, SC_WHITE, PL_TEXT_LEFT);
			v->seg_rc[k] = r;
			cx += wd[k] + gap;
		}
		v->nseg = n;

		ay = y + 32 + barh + 8;
		{
			uint64_t lo = ~0ull, hi = 0;
			for (k = 0; k < n; ++k) {
				if (!v->sec[k].vsize) continue;
				if (v->sec[k].va < lo) lo = v->sec[k].va;
				if (v->sec[k].va + v->sec[k].vsize > hi) hi = v->sec[k].va + v->sec[k].vsize;
			}
			if (lo == ~0ull) lo = 0;
			snprintf(line, sizeof(line), "0x%08X", (unsigned)lo);
			put_text(c, v->fText, bx, ay, 120, 20, line, CLR_TEXT_SECONDARY);
			snprintf(line, sizeof(line), "0x%08X", (unsigned)hi);
		}
		put_text(c, v->fText, bx + bw - 90, ay, 100, 20, line, CLR_TEXT_SECONDARY);
		{
			int ci = v->hover_seg >= 0 ? v->hover_seg : v->sel;
			if (ci >= 0 && ci < n) {
				char sz[32];
				fmt_size(sz, sizeof(sz), v->sec[ci].vsize);
				snprintf(line, sizeof(line), "%s  -  %s", v->sec[ci].name, sz);
				put_text(c, v->fText, bx + bw / 2 - (int)strlen(line) * 3, ay,
					bw / 2, 20, line, CLR_TEXT_PRIMARY);
			}
		}
		y += ch + 16;
	}

	/* ---- section table ---- */
	{
		int rh = 30, hh = 28, th, k;
		int cx[6];
		static const int pct[6] = { 0, 20, 33, 46, 59, 70 };

		th = hh + v->count * rh;           /* every section, no cap */
		draw_card(c, x, y, tw, th);

		for (k = 0; k < 6; ++k) cx[k] = x + 16 + (tw - 32) * pct[k] / 100;
		put_text(c, v->fText, cx[0], y + 4, 120, 20, translate("Name"), CLR_TEXT_SECONDARY);
		put_text(c, v->fText, cx[1], y + 4, 120, 20, translate("Address"), CLR_TEXT_SECONDARY);
		put_text(c, v->fText, cx[2], y + 4, 120, 20, translate("Virtual size"), CLR_TEXT_SECONDARY);
		put_text(c, v->fText, cx[3], y + 4, 120, 20, translate("Raw size"), CLR_TEXT_SECONDARY);
		put_text(c, v->fText, cx[4], y + 4, 80, 20, translate("Access"), CLR_TEXT_SECONDARY);
		put_text(c, v->fText, cx[5], y + 4, 160, 20, translate("Warnings"), CLR_TEXT_SECONDARY);
		pl_fill(c, x + 1, y + hh - 1, x + tw - 1, y + hh, CLR_PANEL_BORDER);

		for (k = 0; k < v->count; ++k) {
			const SecInfo* s = &v->sec[k];
			int ry = y + hh + k * rh;
			PlRect rr = { x + 1, ry, x + tw - 1, ry + rh };
			char fl[96];
			int nf = build_flags(s, fl, sizeof(fl));

			if (k == v->sel || k == v->hover_row) pl_fill(c, rr.l, rr.t, rr.r, rr.b, CLR_SIDEBAR_SEL);
			if (k == v->sel) pl_fill(c, rr.l, rr.t, rr.l + 3, rr.b, CLR_ACCENT);

			pl_fill(c, cx[0], ry + 10, cx[0] + 10, ry + 20, s_pal[k % 6]);
			put_text(c, v->fText, cx[0] + 18, ry + 5, 100, 20, s->name, CLR_TEXT_PRIMARY);
			snprintf(line, sizeof(line), "0x%08X", (unsigned)s->va);
			put_text(c, v->fText, cx[1], ry + 5, 110, 20, line, CLR_TEXT_PRIMARY);
			snprintf(line, sizeof(line), "0x%08X", (unsigned)s->vsize);
			put_text(c, v->fText, cx[2], ry + 5, 110, 20, line, CLR_TEXT_PRIMARY);
			snprintf(line, sizeof(line), "0x%08X", (unsigned)s->rawsize);
			put_text(c, v->fText, cx[3], ry + 5, 110, 20, line, CLR_TEXT_PRIMARY);
			draw_access(v, c, cx[4], ry + 5, s);
			if (nf) put_text(c, v->fText, cx[5], ry + 5, x + tw - 16 - cx[5], 20, fl, SC_RED);
			else put_text(c, v->fText, cx[5], ry + 5, 40, 20, "-", CLR_TEXT_SECONDARY);

			v->row_rc[k] = rr;
		}
		v->nrow = v->count;
		y += th + 16;
	}

	/* ---- detail card ---- */
	if (v->sel >= 0 && v->sel < v->count) {
		const SecInfo* s = &v->sec[v->sel];
		int colw = (tw - 32) / 5, k;
		char fl[96], val[48];
		int nf = build_flags(s, fl, sizeof(fl));

		draw_card(c, x, y, tw, 128);
		put_text(c, v->fTitle, x + 16, y + 10, 200, 24, s->name, CLR_TEXT_PRIMARY);

		if (v->entropy_cb) {
			PlRect b = { x + tw - 16 - 180, y + 8, x + tw - 16, y + 36 };
			draw_button(v, c, b, translate("View in Entropy tab"), v->hover_btn);
			v->btn_entropy = b; v->has_entropy = 1;
		}

		for (k = 0; k < 5; ++k) {
			static const char* keys[5] = { "Address", "Virtual size", "Raw size", "Entropy", "Access" };
			int cx = x + 16 + k * colw;
			put_text(c, v->fText, cx, y + 42, colw, 20, translate(keys[k]), CLR_TEXT_SECONDARY);
			switch (k) {
			case 0: snprintf(val, sizeof(val), "0x%08X", (unsigned)s->va); break;
			case 1: fmt_size(val, sizeof(val), s->vsize); break;
			case 2: fmt_size(val, sizeof(val), s->rawsize); break;
			case 3: snprintf(val, sizeof(val), "%.2f / 8.00", s->entropy); break;
			default: val[0] = 0; break;
			}
			if (k == 4) draw_access(v, c, cx, y + 62, s);
			else put_text(c, v->fText, cx, y + 62, colw, 20, val, CLR_TEXT_PRIMARY);
		}
		put_text(c, v->fText, x + 16, y + 90, tw - 32, 20, describe(s->name), CLR_TEXT_SECONDARY);
		if (nf) put_text(c, v->fText, x + 16, y + 106, tw - 32, 20, fl, SC_RED);
	}

	/* ---- page height and scrollbar ---- */
	v->content_h = y + v->scroll + 128 + 16;       /* detail card + bottom padding */
	if (v->content_h > h) {
		int maxs2 = v->content_h - h;
		int tl = h * h / v->content_h, pos;
		PlRect trk, thb;
		if (tl < 28) tl = 28;
		if (v->scroll > maxs2) { v->scroll = maxs2; pl_invalidate(v->w, NULL); }
		pos = (h - tl) * v->scroll / maxs2;
		trk.l = w - 16; trk.t = 0; trk.r = w; trk.b = h;
		thb.l = w - 6; thb.t = pos; thb.r = w - 1; thb.b = pos + tl;
		pl_round_rect(c, thb, CLR_PANEL_BORDER, CLR_PANEL_BORDER, 6);
		v->sb_track = trk; v->sb_thumb = thb; v->has_sb = 1;
	}
}

/* ================= input ================= */

static int in_rect(PlRect r, int x, int y)
{
	return x >= r.l && x < r.r && y >= r.t && y < r.b;
}

static int hit_row(SecView* v, int x, int y)
{
	int i;
	for (i = 0; i < v->nrow; ++i) if (in_rect(v->row_rc[i], x, y)) return i;
	return -1;
}

static void on_mouse_move(void* u, int x, int y)
{
	SecView* v = (SecView*)u;
	int row = hit_row(v, x, y);
	int btn = v->has_entropy && in_rect(v->btn_entropy, x, y);
	int seg = -1, i;
	for (i = 0; i < v->nseg; ++i)
		if (in_rect(v->seg_rc[i], x, y)) { seg = i; break; }
	if (row != v->hover_row || btn != v->hover_btn || seg != v->hover_seg) {
		v->hover_row = row;
		v->hover_btn = btn;
		v->hover_seg = seg;
		pl_invalidate(v->w, NULL);
	}
}

static void on_mouse_leave(void* u)
{
	SecView* v = (SecView*)u;
	if (v->hover_row != -1 || v->hover_btn || v->hover_seg != -1) {
		v->hover_row = -1;
		v->hover_btn = 0;
		v->hover_seg = -1;
		pl_invalidate(v->w, NULL);
	}
}
static void on_mouse_down(void* u, int x, int y)
{
	SecView* v = (SecView*)u;
	int i, row;

	if (v->has_entropy && in_rect(v->btn_entropy, x, y)) {
		if (v->entropy_cb) v->entropy_cb(v->entropy_user);
		return;
	}
	if (v->has_sb && in_rect(v->sb_track, x, y)) {     /* click the bar to jump */
		int maxs = v->content_h - v->height;
		int tl = v->sb_thumb.b - v->sb_thumb.t;
		if (maxs > 0 && v->height > tl) {
			v->scroll = (y - tl / 2) * maxs / (v->height - tl);
			if (v->scroll < 0) v->scroll = 0;
			if (v->scroll > maxs) v->scroll = maxs;
			pl_invalidate(v->w, NULL);
		}
		return;
	}
	for (i = 0; i < v->nseg; ++i) {
		if (in_rect(v->seg_rc[i], x, y)) {
			v->sel = i;
			pl_invalidate(v->w, NULL);
			return;
		}
	}
	row = hit_row(v, x, y);
	if (row >= 0 && row != v->sel) {
		v->sel = row;
		pl_invalidate(v->w, NULL);
	}
}

static void on_resize(void* u, int w, int h)
{
	SecView* v = (SecView*)u;
	v->width = w; v->height = h;
	pl_invalidate(v->w, NULL);
}

static void on_destroy(void* u)
{
	SecView* v = (SecView*)u;
	pl_font_destroy(v->fTitle);
	pl_font_destroy(v->fText);
	free(v);
}

static void on_wheel(void* u, int x, int y, int delta, int mods)
{
	SecView* v = (SecView*)u;
	(void)x; (void)y; (void)mods;
	if (!delta) return;
	secview_scroll(v, delta > 0 ? -60 : 60);     /* 60 px per notch */
}

static int on_hand(void* u, int x, int y)
{
	SecView* v = (SecView*)u;
	int i;
	if (v->has_entropy && in_rect(v->btn_entropy, x, y)) return 1;
	if (v->has_sb && in_rect(v->sb_track, x, y)) return 1;
	for (i = 0; i < v->nseg; ++i) if (in_rect(v->seg_rc[i], x, y)) return 1;
	return hit_row(v, x, y) >= 0;
}

static const PlWidgetOps s_sec_ops = {
	on_paint, on_mouse_move, on_mouse_leave, on_mouse_down,
	on_resize, on_hand, on_destroy,
	NULL, on_wheel
};

/* ================= public API ================= */

SecView* secview_create(void* parent_native, int x, int y, int w, int h)
{
	SecView* v = (SecView*)calloc(1, sizeof * v);
	if (!v) return NULL;
	v->state = SS_NOFILE;
	v->width = w; v->height = h;
	v->hover_row = -1;
	v->hover_seg = -1;
	v->fTitle = pl_font_create(12, 1);
	v->fText = pl_font_create(9, 0);
	v->w = pl_widget_create(parent_native, &s_sec_ops, v, x, y, w, h);
	if (!v->w) {
		pl_font_destroy(v->fTitle);
		pl_font_destroy(v->fText);
		free(v);
		return NULL;
	}
	return v;
}

PlWidget* secview_widget(const SecView* v) { return v->w; }

void secview_set_rect(SecView* v, int x, int y, int w, int h)
{
	if (v) pl_widget_set_rect(v->w, x, y, w, h);
}

void secview_set_callbacks(SecView* v, void (*open_entropy)(void*), void* user)
{
	if (!v) return;
	v->entropy_cb = open_entropy;
	v->entropy_user = user;
}

void secview_set_file(SecView* v, const char* utf8_path)
{
	if (!v) return;
	v->count = 0;
	v->sel = 0;
	v->hover_row = -1;
	v->hover_btn = 0;
	v->hover_seg = -1;
	v->scroll = 0;
	v->content_h = 0;
	v->err[0] = 0;
	v->arch[0] = 0;
	if (utf8_path && utf8_path[0]) {
		snprintf(v->path, sizeof(v->path), "%s", utf8_path);
		if (is_elf(v->path)) parse_elf(v);
		else parse_pe(v);
	}
	else {
		v->path[0] = 0;
		v->state = SS_NOFILE;
	}
	pl_invalidate(v->w, NULL);
}

void secview_scroll(SecView* v, int delta)
{
	int maxs;
	if (!v) return;
	maxs = v->content_h - v->height;
	if (maxs < 0) maxs = 0;
	v->scroll += delta;
	if (v->scroll < 0) v->scroll = 0;
	if (v->scroll > maxs) v->scroll = maxs;
	pl_invalidate(v->w, NULL);
}