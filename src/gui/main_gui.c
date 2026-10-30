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
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <dwmapi.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")
#ifndef TVM_SETLINECOLOR
#define TVM_SETLINECOLOR (TV_FIRST + 40)
#endif
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_font_options.h>
#include <xxwidgets/xxwidgets_context_options.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/gui_backend.h"
#include "core/config.h"
#include "resource.h"
#include "theme/theme.h"
#include "controls/sidebar.h"
#include "panels/header.h"
#include "panels/infobar.h"
#include "panels/options.h"
#include "controls/tabbar.h"
#include "controls/bottombar.h"
#include "controls/treeview.h"
#include "controls/hexviewer.h"
#include "core/tray.h"
#include "core/dlwatch.h"

 /* Store / VSIX builds get updates from Microsoft */
#ifdef CDIE_STORE_BUILD
#define CDIE_SHOW_UPDATES 0
#define CDIE_SHOW_CONTEXT 0
#else
#define CDIE_SHOW_UPDATES 1
#define CDIE_SHOW_CONTEXT 1
#endif

enum gui_action {
	ACTION_NONE, ACTION_SCAN, ACTION_BROWSE,
	ACTION_OPTIONS, ACTION_ABOUT,
	ACTION_DATABASE_PATHS,
	ACTION_SIGNATURES, ACTION_STRINGS,
	ACTION_MONITOR
};

static xxwidgets_app* g_app;
static xxwidgets_widget* g_window;
static char* g_scanPath;          /* file to scan */

/* Custom dark-chrome child windows */
static Sidebar* g_sidebar;
static Header* g_header_wnd;
static Infobar* g_infobar_wnd;
static Bottombar* g_bottombar;
static ResultsTree* g_tree;
static HexViewer* g_hex;
static int g_page = NAV_OVERVIEW;
static HANDLE g_hexFile = INVALID_HANDLE_VALUE;

static int  g_running, g_pending;
static int  g_startMonitor;
static char* g_lastPath;
static int  g_minWindowWidth = 900;
static int  g_minWindowHeight = 540;
static int g_prefsReady;

static int g_bFlagDeep = 1, g_bFlagHeur = 1, g_bFlagVerbose = 1;
static int g_bFlagAggr, g_bFlagHideUnknown, g_bFlagFormat;
static int g_bDbExtra = 1, g_bDbCustom = 1;
static void layout(void);

static void on_tray_exit(void* user)
{
	(void)user;
	g_running = 0;
	xxwidgets_app_quit(g_app, 0);
}

static HWND native(xxwidgets_widget* widget)
{
	return (HWND)xxwidgets_widget_native_handle(widget);
}

static WCHAR* utf8_to_wide(const char* text)
{
	int length;
	WCHAR* wide;
	if (!text) return NULL;
	length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
	if (length <= 0) return NULL;
	wide = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length * sizeof(WCHAR));
	if (wide) MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, length);
	return wide;
}

static char* wide_to_utf8(const WCHAR* wide)
{
	int length;
	char* text;
	if (!wide) return NULL;
	length = WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
	if (length <= 0) return NULL;
	text = (char*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)length);
	if (text) WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, length, NULL, NULL);
	return text;
}

static char* copy_utf8(const char* text)
{
	SIZE_T length = 0;
	char* copy;
	if (!text) text = "";
	while (text[length]) ++length;
	copy = (char*)HeapAlloc(GetProcessHeap(), 0, length + 1);
	if (copy) CopyMemory(copy, text, length + 1);
	return copy;
}

static void set_status(const char* text)
{
	if (g_bottombar) bottombar_set_status(g_bottombar, text);
}

static void set_status_wide(const WCHAR* wide)
{
	char* text = wide_to_utf8(wide);
	if (text) {
		set_status(text);
		HeapFree(GetProcessHeap(), 0, text);
	}
}

static void set_scan_path(const char* path)
{
	char* copy = copy_utf8(path);
	if (!copy) return;
	if (g_scanPath) HeapFree(GetProcessHeap(), 0, g_scanPath);
	g_scanPath = copy;
}

static void apply_flags_to_backend(void)
{
	gui_backend_set_flags(g_bFlagDeep, g_bFlagHeur, g_bFlagVerbose,
		g_bFlagAggr, g_bFlagHideUnknown, g_bFlagFormat);
	gui_backend_set_databases(g_bDbExtra, g_bDbCustom);
}

static int hex_read(void* u, uint64_t off, void* buf, size_t len)
{
	LARGE_INTEGER pos;
	(void)u;
	if (g_hexFile == INVALID_HANDLE_VALUE) return 0;
	pos.QuadPart = (LONGLONG)off;
	if (!SetFilePointerEx(g_hexFile, pos, NULL, FILE_BEGIN)) return 0;
	while (len) {
		DWORD got = 0, n = len > 0x100000 ? 0x100000 : (DWORD)len;
		if (!ReadFile(g_hexFile, buf, n, &got, NULL) || !got) return 0;
		buf = (char*)buf + got;
		len -= got;
	}
	return 1;
}

static void hex_close(void)
{
	if (g_hexFile != INVALID_HANDLE_VALUE) CloseHandle(g_hexFile);
	g_hexFile = INVALID_HANDLE_VALUE;
}

static void hex_open(const char* utf8_path)
{
	WCHAR* wide = utf8_to_wide(utf8_path);
	LARGE_INTEGER size;
	hex_close();
	if (!wide) return;
	g_hexFile = CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, 0, NULL);
	HeapFree(GetProcessHeap(), 0, wide);
	if (g_hexFile == INVALID_HANDLE_VALUE) return;
	if (!GetFileSizeEx(g_hexFile, &size)) { hex_close(); return; }
	if (g_hex) hexview_set_source(g_hex, (uint64_t)size.QuadPart, hex_read, NULL, 0);
}

typedef struct {
	WCHAR type_str[80];
	WCHAR size_str[80];
	WCHAR size_sub[80];
	WCHAR ep_str[80];
	WCHAR ep_sub[80];
	WCHAR base_str[80];
	WCHAR base_sub[80];
} FileInfo;

