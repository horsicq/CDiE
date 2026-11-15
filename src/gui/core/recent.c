#include "recent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core/config.h"

#ifdef _WIN32
#include <math.h>
#include <windows.h>
#include "theme/theme.h"
#include "core/i18n.h"
#define SAME(a, b) (_stricmp((a), (b)) == 0)
#define DUP _strdup
static void fly_free_paths(void);
#else
#include <unistd.h>
#define SAME(a, b) (strcmp((a), (b)) == 0)
#define DUP strdup
#endif

#if RECENT_MAX > CDIE_RECENT_MAX
#error RECENT_MAX must not exceed CDIE_RECENT_MAX
#endif

static char* g_list[RECENT_MAX];
static int   g_n;

static int file_exists(const char* path)
{
#ifdef _WIN32
	WCHAR w[2048];
	if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 2048)) return 0;
	return GetFileAttributesW(w) != INVALID_FILE_ATTRIBUTES;
#else
	return access(path, F_OK) == 0;
#endif
}

static void persist(void)
{
	CdieConfig cfg;
	int i;
	config_load(&cfg);
	memset(cfg.recent, 0, sizeof(cfg.recent));
	for (i = 0; i < g_n; ++i)
		snprintf(cfg.recent[i], sizeof(cfg.recent[i]), "%s", g_list[i]);
	config_save(&cfg);
}

static void remove_at(int i)
{
	free(g_list[i]);
	for (; i < g_n - 1; ++i) g_list[i] = g_list[i + 1];
	g_list[--g_n] = NULL;
}

void recent_init(void)
{
	CdieConfig cfg;
	int i, changed = 0;

	recent_shutdown();
	if (!config_load(&cfg)) return;
	for (i = 0; i < RECENT_MAX; ++i)
		if (cfg.recent[i][0]) g_list[g_n++] = DUP(cfg.recent[i]);

	for (i = g_n - 1; i >= 0; --i)
		if (!file_exists(g_list[i])) { remove_at(i); changed = 1; }
	if (changed) persist();
}

void recent_shutdown(void)
{
	int i;
	for (i = 0; i < g_n; ++i) { free(g_list[i]); g_list[i] = NULL; }
	g_n = 0;
#ifdef _WIN32
	fly_free_paths();
#endif
}

void recent_add(const char* path)
{
	char* copy;
	int i;
	if (!path || !path[0]) return;
	copy = DUP(path);
	if (!copy) return;
	for (i = 0; i < g_n; ++i)
		if (SAME(g_list[i], path)) { remove_at(i); break; }
	if (g_n == RECENT_MAX) remove_at(g_n - 1);
	for (i = g_n; i > 0; --i) g_list[i] = g_list[i - 1];
	g_list[0] = copy;
	++g_n;
	persist();
}

void recent_clear(void)
{
	recent_shutdown();
	persist();
}

int recent_count(void) { return g_n; }

const char* recent_get(int index)
{
	return (index >= 0 && index < g_n) ? g_list[index] : NULL;
}

#ifdef _WIN32

#define FLY_PAD  th_scale(6)
#define FLY_HEAD th_scale(32)
#define FLY_ROW  th_scale(46)
#define FLY_ACT  th_scale(34)
#define FLY_RAD  th_scale(10)

static PlWidget* g_fly;
static HWND      g_flyWnd, g_flyParent;
static PlFont* g_flyName, * g_flySub, * g_flyBold;
static char* g_flyPath[RECENT_MAX];
static int       g_flyN, g_flyHot = -1, g_flyOpen;
static ULONGLONG g_flyClosedAt;
static int g_flySwallowUp;
static RecentFlyoutHost g_host;
static int g_flyTop;

static int fly_item_y(int i)
{
	int y = FLY_PAD + FLY_HEAD;
	if (i < g_flyN) return y + i * FLY_ROW;
	y += g_flyN * FLY_ROW + th_scale(9);            
	return y + (i - g_flyN) * FLY_ACT;
}

static int fly_item_h(int i) { return i < g_flyN ? FLY_ROW : FLY_ACT; }

static int fly_total_h(void)
{
	return fly_item_y(g_flyN + 1) + FLY_ACT + FLY_PAD;
}

