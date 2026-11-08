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
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controls/sidebar.h"
#include "panels/entropy.h"
#include "theme/theme.h"
#include "core/i18n.h"

#define EV_CURVE_MAX  320
#define EV_REG_MAX    96
#define EV_CHUNK      (64 * 1024)
#define EV_T_PACKED   6.5
#define EV_T_HIGH     7.2
#define ROW_H         22
#define HDR_H         24
#define EV_PAD        18

#define EV_L  PL_TEXT_ELLIPSIS
#define EV_C  PL_TEXT_CENTER
#define EV_R  (PL_TEXT_RIGHT | PL_TEXT_ELLIPSIS)

#define COL_LOW   0x43A047
#define COL_MID   0xFFB300
#define COL_HIGH  0xE53935

enum { B_ENT, B_BYTES, B_GRID, B_RANGE, B_CLEAR, B_RELOAD, B_COPY, B_COUNT };

static const int CX[5] = { 10, 110, 210, 380, 540 };

typedef struct {
	uint64_t off, size;
	double   ent;
	char     name[64];
} Region;

struct EntropyView {
	PlWidget* w;
	EntropyCopyFn copy; void* copyUser;
	EntropyReadFn rd;   void* rdUser;
	unsigned char* buf;

	int      loaded, dirty;
	char     name[260], ftype[16];
	uint64_t fsize;

	double   overall, cmin, cmax;
	unsigned allHist[256];
	int      ncurve;
	uint64_t cbs;
	double   curve[EV_CURVE_MAX];

	int      nreg;
	Region   reg[EV_REG_MAX];
	int      order[EV_REG_MAX];
	int      sortCol, sortAsc;

	int      view, grid, rangeMode, rangePending, rangeX0, scroll, vis;

	int      selActive, selReg;
	uint64_t selOff, selSize;
	double   selEnt;
	unsigned selHist[256];
	char     selLabel[64];

	int      hovBtn, hovRow, hovIn, hovX, hovY;
	PlRect   rb[B_COUNT], rTable, rChart;
	PlFont* fN, * fB, * fBig;
};

static const char* T(const char* k) { const char* t = translate(k); return t ? t : k; }

static PlRect mk(int l, int t, int r, int b)
{
	PlRect x; x.l = l; x.t = t; x.r = r; x.b = b; return x;
}

static int inside(const PlRect* r, int x, int y)
{
	return x >= r->l && x < r->r && y >= r->t && y < r->b;
}

static PlColor mixc(PlColor a, PlColor b, int t)
{
	unsigned ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
	unsigned br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
	if (t < 0) t = 0;
	if (t > 256) t = 256;
	return (PlColor)((((ar * (256 - t) + br * t) >> 8) << 16) |
		(((ag * (256 - t) + bg * t) >> 8) << 8) |
		((ab * (256 - t) + bb * t) >> 8));
}

static PlColor ev_col(double e)
{
	if (e <= 4.5) return COL_LOW;
	if (e < EV_T_PACKED)
		return mixc(COL_LOW, COL_MID, (int)((e - 4.5) / (EV_T_PACKED - 4.5) * 256));
	if (e < EV_T_HIGH)
		return mixc(COL_MID, COL_HIGH, (int)((e - EV_T_PACKED) / (EV_T_HIGH - EV_T_PACKED) * 256));
	return COL_HIGH;
}

static int ev_lvl(double e) { return e >= EV_T_HIGH ? 2 : (e >= EV_T_PACKED ? 1 : 0); }

static const char* ev_status(double e)
{
	int l = ev_lvl(e);
	return l == 2 ? T("encrypted or compressed") : l == 1 ? T("packed") : T("not packed");
}

static void txt(PlCanvas* c, PlFont* f, int l, int t, int r, int b,
	const char* s, PlColor col, int fl)
{
	pl_text(c, f, mk(l, t, r, b), s, col, fl);
}

static unsigned le16(const unsigned char* p) { return p[0] | (p[1] << 8); }
static unsigned le32(const unsigned char* p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24);
}


static double ev_calc(const unsigned* h, uint64_t total)
{
	double e = 0.0;
	int i;
	if (!total) return 0.0;
	for (i = 0; i < 256; ++i)
		if (h[i]) {
			double p = (double)h[i] / (double)total;
			e -= p * log(p) / log(2.0);
		}
	return e;
}

static double ev_range(EntropyView* d, uint64_t off, uint64_t size, unsigned* hist, int maxch)
{
	unsigned local[256];
	uint64_t nch, step, k, total = 0;

	memset(local, 0, sizeof(local));
	if (size && d->rd) {
		nch = (size + EV_CHUNK - 1) / EV_CHUNK;
		step = 1;
		if (maxch > 0 && nch > (uint64_t)maxch) step = nch / (uint64_t)maxch;
		for (k = 0; k < nch; k += step) {
			uint64_t rel = k * EV_CHUNK, len = size - rel;
			size_t i;
			if (len > EV_CHUNK) len = EV_CHUNK;
			if (!d->rd(d->rdUser, off + rel, d->buf, (size_t)len)) continue;
			for (i = 0; i < (size_t)len; ++i) ++local[d->buf[i]];
			total += len;
		}
	}
	if (hist) memcpy(hist, local, sizeof(local));
	return ev_calc(local, total);
}

static void ev_add(EntropyView* d, uint64_t off, uint64_t size, const char* name)
{
	Region* r;
	if (d->nreg >= EV_REG_MAX || !size || off >= d->fsize) return;
	if (off + size > d->fsize) size = d->fsize - off;
	r = &d->reg[d->nreg++];
	r->off = off; r->size = size;
	r->ent = ev_range(d, off, size, NULL, 256);
	snprintf(r->name, sizeof(r->name), "%s", name);
}