static void format_size(ULONGLONG bytes, WCHAR* main_out, WCHAR* sub_out)
{
	if (bytes >= 1024ULL * 1024) {
		ULONGLONG n100 = (bytes * 100ULL) / (1024ULL * 1024);
		wsprintfW(main_out, L"%u.%02u MiB",
			(unsigned)(n100 / 100), (unsigned)(n100 % 100));
	}
	else if (bytes >= 1024) {
		ULONGLONG n10 = (bytes * 10ULL) / 1024;
		wsprintfW(main_out, L"%u.%u KiB",
			(unsigned)(n10 / 10), (unsigned)(n10 % 10));
	}
	else {
		wsprintfW(main_out, L"%u B", (unsigned)bytes);
	}
	if (bytes < 1000000ULL) {
		wsprintfW(sub_out, L"%u bytes", (unsigned)bytes);
	}
	else {
		unsigned hi = (unsigned)(bytes / 1000000ULL);
		unsigned lo = (unsigned)(bytes % 1000000ULL);
		wsprintfW(sub_out, L"%u,%06u bytes", hi, lo);
	}
}

static void get_file_info(const char* utf8_path, const char* type_name, FileInfo* fi)
{
	WCHAR* wpath = utf8_to_wide(utf8_path);

	/* type string */
	if (type_name && type_name[0]) {
		WCHAR* wt = utf8_to_wide(type_name);
		if (wt) { lstrcpynW(fi->type_str, wt, 79); HeapFree(GetProcessHeap(), 0, wt); }
		else lstrcpyW(fi->type_str, L"Unknown");
	}
	else {
		lstrcpyW(fi->type_str, L"Unknown");
	}

	/* file size */
	lstrcpyW(fi->size_str, L"N/A");
	lstrcpyW(fi->size_sub, L"");
	if (wpath) {
		WIN32_FILE_ATTRIBUTE_DATA fa = { 0 };
		if (GetFileAttributesExW(wpath, GetFileExInfoStandard, &fa)) {
			ULONGLONG bytes =
				((ULONGLONG)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
			format_size(bytes, fi->size_str, fi->size_sub);
		}
	}

	/* default EP / base */
	lstrcpyW(fi->ep_str, L"N/A");
	lstrcpyW(fi->ep_sub, L"");
	lstrcpyW(fi->base_str, L"N/A");
	lstrcpyW(fi->base_sub, L"");

	/* Try to read PE optional header */
	if (wpath) {
		HANDLE hf = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ,
			NULL, OPEN_EXISTING, 0, NULL);
		if (hf != INVALID_HANDLE_VALUE) {
			WORD  dos_magic = 0;
			DWORD read = 0, pe_off = 0;
			if (ReadFile(hf, &dos_magic, 2, &read, NULL) && read == 2
				&& dos_magic == 0x5A4D) {
				SetFilePointer(hf, 0x3C, NULL, FILE_BEGIN);
				if (ReadFile(hf, &pe_off, 4, &read, NULL) && read == 4) {
					DWORD pe_sig = 0;
					SetFilePointer(hf, (LONG)pe_off, NULL, FILE_BEGIN);
					if (ReadFile(hf, &pe_sig, 4, &read, NULL)
						&& read == 4 && pe_sig == 0x00004550) {
						WORD opt_magic = 0;
						SetFilePointer(hf, (LONG)(pe_off + 24), NULL, FILE_BEGIN);
						if (ReadFile(hf, &opt_magic, 2, &read, NULL) && read == 2) {
							if (opt_magic == 0x10B) { /* PE32 */
								DWORD ep32 = 0, base32 = 0;
								SetFilePointer(hf, (LONG)(pe_off + 40), NULL, FILE_BEGIN);
								ReadFile(hf, &ep32, 4, &read, NULL);
								SetFilePointer(hf, (LONG)(pe_off + 52), NULL, FILE_BEGIN);
								ReadFile(hf, &base32, 4, &read, NULL);
								wsprintfW(fi->ep_str, L"0x%08X", ep32);
								wsprintfW(fi->base_str, L"0x%08X", base32);
								lstrcpyW(fi->ep_sub, L"RVA");
								lstrcpyW(fi->base_sub, L"Preferred load");
							}
							else if (opt_magic == 0x20B) { /* PE32+ */
								DWORD ep64 = 0;
								ULONGLONG base64 = 0;
								SetFilePointer(hf, (LONG)(pe_off + 40), NULL, FILE_BEGIN);
								ReadFile(hf, &ep64, 4, &read, NULL);
								SetFilePointer(hf, (LONG)(pe_off + 48), NULL, FILE_BEGIN);
								ReadFile(hf, &base64, 8, &read, NULL);
								wsprintfW(fi->ep_str, L"0x%08X", ep64);
								if (base64 > 0xFFFFFFFF)
									wsprintfW(fi->base_str, L"0x%08X%08X",
										(unsigned)(base64 >> 32),
										(unsigned)(base64 & 0xFFFFFFFF));
								else
									wsprintfW(fi->base_str, L"0x%08X",
										(unsigned)base64);
								lstrcpyW(fi->ep_sub, L"RVA");
								lstrcpyW(fi->base_sub, L"Preferred load");
							}
						}
					}
				}
			}
			CloseHandle(hf);
		}
		HeapFree(GetProcessHeap(), 0, wpath);
	}
}

static void w2u(const WCHAR* w, char* out, int cap)
{
	out[0] = 0;
	if (w && !WideCharToMultiByte(CP_UTF8, 0, w, -1, out, cap, NULL, NULL))
		out[0] = 0;
}

