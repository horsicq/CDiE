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
#include <string.h>
#include "treeview.h"
#include "theme/theme.h"
#include "core/i18n.h"

#define HDR_H     40
#define DET_H     28
#define DET_ROWS  4
#define MX        14
#define GAP       3
#define SCROLL_W  6
#define PILL_W  150

typedef struct {
  char* type, * name, * version, * info;
  int   open;
} Item;

struct ResultsTree {
  PlWidget* w;
  Item* items;
  size_t    count, cap;
  int       scroll, width, height;
  PlFont* font, * bold, * tiny, * mono;
};

typedef struct { const char* key; PlColor bg, fg; } Badge;

static char* dup_str(const char* s)
{
  size_t n = s ? strlen(s) : 0;
  char* p = (char*)malloc(n + 1);
  if (p) { if (n) memcpy(p, s, n); p[n] = 0; }
  return p;
}

static int ieq(const char* a, const char* b)
{
  for (; *a && *b; ++a, ++b) {
    char x = *a, y = *b;
    if (x >= 'A' && x <= 'Z') x += 32;
    if (y >= 'A' && y <= 'Z') y += 32;
    if (x != y) return 0;
  }
  return *a == *b;
}

static void badge_colors(const char* type, PlColor* bg, PlColor* fg)
{
  static const Badge badges[] = {
      { "Operation System", PL_RGB(0x1E,0x3A,0x6B), PL_RGB(0x7F,0xB0,0xFF) },
      { "OS",               PL_RGB(0x1E,0x3A,0x6B), PL_RGB(0x7F,0xB0,0xFF) },
      { "Linker",           PL_RGB(0x33,0x2A,0x66), PL_RGB(0xB5,0xA2,0xFF) },
      { "Compiler",         PL_RGB(0x16,0x46,0x40), PL_RGB(0x62,0xDB,0xC4) },
      { "Language",         PL_RGB(0x5A,0x42,0x16), PL_RGB(0xF2,0xC0,0x5C) },
      { "Library",          PL_RGB(0x5A,0x1E,0x48), PL_RGB(0xF2,0x8B,0xC8) },
      { "Protection",       PL_RGB(0x6B,0x1E,0x1E), PL_RGB(0xFF,0x6B,0x6B) },
      { "Protector",        PL_RGB(0x6B,0x1E,0x1E), PL_RGB(0xFF,0x6B,0x6B) },
      { "Packer",           PL_RGB(0x6B,0x1E,0x1E), PL_RGB(0xFF,0x6B,0x6B) },
      { "Licensing",        PL_RGB(0x5A,0x42,0x16), PL_RGB(0xF2,0xC0,0x5C) },
  };
  size_t i;
  int heur = 0;

  if (type[0] == '(') {
    const char* close = strchr(type, ')');
    if (close) {
      type = close + 1;
      while (*type == ' ') ++type;
      heur = 1;
    }
  }

  for (i = 0; i < sizeof badges / sizeof badges[0]; ++i)
    if (ieq(type, badges[i].key)) {
      *bg = heur ? CLR_BTN_GHOST : badges[i].bg;   /* guesses: dark pill, coloured text */
      *fg = badges[i].fg;
      return;
    }
  *bg = CLR_BTN_GHOST; *fg = CLR_TEXT_SECONDARY;
}

/* "(Heur) Packer" -> "(Heur) <translated Packer>". Colours still use the English type. */
static void type_label(const char* type, char* out, size_t cap)
{
  char prefix[40];
  const char* base = type;
  prefix[0] = 0;
  if (type[0] == '(') {
    const char* close = strchr(type, ')');
    if (close) {
      size_t n = (size_t)(close + 1 - type);
      if (n < sizeof prefix) {
        memcpy(prefix, type, n); prefix[n] = 0;
        base = close + 1;
        while (*base == ' ') ++base;
      }
    }
  }
  snprintf(out, cap, "%s%s%s", prefix, prefix[0] ? " " : "", translate(base));
}

static int item_height(const Item* it) { return HDR_H + (it->open ? DET_ROWS * DET_H : 0); }

static int content_height(const ResultsTree* t)
{
  int h = 0; size_t i;
  for (i = 0; i < t->count; ++i) h += item_height(&t->items[i]);
  return h;
}