static void ev_regions(EntropyView* d)
{
	unsigned char hdr[4096];
	size_t got = d->fsize < sizeof(hdr) ? (size_t)d->fsize : sizeof(hdr);

	d->nreg = 0;
	snprintf(d->ftype, sizeof(d->ftype), "Raw");
	if (!d->rd(d->rdUser, 0, hdr, got)) got = 0;

	if (got >= 0x40 && hdr[0] == 'M' && hdr[1] == 'Z') {
		unsigned pe = le32(hdr + 0x3C);
		if ((uint64_t)pe + 0x18 <= got && memcmp(hdr + pe, "PE\0\0", 4) == 0) {
			unsigned nsec = le16(hdr + pe + 6), osz = le16(hdr + pe + 20);
			unsigned magic = le16(hdr + pe + 24), i;
			uint64_t st = (uint64_t)pe + 24 + osz;
			uint64_t avail = got > st ? (got - st) / 40 : 0;
			uint64_t minp = d->fsize, maxe = 0;

			if (nsec > avail) nsec = (unsigned)avail;
			if (nsec > EV_REG_MAX - 2) nsec = EV_REG_MAX - 2;
			snprintf(d->ftype, sizeof(d->ftype), "%s", magic == 0x20B ? "PE64" : "PE32");

			for (i = 0; i < nsec; ++i) {
				unsigned rawsz = le32(hdr + st + i * 40 + 16);
				unsigned rawp = le32(hdr + st + i * 40 + 20);
				if (!rawsz) continue;
				if (rawp < minp) minp = rawp;
				if ((uint64_t)rawp + rawsz > maxe) maxe = (uint64_t)rawp + rawsz;
			}
			if (minp > 0 && minp < d->fsize) ev_add(d, 0, minp, T("Header"));
			for (i = 0; i < nsec; ++i) {
				unsigned rawsz = le32(hdr + st + i * 40 + 16);
				unsigned rawp = le32(hdr + st + i * 40 + 20);
				char nm[9], full[64];
				int k;
				if (!rawsz || rawp >= d->fsize) continue;
				for (k = 0; k < 8; ++k) {
					unsigned char ch = hdr[st + i * 40 + k];
					if (ch < 32 || ch > 126) break;
					nm[k] = (char)ch;
				}
				nm[k] = 0;
				snprintf(full, sizeof(full), "Section (%u) [\"%s\"]", i + 1, nm);
				ev_add(d, rawp, rawsz, full);
			}
			if (maxe && maxe < d->fsize) ev_add(d, maxe, d->fsize - maxe, T("Overlay"));
		}
	}

	if (d->nreg == 0) {                 /* not a PE: equal blocks */
		int nb = d->fsize < 4096 ? 1 : 16, i;
		uint64_t bs = (d->fsize + nb - 1) / nb;
		for (i = 0; i < nb; ++i) {
			char nm[48];
			if ((uint64_t)i * bs >= d->fsize) break;
			snprintf(nm, sizeof(nm), "Block (%d)", i + 1);
			ev_add(d, (uint64_t)i * bs, bs, nm);
		}
	}
}

static EntropyView* g_sort;

static int ev_cmp(const void* a, const void* b)
{
	const Region* x = &g_sort->reg[*(const int*)a];
	const Region* y = &g_sort->reg[*(const int*)b];
	int c = 0;
	switch (g_sort->sortCol) {
	case 1: c = (x->size > y->size) - (x->size < y->size); break;
	case 2: c = (x->ent > y->ent) - (x->ent < y->ent); break;
	case 3: c = ev_lvl(x->ent) - ev_lvl(y->ent); break;
	case 4: c = strcmp(x->name, y->name); break;
	default: break;
	}
	if (!c) c = (x->off > y->off) - (x->off < y->off);
	return g_sort->sortAsc ? c : -c;
}

static void ev_sort(EntropyView* d)
{
	int i;
	for (i = 0; i < d->nreg; ++i) d->order[i] = i;
	g_sort = d;
	qsort(d->order, (size_t)d->nreg, sizeof(int), ev_cmp);
}

static void ev_load(EntropyView* d)
{
	int i, n;
	uint64_t bs;

	d->dirty = 0; d->loaded = 0; d->nreg = 0; d->ncurve = 0;
	d->selActive = 0; d->selReg = -1; d->scroll = 0; d->rangePending = 0;
	if (!d->rd || d->fsize == 0) return;
	if (!d->rd(d->rdUser, 0, d->buf, 1)) return;

	d->overall = ev_range(d, 0, d->fsize, d->allHist, 512);

	n = (int)(d->fsize / 256);
	if (n < 1) n = 1;
	if (n > EV_CURVE_MAX) n = EV_CURVE_MAX;
	bs = (d->fsize + n - 1) / n;
	n = (int)((d->fsize + bs - 1) / bs);
	d->cbs = bs; d->ncurve = n;
	d->cmin = 9.0; d->cmax = 0.0;
	for (i = 0; i < n; ++i) {
		uint64_t o = (uint64_t)i * bs, l = d->fsize - o;
		if (l > bs) l = bs;
		d->curve[i] = ev_range(d, o, l, NULL, 8);
		if (d->curve[i] < d->cmin) d->cmin = d->curve[i];
		if (d->curve[i] > d->cmax) d->cmax = d->curve[i];
	}
	ev_regions(d);
	ev_sort(d);
	d->loaded = 1;
}