static void update_chrome_after_scan(const char* utf8_path, const char* type_name)
{
	FileInfo fi = { { 0 },{ 0 },{ 0 },{ 0 },{ 0 },{ 0 },{ 0 } };

	get_file_info(utf8_path, type_name, &fi);

	if (g_header_wnd) {
		const char* last = NULL, * p;
		for (p = utf8_path; *p; ++p)
			if (*p == '\\' || *p == '/') last = p;
		if (last) {
			char* folder = copy_utf8(utf8_path);
			if (folder) {
				folder[last - utf8_path + 1] = 0;
				header_set_path(g_header_wnd, folder, last + 1);
				HeapFree(GetProcessHeap(), 0, folder);
			}
		}
		else {
			header_set_path(g_header_wnd, "", utf8_path);
		}
	}

	if (g_infobar_wnd) {
		char type[256], size[256], size_sub[256];
		char ep[256], ep_sub[256], base[256], base_sub[256];
		InfoCard cards[4];

		w2u(fi.type_str, type, 256);
		w2u(fi.size_str, size, 256);
		w2u(fi.size_sub, size_sub, 256);
		w2u(fi.ep_str, ep, 256);
		w2u(fi.ep_sub, ep_sub, 256);
		w2u(fi.base_str, base, 256);
		w2u(fi.base_sub, base_sub, 256);

		cards[0].label = "FILE TYPE";    cards[0].value = type; cards[0].subvalue = "";
		cards[1].label = "FILE SIZE";    cards[1].value = size; cards[1].subvalue = size_sub;
		cards[2].label = "ENTRY POINT";  cards[2].value = ep;   cards[2].subvalue = ep_sub;
		cards[3].label = "BASE ADDRESS"; cards[3].value = base; cards[3].subvalue = base_sub;

		infobar_set_cards(g_infobar_wnd, cards, 4);
	}
}

static void clear_scan(void)
{
	if (g_tree) resultstree_clear(g_tree);
	if (g_infobar_wnd) infobar_clear(g_infobar_wnd);
	if (g_header_wnd)  header_clear(g_header_wnd);
}

static void collect_result(const char* type, const char* name,
	const char* version, const char* info, void* user_data)
{
	++*(unsigned int*)user_data;
	resultstree_add(g_tree, type, name, version, info);
}

static void do_scan(void)
{
	char* path, * type = NULL;
	unsigned int count = 0, msec = 0;
	LARGE_INTEGER frequency, started, ended;
	int ok;

	if (config_db_required()) {
		set_status("Select the signature database folder first.");
		g_pending = ACTION_DATABASE_PATHS;
		return;
	}
	if (!g_scanPath || !g_scanPath[0]) { g_pending = ACTION_BROWSE; return; }
	path = copy_utf8(g_scanPath);
	if (!path) { set_status("Out of memory."); return; }

	apply_flags_to_backend();
	clear_scan();
	set_status("Scanning the selected file...");
	UpdateWindow(native(g_window));

	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&started);
	ok = gui_backend_scan_results_type(path, 0, collect_result, &count, NULL, &type);
	QueryPerformanceCounter(&ended);

	if (ok) {
		WCHAR text[512];
		WCHAR* type_name = utf8_to_wide(type && type[0] ? type : "Unknown type");
		if (frequency.QuadPart > 0)
			msec = (unsigned int)(((ended.QuadPart - started.QuadPart) * 1000)
				/ frequency.QuadPart);
		wsprintfW(text, L"Scan complete (%s). %u result%s in %u ms.",
			type_name ? type_name : L"Unknown type",
			count, count == 1 ? L"" : L"s", msec);
		set_status_wide(text);
		if (type_name) HeapFree(GetProcessHeap(), 0, type_name);

		update_chrome_after_scan(path, type);
		if (g_lastPath) HeapFree(GetProcessHeap(), 0, g_lastPath);
		g_lastPath = copy_utf8(path);
		hex_open(path);
	}
	else {
		set_status("Cannot open the file.");
	}
	HeapFree(GetProcessHeap(), 0, path);
	if (type) gui_backend_free(type);
}

static void do_browse(void)
{
	OPENFILENAMEW picker;
	WCHAR file[32768];
	file[0] = 0;
	{
		WCHAR* current = utf8_to_wide(g_scanPath);
		if (current) {
			lstrcpynW(file, current, (int)(sizeof(file) / sizeof(file[0])));
			HeapFree(GetProcessHeap(), 0, current);
		}
	}
	ZeroMemory(&picker, sizeof(picker));
	picker.lStructSize = sizeof(picker);
	picker.hwndOwner = native(g_window);
	picker.lpstrFile = file;
	picker.nMaxFile = (DWORD)(sizeof(file) / sizeof(file[0]));
	picker.lpstrFilter = L"All files\0*.*\0";
	picker.lpstrTitle = L"Select a file to scan";
	picker.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
	if (GetOpenFileNameW(&picker)) {
		char* path = wide_to_utf8(file);
		if (path) {
			set_scan_path(path);
			HeapFree(GetProcessHeap(), 0, path);
			do_scan();
		}
	}
}

static void opt_get_scan(void* u, OptionsScan* o)
{
	(void)u;
	o->deep = g_bFlagDeep; o->heuristic = g_bFlagHeur; o->verbose = g_bFlagVerbose;
	o->aggressive = g_bFlagAggr; o->hide_unknown = g_bFlagHideUnknown;
	o->format = g_bFlagFormat;
	o->db_extra = g_bDbExtra; o->db_custom = g_bDbCustom;
	o->monitor_downloads = dlwatch_enabled();
}

static int opt_set_scan(void* u, const OptionsScan* o)
{
	(void)u;
	g_bFlagDeep = o->deep; g_bFlagHeur = o->heuristic; g_bFlagVerbose = o->verbose;
	g_bFlagAggr = o->aggressive; g_bFlagHideUnknown = o->hide_unknown;
	g_bFlagFormat = o->format;
	g_bDbExtra = o->db_extra; g_bDbCustom = o->db_custom;
	if ((o->monitor_downloads != 0) != (dlwatch_enabled() != 0))
		dlwatch_set_enabled(o->monitor_downloads ? 1 : 0);
	apply_flags_to_backend();
	return 1;
}

