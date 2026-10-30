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
#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern "C" {
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_font_options.h>
#include "core/gui_backend.h"
#include "core/config.h"
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
}

#ifndef CDIE_MAC_HAS_DIALOGS
#define signatures_show(app, win) set_status("Signature viewer is not available on macOS yet.")
#define strings_show(app, win, path) set_status("Strings viewer is not available on macOS yet.")
#endif

/* Mac builds are not Store/VSIX builds; there is no Explorer context menu */
#define CDIE_SHOW_UPDATES 1
#define CDIE_SHOW_CONTEXT 0

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

static Sidebar* g_sidebar;
static Header* g_header_wnd;
static Infobar* g_infobar_wnd;
static Bottombar* g_bottombar;
static ResultsTree* g_tree;
static HexViewer* g_hex;
static int g_page = NAV_OVERVIEW;
static int g_hexFd = -1;

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
static NSWindow* native_window_fwd(void);

static void tray_win_show(void* u)
{
	NSWindow* w = native_window_fwd();
	(void)u;
	if (!w) return;
	if ([w isMiniaturized]) [w deminiaturize:nil];
	[w makeKeyAndOrderFront:nil];
	[NSApp activateIgnoringOtherApps:YES];
}

static void tray_win_hide(void* u)
{
	NSWindow* w = native_window_fwd();
	(void)u;
	if (w) [w orderOut:nil];
}

static int tray_win_visible(void* u)
{
	NSWindow* w = native_window_fwd();
	(void)u;
	return w ? ([w isVisible] ? 1 : 0) : 0;
}

static void on_tray_exit(void* user)
{
	(void)user;
	g_running = 0;
	xxwidgets_app_quit(g_app, 0);
}

static NSWindow* native_window(xxwidgets_widget* widget)
{
	id obj = (__bridge id)xxwidgets_widget_native_handle(widget);
	if ([obj isKindOfClass:[NSWindow class]]) return (NSWindow*)obj;
	if ([obj isKindOfClass:[NSView class]]) return [(NSView*)obj window];
	return nil;
}

static NSWindow* native_window_fwd(void) { return native_window(g_window); }

static void* native(xxwidgets_widget* widget)
{
	return xxwidgets_widget_native_handle(widget);
}

static char* copy_utf8(const char* text)
{
	return strdup(text ? text : "");
}

static void set_status(const char* text)
{
	if (g_bottombar) bottombar_set_status(g_bottombar, text);
}