static void ev_select(EntropyView* d, uint64_t off, uint64_t size, const char* label, int reg)
{
	if (off >= d->fsize || !size) return;
	if (off + size > d->fsize) size = d->fsize - off;
	d->selActive = 1; d->selOff = off; d->selSize = size; d->selReg = reg;
	d->selEnt = ev_range(d, off, size, d->selHist, 256);
	snprintf(d->selLabel, sizeof(d->selLabel), "%s", label);
}

static void ev_ensure_visible(EntropyView* d, int idx)
{
	int pos;
	for (pos = 0; pos < d->nreg; ++pos) if (d->order[pos] == idx) break;
	if (pos >= d->nreg) return;
	if (pos < d->scroll) d->scroll = pos;
	else if (pos >= d->scroll + d->vis) d->scroll = pos - d->vis + 1;
	if (d->scroll < 0) d->scroll = 0;
}

static void ev_select_region(EntropyView* d, int idx)
{
	ev_select(d, d->reg[idx].off, d->reg[idx].size, d->reg[idx].name, idx);
	ev_ensure_visible(d, idx);
}

static int ev_find_region(const EntropyView* d, uint64_t off)
{
	int i;
	for (i = 0; i < d->nreg; ++i)
		if (off >= d->reg[i].off && off < d->reg[i].off + d->reg[i].size) return i;
	return -1;
}

static void ev_layout(EntropyView* d, int w, int h)
{
	int avail, tableH, maxH;
	int L = EV_PAD, R = w - EV_PAD, T0 = EV_PAD;

	d->rb[B_ENT] = mk(L, T0 + 62, L + 84, T0 + 90);
	d->rb[B_BYTES] = mk(L + 90, T0 + 62, L + 174, T0 + 90);
	d->rb[B_GRID] = mk(L + 188, T0 + 62, L + 252, T0 + 90);
	d->rb[B_RANGE] = mk(L + 258, T0 + 62, L + 338, T0 + 90);
	d->rb[B_RELOAD] = mk(R - 80, T0 + 62, R, T0 + 90);
	d->rb[B_COPY] = mk(R - 186, T0 + 62, R - 86, T0 + 90);
	d->rb[B_CLEAR] = mk(R - 262, T0 + 62, R - 192, T0 + 90);

	avail = h - (T0 + 100) - 28 - EV_PAD;
	tableH = HDR_H + ROW_H * d->nreg + 2;
	maxH = avail * 40 / 100;
	if (tableH > maxH) tableH = maxH;
	if (tableH < HDR_H + ROW_H * 3 + 2) tableH = HDR_H + ROW_H * 3 + 2;
	d->rTable = mk(L, T0 + 100, R, T0 + 100 + tableH);
	d->vis = (tableH - HDR_H - 2) / ROW_H;
	if (d->vis < 1) d->vis = 1;
	if (d->scroll > d->nreg - d->vis) d->scroll = d->nreg - d->vis;
	if (d->scroll < 0) d->scroll = 0;

	d->rChart = mk(L, T0 + 100 + tableH + 8, R, h - 28 - EV_PAD);
	if (d->rChart.b < d->rChart.t + 100) d->rChart.b = d->rChart.t + 100;
}

static void ev_plot(const EntropyView* d, PlRect* p)
{
	*p = mk(d->rChart.l + 44, d->rChart.t + 10, d->rChart.r - 14, d->rChart.b - 26);
}

static uint64_t ev_x2off(const EntropyView* d, const PlRect* p, int x)
{
	int W = p->r - p->l;
	double f;
	if (W <= 0) return 0;
	f = (double)(x - p->l) / W;
	if (f < 0) f = 0;
	if (f > 1) f = 1;
	return (uint64_t)(f * (double)d->fsize);
}

static int ev_btn_visible(const EntropyView* d, int b)
{
	if (b == B_CLEAR) return d->selActive || d->rangePending;
	return 1;
}

static int ev_hit_btn(const EntropyView* d, int x, int y)
{
	int i;
	for (i = 0; i < B_COUNT; ++i)
		if (ev_btn_visible(d, i) && inside(&d->rb[i], x, y)) return i;
	return -1;
}

static void ev_button(EntropyView* d, PlCanvas* c, int b, const char* label, int active)
{
	PlRect r = d->rb[b];
	int hov = d->hovBtn == b;
	PlColor fill = active ? CLR_SIDEBAR_SEL : (hov ? CLR_SIDEBAR_HOVER : CLR_SEARCH_BG);
	pl_round_rect(c, r, fill, active ? CLR_ACCENT : CLR_PANEL_BORDER, 8);
	txt(c, d->fN, r.l, r.t, r.r, r.b, label,
		(active || hov) ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY, EV_C);
}

