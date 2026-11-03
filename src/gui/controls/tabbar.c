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
#include "controls/tabbar.h"
#include "theme/theme.h"

static const char* s_labels[TAB_COUNT] = {
		"Scan", "Memory map", "Disassembly", "Hex",
		"Strings", "Hash", "Entropy", "More"
};

struct Tabbar {
	PlWidget* w;
	TabbarCb  cb;
	void* user;
	int       active, hovered, height;
	PlFont* font;
	int       tab_x[TAB_COUNT + 1];
};

static void measure(Tabbar* d)
{
	int x = CONTENT_PAD - 4, i;
	for (i = 0; i < TAB_COUNT; ++i) {
		d->tab_x[i] = x;
		x += pl_text_width(d->font, s_labels[i]) + 42;
	}
	d->tab_x[TAB_COUNT] = x;
}

void tabbar_relayout(Tabbar* t)
{
	if (!t) return;
	measure(t);
	pl_invalidate(t->w, NULL);
}

static int hittest(const Tabbar* d, int mx)
{
	int i;
	for (i = 0; i < TAB_COUNT; ++i)
		if (mx >= d->tab_x[i] && mx < d->tab_x[i + 1]) return i;
	return -1;
}

static void invalidate_tab(Tabbar* d, int i)
{
	PlRect r;
	if (i < 0) return;
	r.l = d->tab_x[i]; r.r = d->tab_x[i + 1];
	r.t = 0;           r.b = d->height;
	pl_invalidate(d->w, &r);
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
	Tabbar* d = (Tabbar*)u;
	int i;
	d->height = h;
	pl_fill(c, 0, 0, w, h, CLR_BG);

	for (i = 0; i < TAB_COUNT; ++i) {
		int tx = d->tab_x[i], tw = d->tab_x[i + 1] - tx;
		int ac = (d->active == i), hv = (d->hovered == i);
		PlRect pill = { tx + 4, 6, tx + tw - 4, h - 6 };
		PlRect txt = { tx, 6, tx + tw, h - 6 };

		if (ac)
			pl_round_rect(c, pill, CLR_ACCENT, CLR_ACCENT, 8);
		else
			pl_round_rect(c, pill, hv ? CLR_BTN_GHOST_HOVER : CLR_BTN_GHOST,
				CLR_BTN_GHOST_BD, 8);
		pl_text(c, d->font, txt, s_labels[i], CLR_TEXT_PRIMARY, PL_TEXT_CENTER);
	}
	pl_line(c, 0, h - 1, w, h - 1, CLR_SEPARATOR);
}

static void on_resize(void* u, int w, int h)
{
	Tabbar* d = (Tabbar*)u;
	(void)w;
	d->height = h;
	pl_invalidate(d->w, NULL);
}

static void on_mouse_move(void* u, int x, int y)
{
	Tabbar* d = (Tabbar*)u;
	int hit = hittest(d, x);
	(void)y;
	if (d->hovered != hit) {
		invalidate_tab(d, d->hovered);
		d->hovered = hit;
		invalidate_tab(d, hit);
	}
}

static void on_mouse_leave(void* u)
{
	Tabbar* d = (Tabbar*)u;
	if (d->hovered != -1) {
		int old = d->hovered;
		d->hovered = -1;
		invalidate_tab(d, old);
	}
}

static void on_mouse_down(void* u, int x, int y)
{
	Tabbar* d = (Tabbar*)u;
	int hit = hittest(d, x);
	(void)y;
	if (hit < 0) return;
	tabbar_set_active(d, hit);
	if (d->cb) d->cb(d->user, hit);
}

static int on_hand(void* u, int x, int y)
{
	(void)y;
	return hittest((Tabbar*)u, x) >= 0;
}

static void on_destroy(void* u)
{
	Tabbar* d = (Tabbar*)u;
	pl_font_destroy(d->font);
	free(d);
}

static const PlWidgetOps s_ops = {
		on_paint, on_mouse_move, on_mouse_leave, on_mouse_down, on_resize, on_hand, on_destroy
};

Tabbar* tabbar_create(void* parent_native, int x, int y, int w, int h,
	TabbarCb cb, void* user)
{
	Tabbar* t = (Tabbar*)calloc(1, sizeof * t);
	if (!t) return NULL;
	t->cb = cb; t->user = user;
	t->active = TAB_SCAN; t->hovered = -1; t->height = h;
	t->font = pl_font_create(9, 0);
	measure(t);
	t->w = pl_widget_create(parent_native, &s_ops, t, x, y, w, h);
	if (!t->w) {
		pl_font_destroy(t->font);
		free(t);
		return NULL;
	}
	return t;
}

void tabbar_destroy(Tabbar* t) { if (t) pl_widget_destroy(t->w); }
PlWidget* tabbar_widget(const Tabbar* t) { return t->w; }
int tabbar_get_active(const Tabbar* t) { return t->active; }

void tabbar_set_active(Tabbar* t, int tab_id)
{
	int old = t->active;
	if (tab_id < 0 || tab_id >= TAB_COUNT || tab_id == old) return;
	t->active = tab_id;
	invalidate_tab(t, old);
	invalidate_tab(t, tab_id);
}