static int opt_get_opt(void* u, OptionsOptimization* o)
{
	gui_backend_optimization_options cur;
	gui_backend_optimization_capabilities caps;
	(void)u;
	if (!gui_backend_get_optimization_options(&cur) ||
		!gui_backend_get_optimization_capabilities(&caps)) return 0;
	o->buffer_size = cur.buffer_size;
	o->file_buffer_size = cur.file_buffer_size;
	o->use_sse2 = cur.use_sse2; o->use_avx2 = cur.use_avx2;
	o->has_sse2 = caps.sse2; o->has_avx2 = caps.avx2;
	return 1;
}

static int opt_set_opt(void* u, const OptionsOptimization* o)
{
	gui_backend_optimization_options cur;
	(void)u;
	if (!gui_backend_get_optimization_options(&cur)) return 0;
	cur.buffer_size = o->buffer_size;
	cur.file_buffer_size = o->file_buffer_size;
	cur.use_sse2 = o->use_sse2; cur.use_avx2 = o->use_avx2;
	return gui_backend_set_optimization_options(&cur) ? 1 : 0;
}

static int opt_browse_folder(void* u, char* out, size_t cap)
{
	BROWSEINFOW picker; LPITEMIDLIST item; WCHAR path[MAX_PATH]; int ok = 0;
	(void)u;
	ZeroMemory(&picker, sizeof(picker));
	picker.hwndOwner = GetActiveWindow();
	picker.lpszTitle = L"Select the database folder";
	picker.ulFlags = BIF_RETURNONLYFSDIRS;
	item = SHBrowseForFolderW(&picker);
	if (item) {
		if (SHGetPathFromIDListW(item, path))
			ok = WideCharToMultiByte(CP_UTF8, 0, path, -1, out, (int)cap, NULL, NULL) > 0;
		CoTaskMemFree(item);
	}
	return ok;
}

static void opt_db_changed(void* u)
{
	WCHAR text[160];
	(void)u;
	clear_scan();
	wsprintfW(text, L"Database reloaded: %d signatures. Choose a file and press Scan.",
		gui_backend_signature_count());
	set_status_wide(text);
}

typedef struct { WCHAR** names; SIZE_T count, cap; int failed; int fixed_only; } font_list;

static int CALLBACK enum_font_proc(const LOGFONTW* lf, const TEXTMETRICW* tm,
	DWORD type, LPARAM param)
{
	font_list* list = (font_list*)param;
	(void)tm;
	if (list->failed) return 0;
	if (lf->lfFaceName[0] == L'@') return 1;
	if (!(type & TRUETYPE_FONTTYPE)) return 1;
	if (list->fixed_only && !(lf->lfPitchAndFamily & FIXED_PITCH)) return 1;

	if (list->count == list->cap) {
		SIZE_T cap = list->cap ? list->cap * 2 : 128;
		WCHAR** n = list->names
			? (WCHAR**)HeapReAlloc(GetProcessHeap(), 0, list->names, cap * sizeof(*n))
			: (WCHAR**)HeapAlloc(GetProcessHeap(), 0, cap * sizeof(*n));
		if (!n) { list->failed = 1; return 0; }
		list->names = n; list->cap = cap;
	}
	{
		SIZE_T len = lstrlenW(lf->lfFaceName) + 1;
		WCHAR* copy = (WCHAR*)HeapAlloc(GetProcessHeap(), 0, len * sizeof(WCHAR));
		if (!copy) { list->failed = 1; return 0; }
		CopyMemory(copy, lf->lfFaceName, len * sizeof(WCHAR));
		list->names[list->count++] = copy;
	}
	return 1;
}

static int cmp_name(const void* a, const void* b)
{
	return lstrcmpiW(*(WCHAR* const*)a, *(WCHAR* const*)b);
}

static void enumerate_fonts(font_list* list, int fixed_only)
{
	LOGFONTW lf = { 0 };
	HDC dc = GetDC(NULL);
	lf.lfCharSet = DEFAULT_CHARSET;
	list->fixed_only = fixed_only;
	EnumFontFamiliesExW(dc, &lf, enum_font_proc, (LPARAM)list, 0);
	ReleaseDC(NULL, dc);
	if (list->count > 1)
		qsort(list->names, list->count, sizeof(WCHAR*), cmp_name);
	{
		SIZE_T i, out = 0;
		for (i = 0; i < list->count; ++i) {
			if (out && !lstrcmpiW(list->names[out - 1], list->names[i]))
				HeapFree(GetProcessHeap(), 0, list->names[i]);
			else
				list->names[out++] = list->names[i];
		}
		list->count = out;
	}
}

static int opt_list_fonts(void* u, int fixed_only,
	void (*add)(const char* family, void* ud), void* ud)
{
	font_list list = { 0 };
	SIZE_T i;
	(void)u;
	enumerate_fonts(&list, fixed_only);
	if (list.failed) {
		for (i = 0; i < list.count; ++i) HeapFree(GetProcessHeap(), 0, list.names[i]);
		if (list.names) HeapFree(GetProcessHeap(), 0, list.names);
		return 0;
	}
	for (i = 0; i < list.count; ++i) {
		char* utf8 = wide_to_utf8(list.names[i]);
		if (utf8) { add(utf8, ud); HeapFree(GetProcessHeap(), 0, utf8); }
		HeapFree(GetProcessHeap(), 0, list.names[i]);
	}
	if (list.names) HeapFree(GetProcessHeap(), 0, list.names);
	return 1;
}

static int opt_get_fonts(void* u, OptionsFont out[OPTIONS_FONT_ROLES])
{
	xxwidgets_font_options fo;
	int k;
	(void)u;
	if (xxwidgets_app_get_font_options(g_app, &fo) != XXWIDGETS_OK) return 0;
	for (k = 0; k < OPTIONS_FONT_ROLES; ++k) {
		lstrcpynA(out[k].family, fo.fonts[k].family, sizeof(out[k].family));
		out[k].point_size = fo.fonts[k].point_size;
		out[k].bold = fo.fonts[k].bold;
		out[k].italic = fo.fonts[k].italic;
	}
	return 1;
}