static void ev_paint_table(EntropyView* d, PlCanvas* c)
{
	PlRect t = d->rTable;
	static const char* heads[5] = { "Offset", "Size", "Entropy", "Status", "Name" };
	PlColor cHdr = mixc(CLR_SEARCH_BG, CLR_TEXT_PRIMARY, 14);
	char s[128];
	int i, r;

	pl_round_rect(c, t, CLR_SEARCH_BG, CLR_PANEL_BORDER, 6);
	pl_fill(c, t.l + 1, t.t + 1, t.r - 1, t.t + HDR_H, cHdr);
	for (i = 0; i < 5; ++i) {
		snprintf(s, sizeof(s), "%s%s", T(heads[i]),
			d->sortCol == i ? (d->sortAsc ? "  \xE2\x96\xB2" : "  \xE2\x96\xBC") : "");
		txt(c, d->fB, t.l + CX[i], t.t + 1, t.r - 8, t.t + HDR_H, s, CLR_TEXT_PRIMARY, EV_L);
	}

	for (r = 0; r < d->vis && d->scroll + r < d->nreg; ++r) {
		int idx = d->order[d->scroll + r];
		const Region* g = &d->reg[idx];
		int y = t.t + HDR_H + r * ROW_H;
		int sel = d->selActive && d->selReg == idx;
		PlColor bg = sel ? CLR_SIDEBAR_SEL : (d->hovRow == r ? CLR_SIDEBAR_HOVER : CLR_SEARCH_BG);
		PlColor col = ev_col(g->ent);

		pl_fill(c, t.l + 1, y, t.r - 1, y + ROW_H, bg);
		if (sel) pl_fill(c, t.l + 1, y, t.l + 4, y + ROW_H, CLR_ACCENT);

		snprintf(s, sizeof(s), "%08llX", (unsigned long long)g->off);
		txt(c, d->fN, t.l + CX[0], y, t.l + CX[1] - 4, y + ROW_H, s, CLR_TEXT_PRIMARY, EV_L);
		snprintf(s, sizeof(s), "%08llX", (unsigned long long)g->size);
		txt(c, d->fN, t.l + CX[1], y, t.l + CX[2] - 4, y + ROW_H, s, CLR_TEXT_PRIMARY, EV_L);
		snprintf(s, sizeof(s), "%.5f", g->ent);
		txt(c, d->fN, t.l + CX[2], y, t.l + CX[2] + 70, y + ROW_H, s, CLR_TEXT_PRIMARY, EV_L);

		pl_fill(c, t.l + CX[2] + 78, y + 8, t.l + CX[2] + 138, y + 14, mixc(bg, CLR_TEXT_PRIMARY, 24));
		pl_fill(c, t.l + CX[2] + 78, y + 8,
			t.l + CX[2] + 78 + (int)(g->ent / 8.0 * 60.0), y + 14, col);

		txt(c, d->fN, t.l + CX[3], y, t.l + CX[4] - 4, y + ROW_H, ev_status(g->ent), col, EV_L);
		txt(c, d->fN, t.l + CX[4], y, t.r - 14, y + ROW_H, g->name, CLR_TEXT_PRIMARY, EV_L);
	}

	if (d->nreg > d->vis) {              
		int top = t.t + HDR_H + 2;
		int th = t.b - 2 - top;
		int len = th * d->vis / d->nreg;
		int pos;
		if (len < 24) len = 24;
		if (len > th) len = th;
		pos = (th - len) * d->scroll / (d->nreg - d->vis);
		pl_round_rect(c, mk(t.r - 6, top + pos, t.r - 3, top + pos + len),
			CLR_BTN_GHOST_BD, CLR_BTN_GHOST_BD, 1);
	}
}

