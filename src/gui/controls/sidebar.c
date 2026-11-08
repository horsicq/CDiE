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
#include <stdlib.h>
#include "controls/sidebar.h"
#include "theme/theme.h"
#include "theme/logo_data.h"
#include "core/i18n.h"

#define LOGO_DRAW 24
#define SB_GROUPS 3
#define GRP_H     28

struct Sidebar {
	PlWidget* w;
	SidebarCb cb;
	void* user;
	int       selected, hovered, width, height, scroll;
	int       open[SB_GROUPS];
	PlFont* fontNav, * fontTitle, * fontBold;
};

static const char* s_labels[NAV_COUNT] = {
		"Overview", "Sections", "Signatures", "Strings", "Hex viewer",
		"Disassembly", "Entropy", "VirusTotal", "NT headers", "YARA rules", "Options" 
};
typedef struct { int group; int nav; } SbRow;     

static const char* s_group_names[SB_GROUPS] = { "ANALYSIS", "DATA", "CODE" };

static const SbRow s_rows[] = {
	{ 0, -1 },{ 0, NAV_OVERVIEW },{ 0, NAV_SECTIONS },{ 0, NAV_HEADERS },{ 0, NAV_SIGNATURES },{ 0, NAV_VIRUSTOTAL },
	{ 1, -1 },{ 1, NAV_STRINGS },{ 1, NAV_HEX },{ 1, NAV_ENTROPY },
	{ 2, -1 },{ 2, NAV_DISASM },{ 2, NAV_COUNT - 2 }
};
#define SB_ROWS ((int)(sizeof s_rows / sizeof s_rows[0]))

static int row_visible(const Sidebar* d, int r)
{
	return s_rows[r].nav < 0 || d->open[s_rows[r].group];
}

static int row_h(int r) { return s_rows[r].nav < 0 ? GRP_H : NAV_H; }

#define LIST_TOP (LOGO_H + SEARCH_MY + SEARCH_H + SEARCH_MY + 4)

static int row_y(const Sidebar* d, int r)
{
	int y = LIST_TOP - d->scroll, j;
	for (j = 0; j < r; ++j)
		if (row_visible(d, j)) y += row_h(j);
	return y;
}

static int nav_item_y(const Sidebar* d, int i)
{
	int r;
	if (i == NAV_OPTIONS) return d->height - NAV_H - 8;
	for (r = 0; r < SB_ROWS; ++r)
		if (s_rows[r].nav == i) return row_visible(d, r) ? row_y(d, r) : -1;
	return -1;
}

static int list_bottom(const Sidebar* d)
{
	return nav_item_y(d, NAV_OPTIONS) - 8;     
}

static int content_h(const Sidebar* d)
{
	int r, h = 0;
	for (r = 0; r < SB_ROWS; ++r)
		if (row_visible(d, r)) h += row_h(r);
	return h;
}

static void clamp_scroll(Sidebar* d)
{
	int max = content_h(d) - (list_bottom(d) - LIST_TOP);
	if (max < 0) max = 0;
	if (d->scroll > max) d->scroll = max;
	if (d->scroll < 0) d->scroll = 0;
}

static int sidebar_hittest(Sidebar* d, int my)
{
	int r, y;
	for (r = 0; r < SB_ROWS; ++r) {
		if (!row_visible(d, r)) continue;
		if (my < LIST_TOP || my >= list_bottom(d)) continue;   /* scrolled out of view */
		y = row_y(d, r);
		if (my >= y && my < y + row_h(r))
			return s_rows[r].nav >= 0 ? s_rows[r].nav : -(s_rows[r].group + 2);
	}
	y = nav_item_y(d, NAV_OPTIONS);
	if (my >= y && my < y + NAV_H) return NAV_OPTIONS;
	return -1;
}

static int group_y(const Sidebar* d, int g)
{
	int r;
	for (r = 0; r < SB_ROWS; ++r)
		if (s_rows[r].nav < 0 && s_rows[r].group == g) return row_y(d, r);
	return -1;
}

static void invalidate_item(Sidebar* d, int i)
{
	PlRect r;
	int y, hh;
	if (i == -1) return;
	if (i < -1) { y = group_y(d, -i - 2); hh = GRP_H; }
	else { y = nav_item_y(d, i);   hh = NAV_H; }
	if (y < 0) return;                    
	r.l = 0; r.r = d->width;
	r.t = y; r.b = y + hh;
	pl_invalidate(d->w, &r);
}

