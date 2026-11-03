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

struct Sidebar {
	PlWidget* w;
	SidebarCb cb;
	void* user;
	int       selected, hovered, width, height;
	PlFont* fontNav, * fontTitle, * fontBold;
};

static const char* s_labels[NAV_COUNT] = {
		"Overview", "Sections", "Signatures", "Strings", "Hex viewer",
		"Disassembly", "Entropy", "VirusTotal", "YARA rules", "Options"
};
static const int s_sep_after[NAV_COUNT] = { 0, 0, 0, 0, 0, 0, 1, 0, 1, 0 };

static int nav_item_y(int i, int client_h)
{
	int y, j;
	if (i == NAV_OPTIONS)
		return client_h - BOTTOMBAR_H - NAV_H - 8;
	y = LOGO_H + SEARCH_MY + SEARCH_H + SEARCH_MY + 4;
	for (j = 0; j < i; ++j) {
		y += NAV_H;
		if (s_sep_after[j]) y += 14;
	}
	return y;
}

static int sidebar_hittest(Sidebar* d, int my)
{
	int i;
	for (i = 0; i < NAV_COUNT; ++i) {
		int y = nav_item_y(i, d->height);
		if (my >= y && my < y + NAV_H) return i;
	}
	return -1;
}

static void invalidate_item(Sidebar* d, int i)
{
	PlRect r;
	if (i < 0) return;
	r.l = 0; r.r = d->width;
	r.t = nav_item_y(i, d->height);
	r.b = r.t + NAV_H;
	pl_invalidate(d->w, &r);
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
	int y = nav_item_y(i, h);
	int sel = (d->selected == i), hov = (d->hovered == i);
	PlColor bg = sel ? CLR_SIDEBAR_SEL : (hov ? CLR_SIDEBAR_HOVER : CLR_SIDEBAR_BG);
	PlRect r = { 22, y, w - 8, y + NAV_H };

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
	PlRect dr = { 14, 13, 38, 37 };
	PlRect tr = { 14 + LOGO_DRAW + 8, 13, w - 8, 37 };
	int i;

	d->width = w; d->height = h;

	pl_fill(c, 0, 0, w, h, CLR_SIDEBAR_BG);
	pl_fill(c, 0, 0, w, LOGO_H, CLR_LOGO_BG);

	pl_image(c, 14, (LOGO_H - LOGO_DRAW) / 2, LOGO_DRAW, LOGO_DRAW,
		LOGO_IMG_W, LOGO_IMG_H, logo_rgba);
	pl_text(c, d->fontTitle, tr, translate("Detect It Easy"), CLR_TEXT_PRIMARY, PL_TEXT_ELLIPSIS);
	pl_line(c, 0, LOGO_H, w, LOGO_H, CLR_SEPARATOR);

	draw_search(d, c, w);

	for (i = 0; i < NAV_COUNT - 1; ++i) {
		draw_item(d, c, i, w, h);
		if (s_sep_after[i]) {
			int sy = nav_item_y(i, h) + NAV_H + 6;
			pl_line(c, SEARCH_MX, sy, w - SEARCH_MX, sy, CLR_SEPARATOR);
		}
	}

	{   /* Options, pinned to the bottom */
		int yi = nav_item_y(NAV_OPTIONS, h);
		pl_line(c, SEARCH_MX, yi - 8, w - SEARCH_MX, yi - 8, CLR_SEPARATOR);
		draw_item(d, c, NAV_OPTIONS, w, h);
	}

	pl_line(c, w - 1, 0, w - 1, h, CLR_SEPARATOR);
}

static void on_resize(void* u, int w, int h)
{
	Sidebar* d = (Sidebar*)u;
	d->width = w; d->height = h;
	pl_invalidate(d->w, NULL);
}

static void on_mouse_move(void* u, int x, int y)
{
	Sidebar* d = (Sidebar*)u;
	int hit = sidebar_hittest(d, y);
	(void)x;
	if (d->hovered != hit) {
		invalidate_item(d, d->hovered);
		d->hovered = hit;
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
	if (hit < 0) return;
	if (d->selected != hit) {
		invalidate_item(d, d->selected);
		d->selected = hit;
		invalidate_item(d, hit);
	}
	if (d->cb) d->cb(d->user, hit);
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
		on_paint, on_mouse_move, on_mouse_leave, on_mouse_down, on_resize, NULL, on_destroy
};

Sidebar* sidebar_create(void* parent_native, int x, int y, int w, int h,
	SidebarCb cb, void* user)
{
	Sidebar* s = (Sidebar*)calloc(1, sizeof * s);
	if (!s) return NULL;
	s->cb = cb; s->user = user;
	s->selected = NAV_OVERVIEW; s->hovered = -1;
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