static void ev_chart_entropy(EntropyView* d, PlCanvas* c, const PlRect* p)
{
	PlColor bgc = CLR_SEARCH_BG, cT = CLR_TEXT_PRIMARY, cS = CLR_TEXT_SECONDARY;
	PlColor cGrid = mixc(bgc, cT, 28), cAxis = mixc(bgc, cT, 90);
	int W = p->r - p->l, H = p->b - p->t, n = d->ncurve;
	int x, i, py = 0, hasBand = 0, bx0 = 0, bx1 = 0;
	char s[160];

	if (W < 4 || H < 4 || n < 1) return;

	if (d->rangePending && d->hovIn) {
		bx0 = d->rangeX0 < d->hovX ? d->rangeX0 : d->hovX;
		bx1 = d->rangeX0 < d->hovX ? d->hovX : d->rangeX0;
		hasBand = bx1 > bx0;
	}
	else if (d->selActive) {
		bx0 = p->l + (int)((double)d->selOff / (double)d->fsize * W);
		bx1 = p->l + (int)((double)(d->selOff + d->selSize) / (double)d->fsize * W);
		if (bx1 <= bx0) bx1 = bx0 + 1;
		hasBand = 1;
	}

	for (x = 0; x < W; ++x) {
		double t = ((x + 0.5) / W) * n - 0.5, f, v;
		int i0 = (int)floor(t), i1, y, px = p->l + x, in;
		PlColor col;
		f = t - i0; i1 = i0 + 1;
		if (i0 < 0) i0 = 0;
		if (i0 >= n) i0 = n - 1;
		if (i1 >= n) i1 = n - 1;
		v = d->curve[i0] * (1.0 - f) + d->curve[i1] * f;
		y = p->b - (int)(v / 8.0 * H + 0.5);
		col = ev_col(v);
		in = hasBand ? (px >= bx0 && px < bx1) : 1;
		pl_fill(c, px, y, px + 1, p->b, mixc(bgc, col, hasBand ? (in ? 190 : 70) : 120));
		if (x == 0) py = y;
		pl_fill(c, px, y < py ? y : py, px + 1, (y > py ? y : py) + 1, in ? col : mixc(bgc, col, 170));
		py = y;
	}

	for (i = 1; i < d->nreg; ++i) {      /* region dividers */
		int rx = p->l + (int)((double)d->reg[i].off / (double)d->fsize * W);
		pl_fill(c, rx, p->t, rx + 1, p->b, mixc(bgc, cT, 45));
	}

	for (i = 0; i <= 8; ++i) {           /* y labels + optional grid */
		int y = p->b - i * H / 8;
		if (d->grid && i > 0) {
			int gx;
			for (gx = p->l; gx < p->r; gx += 2) pl_fill(c, gx, y, gx + 1, y + 1, cGrid);
		}
		snprintf(s, sizeof(s), "%d", i);
		txt(c, d->fN, d->rChart.l + 4, y - 9, p->l - 8, y + 9, s, cS, EV_R);
	}
	if (d->grid)
		for (i = 1; i < 8; ++i) {
			int gx = p->l + i * W / 8, gy;
			for (gy = p->t; gy < p->b; gy += 2) pl_fill(c, gx, gy, gx + 1, gy + 1, cGrid);
		}

	{                                    /* threshold lines */
		int y1 = p->b - (int)(EV_T_PACKED / 8.0 * H), y2 = p->b - (int)(EV_T_HIGH / 8.0 * H), gx;
		for (gx = p->l; gx < p->r; gx += 6) {
			pl_fill(c, gx, y1, gx + 3, y1 + 1, COL_MID);
			pl_fill(c, gx, y2, gx + 3, y2 + 1, COL_HIGH);
		}
	}

	pl_fill(c, p->l - 1, p->t, p->l, p->b + 1, cAxis);
	pl_fill(c, p->l - 1, p->b, p->r, p->b + 1, cAxis);
	for (i = 0; i <= 4; ++i) {
		int tx = p->l + i * W / 4;
		int l = i == 0 ? tx : (i == 4 ? tx - 120 : tx - 60);
		pl_fill(c, tx, p->b + 1, tx + 1, p->b + 5, cAxis);
		snprintf(s, sizeof(s), "0x%llX", (unsigned long long)((double)d->fsize * i / 4.0));
		txt(c, d->fN, l, p->b + 5, l + 120, p->b + 22, s, cS, i == 0 ? EV_L : (i == 4 ? EV_R : EV_C));
	}

	if (d->hovIn) {                      /* crosshair + tooltip */
		uint64_t off = ev_x2off(d, p, d->hovX);
		int bi = (int)(off / d->cbs), ri = ev_find_region(d, off), tx = d->hovX + 14, ty = p->t + 6;
		if (bi >= n) bi = n - 1;
		pl_fill(c, d->hovX, p->t, d->hovX + 1, p->b, CLR_ACCENT);
		if (tx + 250 > p->r) tx = d->hovX - 264;
		pl_round_rect(c, mk(tx, ty, tx + 250, ty + 68), CLR_BG, CLR_ACCENT, 6);
		snprintf(s, sizeof(s), "%s: 0x%llX", T("Offset"), (unsigned long long)off);
		txt(c, d->fN, tx + 10, ty + 4, tx + 240, ty + 22, s, cT, EV_L);
		snprintf(s, sizeof(s), "%s: %.5f  (%s)", T("Entropy"), d->curve[bi], ev_status(d->curve[bi]));
		txt(c, d->fN, tx + 10, ty + 22, tx + 240, ty + 40, s, ev_col(d->curve[bi]), EV_L);
		txt(c, d->fN, tx + 10, ty + 42, tx + 240, ty + 62,
			ri >= 0 ? d->reg[ri].name : T("(no region)"), cS, EV_L);
	}
}

