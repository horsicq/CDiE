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
#include <time.h>
#include <stdint.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define PSEP '\\'
#else
#define PSEP '/'
#endif

#include "platform/platform.h"
#include "panels/strings.h"
#include "core/i18n.h"
#include "theme/theme.h"

#define MINLEN       4                    /* minimum string length          */
#define MAXTXT       1000                 /* max stored chars per string    */
#define SCAN_LIMIT   (64u * 1024u * 1024u)/* only the first 64 MiB scanned  */
#define MAX_STRINGS  1000000

#define HEAD   34
#define ROW_H  22
#define BAR_H  56
#define COL_OFF   14
#define COL_TYPE  130
#define COL_TXT   210

enum { T_ASCII, T_UTF16 };
enum { F_FILTER, F_LIST };

typedef struct { char s[256]; int len, caret; } Edit;
typedef struct { uint64_t off; size_t toff; int type; } Str;

struct StringsView {
  PlWidget* w;
  PlWindow* win;
  PlFont* font, * fontb;
  int W, H, focus, hov_close;
  PlRect r_filter, r_card, r_list, r_bar, r_close;
  Edit filter;

  char path[1024];
  Str* str; size_t n, cap;
  char* pool; size_t plen, pcap;
  int truncated;
  uint64_t scanned;

  int* vis; int nvis;
  int top, sel;
  int dirty;
  int caret_on;
  unsigned long blink_t;
};

typedef struct StringsView Dlg;

static PlRect mkr(int l, int t, int r, int b) { PlRect x; x.l = l; x.t = t; x.r = r; x.b = b; return x; }
static int inr(const PlRect* r, int x, int y) { return x >= r->l && x < r->r && y >= r->t && y < r->b; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static void text_right(PlCanvas* c, PlFont* f, PlRect r, const char* t, PlColor col)
{
  int w = pl_text_width(f, t);
  if (w < r.r - r.l) r.l = r.r - w;
  pl_text(c, f, r, t, col, PL_TEXT_ELLIPSIS);
}

static void redraw(Dlg* d)
{
  PlRect r = mkr(0, 0, d->W, d->H);
  pl_invalidate(d->w, &r);
}

static int u8enc(uint32_t c, char* o)
{
  if (c < 0x80) { o[0] = (char)c; return 1; }
  if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 63)); return 2; }
  if (c < 0x10000) {
    o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 63));
    o[2] = (char)(0x80 | (c & 63)); return 3;
  }
  o[0] = (char)(0xF0 | (c >> 18)); o[1] = (char)(0x80 | ((c >> 12) & 63));
  o[2] = (char)(0x80 | ((c >> 6) & 63)); o[3] = (char)(0x80 | (c & 63)); return 4;
}
static size_t u8prev(const char* b, size_t p)
{
  if (p == 0) return 0;
  --p;
  while (p > 0 && ((unsigned char)b[p] & 0xC0) == 0x80) --p;
  return p;
}
static size_t u8next(const char* b, size_t p, size_t end)
{
  if (p >= end) return end;
  ++p;
  while (p < end && ((unsigned char)b[p] & 0xC0) == 0x80) ++p;
  return p;
}

static int contains_ci(const char* s, const char* f)
{
  size_t fl = strlen(f), sl = strlen(s), i, k;
  if (!fl) return 1;
  for (i = 0; i + fl <= sl; ++i) {
    for (k = 0; k < fl; ++k) {
      int x = (unsigned char)s[i + k], y = (unsigned char)f[k];
      if (x >= 'A' && x <= 'Z') x += 32;
      if (y >= 'A' && y <= 'Z') y += 32;
      if (x != y) break;
    }
    if (k == fl) return 1;
  }
  return 0;
}

#ifdef _WIN32
static FILE* fopen_u8(const char* path, const char* mode)
{
  wchar_t* w, m[8];
  FILE* f;
  int n, i = 0;
  n = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
  if (n <= 0) return NULL;
  w = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
  if (!w) return NULL;
  MultiByteToWideChar(CP_UTF8, 0, path, -1, w, n);
  while (mode[i] && i < 7) { m[i] = (wchar_t)mode[i]; ++i; }
  m[i] = 0;
  f = _wfopen(w, m);
  free(w);
  return f;
}
#else
static FILE* fopen_u8(const char* path, const char* mode) { return fopen(path, mode); }
#endif