static int opt_set_fonts(void* u, const OptionsFont in[OPTIONS_FONT_ROLES])
{
	xxwidgets_font_options fo;
	int k;
	(void)u;
	if (xxwidgets_app_get_font_options(g_app, &fo) != XXWIDGETS_OK) return 0;
	for (k = 0; k < OPTIONS_FONT_ROLES; ++k) {
		lstrcpynA(fo.fonts[k].family, in[k].family, sizeof(fo.fonts[k].family));
		fo.fonts[k].point_size = in[k].point_size;
		fo.fonts[k].bold = in[k].bold ? 1 : 0;
		fo.fonts[k].italic = in[k].italic ? 1 : 0;
	}
	if (xxwidgets_app_set_font_options(g_app, &fo) != XXWIDGETS_OK) return 0;
	layout();
	set_status("Fonts applied to the interface, results and dialogs.");
	return 1;
}

static int context_access(int query, int* value)
{
	WCHAR executable[32768];
	char* path;
	xxwidgets_context_config config = {
		"cdie", "Scan with cdie", "*", NULL, XXWIDGETS_CONTEXT_CURRENT_USER
	};
	xxwidgets_status status;
	DWORD length = GetModuleFileNameW(NULL, executable,
		(DWORD)(sizeof(executable) / sizeof(executable[0])));
	if (!length || length >= sizeof(executable) / sizeof(executable[0])) return 0;
	path = wide_to_utf8(executable);
	if (!path) return 0;
	config.executable = path;
	if (query) {
		int registered = 0;
		status = xxwidgets_context_is_registered(&config, &registered);
		if (status == XXWIDGETS_OK) *value = registered != 0;
	}
	else {
		status = xxwidgets_context_set_enabled(&config, *value ? 1 : 0);
	}
	HeapFree(GetProcessHeap(), 0, path);
	return status == XXWIDGETS_OK;
}

static int opt_context_get(void* u, int* enabled)
{
	(void)u;
	return context_access(1, enabled);
}

static int opt_context_set(void* u, int enabled)
{
	int ok;
	(void)u;
	ok = context_access(0, &enabled);
	if (ok)
		set_status("Explorer context menu updated for the current user.");
	return ok;
}

static const char* opt_get_version(void* u)
{
	const char* v = gui_backend_app_title();
	(void)u;
	while (*v && *v != ' ') ++v;
	if (*v) ++v;
	return v;
}

static int opt_open_url(void* u, const char* url)
{
	WCHAR* wide = utf8_to_wide(url);
	HINSTANCE r;
	(void)u;
	if (!wide) return 0;
	r = ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
	HeapFree(GetProcessHeap(), 0, wide);
	return (INT_PTR)r > 32;
}

static int opt_copy_text(void* u, const char* text)
{
	WCHAR* wide = utf8_to_wide(text);
	SIZE_T bytes;
	HGLOBAL mem;
	int ok = 0;
	(void)u;
	if (!wide) return 0;
	bytes = ((SIZE_T)lstrlenW(wide) + 1) * sizeof(WCHAR);
	mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
	if (mem) {
		void* p = GlobalLock(mem);
		if (p) {
			CopyMemory(p, wide, bytes);
			GlobalUnlock(mem);
			if (OpenClipboard(native(g_window))) {
				EmptyClipboard();
				if (SetClipboardData(CF_UNICODETEXT, mem)) ok = 1;
				CloseClipboard();
			}
		}
		if (!ok) GlobalFree(mem);
	}
	HeapFree(GetProcessHeap(), 0, wide);
	return ok;
}

static int opt_check_update(void* u, char* out, size_t cap)
{
	HINSTANCE r;
	(void)u;
	r = ShellExecuteW(NULL, L"open",
		L"https://github.com/horsicq/CDiE/releases",
		NULL, NULL, SW_SHOWNORMAL);
	if ((INT_PTR)r <= 32) {
		lstrcpynA(out, "Could not open the browser.", (int)cap);
		return 0;
	}
	lstrcpynA(out, "Opened the releases page in your browser.", (int)cap);
	return 1;
}

static void do_options_window(int page)
{
	OptionsHost host;
	ZeroMemory(&host, sizeof(host));
	host.get_scan = opt_get_scan;
	host.set_scan = opt_set_scan;
	host.get_optimization = opt_get_opt;
	host.set_optimization = opt_set_opt;
	host.context_available = CDIE_SHOW_CONTEXT;
	host.updates_available = CDIE_SHOW_UPDATES;
	host.browse_folder = opt_browse_folder;
	host.db_changed = opt_db_changed;
	host.get_fonts = opt_get_fonts;
	host.set_fonts = opt_set_fonts;
	host.list_fonts = opt_list_fonts;
	host.context_get = opt_context_get;
	host.context_set = opt_context_set;
	host.get_version = opt_get_version;
	host.check_update = opt_check_update;
	host.open_url = opt_open_url;
	host.copy_text = opt_copy_text;
	options_show(g_app, g_window, &host, page);
}

static void do_about(void)
{
	do_options_window(OPTIONS_PAGE_ABOUT);
}