static void ev_chart_bytes(EntropyView* d, PlCanvas* c, const PlRect* p)
{
	PlColor bgc = CLR_SEARCH_BG, cT = CLR_TEXT_PRIMARY, cS = CLR_TEXT_SECONDARY;
	const unsigned* hs = d->selActive ? d->selHist : d->allHist;
	int W = p->r - p->l, H = p->b - p->t, b, i, hov = -1;
	unsigned mx = 0, uniq = 0;
	uint64_t total = 0, pr = 0;
	double lmx;
	char s[160];

	if (W < 8 || H < 8) return;
	for (b = 0; b < 256; ++b) {
		if (hs[b] > mx) mx = hs[b];
		if (hs[b]) ++uniq;
		total += hs[b];
		if (b >= 0x20 && b < 0x7F) pr += hs[b];
	}
	lmx = log(1.0 + (double)mx);
	if (d->hovIn) hov = (d->hovX - p->l) * 256 / W;

	for (b = 0; b < 256; ++b) {
		int x0 = p->l + b * W / 256, x1 = p->l + (b + 1) * W / 256;
		int h = lmx > 0 ? (int)(log(1.0 + hs[b]) / lmx * H) : 0;
		PlColor col;
		if (b == 0) col = 0x5C9DFF;
		else if (b == 0xFF) col = COL_HIGH;
		else if (b >= 0x20 && b < 0x7F) col = COL_LOW;
		else if (b < 0x20 || b == 0x7F) col = 0x9E9EB8;
		else col = COL_MID;
		if (b == hov) col = mixc(col, 0xFFFFFF, 90);
		if (hs[b] && h < 2) h = 2;
		pl_fill(c, x0, p->b - h, x1 > x0 + 2 ? x1 - 1 : x1, p->b, col);
	}

	for (i = 0; i <= 4; ++i) {
		int tx = p->l + i * W / 4, l = i == 0 ? tx : (i == 4 ? tx - 60 : tx - 30);
		snprintf(s, sizeof(s), "0x%02X", i == 4 ? 255 : i * 64);
		txt(c, d->fN, l, p->b + 5, l + 60, p->b + 22, s, cS, i == 0 ? EV_L : (i == 4 ? EV_R : EV_C));
	}
	pl_fill(c, p->l - 1, p->b, p->r, p->b + 1, mixc(bgc, cT, 90));

	if (total) {
		snprintf(s, sizeof(s), "%s %u/256   %s %.1f%%   %s %.1f%%   (%s)",
			T("Unique"), uniq, T("Zero"), hs[0] * 100.0 / (double)total,
			T("Printable"), (double)pr * 100.0 / (double)total, T("log scale"));
		txt(c, d->fN, p->l + 8, p->t, p->r - 8, p->t + 20, s, cS, EV_L);
	}
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
	EntropyView* d = (EntropyView*)u;
	PlColor oc;
	char s[512];
	PlRect frame;
	int P = EV_PAD;

	if (w <= 0 || h <= 0) return;
	if (d->dirty) ev_load(d);
	ev_layout(d, w, h);
	pl_fill(c, 0, 0, w, h, CLR_BG);
	frame = mk(0, 0, w - 1, h - 1);
	pl_round_rect(c, frame, CLR_PANEL_BODY, CLR_PANEL_BORDER, 12);

	if (!d->loaded) {
		txt(c, d->fN, P, P + 8, w - P, P + 30,
			d->fsize ? T("Cannot read the file.") : T("Scan a file first to view its entropy."),
			CLR_TEXT_SECONDARY, EV_L);
		return;
	}

	/* total entropy card */
	oc = ev_col(d->overall);
	txt(c, d->fN, P, P, P + 170, P + 18, T("TOTAL ENTROPY"), CLR_TEXT_SECONDARY, EV_L);
	snprintf(s, sizeof(s), "%.5f", d->overall);
	txt(c, d->fBig, P, P + 18, P + 170, P + 52, s, oc, EV_L);
	{
		int bx0 = P + 182, bx1 = w - P, x;
		int o = P;
		pl_round_rect(c, mk(bx0, o + 8, bx1, o + 28), CLR_SEARCH_BG, CLR_PANEL_BORDER, 6);
		for (x = bx0 + 2; x < bx1 - 2; ++x) {
			double f = (double)(x - bx0 - 2) / (double)(bx1 - bx0 - 4);
			if (f * 8.0 > d->overall) break;
			pl_fill(c, x, o + 11, x + 1, o + 25, ev_col(f * 8.0));
		}
		pl_fill(c, bx0 + (int)(EV_T_PACKED / 8.0 * (bx1 - bx0)), o + 6, bx0 + (int)(EV_T_PACKED / 8.0 * (bx1 - bx0)) + 1, o + 30, CLR_TEXT_SECONDARY);
		pl_fill(c, bx0 + (int)(EV_T_HIGH / 8.0 * (bx1 - bx0)), o + 6, bx0 + (int)(EV_T_HIGH / 8.0 * (bx1 - bx0)) + 1, o + 30, CLR_TEXT_SECONDARY);
		snprintf(s, sizeof(s), "%s (%d%%)", ev_status(d->overall), (int)(d->overall / 8.0 * 100.0));
		txt(c, d->fB, bx0, o + 32, bx0 + 320, o + 52, s, oc, EV_L);
		snprintf(s, sizeof(s), "%s   |   %s %.2f   |   %s %.2f", d->ftype, T("Low"), d->cmin, T("Peak"), d->cmax);
		txt(c, d->fN, bx1 - 320, o + 32, bx1, o + 52, s, CLR_TEXT_SECONDARY, EV_R);
	}

	/* toolbar */
	ev_button(d, c, B_ENT, T("Entropy"), d->view == 0);
	ev_button(d, c, B_BYTES, T("Bytes"), d->view == 1);
	{
		PlRect g = d->rb[B_GRID], cb = mk(g.l, g.t + 7, g.l + 14, g.t + 21);
		pl_round_rect(c, cb, d->grid ? CLR_ACCENT : CLR_SEARCH_BG,
			d->hovBtn == B_GRID ? CLR_TEXT_PRIMARY : CLR_PANEL_BORDER, 4);
		if (d->grid) {
			pl_fill(c, cb.l + 3, cb.t + 7, cb.l + 6, cb.t + 10, CLR_TEXT_PRIMARY);
			pl_fill(c, cb.l + 5, cb.t + 9, cb.l + 11, cb.t + 11, CLR_TEXT_PRIMARY);
			pl_fill(c, cb.l + 9, cb.t + 4, cb.l + 11, cb.t + 9, CLR_TEXT_PRIMARY);
		}
		txt(c, d->fN, g.l + 22, g.t, g.r, g.b, T("Grid"), CLR_TEXT_SECONDARY, EV_L);
	}
	ev_button(d, c, B_RANGE, T("Range"), d->rangeMode);
	if (ev_btn_visible(d, B_CLEAR)) ev_button(d, c, B_CLEAR, T("Clear"), 0);
	ev_button(d, c, B_COPY, T("Copy all"), 0);
	ev_button(d, c, B_RELOAD, T("Reload"), 0);

	ev_paint_table(d, c);

	{
		PlRect p;
		pl_round_rect(c, d->rChart, CLR_SEARCH_BG, CLR_PANEL_BORDER, 6);
		ev_plot(d, &p);
		if (d->view == 0) ev_chart_entropy(d, c, &p);
		else ev_chart_bytes(d, c, &p);
	}

	/* footer */
	{
		PlRect p;
		ev_plot(d, &p);
		if (d->rangePending)
			snprintf(s, sizeof(s), "%s", T("Range: click the end point on the chart."));
		else if (d->view == 1 && d->hovIn) {
			const unsigned* hs = d->selActive ? d->selHist : d->allHist;
			uint64_t tot = 0;
			int k, b = (d->hovX - p.l) * 256 / (p.r - p.l);
			if (b < 0) b = 0;
			if (b > 255) b = 255;
			for (k = 0; k < 256; ++k) tot += hs[k];
			snprintf(s, sizeof(s), "%s 0x%02X  |  %s %u  (%.3f%%)", T("Byte"), b, T("count"),
				hs[b], tot ? hs[b] * 100.0 / (double)tot : 0.0);
		}
		else if (d->selActive)
			snprintf(s, sizeof(s), "%s: 0x%llX - 0x%llX  |  %s %llu B  |  %s %.5f (%s)  |  %s",
				T("Selection"), (unsigned long long)d->selOff,
				(unsigned long long)(d->selOff + d->selSize - 1),
				T("Size"), (unsigned long long)d->selSize,
				T("Entropy"), d->selEnt, ev_status(d->selEnt), d->selLabel);
		else
			snprintf(s, sizeof(s), "%s  |  %s %llu B  |  %d %s  |  %s", T("Whole file"),
				T("Block size"), (unsigned long long)d->cbs, d->ncurve, T("blocks"),
				T("Click a region, or use Range for a custom span."));
		txt(c, d->fN, EV_PAD, h - EV_PAD - 24, w - EV_PAD, h - EV_PAD - 2, s, CLR_TEXT_SECONDARY, EV_L);
	}
}