static void clamp_scroll(ResultsTree* t)
{
  int max = content_height(t) - t->height;
  if (max < 0) max = 0;
  if (t->scroll > max) t->scroll = max;
  if (t->scroll < 0)   t->scroll = 0;
}

static void free_item(Item* it)
{
  free(it->type); free(it->name); free(it->version); free(it->info);
}

static void paint_chevron(PlCanvas* c, int cx, int cy, int open, PlColor col)
{
  if (open) {
    pl_line(c, cx - 4, cy - 2, cx, cy + 2, col);
    pl_line(c, cx, cy + 2, cx + 5, cy - 3, col);
  }
  else {
    pl_line(c, cx - 2, cy - 4, cx + 2, cy, col);
    pl_line(c, cx + 2, cy, cx - 3, cy + 5, col);
  }
}

static void paint_header(ResultsTree* t, PlCanvas* c, const Item* it,
  int y, int w)
{
  PlColor bg, fg;
  PlRect card = { MX + 16, y + GAP, w - MX - SCROLL_W, y + HDR_H - GAP };
  int mid = (card.t + card.b) / 2;
  int pill_w = PILL_W, x;
  char up[80], label[80]; size_t i;

  pl_round_rect(c, card, CLR_PANEL, CLR_PANEL_BORDER, 16);
  pl_line(c, MX + 2, mid, card.l, mid, CLR_PANEL_BORDER);

  type_label(it->type, label, sizeof label);
  for (i = 0; label[i] && i < sizeof up - 1; ++i) {
    char ch = label[i];
    up[i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ch;
  }
  up[i] = 0;
  badge_colors(it->type, &bg, &fg);
  

  {
    PlRect pill = { card.l + 14, mid - 11, card.l + 14 + pill_w, mid + 11 };
    pl_round_rect(c, pill, bg, bg, 22);
    pl_text(c, t->tiny, pill, up, fg, PL_TEXT_CENTER);
  }

  x = card.l + 14 + pill_w + 14;
  {
    PlRect nr = { x, card.t, card.r - 40, card.b };
    pl_text(c, t->bold, nr, it->name, CLR_TEXT_PRIMARY, 0);
    if (it->version[0]) {
      PlRect vr = { x + pl_text_width(t->bold, it->name) + 10,
                    card.t, card.r - 40, card.b };
      pl_text(c, t->mono, vr, it->version, CLR_TEXT_ACCENT, 0);
    }
  }
  paint_chevron(c, card.r - 20, mid, it->open, CLR_TEXT_SECONDARY);
}

static void paint_detail(ResultsTree* t, PlCanvas* c, const char* label,
  const char* value, int y, int w)
{
  int l = MX + 32, r = w - MX - 14 - SCROLL_W;
  PlRect lr = { l + 14,  y, l + 110, y + DET_H };
  PlRect vr = { l + 120, y, r - 14,  y + DET_H };
  pl_fill(c, l, y, r, y + DET_H, CLR_SIDEBAR_SEL);
  pl_text(c, t->font, lr, label, CLR_TEXT_SECONDARY, 0);
  pl_text(c, t->mono, vr, value, CLR_TEXT_PRIMARY, 0);
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
  ResultsTree* t = (ResultsTree*)u;
  int y = -t->scroll, total = content_height(t);
  size_t i;
  t->width = w; t->height = h;
  PlRect frame = { 0, 0, w - 1, h - 1 };
  pl_fill(c, 0, 0, w, h, CLR_BG);
  pl_round_rect(c, frame, CLR_PANEL_BODY, CLR_PANEL_BORDER, 12);

  for (i = 0; i < t->count; ++i) {
    const Item* it = &t->items[i];
    int ih = item_height(it);
    if (y + ih > 0 && y < h) {
      paint_header(t, c, it, y, w);
      if (it->open) {
        int dy = y + HDR_H;
        {
          char tl[80];
          type_label(it->type, tl, sizeof tl);
          paint_detail(t, c, translate("Type"), tl, dy, w);
        }
        paint_detail(t, c, translate("Name"), it->name, dy + DET_H, w);
        paint_detail(t, c, translate("Version"), it->version, dy + 2 * DET_H, w);
        paint_detail(t, c, translate("Info"), it->info, dy + 3 * DET_H, w);
      }
    }
    y += ih;
    if (y >= h) break;
  }

  if (total > h) {                       /* thin scrollbar thumb */
    int th = h * h / total, ty;
    if (th < 24) th = 24;
    ty = (int)((long long)t->scroll * (h - th) / (total - h));
    {
      PlRect thumb = { w - SCROLL_W, ty, w - 1, ty + th };
      pl_round_rect(c, thumb, CLR_PANEL_BORDER, CLR_PANEL_BORDER, 6);
    }
  }
}

static int hit_header(const ResultsTree* t, int my)
{
  int y = -t->scroll; size_t i;
  for (i = 0; i < t->count; ++i) {
    if (my >= y && my < y + HDR_H) return (int)i;
    y += item_height(&t->items[i]);
  }
  return -1;
}

static void on_mouse_down(void* u, int x, int y)
{
  ResultsTree* t = (ResultsTree*)u;
  int hit = hit_header(t, y);
  (void)x;
  if (hit < 0) return;
  t->items[hit].open = !t->items[hit].open;
  clamp_scroll(t);
  pl_invalidate(t->w, NULL);
}

static void on_key(void* u, int key, uint32_t ch, int mods)
{
  (void)u; (void)key; (void)ch; (void)mods;
}

static void on_wheel(void* u, int x, int y, int delta, int mods)
{
  ResultsTree* t = (ResultsTree*)u;
  int old = t->scroll;
  (void)x; (void)y; (void)mods;
  t->scroll -= delta * 40;      /* flip the sign if it goes backwards */
  clamp_scroll(t);
  if (t->scroll != old) pl_invalidate(t->w, NULL);
}

static int on_hand(void* u, int x, int y)
{
  (void)x;
  return hit_header((ResultsTree*)u, y) >= 0;
}

static void on_resize(void* u, int w, int h)
{
  ResultsTree* t = (ResultsTree*)u;
  t->width = w; t->height = h;
  clamp_scroll(t);
  pl_invalidate(t->w, NULL);
}

static void on_destroy(void* u)
{
  ResultsTree* t = (ResultsTree*)u;
  resultstree_clear(t);
  free(t->items);
  pl_font_destroy(t->font);  pl_font_destroy(t->bold);
  pl_font_destroy(t->tiny); pl_font_destroy(t->mono);
  free(t);
}

static const PlWidgetOps s_ops = {
    on_paint, NULL, NULL, on_mouse_down,
    on_resize, on_hand, on_destroy,
    on_key, on_wheel
};

ResultsTree* resultstree_create(void* parent_native, int x, int y, int w, int h)
{
  ResultsTree* t = (ResultsTree*)calloc(1, sizeof * t);
  if (!t) return NULL;
  t->width = w; t->height = h;
  t->font = pl_font_create(10, 0);
  t->bold = pl_font_create(10, 1);
  t->tiny = pl_font_create(8, 1);
  t->mono = pl_font_create(10, 0);
  t->w = pl_widget_create(parent_native, &s_ops, t, x, y, w, h);
  if (!t->w) { on_destroy(t); return NULL; }
  return t;
}

void resultstree_destroy(ResultsTree* t) { if (t) pl_widget_destroy(t->w); }
PlWidget* resultstree_widget(const ResultsTree* t) { return t->w; }
int resultstree_count(const ResultsTree* t) { return (int)t->count; }

void resultstree_clear(ResultsTree* t)
{
  size_t i;
  for (i = 0; i < t->count; ++i) free_item(&t->items[i]);
  t->count = 0; t->scroll = 0;
  if (t->w) pl_invalidate(t->w, NULL);
}

int resultstree_add(ResultsTree* t, const char* type, const char* name,
  const char* version, const char* info)
{
  Item it;
  if (t->count == t->cap) {
    size_t cap = t->cap ? t->cap * 2 : 16;
    Item* p = (Item*)realloc(t->items, cap * sizeof * p);
    if (!p) return 0;
    t->items = p; t->cap = cap;
  }
  it.type = dup_str(type);   it.name = dup_str(name);
  it.version = dup_str(version); it.info = dup_str(info);
  it.open = 0;
  if (!it.type || !it.name || !it.version || !it.info) { free_item(&it); return 0; }
  t->items[t->count++] = it;
  pl_invalidate(t->w, NULL);
  return 1;
}