static void e_ins(Edit* e, uint32_t cp)
{
  char t[4];
  int n = u8enc(cp, t);
  if (e->len + n >= (int)sizeof(e->s)) return;
  memmove(e->s + e->caret + n, e->s + e->caret, (size_t)(e->len - e->caret + 1));
  memcpy(e->s + e->caret, t, (size_t)n);
  e->caret += n; e->len += n;
}

static int e_key(Edit* e, int key, uint32_t ch, int mods)
{
  if (key == PL_KEY_CHAR && !(mods & PL_MOD_CTRL) && ch >= 32) { e_ins(e, ch); return 1; }
  if (key == PL_KEY_BACK && e->caret > 0) {
    int p = (int)u8prev(e->s, (size_t)e->caret);
    memmove(e->s + p, e->s + e->caret, (size_t)(e->len - e->caret + 1));
    e->len -= e->caret - p; e->caret = p;
    return 1;
  }
  if (key == PL_KEY_DEL && e->caret < e->len) {
    int q = (int)u8next(e->s, (size_t)e->caret, (size_t)e->len);
    memmove(e->s + e->caret, e->s + q, (size_t)(e->len - q + 1));
    e->len -= q - e->caret;
    return 1;
  }
  if (key == PL_KEY_LEFT) e->caret = (int)u8prev(e->s, (size_t)e->caret);
  else if (key == PL_KEY_RIGHT) e->caret = (int)u8next(e->s, (size_t)e->caret, (size_t)e->len);
  else if (key == PL_KEY_HOME) e->caret = 0;
  else if (key == PL_KEY_END) e->caret = e->len;
  return 0;
}

static int printable(unsigned char c) { return (c >= 32 && c < 127) || c == '\t'; }

static void add_str(Dlg* d, uint64_t off, int type, const unsigned char* p, size_t n, size_t step)
{
  size_t k, take = n > MAXTXT ? MAXTXT : n;
  if (d->n >= MAX_STRINGS) { d->truncated = 1; return; }
  if (d->n == d->cap) {
    size_t nc = d->cap ? d->cap * 2 : 4096;
    Str* ns = (Str*)realloc(d->str, nc * sizeof(Str));
    if (!ns) { d->truncated = 1; return; }
    d->str = ns; d->cap = nc;
  }
  if (d->plen + take + 1 > d->pcap) {
    size_t nc = d->pcap ? d->pcap : 65536;
    char* np;
    while (nc < d->plen + take + 1) nc *= 2;
    np = (char*)realloc(d->pool, nc);
    if (!np) { d->truncated = 1; return; }
    d->pool = np; d->pcap = nc;
  }
  d->str[d->n].off = off;
  d->str[d->n].type = type;
  d->str[d->n].toff = d->plen;
  ++d->n;
  for (k = 0; k < take; ++k) {
    char ch = (char)p[k * step];
    d->pool[d->plen++] = ch == '\t' ? ' ' : ch;
  }
  d->pool[d->plen++] = 0;
}

static int str_cmp(const void* a, const void* b)
{
  const Str* x = (const Str*)a, * y = (const Str*)b;
  if (x->off != y->off) return x->off < y->off ? -1 : 1;
  return x->type - y->type;
}

static void extract(Dlg* d, const unsigned char* b, size_t n)
{
  size_t i = 0, j;

  while (i < n) {
    if (printable(b[i])) {
      j = i;
      while (j < n && printable(b[j])) ++j;
      if (j - i >= MINLEN) add_str(d, i, T_ASCII, b + i, j - i, 1);
      i = j;
    }
    else ++i;
  }

  i = 0;
  while (i + 1 < n) {
    if (printable(b[i]) && b[i + 1] == 0) {
      j = i;
      while (j + 1 < n && printable(b[j]) && b[j + 1] == 0) j += 2;
      if ((j - i) / 2 >= MINLEN) add_str(d, i, T_UTF16, b + i, (j - i) / 2, 2);
      i = j;
    }
    else ++i;
  }

  if (d->n > 1) qsort(d->str, d->n, sizeof(Str), str_cmp);
}