static void ev_copy(EntropyView* d)
{
	size_t cap = (size_t)(d->nreg + 8) * 256, used = 0;
	char* b;
	int i;
	if (!d->loaded || !d->copy) return;
	b = (char*)malloc(cap);
	if (!b) return;
#define EV_APP(...) do { int n_ = snprintf(b + used, cap - used, __VA_ARGS__); \
	if (n_ > 0 && (size_t)n_ < cap - used) used += (size_t)n_; } while (0)
	EV_APP("%s\r\n%s: %llu bytes\r\n%s: %.5f (%s)\r\n\r\n", d->name, T("Size"),
		(unsigned long long)d->fsize, T("Total entropy"), d->overall, ev_status(d->overall));
	EV_APP("Offset\tSize\tEntropy\tStatus\tName\r\n");
	for (i = 0; i < d->nreg; ++i) {
		const Region* g = &d->reg[d->order[i]];
		EV_APP("%08llX\t%08llX\t%.5f\t%s\t%s\r\n", (unsigned long long)g->off,
			(unsigned long long)g->size, g->ent, ev_status(g->ent), g->name);
	}
#undef EV_APP
	d->copy(d->copyUser, b);
	free(b);
}

static void ev_action(EntropyView* d, int b)
{
	switch (b) {
	case B_ENT:    d->view = 0; break;
	case B_BYTES:  d->view = 1; break;
	case B_GRID:   d->grid = !d->grid; break;
	case B_RANGE:  d->rangeMode = !d->rangeMode; d->rangePending = 0; break;
	case B_CLEAR:  d->selActive = 0; d->selReg = -1; d->rangePending = 0; break;
	case B_RELOAD: d->dirty = 1; break;
	case B_COPY:   ev_copy(d); break;
	}
}

static void on_mouse_move(void* u, int x, int y)
{
	EntropyView* d = (EntropyView*)u;
	PlRect p;
	if (!d->loaded) return;
	d->hovX = x; d->hovY = y;
	d->hovBtn = ev_hit_btn(d, x, y);
	d->hovRow = -1;
	if (inside(&d->rTable, x, y) && y >= d->rTable.t + HDR_H) {
		int row = (y - d->rTable.t - HDR_H) / ROW_H;
		if (row < d->vis && d->scroll + row < d->nreg) d->hovRow = row;
	}
	ev_plot(d, &p);
	d->hovIn = inside(&p, x, y);
	pl_invalidate(d->w, NULL);
}

static void on_mouse_leave(void* u)
{
	EntropyView* d = (EntropyView*)u;
	d->hovBtn = -1; d->hovRow = -1; d->hovIn = 0;
	pl_invalidate(d->w, NULL);
}

static void on_mouse_down(void* u, int x, int y)
{
	EntropyView* d = (EntropyView*)u;
	PlRect p;
	int b;
	if (!d->loaded) return;

	b = ev_hit_btn(d, x, y);
	if (b >= 0) { ev_action(d, b); pl_invalidate(d->w, NULL); return; }

	if (inside(&d->rTable, x, y)) {
		if (y < d->rTable.t + HDR_H) {                      /* sort */
			int rx = x - d->rTable.l, col = 0, k;
			for (k = 4; k >= 0; --k) if (rx >= CX[k] - 6) { col = k; break; }
			if (d->sortCol == col) d->sortAsc = !d->sortAsc;
			else { d->sortCol = col; d->sortAsc = 1; }
			ev_sort(d);
		}
		else if (d->nreg > d->vis && x >= d->rTable.r - 14) { /* scrollbar track */
			int th = d->rTable.b - d->rTable.t - HDR_H - 4;
			d->scroll = (y - d->rTable.t - HDR_H) * d->nreg / (th > 0 ? th : 1) - d->vis / 2;
			if (d->scroll > d->nreg - d->vis) d->scroll = d->nreg - d->vis;
			if (d->scroll < 0) d->scroll = 0;
		}
		else {
			int row = (y - d->rTable.t - HDR_H) / ROW_H;
			if (row < d->vis && d->scroll + row < d->nreg)
				ev_select_region(d, d->order[d->scroll + row]);
		}
		pl_invalidate(d->w, NULL);
		return;
	}

	ev_plot(d, &p);
	if (d->view == 0 && inside(&p, x, y)) {
		if (d->rangeMode && !d->rangePending) {
			d->rangePending = 1; d->rangeX0 = x;
		}
		else if (d->rangeMode && d->rangePending && abs(x - d->rangeX0) >= 4) {
			int lo = x < d->rangeX0 ? x : d->rangeX0, hi = x < d->rangeX0 ? d->rangeX0 : x;
			uint64_t o0 = ev_x2off(d, &p, lo), o1 = ev_x2off(d, &p, hi);
			if (o1 <= o0) o1 = o0 + 1;
			ev_select(d, o0, o1 - o0, T("Custom range"), -1);
			d->rangePending = 0;
		}
		else {
			int ri = ev_find_region(d, ev_x2off(d, &p, x));
			d->rangePending = 0;
			if (ri >= 0) ev_select_region(d, ri);
			else d->selActive = 0;
		}
		pl_invalidate(d->w, NULL);
	}
}