static int fly_hit(int y)
{
	int i;
	for (i = 0; i < g_flyN + 2; ++i)
		if (y >= fly_item_y(i) && y < fly_item_y(i) + fly_item_h(i)) return i;
	return -1;
}

static void fly_free_paths(void)
{
	int i;
	for (i = 0; i < RECENT_MAX; ++i) {
		free(g_flyPath[i]);
		g_flyPath[i] = NULL;
	}
	g_flyN = 0;
}

void recent_flyout_close(void)
{
	if (!g_flyOpen) return;
	g_flyOpen = 0;
	g_flyHot = -1;
	if (g_flyWnd) SetWindowRgn(g_flyWnd, NULL, FALSE);
	if (g_fly) pl_widget_set_rect(g_fly, 0, 0, 0, 0);
	g_flyWnd = NULL;
	fly_free_paths();
}

int recent_flyout_is_open(void) { return g_flyOpen; }

static void fly_activate(int i)
{
	char* pick = NULL;
	int n = g_flyN;                 
	if (i < 0) return;
	if (i < n && g_flyPath[i]) pick = DUP(g_flyPath[i]);
	recent_flyout_close();

	if (i < n) {
		if (pick && g_host.on_pick) g_host.on_pick(pick, g_host.user);
	}
	else if (i == n) {
		if (g_host.on_open) g_host.on_open(g_host.user);
	}
	else {
		if (g_host.on_clear) g_host.on_clear(g_host.user);
	}
	free(pick);
}

static void fly_step(int d)
{
	int total = g_flyN + 2;
	if (g_flyHot < 0) g_flyHot = d > 0 ? 0 : total - 1;
	else g_flyHot = (g_flyHot + d + total) % total;
	if (g_fly) pl_invalidate(g_fly, NULL);
}

static int fly_inset(int rad, int y)
{
	double dy = rad - y - 0.5;
	return rad - (int)(sqrt((double)rad * rad - dy * dy) + 0.5);
}

static int fly_clamp_rad(int rad, int w, int h)
{
	if (rad * 2 > w) rad = w / 2;
	if (rad * 2 > h) rad = h / 2;
	return rad;
}

#define FLY_BACKDROP CLR_PANEL_BODY

static PlColor fly_lerp(PlColor a, PlColor b, double t)
{
	PlColor out = 0;
	unsigned char* pa = (unsigned char*)&a;
	unsigned char* pb = (unsigned char*)&b;
	unsigned char* po = (unsigned char*)&out;
	size_t i;
	if (t < 0) t = 0;
	if (t > 1) t = 1;
	for (i = 0; i < sizeof(PlColor); ++i)
		po[i] = (unsigned char)(pa[i] + (pb[i] - pa[i]) * t + 0.5);
	return out;
}

static double fly_cov(double rad, double d)
{
	double v = rad - d + 0.5;
	return v < 0 ? 0 : (v > 1 ? 1 : v);
}

static void fly_frame(PlCanvas* c, int w, int h, int rad)
{
	int x, y;
	rad = fly_clamp_rad(rad, w, h);

	pl_fill(c, rad, 0, w - rad, h, CLR_PANEL_BORDER);
	pl_fill(c, 0, rad, w, h - rad, CLR_PANEL_BORDER);
	pl_fill(c, rad, 1, w - rad, h - 1, CLR_PANEL_BODY);
	pl_fill(c, 1, rad, w - 1, h - rad, CLR_PANEL_BODY);

	for (y = 0; y < rad; ++y) {
		for (x = 0; x < rad; ++x) {
			double dx = rad - (x + 0.5), dy = rad - (y + 0.5);
			double d = sqrt(dx * dx + dy * dy);
			double outer = fly_cov(rad, d);
			double inner = fly_cov(rad - 1, d);
			PlColor col = fly_lerp(FLY_BACKDROP, CLR_PANEL_BORDER, outer);
			col = fly_lerp(col, CLR_PANEL_BODY, inner);
			pl_fill(c, x, y, x + 1, y + 1, col);
			pl_fill(c, w - 1 - x, y, w - x, y + 1, col);
			pl_fill(c, x, h - 1 - y, x + 1, h - y, col);
			pl_fill(c, w - 1 - x, h - 1 - y, w - x, h - y, col);
		}
	}
}