static int load_and_extract(Dlg* d, const char* path)
{
  FILE* f = fopen_u8(path, "rb");
  unsigned char* b = NULL;
  size_t len = 0, cap = 0, got;
  if (!f) return 0;
  for (;;) {
    size_t want;
    if (len == cap) {
      size_t nc = cap ? cap * 2 : 65536;
      unsigned char* nb = (unsigned char*)realloc(b, nc);
      if (!nb) break;
      b = nb; cap = nc;
    }
    want = cap - len;
    if (len + want > SCAN_LIMIT) want = SCAN_LIMIT - len;
    if (want == 0) { d->truncated = 1; break; }   /* hit the scan limit */
    got = fread(b + len, 1, want, f);
    len += got;
    if (got < want) break;                        /* EOF */
  }
  fclose(f);
  if (!b) return 0;
  d->scanned = len;
  extract(d, b, len);
  free(b);
  return 1;
}

static const char* str_text(const Dlg* d, size_t i) { return d->pool + d->str[i].toff; }

static int list_rows(const Dlg* d) { return imax(1, (d->r_list.b - d->r_list.t) / ROW_H); }

static void clamp_top(Dlg* d)
{
  int max = imax(0, d->nvis - list_rows(d));
  if (d->top > max) d->top = max;
  if (d->top < 0) d->top = 0;
}

static void rebuild_vis(Dlg* d)
{
  size_t i;
  free(d->vis);
  d->vis = (int*)malloc((d->n + 1) * sizeof(int));
  d->nvis = 0;
  d->top = 0; d->sel = -1;
  if (!d->vis) return;
  for (i = 0; i < d->n; ++i)
    if (!d->filter.s[0] || contains_ci(str_text(d, i), d->filter.s))
      d->vis[d->nvis++] = (int)i;
}

static int pos_of_entry(const Dlg* d, int entry)
{
  int i;
  for (i = 0; i < d->nvis; ++i) if (d->vis[i] == entry) return i;
  return -1;
}

static void select_pos(Dlg* d, int pos)
{
  int rows = list_rows(d);
  if (d->nvis == 0) return;
  pos = imax(0, imin(pos, d->nvis - 1));
  d->sel = d->vis[pos];
  if (pos < d->top) d->top = pos;
  else if (pos >= d->top + rows) d->top = pos - rows + 1;
  clamp_top(d);
}

static void card(PlCanvas* c, PlRect r, int headH)
{
  pl_round_rect(c, r, CLR_PANEL_BODY, CLR_PANEL_BORDER, 12);
  pl_round_rect(c, mkr(r.l + 1, r.t + 1, r.r - 1, r.t + headH + 12), CLR_PANEL_HEADER, CLR_PANEL_HEADER, 11);
  pl_fill(c, r.l + 1, r.t + headH, r.r - 1, r.t + headH + 12, CLR_PANEL_BODY);
  pl_line(c, r.l + 1, r.t + headH, r.r - 1, r.t + headH, CLR_SEPARATOR);
}