static void draw_group(Sidebar* d, PlCanvas* c, int g, int y, int w)
{
	PlRect r = { 22, y, w - 34, y + GRP_H };
	int cx = w - 20, cy = y + GRP_H / 2;
	if (d->hovered == -(g + 2))
		pl_fill(c, 0, y, w, y + GRP_H, CLR_SIDEBAR_HOVER);
	pl_text(c, d->fontBold, r, translate(s_group_names[g]),
		CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
	if (d->open[g]) {
		pl_line(c, cx - 4, cy - 2, cx, cy + 2, CLR_TEXT_SECONDARY);
		pl_line(c, cx, cy + 2, cx + 5, cy - 3, CLR_TEXT_SECONDARY);
	}
	else {
		pl_line(c, cx - 2, cy - 4, cx + 2, cy, CLR_TEXT_SECONDARY);
		pl_line(c, cx + 2, cy, cx - 3, cy + 5, CLR_TEXT_SECONDARY);
	}
}

static void draw_search(Sidebar* d, PlCanvas* c, int width)
{
	int sx = SEARCH_MX, sy = LOGO_H + SEARCH_MY;
	int sw = width - 2 * SEARCH_MX;
	int cx = sx + 14, cy = sy + SEARCH_H / 2;
	PlRect box = { sx, sy, sx + sw, sy + SEARCH_H };
	PlRect tr = { sx + 26, sy, sx + sw - 4, sy + SEARCH_H };

	pl_round_rect(c, box, CLR_SEARCH_BG, CLR_PANEL_BORDER, 6);
	pl_ellipse(c, cx - 5, cy - 5, cx + 5, cy + 5, PL_NONE, CLR_TEXT_SECONDARY);
	pl_line(c, cx + 4, cy + 4, cx + 8, cy + 8, CLR_TEXT_SECONDARY);
	pl_text(c, d->fontNav, tr, translate("Search"), CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
}

static void draw_item(Sidebar* d, PlCanvas* c, int i, int w, int h)
{
	int y = nav_item_y(d, i);
	int sel = (d->selected == i), hov = (d->hovered == i);
	PlColor bg = sel ? CLR_SIDEBAR_SEL : (hov ? CLR_SIDEBAR_HOVER : CLR_SIDEBAR_BG);
	PlRect r = { 30, y, w - 8, y + NAV_H };

	pl_fill(c, 0, y, w, y + NAV_H, bg);
	if (sel) pl_fill(c, 0, y, 3, y + NAV_H, CLR_ACCENT);
	pl_text(c, d->fontNav, r, translate(s_labels[i]),
		sel ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
}

static void draw_logo(PlCanvas* c, int x, int y, PlColor bg)
{
	int px, py;
	int br = (bg >> 16) & 255, bgc = (bg >> 8) & 255, bb = bg & 255;
	for (py = 0; py < LOGO_IMG_H; ++py)
		for (px = 0; px < LOGO_IMG_W; ++px) {
			const unsigned char* p = &logo_rgba[(py * LOGO_IMG_W + px) * 4];
			int a = p[3];
			int r, g, b;
			if (a == 0) continue;
			r = (p[0] * a + br * (255 - a)) / 255;
			g = (p[1] * a + bgc * (255 - a)) / 255;
			b = (p[2] * a + bb * (255 - a)) / 255;
			pl_fill(c, x + px, y + py, x + px + 1, y + py + 1,
				(PlColor)((r << 16) | (g << 8) | b));
		}
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
	Sidebar* d = (Sidebar*)u;
	PlRect tr = { 14 + LOGO_DRAW + 8, 13, w - 8, 37 };
	int i, lb, th, ch;

	d->width = w; d->height = h;
	clamp_scroll(d);
	lb = list_bottom(d);

	pl_fill(c, 0, 0, w, h, CLR_SIDEBAR_BG);

	for (i = 0; i < SB_ROWS; ++i) {
		if (!row_visible(d, i)) continue;
		if (s_rows[i].nav < 0) draw_group(d, c, s_rows[i].group, row_y(d, i), w);
		else draw_item(d, c, s_rows[i].nav, w, h);
	}

	pl_fill(c, 0, 0, w, LIST_TOP, CLR_SIDEBAR_BG);
	pl_fill(c, 0, lb, w, h, CLR_SIDEBAR_BG);

	pl_fill(c, 0, 0, w, LOGO_H, CLR_LOGO_BG);
	pl_image(c, 14, (LOGO_H - LOGO_DRAW) / 2, LOGO_DRAW, LOGO_DRAW,
		LOGO_IMG_W, LOGO_IMG_H, logo_rgba);
	pl_text(c, d->fontTitle, tr, translate("Detect It Easy"), CLR_TEXT_PRIMARY, PL_TEXT_ELLIPSIS);
	pl_line(c, 0, LOGO_H, w, LOGO_H, CLR_SEPARATOR);

	draw_search(d, c, w);

	pl_line(c, SEARCH_MX, lb, w - SEARCH_MX, lb, CLR_SEPARATOR);
	draw_item(d, c, NAV_OPTIONS, w, h);

	th = lb - LIST_TOP;
	ch = content_h(d);
	if (ch > th && th > 0) {                    
		int len = th * th / ch, pos;
		if (len < 24) len = 24;
		if (len > th) len = th;
		pos = (th - len) * d->scroll / (ch - th);
		{
			PlRect sb = { w - 6, LIST_TOP + pos, w - 3, LIST_TOP + pos + len };
			pl_round_rect(c, sb, CLR_BTN_GHOST_BD, CLR_BTN_GHOST_BD, 1);
		}
	}

	pl_line(c, w - 1, 0, w - 1, h, CLR_SEPARATOR);
}

static void on_resize(void* u, int w, int h)
{
	Sidebar* d = (Sidebar*)u;
	d->width = w; d->height = h;
	clamp_scroll(d);
	pl_invalidate(d->w, NULL);
}

static void on_mouse_move(void* u, int x, int y)
{
	Sidebar* d = (Sidebar*)u;
	int hit = sidebar_hittest(d, y);
	(void)x;
	if (d->hovered != hit) {
		int old = d->hovered;
		d->hovered = hit;
		invalidate_item(d, old);
		invalidate_item(d, hit);
	}
}

static void on_mouse_leave(void* u)
{
	Sidebar* d = (Sidebar*)u;
	if (d->hovered != -1) {
		invalidate_item(d, d->hovered);
		d->hovered = -1;
	}
}

static void on_mouse_down(void* u, int x, int y)
{
	Sidebar* d = (Sidebar*)u;
	int hit = sidebar_hittest(d, y);
	(void)x;
	if (y >= LOGO_H + SEARCH_MY && y < LOGO_H + SEARCH_MY + SEARCH_H) {
		if (d->cb) d->cb(d->user, 1000);  /* 1000 = open the command palette */
		return;
	}
	if (hit == -1) return;
	if (hit < -1) {                       /* group header: fold / unfold on click */
		int g = -hit - 2;
		d->open[g] = !d->open[g];
		clamp_scroll(d);                 /* folding can shrink the list */
		pl_invalidate(d->w, NULL);
		return;
	}
	if (d->selected != hit) {
		invalidate_item(d, d->selected);
		d->selected = hit;
		invalidate_item(d, hit);
	}
	if (d->cb) d->cb(d->user, hit);
}

static void on_wheel(void* u, int x, int y, int delta, int mods)
{
	Sidebar* d = (Sidebar*)u;
	(void)x; (void)y; (void)mods;
	d->scroll -= delta * NAV_H * 2;      /* delta = notches, positive = up */
	clamp_scroll(d);
	pl_invalidate(d->w, NULL);
}

static void on_destroy(void* u)
{
	Sidebar* d = (Sidebar*)u;
	pl_font_destroy(d->fontNav);
	pl_font_destroy(d->fontTitle);
	pl_font_destroy(d->fontBold);
	free(d);
}

static const PlWidgetOps s_ops = {
		on_paint, on_mouse_move, on_mouse_leave, on_mouse_down, on_resize, NULL, on_destroy,
		NULL, on_wheel 
};

Sidebar* sidebar_create(void* parent_native, int x, int y, int w, int h,
	SidebarCb cb, void* user)
{
	Sidebar* s = (Sidebar*)calloc(1, sizeof * s);
	if (!s) return NULL;
	s->cb = cb; s->user = user;
	s->selected = NAV_OVERVIEW; s->hovered = -1;
	s->open[0] = 1;                       /* only ANALYSIS starts open */
	s->width = w; s->height = h;
	s->fontNav = pl_font_create(9, 0);
	s->fontTitle = pl_font_create(10, 1);
	s->fontBold = pl_font_create(9, 1);
	s->w = pl_widget_create(parent_native, &s_ops, s, x, y, w, h);
	if (!s->w) {
		pl_font_destroy(s->fontNav);
		pl_font_destroy(s->fontTitle);
		pl_font_destroy(s->fontBold);
		free(s);
		return NULL;
	}
	return s;
}

void sidebar_destroy(Sidebar* s) { if (s) pl_widget_destroy(s->w); }
PlWidget* sidebar_widget(const Sidebar* s) { return s->w; }
int sidebar_get_selected(const Sidebar* s) { return s->selected; }

void sidebar_set_selected(Sidebar* s, int nav_id)
{
	if (nav_id < 0 || nav_id >= NAV_COUNT || s->selected == nav_id) return;
	s->selected = nav_id;
	pl_invalidate(s->w, NULL);
}