static void layout(void)
{
	RECT bounds, window_bounds;
	int W, H, cx, cy, cw;
	if (!g_window) return;

	GetClientRect(native(g_window), &bounds);
	W = bounds.right; H = bounds.bottom;

	if (g_sidebar)
		pl_widget_set_rect(sidebar_widget(g_sidebar), 0, 0, SIDEBAR_W, H);

	cx = SIDEBAR_W;
	cw = W - SIDEBAR_W;
	cy = 0;

	if (g_header_wnd) {
		pl_widget_set_rect(header_widget(g_header_wnd), cx, cy, cw, HEADER_H);
		cy += HEADER_H;
	}
	if (g_infobar_wnd) {
		pl_widget_set_rect(infobar_widget(g_infobar_wnd), cx, cy, cw, INFOBAR_H);
		cy += INFOBAR_H;
	}
	if (g_bottombar)
		pl_widget_set_rect(bottombar_widget(g_bottombar),
			cx, H - BOTTOMBAR_H, cw, BOTTOMBAR_H);

	{
		int tx = cx + CONTENT_PAD, tw = cw - 2 * CONTENT_PAD;
		int ty = cy + 4, th = H - cy - BOTTOMBAR_H - 8;
		if (tw < 40) tw = 40;
		if (th < 40) th = 40;
		if (g_tree)
			pl_widget_set_rect(resultstree_widget(g_tree), tx, ty,
				g_page == NAV_HEX ? 0 : tw, g_page == NAV_HEX ? 0 : th);
		if (g_hex)
			hexview_set_rect(g_hex, tx, ty,
				g_page == NAV_HEX ? tw : 0, g_page == NAV_HEX ? th : 0);
	}

	GetWindowRect(native(g_window), &window_bounds);
	g_minWindowWidth = SIDEBAR_W + 520 +
		(window_bounds.right - window_bounds.left - W);
	if (g_minWindowWidth < 900) g_minWindowWidth = 900;
	g_minWindowHeight = HEADER_H + INFOBAR_H + TABBAR_H + BOTTOMBAR_H + 200 +
		(window_bounds.bottom - window_bounds.top - H);
	if (g_minWindowHeight < 540) g_minWindowHeight = 540;
}

static void on_browse(void* user)
{
	(void)user;
	g_pending = ACTION_BROWSE;
}

static void on_nav(void* user, int nav_id)
{
	(void)user;
	switch (nav_id) {
	case NAV_OPTIONS:
		g_pending = ACTION_OPTIONS;
		break;

	case NAV_SIGNATURES:
		g_pending = ACTION_SIGNATURES;
		break;

	case NAV_STRINGS:
		g_pending = ACTION_STRINGS;
		break;

	case NAV_OVERVIEW:
	case NAV_HEX:
		g_page = nav_id;
		layout();
		break;

	default:
		break;
	}
}

static void mon_request_scan(const char* path, void* u)
{
	(void)u;
	set_scan_path(path);
	g_pending = ACTION_SCAN;
}

static void mon_set_status(const char* text, void* u)
{
	(void)u;
	set_status(text);
}

static void mon_restore_flags(void* u)
{
	(void)u;
	apply_flags_to_backend();
}

static LRESULT CALLBACK window_subclass(HWND handle, UINT message,
	WPARAM wp, LPARAM lp,
	UINT_PTR subclass_id, DWORD_PTR reference)
{
	LRESULT tray_result;
	(void)subclass_id; (void)reference;
	if (tray_handle_message(handle, message, wp, lp, &tray_result))
		return tray_result;
	if (dlwatch_handle_message(handle, message, wp, lp, &tray_result))
		return tray_result;
	switch (message) {
	case WM_GETMINMAXINFO: {
		MINMAXINFO* info = (MINMAXINFO*)lp;
		info->ptMinTrackSize.x = g_minWindowWidth;
		info->ptMinTrackSize.y = g_minWindowHeight;
		return 0;
	}

	case WM_ERASEBKGND: {
		HDC dc = (HDC)wp;
		RECT r; GetClientRect(handle, &r);
		th_fill(dc, r.left, r.top, r.right, r.bottom, CLR_BG);
		return 1;
	}

	case WM_DROPFILES: {
		HDROP drop = (HDROP)wp;
		UINT  length = DragQueryFileW(drop, 0, NULL, 0);
		WCHAR* wide = (WCHAR*)HeapAlloc(GetProcessHeap(), 0,
			((SIZE_T)length + 1) * sizeof(WCHAR));
		if (wide && DragQueryFileW(drop, 0, wide, length + 1)) {
			char* path = wide_to_utf8(wide);
			if (path) {
				set_scan_path(path);
				HeapFree(GetProcessHeap(), 0, path);
				g_pending = ACTION_SCAN;
			}
		}
		if (wide) HeapFree(GetProcessHeap(), 0, wide);
		DragFinish(drop);
		return 0;
	}

	case WM_NCDESTROY:
		RemoveWindowSubclass(handle, window_subclass, 1);
		break;

	default: break;
	}
	return DefSubclassProc(handle, message, wp, lp);
}

static void on_event(xxwidgets_app* app, const xxwidgets_event* event,
	void* user_data)
{
	(void)user_data;
	if (event->type == XXWIDGETS_EVENT_CLOSE && event->widget == g_window) {
		g_running = 0;
		xxwidgets_app_quit(app, 0);
	}
	else if (event->type == XXWIDGETS_EVENT_RESIZE && event->widget == g_window) {
		layout();
	}
	else if (event->type == XXWIDGETS_EVENT_SHORTCUT) {
		g_pending = event->value;
	}
}