static void on_resize(void* u, int w, int h)
{
	EntropyView* d = (EntropyView*)u;
	(void)w; (void)h;
	pl_invalidate(d->w, NULL);
}

static void on_destroy(void* u)
{
	EntropyView* d = (EntropyView*)u;
	pl_font_destroy(d->fN);
	pl_font_destroy(d->fB);
	pl_font_destroy(d->fBig);
	free(d->buf);
	free(d);
}

static void on_wheel(void* u, int x, int y, int delta, int mods)
{
	EntropyView* d = (EntropyView*)u;
	(void)x; (void)y; (void)mods;
	if (!d->loaded || !delta) return;
	entropyview_scroll(d, delta > 0 ? -3 : 3);   
}

static void on_key(void* u, int key, uint32_t ch, int mods)
{
	EntropyView* d = (EntropyView*)u;
	if (!d->loaded) return;
	switch (key) {
	case PL_KEY_UP:   entropyview_scroll(d, -1); break;
	case PL_KEY_DOWN: entropyview_scroll(d, 1); break;
	case PL_KEY_PGUP: entropyview_scroll(d, -d->vis); break;
	case PL_KEY_PGDN: entropyview_scroll(d, d->vis); break;
	case PL_KEY_HOME: entropyview_scroll(d, -d->nreg); break;
	case PL_KEY_END:  entropyview_scroll(d, d->nreg); break;
	case PL_KEY_ESC:
		d->selActive = 0; d->selReg = -1; d->rangePending = 0;
		pl_invalidate(d->w, NULL);
		break;
	case PL_KEY_CHAR:
		if ((mods & PL_MOD_CTRL) && (ch == 'c' || ch == 'C')) ev_copy(d);
		break;
	}
}

static int on_hand(void* u, int x, int y)
{
	EntropyView* d = (EntropyView*)u;
	PlRect p;
	if (!d->loaded) return 0;
	if (ev_hit_btn(d, x, y) >= 0) return 1;
	if (inside(&d->rTable, x, y)) return 1;
	ev_plot(d, &p);
	return d->view == 0 && inside(&p, x, y);
}

static const PlWidgetOps s_ops = {
	on_paint, on_mouse_move, on_mouse_leave, on_mouse_down, on_resize,
	on_hand, on_destroy, on_key, on_wheel
};

EntropyView* entropyview_create(void* parent_native, int x, int y, int w, int h)
{
	EntropyView* d = (EntropyView*)calloc(1, sizeof(*d));
	if (!d) return NULL;
	d->buf = (unsigned char*)malloc(EV_CHUNK);
	if (!d->buf) { free(d); return NULL; }
	d->hovBtn = -1; d->hovRow = -1; d->selReg = -1; d->sortAsc = 1; d->vis = 8;
	d->fN = pl_font_create(9, 0);
	d->fB = pl_font_create(9, 1);
	d->fBig = pl_font_create(20, 1);
	d->w = pl_widget_create(parent_native, &s_ops, d, x, y, w, h);
	if (!d->w) {
		pl_font_destroy(d->fN); pl_font_destroy(d->fB); pl_font_destroy(d->fBig);
		free(d->buf); free(d);
		return NULL;
	}
	return d;
}

void entropyview_destroy(EntropyView* e) { if (e) pl_widget_destroy(e->w); }

void entropyview_set_rect(EntropyView* e, int x, int y, int w, int h)
{
	if (e) pl_widget_set_rect(e->w, x, y, w, h);
}

void entropyview_set_callbacks(EntropyView* e, EntropyCopyFn copy, void* user)
{
	if (!e) return;
	e->copy = copy; e->copyUser = user;
}

void entropyview_open(EntropyView* e, const char* utf8_path,
	EntropyReadFn rd, void* rd_user, uint64_t size)
{
	const char* last = utf8_path, * q;
	if (!e) return;
	e->rd = rd; e->rdUser = rd_user; e->fsize = size;
	for (q = utf8_path ? utf8_path : ""; *q; ++q)
		if (*q == '\\' || *q == '/') last = q + 1;
	snprintf(e->name, sizeof(e->name), "%s", last ? last : "");
	e->loaded = 0; e->dirty = 1;
	pl_invalidate(e->w, NULL);
}

void entropyview_close(EntropyView* e)
{
	if (!e) return;
	e->rd = NULL; e->fsize = 0; e->loaded = 0; e->dirty = 0;
	pl_invalidate(e->w, NULL);
}

void entropyview_scroll(EntropyView* e, int rows)
{
	if (!e || !e->loaded) return;
	e->scroll += rows;
	if (e->scroll > e->nreg - e->vis) e->scroll = e->nreg - e->vis;
	if (e->scroll < 0) e->scroll = 0;
	pl_invalidate(e->w, NULL);
}