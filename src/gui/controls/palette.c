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
#include "platform/platform.h"
#include "theme/theme.h"
#include "controls/palette.h"
#include "core/i18n.h"

#define PAL_W     440
#define PAL_IN_H  44
#define PAL_ROW_H 30
#define PAL_ROWS  8
#define PAL_MAX   64

typedef struct { char label[64]; char kind[16]; int id; } Entry;

struct Palette {
	PlWidget* w;
	PlFont* ui, * mono;
	PaletteCb cb;
	void* user;
	Entry items[PAL_MAX];
	int count;
	int filt[PAL_MAX], nfilt, sel, top, open;
	char query[64];
	int ax, ay, aw, ah, W, H;
	int rx, ry;
};

static const char* tr(const char* k) { const char* t = translate(k); return t ? t : k; }

static char low(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int fuzzy(const char* q, const char* s)
{
	while (*q == ' ') ++q;
	for (; *s && *q; ++s) {
		if (*q == ' ') { ++q; --s; continue; }
		if (low(*q) == low(*s)) ++q;
	}
	while (*q == ' ') ++q;
	return *q == 0;
}

static void edit_append(char* s, size_t cap, uint32_t ch)
{
	size_t n = strlen(s);
	if (n + 5 >= cap) return;
	if (ch < 0x80) s[n++] = (char)ch;
	else if (ch < 0x800) { s[n++] = (char)(0xC0 | (ch >> 6)); s[n++] = (char)(0x80 | (ch & 63)); }
	else if (ch < 0x10000) { s[n++] = (char)(0xE0 | (ch >> 12)); s[n++] = (char)(0x80 | ((ch >> 6) & 63)); s[n++] = (char)(0x80 | (ch & 63)); }
	else { s[n++] = (char)(0xF0 | (ch >> 18)); s[n++] = (char)(0x80 | ((ch >> 12) & 63)); s[n++] = (char)(0x80 | ((ch >> 6) & 63)); s[n++] = (char)(0x80 | (ch & 63)); }
	s[n] = 0;
}

static void edit_back(char* s)
{
	size_t n = strlen(s);
	while (n > 0 && ((unsigned char)s[n - 1] & 0xC0) == 0x80) --n;
	if (n > 0) s[n - 1] = 0;
}

static void relayout(Palette* p)
{
	int rows = p->nfilt < PAL_ROWS ? p->nfilt : PAL_ROWS;
	if (rows < 1) rows = 1;
	p->W = p->aw - 20 < PAL_W ? p->aw - 20 : PAL_W;
	if (p->W < 200) p->W = 200;
	p->H = PAL_IN_H + 1 + rows * PAL_ROW_H + 8;
	p->rx = p->ax + (p->aw - p->W) / 2;
	p->ry = p->ay + 40;
	pl_widget_set_rect(p->w, p->rx, p->ry, p->W, p->H);
	pl_invalidate(p->w, NULL);
}

static void refilter(Palette* p)
{
	int i;
	p->nfilt = 0;
	for (i = 0; i < p->count; ++i)
		if (fuzzy(p->query, tr(p->items[i].label))) p->filt[p->nfilt++] = i;
	p->sel = 0; p->top = 0;
	relayout(p);
}

static void keep_visible(Palette* p)
{
	if (p->sel < p->top) p->top = p->sel;
	else if (p->sel >= p->top + PAL_ROWS) p->top = p->sel - PAL_ROWS + 1;
}

static void run_selected(Palette* p)
{
	int id;
	if (p->sel < 0 || p->sel >= p->nfilt) return;
	id = p->items[p->filt[p->sel]].id;
	palette_close(p);
	if (p->cb) p->cb(p->user, id);
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
	Palette* p = (Palette*)u;
	PlRect frame = { 0, 0, w - 1, h - 1 };
	PlRect in = { 16, 0, w - 16, PAL_IN_H };
	int r;

	pl_fill(c, 0, 0, w, h, CLR_PANEL_BODY);
	pl_round_rect(c, frame, CLR_PANEL, CLR_ACCENT, 12);

	if (p->query[0]) {
		int cx = 16 + pl_text_width(p->mono, p->query);
		if (cx > w - 16) cx = w - 16;
		pl_text(c, p->mono, in, p->query, CLR_TEXT_PRIMARY, PL_TEXT_ELLIPSIS);
		pl_line(c, cx + 1, 13, cx + 1, PAL_IN_H - 13, CLR_TEXT_PRIMARY);
	}
	else {
		pl_text(c, p->ui, in, tr("Type to search views and actions..."),
			CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
		pl_line(c, 16, 13, 16, PAL_IN_H - 13, CLR_TEXT_PRIMARY);
	}
	pl_line(c, 1, PAL_IN_H, w - 2, PAL_IN_H, CLR_SEPARATOR);

	if (p->nfilt == 0) {
		PlRect nr = { 18, PAL_IN_H + 1, w - 18, PAL_IN_H + 1 + PAL_ROW_H };
		pl_text(c, p->ui, nr, tr("No matches"), CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
		return;
	}
	for (r = 0; r < PAL_ROWS && p->top + r < p->nfilt; ++r) {
		int idx = p->top + r;
		const Entry* e = &p->items[p->filt[idx]];
		int y = PAL_IN_H + 1 + r * PAL_ROW_H;
		PlRect row = { 6, y, w - 6, y + PAL_ROW_H };
		PlRect lr = { 18, y, w - 100, y + PAL_ROW_H };
		PlRect kr = { w - 90, y, w - 16, y + PAL_ROW_H };
		if (idx == p->sel) pl_round_rect(c, row, CLR_SIDEBAR_SEL, CLR_SIDEBAR_SEL, 6);
		pl_text(c, p->ui, lr, tr(e->label),
			idx == p->sel ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
		pl_text(c, p->ui, kr, tr(e->kind), CLR_TEXT_SECONDARY, PL_TEXT_RIGHT);
	}
}

static int row_at(Palette* p, int y)
{
	int r, idx;
	if (y < PAL_IN_H + 1) return -1;
	r = (y - PAL_IN_H - 1) / PAL_ROW_H;
	idx = p->top + r;
	return (r < PAL_ROWS && idx < p->nfilt) ? idx : -1;
}

static void on_mouse_move(void* u, int x, int y)
{
	Palette* p = (Palette*)u;
	int idx = row_at(p, y);
	(void)x;
	if (idx >= 0 && idx != p->sel) { p->sel = idx; pl_invalidate(p->w, NULL); }
}

static void on_mouse_down(void* u, int x, int y)
{
	Palette* p = (Palette*)u;
	int idx = row_at(p, y);
	(void)x;
	if (idx >= 0) { p->sel = idx; run_selected(p); }
}

static int on_hand(void* u, int x, int y)
{
	(void)x;
	return row_at((Palette*)u, y) >= 0;
}

static void on_wheel(void* u, int x, int y, int delta, int mods)
{
	Palette* p = (Palette*)u;
	(void)x; (void)y; (void)mods;
	if (!delta || p->nfilt <= PAL_ROWS) return;
	p->top += delta > 0 ? -1 : 1;
	if (p->top < 0) p->top = 0;
	if (p->top > p->nfilt - PAL_ROWS) p->top = p->nfilt - PAL_ROWS;
	pl_invalidate(p->w, NULL);
}

static void on_key(void* u, int key, uint32_t ch, int mods)
{
	Palette* p = (Palette*)u;
	(void)mods;
	if (key == PL_KEY_ESC) { palette_close(p); return; }
	if (key == PL_KEY_ENTER) { run_selected(p); return; }
	if (key == PL_KEY_DOWN) { if (p->sel + 1 < p->nfilt) ++p->sel; keep_visible(p); }
	else if (key == PL_KEY_UP) { if (p->sel > 0) --p->sel; keep_visible(p); }
	else if (key == PL_KEY_BACK || ch == 8) { edit_back(p->query); refilter(p); return; }
	else if (ch >= 32 && ch != 127) { edit_append(p->query, sizeof p->query, ch); refilter(p); return; }
	pl_invalidate(p->w, NULL);
}

void palette_key(Palette* p, int key, uint32_t ch, int mods)
{
	if (p && p->open) on_key(p, key, ch, mods);
}

static void on_resize(void* u, int w, int h)
{
	(void)w; (void)h;
	pl_invalidate(((Palette*)u)->w, NULL);
}

static void on_destroy(void* u)
{
	Palette* p = (Palette*)u;
	pl_font_destroy(p->ui);
	pl_font_destroy(p->mono);
	free(p);
}

static const PlWidgetOps s_ops = {
	on_paint, on_mouse_move, NULL, on_mouse_down,
	on_resize, on_hand, on_destroy,
	on_key, on_wheel
};

Palette* palette_create(void* parent_native, PaletteCb cb, void* user)
{
	Palette* p = (Palette*)calloc(1, sizeof * p);
	if (!p) return NULL;
	p->cb = cb; p->user = user;
	p->ui = pl_font_create(10, 0);
	p->mono = pl_font_create(11, 0);
	p->w = pl_widget_create(parent_native, &s_ops, p, 0, 0, 0, 0);
	if (!p->ui || !p->mono || !p->w) {
		if (p->ui) pl_font_destroy(p->ui);
		if (p->mono) pl_font_destroy(p->mono);
		free(p);
		return NULL;
	}
	return p;
}

void palette_destroy(Palette* p) { if (p) pl_widget_destroy(p->w); }
PlWidget* palette_widget(const Palette* p) { return p ? p->w : NULL; }
int palette_is_open(const Palette* p) { return p && p->open; }

int palette_get_rect(const Palette* p, int* x, int* y, int* w, int* h)
{
	if (!p || !p->open) return 0;
	*x = p->rx; *y = p->ry; *w = p->W; *h = p->H;
	return 1;
}

void palette_add(Palette* p, const char* label, const char* kind, int id)
{
	Entry* e;
	if (!p || p->count >= PAL_MAX) return;
	e = &p->items[p->count++];
	snprintf(e->label, sizeof e->label, "%s", label);
	snprintf(e->kind, sizeof e->kind, "%s", kind);
	e->id = id;
}

void palette_open(Palette* p, int ax, int ay, int aw, int ah)
{
	if (!p) return;
	p->ax = ax; p->ay = ay; p->aw = aw; p->ah = ah;
	p->query[0] = 0;
	p->open = 1;
	refilter(p);
}

void palette_close(Palette* p)
{
	if (!p || !p->open) return;
	p->open = 0;
	pl_widget_set_rect(p->w, 0, 0, 0, 0);
}