static int create_controls(void)
{
	xxwidgets_shortcut shortcuts[] = {
		{ "F5",           ACTION_SCAN },
		{ "Ctrl+O",       ACTION_BROWSE },
		{ "F1",           ACTION_ABOUT },
		{ "Ctrl+Shift+D", ACTION_DATABASE_PATHS },
		{ "Ctrl+M",       ACTION_MONITOR }
	};

	if (!SetWindowSubclass(native(g_window), window_subclass, 1, 0))
		return 0;

	DragAcceptFiles(native(g_window), TRUE);

	if (xxwidgets_window_set_shortcuts(g_window, shortcuts,
		sizeof(shortcuts) / sizeof(shortcuts[0])) != XXWIDGETS_OK)
		return 0;

	g_sidebar = sidebar_create(native(g_window), 0, 0, SIDEBAR_W, 600, on_nav, NULL);
	g_header_wnd = header_create(native(g_window), SIDEBAR_W, 0, 800, HEADER_H,
		on_browse, NULL);
	g_infobar_wnd = infobar_create(native(g_window), SIDEBAR_W, HEADER_H, 800, INFOBAR_H);
	g_bottombar = bottombar_create(native(g_window), SIDEBAR_W, 500, 800, BOTTOMBAR_H, NULL, NULL);
	g_tree = resultstree_create(native(g_window), SIDEBAR_W, 0, 100, 100);
	g_hex = hexview_create(native(g_window), NULL, SIDEBAR_W, 0, 0, 0);
	if (g_hex) hexview_set_callbacks(g_hex, NULL, opt_copy_text, NULL);

	if (!g_sidebar || !g_header_wnd || !g_infobar_wnd || !g_bottombar || !g_tree || !g_hex) {
		WCHAR msg[200];
		wsprintfW(msg,
			L"Chrome creation failed (1 = ok):\nsidebar=%d header=%d infobar=%d "
			L"bottombar=%d tree=%d\nGetLastError=%lu",
			g_sidebar != NULL, g_header_wnd != NULL, g_infobar_wnd != NULL,
			g_bottombar != NULL, g_tree != NULL, GetLastError());
		MessageBoxW(NULL, msg, L"cdie", MB_ICONERROR);
		return 0;
	}
	return 1;
}

static int set_startup_file(void)
{
	int argc = 0, has_file = 0;
	WCHAR** arguments = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (arguments && argc > 1) {
		char* path = wide_to_utf8(arguments[1]);
		if (path) {
			set_scan_path(path);
			has_file = g_scanPath != NULL;
			HeapFree(GetProcessHeap(), 0, path);
		}
	}
	if (arguments) LocalFree(arguments);
	return has_file;
}

static void fill_prefs(CdieConfig* cfg)
{
	OptionsOptimization opt;
	OptionsFont fonts[OPTIONS_FONT_ROLES];
	int k;

	cfg->has_prefs = 1;
	cfg->deep = g_bFlagDeep;           cfg->heuristic = g_bFlagHeur;
	cfg->verbose = g_bFlagVerbose;     cfg->aggressive = g_bFlagAggr;
	cfg->hide_unknown = g_bFlagHideUnknown; cfg->format = g_bFlagFormat;
	cfg->db_extra = g_bDbExtra;        cfg->db_custom = g_bDbCustom;
	cfg->monitor_downloads = dlwatch_enabled();

	if (opt_get_opt(NULL, &opt)) {
		cfg->use_sse2 = opt.use_sse2;
		cfg->use_avx2 = opt.use_avx2;
		cfg->buffer_size = (unsigned long long)opt.buffer_size;
		cfg->file_buffer_size = (unsigned long long)opt.file_buffer_size;
	}

	if (opt_get_fonts(NULL, fonts)) {
		for (k = 0; k < OPTIONS_FONT_ROLES && k < CDIE_FONT_ROLES; ++k) {
			snprintf(cfg->font_family[k], sizeof(cfg->font_family[k]), "%s", fonts[k].family);
			cfg->font_size[k] = fonts[k].point_size;
			cfg->font_bold[k] = fonts[k].bold;
			cfg->font_italic[k] = fonts[k].italic;
		}
	}
}

static int init_backend_with_config(void)
{
	CdieConfig cfg;
	int ok = gui_backend_init();
	int have_file = config_load(&cfg);

	if (have_file && cfg.main_db[0]) {
		if (gui_backend_set_db_paths(cfg.main_db, cfg.extra_db, cfg.custom_db))
			ok = 1;            /* saved folders worked, even if the default failed */
		return ok;
	}

	/* Program Files / Store: never use the shipped folder, the user must choose one */
	if (!config_is_portable()) return 0;

	if (!have_file) {
		/* Portable first run: create the file with the paths the engine is using now */
		const char* m = gui_backend_main_db_path();
		const char* e = gui_backend_extra_db_path();
		const char* c = gui_backend_custom_db_path();
		memset(&cfg, 0, sizeof(cfg));
		snprintf(cfg.main_db, sizeof(cfg.main_db), "%s", m ? m : "");
		snprintf(cfg.extra_db, sizeof(cfg.extra_db), "%s", e ? e : "");
		snprintf(cfg.custom_db, sizeof(cfg.custom_db), "%s", c ? c : "");
		fill_prefs(&cfg);
		config_save(&cfg);
	}
	return ok;
}

static void save_config_on_exit(void)
{
	CdieConfig cfg;
	const char* m;
	const char* e;
	const char* c;
	if (config_db_required()) return;
	config_load(&cfg);                 /* keep saved preferences */
	m = gui_backend_main_db_path();
	e = gui_backend_extra_db_path();
	c = gui_backend_custom_db_path();
	snprintf(cfg.main_db, sizeof(cfg.main_db), "%s", m ? m : "");
	snprintf(cfg.extra_db, sizeof(cfg.extra_db), "%s", e ? e : "");
	snprintf(cfg.custom_db, sizeof(cfg.custom_db), "%s", c ? c : "");
	if (g_prefsReady) fill_prefs(&cfg);
	config_save(&cfg);
}

static void apply_saved_prefs(void)
{
	CdieConfig cfg;
	OptionsOptimization opt;
	OptionsFont fonts[OPTIONS_FONT_ROLES];
	int k;

	if (!config_load(&cfg) || !cfg.has_prefs) return;

	g_bFlagDeep = cfg.deep;           g_bFlagHeur = cfg.heuristic;
	g_bFlagVerbose = cfg.verbose;     g_bFlagAggr = cfg.aggressive;
	g_bFlagHideUnknown = cfg.hide_unknown; g_bFlagFormat = cfg.format;
	g_bDbExtra = cfg.db_extra;        g_bDbCustom = cfg.db_custom;
	g_startMonitor = cfg.monitor_downloads;
	apply_flags_to_backend();

	if (opt_get_opt(NULL, &opt)) {     /* fills has_sse2 / has_avx2 */
		opt.buffer_size = (size_t)cfg.buffer_size;
		opt.file_buffer_size = (size_t)cfg.file_buffer_size;
		opt.use_sse2 = cfg.use_sse2 && opt.has_sse2;
		opt.use_avx2 = cfg.use_avx2 && opt.has_avx2;
		opt_set_opt(NULL, &opt);
	}

	if (opt_get_fonts(NULL, fonts)) {
		for (k = 0; k < OPTIONS_FONT_ROLES && k < CDIE_FONT_ROLES; ++k) {
			snprintf(fonts[k].family, sizeof(fonts[k].family), "%s", cfg.font_family[k]);
			fonts[k].point_size = cfg.font_size[k];
			fonts[k].bold = cfg.font_bold[k];
			fonts[k].italic = cfg.font_italic[k];
		}
		opt_set_fonts(NULL, fonts);
	}
}