static void set_status_fmt(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void set_status_fmt(const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	set_status(buf);
}

static void set_scan_path(const char* path)
{
	char* copy = copy_utf8(path);
	if (!copy) return;
	free(g_scanPath);
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
	(void)u;
	if (g_hexFd < 0) return 0;
	while (len) {
		ssize_t got = pread(g_hexFd, buf, len > 0x100000 ? 0x100000 : len, (off_t)off);
		if (got <= 0) return 0;
		buf = (char*)buf + got;
		off += (uint64_t)got;
		len -= (size_t)got;
	}
	return 1;
}

static void hex_close(void)
{
	if (g_hexFd >= 0) close(g_hexFd);
	g_hexFd = -1;
}

static void hex_open(const char* utf8_path)
{
	struct stat st;
	hex_close();
	g_hexFd = open(utf8_path, O_RDONLY);
	if (g_hexFd < 0) return;
	if (fstat(g_hexFd, &st) != 0) { hex_close(); return; }
	if (g_hex) hexview_set_source(g_hex, (uint64_t)st.st_size, hex_read, NULL, 0);
}

typedef struct {
	char type_str[256];
	char size_str[80];
	char size_sub[80];
	char ep_str[80];
	char ep_sub[80];
	char base_str[80];
	char base_sub[80];
} ScanFileInfo;

static void format_size(unsigned long long bytes, char* main_out, char* sub_out)
{
	if (bytes >= 1024ULL * 1024) {
		unsigned long long n100 = (bytes * 100ULL) / (1024ULL * 1024);
		snprintf(main_out, 80, "%u.%02u MiB", (unsigned)(n100 / 100), (unsigned)(n100 % 100));
	}
	else if (bytes >= 1024) {
		unsigned long long n10 = (bytes * 10ULL) / 1024;
		snprintf(main_out, 80, "%u.%u KiB", (unsigned)(n10 / 10), (unsigned)(n10 % 10));
	}
	else {
		snprintf(main_out, 80, "%u B", (unsigned)bytes);
	}
	if (bytes < 1000000ULL)
		snprintf(sub_out, 80, "%u bytes", (unsigned)bytes);
	else
		snprintf(sub_out, 80, "%u,%06u bytes",
			(unsigned)(bytes / 1000000ULL), (unsigned)(bytes % 1000000ULL));
}

static int read_at(FILE* f, long off, void* buf, size_t len)
{
	if (fseek(f, off, SEEK_SET) != 0) return 0;
	return fread(buf, 1, len, f) == len;
}

static void get_file_info(const char* path, const char* type_name, ScanFileInfo* fi)
{
	struct stat st;
	FILE* f;

	snprintf(fi->type_str, sizeof(fi->type_str), "%s",
		(type_name && type_name[0]) ? type_name : "Unknown");

	strcpy(fi->size_str, "N/A"); fi->size_sub[0] = 0;
	strcpy(fi->ep_str, "N/A");   fi->ep_sub[0] = 0;
	strcpy(fi->base_str, "N/A"); fi->base_sub[0] = 0;

	if (stat(path, &st) == 0)
		format_size((unsigned long long)st.st_size, fi->size_str, fi->size_sub);

	/* PE headers (little-endian host assumed: x86_64 / arm64) */
	f = fopen(path, "rb");
	if (!f) return;
	{
		uint16_t dos_magic = 0, opt_magic = 0;
		uint32_t pe_off = 0, pe_sig = 0;
		if (read_at(f, 0, &dos_magic, 2) && dos_magic == 0x5A4D &&
			read_at(f, 0x3C, &pe_off, 4) &&
			read_at(f, (long)pe_off, &pe_sig, 4) && pe_sig == 0x00004550 &&
			read_at(f, (long)(pe_off + 24), &opt_magic, 2)) {
			uint32_t ep = 0;
			read_at(f, (long)(pe_off + 40), &ep, 4);
			if (opt_magic == 0x10B) {
				uint32_t base32 = 0;
				read_at(f, (long)(pe_off + 52), &base32, 4);
				snprintf(fi->ep_str, 80, "0x%08X", ep);
				snprintf(fi->base_str, 80, "0x%08X", base32);
				strcpy(fi->ep_sub, "RVA");
				strcpy(fi->base_sub, "Preferred load");
			}
			else if (opt_magic == 0x20B) {
				uint64_t base64 = 0;
				read_at(f, (long)(pe_off + 48), &base64, 8);
				snprintf(fi->ep_str, 80, "0x%08X", ep);
				if (base64 > 0xFFFFFFFFULL)
					snprintf(fi->base_str, 80, "0x%08X%08X",
						(unsigned)(base64 >> 32), (unsigned)(base64 & 0xFFFFFFFF));
				else
					snprintf(fi->base_str, 80, "0x%08X", (unsigned)base64);
				strcpy(fi->ep_sub, "RVA");
				strcpy(fi->base_sub, "Preferred load");
			}
		}
	}
	fclose(f);
}

static void update_chrome_after_scan(const char* utf8_path, const char* type_name)
{
	ScanFileInfo fi;
	memset(&fi, 0, sizeof(fi));
	get_file_info(utf8_path, type_name, &fi);

	if (g_header_wnd) {
		const char* last = NULL, * p;
		for (p = utf8_path; *p; ++p)
			if (*p == '/') last = p;
		if (last) {
			char* folder = copy_utf8(utf8_path);
			if (folder) {
				folder[last - utf8_path + 1] = 0;
				header_set_path(g_header_wnd, folder, last + 1);
				free(folder);
			}
		}
		else {
			header_set_path(g_header_wnd, "", utf8_path);
		}
	}

	if (g_infobar_wnd) {
		InfoCard cards[4];
		cards[0].label = "FILE TYPE";    cards[0].value = fi.type_str; cards[0].subvalue = "";
		cards[1].label = "FILE SIZE";    cards[1].value = fi.size_str; cards[1].subvalue = fi.size_sub;
		cards[2].label = "ENTRY POINT";  cards[2].value = fi.ep_str;   cards[2].subvalue = fi.ep_sub;
		cards[3].label = "BASE ADDRESS"; cards[3].value = fi.base_str; cards[3].subvalue = fi.base_sub;
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
	struct timespec started, ended;
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
	[[native_window(g_window) contentView] displayIfNeeded];

	clock_gettime(CLOCK_MONOTONIC, &started);
	ok = gui_backend_scan_results_type(path, 0, collect_result, &count, NULL, &type);
	clock_gettime(CLOCK_MONOTONIC, &ended);

	if (ok) {
		msec = (unsigned int)((ended.tv_sec - started.tv_sec) * 1000LL +
			(ended.tv_nsec - started.tv_nsec) / 1000000LL);
		set_status_fmt("Scan complete (%s). %u result%s in %u ms.",
			(type && type[0]) ? type : "Unknown type",
			count, count == 1 ? "" : "s", msec);

		update_chrome_after_scan(path, type);
		free(g_lastPath);
		g_lastPath = copy_utf8(path);
		hex_open(path);
	}
	else {
		set_status("Cannot open the file.");
	}
	free(path);
	if (type) gui_backend_free(type);
}

static void do_browse(void)
{
	@autoreleasepool {
		NSOpenPanel* panel = [NSOpenPanel openPanel];
		panel.title = @"Select a file to scan";
		panel.canChooseFiles = YES;
		panel.canChooseDirectories = NO;
		panel.allowsMultipleSelection = NO;
		panel.treatsFilePackagesAsDirectories = YES;
		if (g_scanPath && g_scanPath[0]) {
			NSString* cur = [NSString stringWithUTF8String:g_scanPath];
			panel.directoryURL = [NSURL fileURLWithPath:
				[cur stringByDeletingLastPathComponent]];
		}
		if ([panel runModal] == NSModalResponseOK && panel.URL) {
			set_scan_path([[panel.URL path] UTF8String]);
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
	int ok = 0;
	(void)u;
	@autoreleasepool {
		NSOpenPanel* panel = [NSOpenPanel openPanel];
		panel.title = @"Select the database folder";
		panel.canChooseFiles = NO;
		panel.canChooseDirectories = YES;
		panel.canCreateDirectories = YES;
		panel.allowsMultipleSelection = NO;
		if ([panel runModal] == NSModalResponseOK && panel.URL) {
			const char* p = [[panel.URL path] UTF8String];
			if (p && strlen(p) < cap) { memcpy(out, p, strlen(p) + 1); ok = 1; }
		}
	}
	return ok;
}

static void opt_db_changed(void* u)
{
	(void)u;
	clear_scan();
	set_status_fmt("Database reloaded: %d signatures. Choose a file and press Scan.",
		gui_backend_signature_count());
}

static int opt_list_fonts(void* u, int fixed_only,
	void (*add)(const char* family, void* ud), void* ud)
{
	(void)u;
	@autoreleasepool {
		NSFontManager* fm = [NSFontManager sharedFontManager];
		NSArray<NSString*>* families =
			[[fm availableFontFamilies] sortedArrayUsingSelector:
				@selector(localizedCaseInsensitiveCompare:)];
		for (NSString* family in families) {
			if ([family hasPrefix:@"."]) continue;      /* system-internal faces */
			if (fixed_only) {
				NSFont* f = [fm fontWithFamily:family traits:0 weight:5 size:12];
				if (!f || !f.isFixedPitch) continue;
			}
			const char* utf8 = [family UTF8String];
			if (utf8) add(utf8, ud);
		}
	}
	return 1;
}

static int opt_get_fonts(void* u, OptionsFont out[OPTIONS_FONT_ROLES])
{
	xxwidgets_font_options fo;
	int k;
	(void)u;
	if (xxwidgets_app_get_font_options(g_app, &fo) != XXWIDGETS_OK) return 0;
	for (k = 0; k < OPTIONS_FONT_ROLES; ++k) {
		snprintf(out[k].family, sizeof(out[k].family), "%s", fo.fonts[k].family);
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
		snprintf(fo.fonts[k].family, sizeof(fo.fonts[k].family), "%s", in[k].family);
		fo.fonts[k].point_size = in[k].point_size;
		fo.fonts[k].bold = in[k].bold ? 1 : 0;
		fo.fonts[k].italic = in[k].italic ? 1 : 0;
	}
	if (xxwidgets_app_set_font_options(g_app, &fo) != XXWIDGETS_OK) return 0;
	layout();
	set_status("Fonts applied to the interface, results and dialogs.");
	return 1;
}

static int opt_context_get(void* u, int* enabled)
{
	(void)u; (void)enabled;
	return 0;
}

static int opt_context_set(void* u, int enabled)
{
	(void)u; (void)enabled;
	return 0;
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
	int ok = 0;
	(void)u;
	@autoreleasepool {
		NSString* s = url ? [NSString stringWithUTF8String:url] : nil;
		NSURL* nsurl = s ? [NSURL URLWithString:s] : nil;
		if (nsurl) ok = [[NSWorkspace sharedWorkspace] openURL:nsurl] ? 1 : 0;
	}
	return ok;
}

static int opt_copy_text(void* u, const char* text)
{
	int ok = 0;
	(void)u;
	if (!text) return 0;
	@autoreleasepool {
		NSString* s = [NSString stringWithUTF8String:text];
		if (s) {
			NSPasteboard* pb = [NSPasteboard generalPasteboard];
			[pb clearContents];
			ok = [pb setString:s forType:NSPasteboardTypeString] ? 1 : 0;
		}
	}
	return ok;
}

static int opt_check_update(void* u, char* out, size_t cap)
{
	(void)u;
	if (!opt_open_url(NULL, "https://github.com/horsicq/CDiE/releases")) {
		snprintf(out, cap, "%s", "Could not open the browser.");
		return 0;
	}
	snprintf(out, cap, "%s", "Opened the releases page in your browser.");
	return 1;
}

static void do_options_window(int page)
{
	OptionsHost host;
	memset(&host, 0, sizeof(host));
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
	NSWindow* win;
	NSRect content;
	int W, H, cx, cy, cw;
	if (!g_window) return;
	win = native_window(g_window);
	if (!win) return;

	content = [[win contentView] bounds];
	W = (int)content.size.width; H = (int)content.size.height;

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

	g_minWindowWidth = SIDEBAR_W + 520;
	if (g_minWindowWidth < 900) g_minWindowWidth = 900;
	g_minWindowHeight = HEADER_H + INFOBAR_H + TABBAR_H + BOTTOMBAR_H + 200;
	if (g_minWindowHeight < 540) g_minWindowHeight = 540;
	[win setContentMinSize:NSMakeSize(g_minWindowWidth, g_minWindowHeight)];
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
	case NAV_OPTIONS:    g_pending = ACTION_OPTIONS;    break;
	case NAV_SIGNATURES: g_pending = ACTION_SIGNATURES; break;
	case NAV_STRINGS:    g_pending = ACTION_STRINGS;    break;
	case NAV_OVERVIEW:
	case NAV_HEX:
		g_page = nav_id;
		layout();
		break;
	default: break;
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

static NSDragOperation cdie_dragEntered(id self, SEL _cmd, id<NSDraggingInfo> info)
{
	(void)self; (void)_cmd;
	return [[info draggingPasteboard] canReadObjectForClasses:@[[NSURL class]]
		options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }]
		? NSDragOperationCopy : NSDragOperationNone;
}

static BOOL cdie_performDrag(id self, SEL _cmd, id<NSDraggingInfo> info)
{
	NSArray* urls;
	(void)self; (void)_cmd;
	urls = [[info draggingPasteboard] readObjectsForClasses:@[[NSURL class]]
		options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
	if ([urls count] > 0) {
		const char* p = [[(NSURL*)[urls objectAtIndex:0] path] UTF8String];
		if (p) {
			set_scan_path(p);
			g_pending = ACTION_SCAN;
			return YES;
		}
	}
	return NO;
}

static void install_drop_target(NSWindow* win)
{
	NSView* view = [win contentView];
	Class base = object_getClass(view);
	Class sub = objc_allocateClassPair(base, "CdieDropContentView", 0);
	if (!sub) sub = objc_getClass("CdieDropContentView");
	if (sub && sub != base) {
		class_addMethod(sub, @selector(draggingEntered:), (IMP)cdie_dragEntered, "Q@:@");
		class_addMethod(sub, @selector(draggingUpdated:), (IMP)cdie_dragEntered, "Q@:@");
		class_addMethod(sub, @selector(performDragOperation:), (IMP)cdie_performDrag, "c@:@");
		objc_registerClassPair(sub);
		object_setClass(view, sub);
	}
	[view registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
}

static void install_minimize_hook(NSWindow* win)
{
	if (!win) return;
	[[NSNotificationCenter defaultCenter]
		addObserverForName:NSWindowDidMiniaturizeNotification object:win queue:nil
		usingBlock:^(NSNotification* n) {
			NSWindow* w = (NSWindow*)n.object;
			if (tray_intercept_minimize()) {
				[w deminiaturize:nil];
				[w orderOut:nil];
			}
		}];
}

static void on_event(xxwidgets_app* app, const xxwidgets_event* event,
	void* user_data)
{
	(void)user_data;
	if (event->type == XXWIDGETS_EVENT_CLOSE && event->widget == g_window) {
		if (tray_intercept_close()) return;     
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
		{ "Cmd+O",        ACTION_BROWSE },
		{ "F1",           ACTION_ABOUT },
		{ "Cmd+Shift+D",  ACTION_DATABASE_PATHS },
		{ "Cmd+Shift+M",  ACTION_MONITOR }   
	};
	NSWindow* win = native_window(g_window);
	void* parent = native(g_window);

	if (!win) return 0;
	install_drop_target(win);

	if (xxwidgets_window_set_shortcuts(g_window, shortcuts,
		sizeof(shortcuts) / sizeof(shortcuts[0])) != XXWIDGETS_OK)
		return 0;

	g_sidebar = sidebar_create(parent, 0, 0, SIDEBAR_W, 600, on_nav, NULL);
	g_header_wnd = header_create(parent, SIDEBAR_W, 0, 800, HEADER_H, on_browse, NULL);
	g_infobar_wnd = infobar_create(parent, SIDEBAR_W, HEADER_H, 800, INFOBAR_H);
	g_bottombar = bottombar_create(parent, SIDEBAR_W, 500, 800, BOTTOMBAR_H, NULL, NULL);
	g_tree = resultstree_create(parent, SIDEBAR_W, 0, 100, 100);
	g_hex = hexview_create(parent, NULL, SIDEBAR_W, 0, 0, 0);
	if (g_hex) hexview_set_callbacks(g_hex, NULL, opt_copy_text, NULL);

	if (!g_sidebar || !g_header_wnd || !g_infobar_wnd || !g_bottombar || !g_tree || !g_hex) {
		@autoreleasepool {
			NSAlert* alert = [[NSAlert alloc] init];
			alert.messageText = @"cdie";
			alert.informativeText = [NSString stringWithFormat:
				@"Chrome creation failed (1 = ok):\nsidebar=%d header=%d infobar=%d "
				@"bottombar=%d tree=%d hex=%d",
				g_sidebar != NULL, g_header_wnd != NULL, g_infobar_wnd != NULL,
				g_bottombar != NULL, g_tree != NULL, g_hex != NULL];
			alert.alertStyle = NSAlertStyleCritical;
			[alert runModal];
		}
		return 0;
	}
	return 1;
}

static void error_box(const char* text)
{
	@autoreleasepool {
		NSAlert* alert = [[NSAlert alloc] init];
		alert.messageText = @"cdie";
		alert.informativeText = [NSString stringWithUTF8String:text];
		alert.alertStyle = NSAlertStyleCritical;
		[alert runModal];
	}
}

static int set_startup_file(void)
{
	int has_file = 0;
	@autoreleasepool {
		NSArray<NSString*>* args = [[NSProcessInfo processInfo] arguments];
		for (NSUInteger i = 1; i < [args count]; ++i) {
			NSString* a = args[i];
			if ([a hasPrefix:@"-psn_"]) continue;     /* legacy launch-services arg */
			if ([a hasPrefix:@"-"]) continue;
			set_scan_path([a UTF8String]);
			has_file = g_scanPath != NULL;
			break;
		}
	}
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
			ok = 1;
		return ok;
	}

	if (!config_is_portable()) return 0;

	if (!have_file) {
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
	config_load(&cfg);
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

	if (opt_get_opt(NULL, &opt)) {
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

extern "C" int gui_run(void)
{
	xxwidgets_config config = { XXWIDGETS_BACKEND_NATIVE, on_event, NULL };
	xxwidgets_rect   bounds = { 8, 4, 118, 40 };
	xxwidgets_status status;
	int   result = 0, db_ok = 0;
	char  title[160];

	@autoreleasepool {
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

		status = xxwidgets_app_create(&config, &g_app);
		if (status != XXWIDGETS_OK) {
			error_box("xxwidgets_app_create failed");
			return 1;
		}

#if defined(__arm64__)
		snprintf(title, sizeof(title), "%s (arm64)", gui_backend_app_title());
#else
		snprintf(title, sizeof(title), "%s (x64)", gui_backend_app_title());
#endif

		xxwidgets_app_set_create_hidden(g_app, 1);
		status = xxwidgets_widget_create(g_app, NULL, XXWIDGETS_WINDOW,
			title, bounds, &g_window);
		xxwidgets_app_set_create_hidden(g_app, 0);
		if (status != XXWIDGETS_OK) {
			error_box("Main window creation failed");
			result = 1; goto finish;
		}

		{
			NSWindow* win = native_window(g_window);
			if (win) {
				win.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
				win.backgroundColor = [NSColor colorWithSRGBRed:0.07 green:0.08 blue:0.10 alpha:1.0]; /* match CLR_BG */
				[win setContentSize:NSMakeSize(1150, 700)];
				[win center];
			}
		}

		xxwidgets_widget_set_visible(g_window, 0);
		if (!create_controls()) {
			result = 1; goto finish;
		}

		tray_init(on_tray_exit, NULL);
		{
			TrayWindowOps ops;
			memset(&ops, 0, sizeof(ops));
			ops.show = tray_win_show;
			ops.hide = tray_win_hide;
			ops.is_visible = tray_win_visible;
			tray_set_window_ops(&ops);
		}
		install_minimize_hook(native_window(g_window));
		{
			DlWatchHost mh;
			memset(&mh, 0, sizeof(mh));
			mh.native = native(g_window);
			mh.request_scan = mon_request_scan;
			mh.set_status = mon_set_status;
			mh.restore_flags = mon_restore_flags;
			dlwatch_init(&mh);
		}

		tray_set_callbacks(dlwatch_balloon_click, NULL, dlwatch_menu_toggle, NULL);

		db_ok = init_backend_with_config();
		apply_saved_prefs();
		g_prefsReady = 1;
		if (db_ok) {
			set_status_fmt("Ready. %d signatures loaded. Choose a file or drag one here, then press Scan.",
				gui_backend_signature_count());
		}
		else {
			set_status("Main signature database not found. "
				"Select the database folder in the window that opens.");
		}
		apply_flags_to_backend();
		layout();

		xxwidgets_widget_set_visible(g_window, 1);
		[NSApp activateIgnoringOtherApps:YES];
		if (set_startup_file()) do_scan();
		if (g_startMonitor) dlwatch_set_enabled(1);
		if (!db_ok) g_pending = ACTION_DATABASE_PATHS;

		g_running = 1;
		while (g_running) {
			int action;
			@autoreleasepool {
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
		}

finish:
		hex_close();
		dlwatch_shutdown();
		tray_remove();
		save_config_on_exit();
		gui_backend_shutdown();
		if (g_app && xxwidgets_app_destroy(g_app) != XXWIDGETS_OK) result = 1;
		g_app = NULL;
		free(g_lastPath); g_lastPath = NULL;
		free(g_scanPath); g_scanPath = NULL;
	}
	return result;
}