static HRGN fly_region(int w, int h, int rad)
{
	HRGN rgn;
	int y;
	rad = fly_clamp_rad(rad, w, h);
	rgn = CreateRectRgn(0, rad, w, h - rad);
	if (!rgn) return NULL;
	for (y = 0; y < rad; ++y) {
		int in = 0;
		HRGN a, b;
		while (in < rad) {
			double dx = rad - (in + 0.5), dy = rad - (y + 0.5);
			if (fly_cov((double)rad, sqrt(dx * dx + dy * dy)) > 0.0) break;
			++in;
		}
		a = CreateRectRgn(in, y, w - in, y + 1);
		b = CreateRectRgn(in, h - y - 1, w - in, h - y);
		if (a) { CombineRgn(rgn, rgn, a, RGN_OR); DeleteObject(a); }
		if (b) { CombineRgn(rgn, rgn, b, RGN_OR); DeleteObject(b); }
	}
	return rgn;
}

static void fly_hot_bg(PlCanvas* c, int w, int y, int h)
{
	PlRect r = { th_scale(6), y, w - th_scale(6), y + h };
	pl_round_rect(c, r, CLR_SIDEBAR_SEL, CLR_SIDEBAR_SEL, th_scale(6));
}

static void fly_paint(void* u, PlCanvas* c, int w, int h)
{
	int i, pad = th_scale(16);
	PlRect r;
	(void)u;

	fly_frame(c, w, h, FLY_RAD);

	r.l = pad; r.t = FLY_PAD; r.r = w - pad; r.b = FLY_PAD + FLY_HEAD;
	pl_text(c, g_flyBold, r, translate("Recent files"),
		CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);

	for (i = 0; i < g_flyN; ++i) {
		int y = fly_item_y(i);
		const char* full = g_flyPath[i], * name = full, * p;
		char* folder;
		if (!full) continue;
		for (p = full; *p; ++p)
			if (*p == '\\' || *p == '/') name = p + 1;

		if (i == g_flyHot) fly_hot_bg(c, w, y, FLY_ROW);
		r.l = pad; r.r = w - pad;
		r.t = y + th_scale(5); r.b = y + th_scale(24);
		pl_text(c, g_flyName, r, name, CLR_TEXT_PRIMARY, PL_TEXT_ELLIPSIS);

		folder = DUP(full);
		if (folder) {
			folder[name > full ? (name - full - 1) : 0] = 0;
			r.t = y + th_scale(24); r.b = y + th_scale(41);
			pl_text(c, g_flySub, r, folder, CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
			free(folder);
		}
	}

	{
		int sy = fly_item_y(g_flyN) - th_scale(5);
		pl_line(c, pad, sy, w - pad, sy, CLR_SEPARATOR);
	}
	for (i = g_flyN; i < g_flyN + 2; ++i) {
		int y = fly_item_y(i);
		if (i == g_flyHot) fly_hot_bg(c, w, y, FLY_ACT);
		r.l = pad; r.r = w - pad; r.t = y; r.b = y + FLY_ACT;
		pl_text(c, g_flyName, r,
			translate(i == g_flyN ? "Open file..." : "Clear recent files"),
			i == g_flyHot ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
	}
}

static int fly_cursor_y(void)
{
	POINT p;
	if (!GetCursorPos(&p)) return -1;
	ScreenToClient(g_flyParent, &p);
	return p.y - g_flyTop;
}

static void fly_mouse_move(void* u, int x, int y)
{
	int hit = fly_hit(fly_cursor_y());
	(void)u; (void)x; (void)y;
	if (hit != g_flyHot) { g_flyHot = hit; pl_invalidate(g_fly, NULL); }
}

static void fly_mouse_leave(void* u)
{
	(void)u;
	if (g_flyHot != -1) { g_flyHot = -1; pl_invalidate(g_fly, NULL); }
}

static void fly_mouse_down(void* u, int x, int y)
{
	int hit = fly_hit(fly_cursor_y());
	(void)u; (void)x; (void)y;
	if (hit >= 0) g_flySwallowUp = 1;
	fly_activate(hit);
}

static void fly_resize(void* u, int w, int h)
{
	(void)u; (void)w; (void)h;
	if (g_fly) pl_invalidate(g_fly, NULL);
}

static void fly_destroy(void* u)
{
	(void)u;
	pl_font_destroy(g_flyName);  g_flyName = NULL;
	pl_font_destroy(g_flySub);   g_flySub = NULL;
	pl_font_destroy(g_flyBold);  g_flyBold = NULL;
	g_fly = NULL;
}

static const PlWidgetOps s_flyOps = {
	fly_paint, fly_mouse_move, fly_mouse_leave, fly_mouse_down, fly_resize, NULL, fly_destroy,
	NULL, NULL
};

void recent_flyout_create(void* parent_native, const RecentFlyoutHost* host)
{
	g_flyParent = (HWND)parent_native;
	if (host) g_host = *host;
	g_flyName = pl_font_create(9, 0);
	g_flySub = pl_font_create(8, 0);
	g_flyBold = pl_font_create(9, 1);
	g_fly = pl_widget_create(parent_native, &s_flyOps, NULL, 0, 0, 0, 0);
}

static void fly_raise(int x, int y, int w, int h)
{
	HWND c;
	g_flyWnd = NULL;
	for (c = GetWindow(g_flyParent, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
		RECT r; POINT pt;
		GetWindowRect(c, &r);
		pt.x = r.left; pt.y = r.top;
		ScreenToClient(g_flyParent, &pt);
		if (abs(pt.x - x) <= 1 && abs(pt.y - y) <= 1 &&
			abs((r.right - r.left) - w) <= 1 && abs((r.bottom - r.top) - h) <= 1 &&
			IsWindowVisible(c)) {
			BringWindowToTop(c);
			g_flyWnd = c;
			return;
		}
	}
}

int recent_flyout_toggle(int x, int bottom)
{
	int i, y, w, h;

	if (!g_fly) return 1;
	if (g_flyOpen) { recent_flyout_close(); return 1; }
	if (GetTickCount64() - g_flyClosedAt < 300) return 1;   
	if (g_n <= 0) return 0;

	fly_free_paths();
	for (i = 0; i < g_n; ++i) g_flyPath[i] = DUP(g_list[i]);
	g_flyN = g_n;
	g_flyHot = -1;

	w = th_scale(360);
	h = fly_total_h();
	if (h > bottom) h = bottom;
	y = bottom - h;
	if (y < 0) y = 0;
	g_flyTop = y;

	pl_widget_set_rect(g_fly, x, y, w, h);
	fly_raise(x, y, w, h);
	if (g_flyWnd) {
		HRGN rgn = fly_region(w, h, FLY_RAD);
		if (rgn) SetWindowRgn(g_flyWnd, rgn, TRUE);   
	}
	g_flyOpen = 1;
	pl_invalidate(g_fly, NULL);
	return 1;
}

void recent_flyout_filter(void* msg)
{
	MSG* m = (MSG*)msg;

	if (m->message == WM_LBUTTONUP && g_flySwallowUp) {
		g_flySwallowUp = 0;
		m->message = WM_NULL;
		return;
	}

	if (m->message == WM_LBUTTONDOWN || m->message == WM_RBUTTONDOWN ||
		m->message == WM_NCLBUTTONDOWN) {
		RECT fr;
		if (g_flyWnd && GetWindowRect(g_flyWnd, &fr) && !PtInRect(&fr, m->pt)) {
			recent_flyout_close();
			g_flyClosedAt = GetTickCount64();    
		}
	}
	else if (m->message == WM_KEYDOWN) {
		switch (m->wParam) {
		case VK_ESCAPE: recent_flyout_close(); m->message = WM_NULL; break;
		case VK_UP:     fly_step(-1);          m->message = WM_NULL; break;
		case VK_DOWN:   fly_step(1);           m->message = WM_NULL; break;
		case VK_RETURN:
			if (g_flyHot >= 0) fly_activate(g_flyHot);
			m->message = WM_NULL;
			break;
		default: break;
		}
	}
}

#endif /* _WIN32 */