int gui_run(void)
{
	xxwidgets_config config = { XXWIDGETS_BACKEND_NATIVE, on_event, NULL };
	xxwidgets_rect   bounds = { 8, 4, 118, 40 };
	xxwidgets_status status;
	int   result = 0, db_ok = 0;
	WCHAR title[160], ready[512];
	WCHAR* base;
	char* utf8_title;

	status = xxwidgets_app_create(&config, &g_app);
	if (status != XXWIDGETS_OK) {
		MessageBoxW(NULL, L"xxwidgets_app_create failed", L"cdie", MB_ICONERROR);
		return 1;
	}

	base = utf8_to_wide(gui_backend_app_title());
#if defined(_WIN64)
	wsprintfW(title, L"%s (x64)", base ? base : L"cdie");
#else
	wsprintfW(title, L"%s (x86)", base ? base : L"cdie");
#endif
	if (base) HeapFree(GetProcessHeap(), 0, base);

	utf8_title = wide_to_utf8(title);
	if (!utf8_title) { result = 1; goto finish; }
	xxwidgets_app_set_create_hidden(g_app, 1);
	status = xxwidgets_widget_create(g_app, NULL, XXWIDGETS_WINDOW,
		utf8_title, bounds, &g_window);
	xxwidgets_app_set_create_hidden(g_app, 0);
	HeapFree(GetProcessHeap(), 0, utf8_title);
	if (status != XXWIDGETS_OK) {
		MessageBoxW(NULL, L"Main window creation failed", L"cdie", MB_ICONERROR);
		result = 1; goto finish;
	}

	{
		BOOL dark = TRUE;
		DwmSetWindowAttribute(native(g_window), 20, &dark, sizeof(dark));
	}

	xxwidgets_widget_set_visible(g_window, 0);
	if (!create_controls()) {
		MessageBoxW(NULL, L"create_controls failed", L"cdie", MB_ICONERROR);
		result = 1; goto finish;
	}

	tray_init(on_tray_exit, NULL);
	tray_win32_attach(native(g_window));
	{
		DlWatchHost mh;
		memset(&mh, 0, sizeof(mh));          /* ZeroMemory is Windows-only */
		mh.native = native(g_window);        /* was mh.hwnd; HWND on Windows, ignored elsewhere */
		mh.request_scan = mon_request_scan;
		mh.set_status = mon_set_status;
		mh.restore_flags = mon_restore_flags;
		dlwatch_init(&mh);
	}

	tray_set_callbacks(dlwatch_balloon_click, NULL, dlwatch_menu_toggle, NULL);
	SetWindowPos(native(g_window), NULL, 0, 0, 1150, 700, SWP_NOMOVE | SWP_NOZORDER);
	db_ok = init_backend_with_config();
	apply_saved_prefs();          /* before the status text, since set_fonts writes one */
	g_prefsReady = 1;
	if (db_ok) {
		wsprintfW(ready, L"Ready. %d signatures loaded. Choose a file or drag one here, then press Scan.",
			gui_backend_signature_count());
		set_status_wide(ready);
	}
	else {
		set_status("Main signature database not found. "
			"Select the database folder in the window that opens.");
	}
	apply_flags_to_backend();
	layout();

	xxwidgets_widget_set_visible(g_window, 1);
	if (set_startup_file()) do_scan();
	if (g_startMonitor) dlwatch_set_enabled(1);
	if (!db_ok) g_pending = ACTION_DATABASE_PATHS;

	g_running = 1;
	while (g_running) {
		int action;
		status = xxwidgets_app_poll(g_app, 30);
		if (status != XXWIDGETS_OK) { result = 1; break; }
		if (!g_running) break;
		action = g_pending; g_pending = ACTION_NONE;
		switch (action) {
		case ACTION_SCAN:           do_scan();           break;
		case ACTION_BROWSE:         do_browse();         break;
		case ACTION_OPTIONS:        do_options_window(OPTIONS_PAGE_SCAN); break;
		case ACTION_ABOUT:          do_about();          break;
		case ACTION_MONITOR:        dlwatch_set_enabled(!dlwatch_enabled()); break;
		case ACTION_DATABASE_PATHS: do_options_window(OPTIONS_PAGE_DATABASES); break;
		case ACTION_SIGNATURES:     signatures_show(g_app, g_window); break;
		case ACTION_STRINGS:
			if (g_lastPath) strings_show(g_app, g_window, g_lastPath);
			else set_status("Scan a file first to view its strings.");
			break;
		default: break;
		}
		if (g_sidebar && sidebar_get_selected(g_sidebar) != g_page)
			sidebar_set_selected(g_sidebar, g_page);
	}

finish:
	hex_close();
	dlwatch_shutdown();
	tray_remove();
	save_config_on_exit();
	gui_backend_shutdown();
	if (xxwidgets_app_destroy(g_app) != XXWIDGETS_OK) result = 1;
	g_app = NULL;
	if (g_lastPath) HeapFree(GetProcessHeap(), 0, g_lastPath);
	g_lastPath = NULL;
	if (g_scanPath) HeapFree(GetProcessHeap(), 0, g_scanPath);
	g_scanPath = NULL;
	return result;
}