static void paint_list(Dlg* d, PlCanvas* c)
{
  int rows = list_rows(d), i;
  PlRect hd = mkr(d->r_card.l, d->r_card.t, d->r_card.r, d->r_card.t + HEAD);
  int x0 = d->r_list.l;

  /* column titles */
  pl_text(c, d->fontb, mkr(x0 + COL_OFF, hd.t, x0 + COL_TYPE, hd.b), translate("OFFSET"), CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
  pl_text(c, d->fontb, mkr(x0 + COL_TYPE, hd.t, x0 + COL_TXT, hd.b), translate("TYPE"), CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
  pl_text(c, d->fontb, mkr(x0 + COL_TXT, hd.t, d->r_list.r - 10, hd.b), translate("TEXT"), CLR_TEXT_SECONDARY, PL_TEXT_LEFT);

  if (d->nvis == 0) {
    pl_text(c, d->font, mkr(x0 + COL_OFF, d->r_list.t + 8, d->r_list.r - 10, d->r_list.t + 8 + ROW_H),
      d->n == 0 ? translate("No strings found.") : translate("No strings match the filter."), CLR_TEXT_CODE, PL_TEXT_LEFT);
    return;
  }

  for (i = 0; i < rows; ++i) {
    int vi = d->top + i, y = d->r_list.t + i * ROW_H, idx;
    char off[32];
    PlColor tc;
    if (vi >= d->nvis) break;
    idx = d->vis[vi];
    if (idx == d->sel)
      pl_round_rect(c, mkr(d->r_list.l + 4, y, d->r_list.r - 4, y + ROW_H), CLR_SIDEBAR_SEL, CLR_SIDEBAR_SEL, 6);

    snprintf(off, sizeof(off), "0x%08llX", (unsigned long long)d->str[idx].off);
    tc = d->str[idx].type == T_ASCII ? PL_RGB(98, 219, 196) : PL_RGB(187, 154, 247);

    pl_text(c, d->font, mkr(x0 + COL_OFF, y, x0 + COL_TYPE - 6, y + ROW_H), off, CLR_TEXT_CODE, PL_TEXT_LEFT);
    pl_text(c, d->font, mkr(x0 + COL_TYPE, y, x0 + COL_TXT - 6, y + ROW_H),
      d->str[idx].type == T_ASCII ? "ASCII" : "UTF-16", tc, PL_TEXT_LEFT);
    pl_text(c, d->font, mkr(x0 + COL_TXT, y, d->r_list.r - 12, y + ROW_H),
      str_text(d, (size_t)idx), CLR_TEXT_PRIMARY, PL_TEXT_ELLIPSIS);
  }

  if (d->nvis > rows) {                       /* thin scroll indicator */
    int th = d->r_list.b - d->r_list.t, bh = imax(24, th * rows / d->nvis);
    int by = d->r_list.t + (th - bh) * d->top / imax(1, d->nvis - rows);
    pl_round_rect(c, mkr(d->r_list.r - 6, by, d->r_list.r - 3, by + bh),
      CLR_BTN_GHOST_BD, CLR_BTN_GHOST_BD, 1);
  }
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
  Dlg* d = (Dlg*)u;
  char t[160];
  const char* name;
  int fx, fy;

  d->W = w; d->H = h;
  pl_fill(c, 0, 0, w, h, CLR_BG);

  /* filter box */
  pl_round_rect(c, d->r_filter, CLR_PANEL_HEADER, d->focus == F_FILTER ? CLR_ACCENT : CLR_PANEL_BORDER, 8);
  fx = d->r_filter.l; fy = d->r_filter.t;
  pl_ellipse(c, fx + 12, fy + 10, fx + 22, fy + 20, CLR_PANEL_HEADER, CLR_TEXT_SECONDARY);
  pl_line(c, fx + 21, fy + 19, fx + 26, fy + 24, CLR_TEXT_SECONDARY);
  {
    PlRect tr = mkr(d->r_filter.l + 34, d->r_filter.t, d->r_filter.r - 150, d->r_filter.b);
    if (d->filter.len == 0)
      pl_text(c, d->font, tr, translate("Filter strings"), CLR_TEXT_CODE, PL_TEXT_ELLIPSIS);
    else
      pl_text(c, d->font, tr, d->filter.s, CLR_TEXT_PRIMARY, PL_TEXT_ELLIPSIS);
    if (d->focus == F_FILTER && d->caret_on) {
      char tmp[260];
      int x;
      memcpy(tmp, d->filter.s, (size_t)d->filter.caret); tmp[d->filter.caret] = 0;
      x = tr.l + (d->filter.caret ? pl_text_width(d->font, tmp) : 0);
      if (x < tr.r) pl_line(c, x, tr.t + 5, x, tr.b - 5, CLR_TEXT_PRIMARY);
    }
  }
  snprintf(t, sizeof(t), translate("%d of %d"), d->nvis, (int)d->n);
  text_right(c, d->font, mkr(d->r_filter.r - 140, d->r_filter.t, d->r_filter.r - 12, d->r_filter.b),
    t, CLR_TEXT_CODE);

  card(c, d->r_card, HEAD);
  paint_list(d, c);

  pl_fill(c, d->r_bar.l, d->r_bar.t, d->r_bar.r, d->r_bar.b, CLR_BG);
  pl_line(c, d->r_bar.l, d->r_bar.t, d->r_bar.r, d->r_bar.t, CLR_SEPARATOR);
  name = strrchr(d->path, PSEP);
  name = name ? name + 1 : d->path;
  if (d->truncated)
    snprintf(t, sizeof(t), translate("%s  \xC2\xB7  first %u MiB scanned  \xC2\xB7  min length %d"),
      name, (unsigned)(SCAN_LIMIT / (1024u * 1024u)), MINLEN);
  else
    snprintf(t, sizeof(t), translate("%s  \xC2\xB7  %llu bytes scanned  \xC2\xB7  min length %d"),
      name, (unsigned long long)d->scanned, MINLEN);
  pl_text(c, d->font, mkr(14, d->r_bar.t, d->r_bar.r - 14, d->r_bar.b), t, CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
}

static void layout(Dlg* d)
{
  const int m = 12, bh = 34;
  int W = d->W, H = d->H, bot = H - BAR_H - m;

  d->r_filter = mkr(m, m, W - m, m + bh);
  d->r_card = mkr(m, d->r_filter.b + 12, W - m, bot);
  d->r_list = mkr(d->r_card.l + 1, d->r_card.t + HEAD + 4, d->r_card.r - 1, d->r_card.b - 6);
  d->r_bar = mkr(0, H - BAR_H, W, H);
  d->r_close = mkr(0, 0, 0, 0);   /* no close button when embedded */
}

static void on_resize(void* u, int w, int h)
{
  Dlg* d = (Dlg*)u;
  d->W = w; d->H = h;
  layout(d);
  clamp_top(d);
  pl_invalidate(d->w, NULL);
}

static void on_mouse_move(void* u, int x, int y)
{
  Dlg* d = (Dlg*)u;
  int h = inr(&d->r_close, x, y);
  if (h != d->hov_close) { d->hov_close = h; redraw(d); }
}
static void on_mouse_leave(void* u)
{
  Dlg* d = (Dlg*)u;
  if (d->hov_close) { d->hov_close = 0; redraw(d); }
}

static void on_mouse_down(void* u, int x, int y)
{
  Dlg* d = (Dlg*)u;
  if (inr(&d->r_close, x, y)) { pl_window_close(d->win); return; }
  if (inr(&d->r_filter, x, y)) { d->focus = F_FILTER; d->filter.caret = d->filter.len; }
  else if (inr(&d->r_list, x, y)) {
    int row = d->top + (y - d->r_list.t) / ROW_H;
    d->focus = F_LIST;
    if (row >= 0 && row < d->nvis) d->sel = d->vis[row];
  }
  redraw(d);
}

static int on_hand(void* u, int x, int y)
{
  Dlg* d = (Dlg*)u;
  if (inr(&d->r_close, x, y)) return 1;
  if (inr(&d->r_list, x, y)) return (d->top + (y - d->r_list.t) / ROW_H) < d->nvis;
  return 0;
}

static void on_wheel(void* u, int x, int y, int delta, int mods)
{
  Dlg* d = (Dlg*)u;
  (void)mods;
  if (inr(&d->r_list, x, y) || inr(&d->r_card, x, y)) {
    d->top -= delta * 3;
    clamp_top(d);
    redraw(d);
  }
}

static unsigned long blink_now(void)
{
#ifdef _WIN32
  return (unsigned long)GetTickCount();
#else
  return (unsigned long)(clock() * 1000 / CLOCKS_PER_SEC);
#endif
}

static unsigned long blink_period(void)
{
#ifdef _WIN32
  UINT t = GetCaretBlinkTime();
  return (t == 0 || t == INFINITE) ? 530 : (unsigned long)t;
#else
  return 530;
#endif
}

static void on_key(void* u, int key, uint32_t ch, int mods)
{
  Dlg* d = (Dlg*)u;
  int pos = d->sel >= 0 ? pos_of_entry(d, d->sel) : -1;
  int rows = list_rows(d);
  d->caret_on = 1; d->blink_t = blink_now();

  if (key == PL_KEY_ESC) { pl_window_close(d->win); return; }
  if (key == PL_KEY_CHAR && (mods & PL_MOD_CTRL) && (ch == 'f' || ch == 'F')) {
    d->focus = F_FILTER; d->filter.caret = d->filter.len; redraw(d); return;
  }

  if (key == PL_KEY_DOWN) select_pos(d, pos < 0 ? 0 : pos + 1);
  else if (key == PL_KEY_UP) select_pos(d, pos < 0 ? 0 : pos - 1);
  else if (key == PL_KEY_PGDN) select_pos(d, pos < 0 ? d->top + rows : pos + rows);
  else if (key == PL_KEY_PGUP) select_pos(d, pos < 0 ? 0 : pos - rows);
  else if (d->focus == F_LIST && key == PL_KEY_HOME) select_pos(d, 0);
  else if (d->focus == F_LIST && key == PL_KEY_END) select_pos(d, d->nvis - 1);
  else if (d->focus == F_FILTER) {
    if (e_key(&d->filter, key, ch, mods)) rebuild_vis(d);
  }
  redraw(d);
}

static void on_destroy(void* u)
{
  Dlg* d = (Dlg*)u;
  free(d->str); free(d->pool); free(d->vis);
  if (d->font) pl_font_destroy(d->font);
  if (d->fontb) pl_font_destroy(d->fontb);
  free(d);
}

static const PlWidgetOps s_ops = {
  on_paint, on_mouse_move, on_mouse_leave, on_mouse_down,
  on_resize, on_hand, on_destroy,
  on_key, on_wheel
};

static void load_now(Dlg* d)
{
  free(d->str);  d->str = NULL;  d->n = d->cap = 0;
  free(d->pool); d->pool = NULL; d->plen = d->pcap = 0;
  d->truncated = 0;
  d->scanned = 0;
  if (d->path[0]) load_and_extract(d, d->path);
  rebuild_vis(d);
  d->dirty = 0;
}

StringsView* stringsview_create(void* parent, int x, int y, int w, int h)
{
  Dlg* d = (Dlg*)calloc(1, sizeof(*d));
  if (!d) return NULL;
  d->W = w; d->H = h; d->sel = -1; d->focus = F_FILTER;
  d->caret_on = 1;
  d->font = pl_font_create(9, 0);
  d->fontb = pl_font_create(9, 1);
  if (!d->font || !d->fontb) { on_destroy(d); return NULL; }
  d->w = pl_widget_create(parent, &s_ops, d, x, y, w, h);
  if (!d->w) { on_destroy(d); return NULL; }
  layout(d);
  return d;
}

void stringsview_set_rect(StringsView* d, int x, int y, int w, int h)
{
  if (!d) return;
  pl_widget_set_rect(d->w, x, y, w, h);
  d->W = w; d->H = h;
  layout(d);
  clamp_top(d);
  pl_invalidate(d->w, NULL);
}

void stringsview_set_file(StringsView* d, const char* path)
{
  if (!d) return;
  strncpy(d->path, path ? path : "", sizeof(d->path) - 1);
  d->path[sizeof(d->path) - 1] = 0;
  d->dirty = 1;
  if (d->W > 0) { load_now(d); redraw(d); }   /* visible: load now, else wait */
}

void stringsview_activate(StringsView* d)
{
  if (!d) return;
  if (d->dirty) load_now(d);
  redraw(d);
}

void stringsview_key(StringsView* d, int key, uint32_t ch, int mods)
{
  if (!d) return;
  on_key(d, key, ch, mods);
}

void stringsview_tick(StringsView* d)
{
  unsigned long now;
  if (!d || d->W <= 0 || d->focus != F_FILTER) return;
  now = blink_now();
  if (now - d->blink_t >= blink_period()) {
    d->blink_t = now;
    d->caret_on = !d->caret_on;
    redraw(d);
  }
}