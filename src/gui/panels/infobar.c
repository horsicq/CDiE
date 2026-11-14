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
#include "panels/infobar.h"
#include "theme/theme.h"

#define MAX_CARDS    4
#define MAX_CARD_STR 128

#define TXT_LEFT (PL_TEXT_LEFT | PL_TEXT_ELLIPSIS)

typedef struct {
  char label[MAX_CARD_STR];
  char value[MAX_CARD_STR];
  char subvalue[MAX_CARD_STR];
} CardData;

struct Infobar {
  PlWidget* w;
  CardData  cards[MAX_CARDS];
  int       count, width, height;
  PlFont* fontLabel, * fontValue, * fontSub;
};

static void card_rect(const Infobar* d, int i, int count, PlRect* out)
{
  int pad = CONTENT_PAD, card_w;
  if (count <= 0) count = 1;
  card_w = (d->width - 2 * pad) / count;
  out->l = pad + i * card_w;
  out->t = th_scale(8);
  out->r = out->l + card_w;
  out->b = d->height - th_scale(8);
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
  Infobar* d = (Infobar*)u;
  int i;

  d->width = w; d->height = h;
  pl_fill(c, 0, 0, w, h, CLR_BG);
  if (d->count <= 0) return;

  {
    PlRect first, last, all;
    card_rect(d, 0, d->count, &first);
    card_rect(d, d->count - 1, d->count, &last);
    all.l = first.l; all.t = first.t; all.r = last.r; all.b = first.b;
    pl_round_rect(c, all, CLR_PANEL, CLR_PANEL_BORDER, 12);
  }

  for (i = 0; i < d->count; ++i) {
    PlRect card, dot, lbl, val, sub;
    card_rect(d, i, d->count, &card);

    if (i > 0)
      pl_line(c, card.l, card.t + 1, card.l, card.b - 1, CLR_PANEL_BORDER);

    dot.l = card.l + th_scale(13); dot.t = card.t + th_scale(8);
    dot.r = card.l + th_scale(19); dot.b = card.t + th_scale(14);
    pl_round_rect(c, dot, CLR_ACCENT, CLR_ACCENT, 3);

    lbl.l = card.l + th_scale(25); lbl.t = card.t + th_scale(6);
    lbl.r = card.r - th_scale(8);  lbl.b = card.t + th_scale(20);
    pl_text(c, d->fontLabel, lbl, d->cards[i].label,
      CLR_TEXT_SECONDARY, TXT_LEFT);

    val.l = card.l + th_scale(12); val.t = card.t + th_scale(20);
    val.r = card.r - th_scale(8);  val.b = card.t + th_scale(42);
    pl_text(c, d->fontValue, val, d->cards[i].value,
      CLR_TEXT_PRIMARY, TXT_LEFT);

    sub.l = card.l + th_scale(12); sub.t = card.t + th_scale(40);
    sub.r = card.r - th_scale(8);  sub.b = card.b - th_scale(6);
    pl_text(c, d->fontSub, sub, d->cards[i].subvalue,
      CLR_TEXT_SECONDARY, TXT_LEFT);
  }
}

static void on_resize(void* u, int w, int h)
{
  Infobar* d = (Infobar*)u;
  d->width = w; d->height = h;
  pl_invalidate(d->w, NULL);
}

static void on_mouse_move(void* u, int x, int y) { (void)u; (void)x; (void)y; }
static void on_mouse_leave(void* u) { (void)u; }
static void on_mouse_down(void* u, int x, int y) { (void)u; (void)x; (void)y; }
static int  on_hand(void* u, int x, int y) { (void)u; (void)x; (void)y; return 0; }

static void on_destroy(void* u)
{
  Infobar* d = (Infobar*)u;
  pl_font_destroy(d->fontLabel);
  pl_font_destroy(d->fontValue);
  pl_font_destroy(d->fontSub);
  free(d);
}

static const PlWidgetOps s_ops = {
    on_paint, on_mouse_move, on_mouse_leave, on_mouse_down, on_resize, on_hand, on_destroy
};

static void copy_str(char* dst, size_t cap, const char* src)
{
  size_t n;
  if (!src) src = "";
  n = strlen(src);
  if (n >= cap) {
    n = cap - 1;
    while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

Infobar* infobar_create(void* parent_native, int x, int y, int w, int h)
{
  Infobar* d = (Infobar*)calloc(1, sizeof * d);
  if (!d) return NULL;
  d->width = w; d->height = h;
  d->fontLabel = pl_font_create(7, 1);
  d->fontValue = pl_font_create(12, 1);
  d->fontSub = pl_font_create(8, 0);
  d->w = pl_widget_create(parent_native, &s_ops, d, x, y, w, h);
  if (!d->w) {
    pl_font_destroy(d->fontLabel);
    pl_font_destroy(d->fontValue);
    pl_font_destroy(d->fontSub);
    free(d);
    return NULL;
  }
  return d;
}

void      infobar_destroy(Infobar* d) { if (d) pl_widget_destroy(d->w); }
PlWidget* infobar_widget(const Infobar* d) { return d->w; }

void infobar_set_cards(Infobar* d, const InfoCard* cards, int count)
{
  int i;
  if (!d || !cards || count <= 0) return;
  if (count > MAX_CARDS) count = MAX_CARDS;
  d->count = count;
  for (i = 0; i < count; ++i) {
    copy_str(d->cards[i].label, sizeof d->cards[i].label, cards[i].label);
    copy_str(d->cards[i].value, sizeof d->cards[i].value, cards[i].value);
    copy_str(d->cards[i].subvalue, sizeof d->cards[i].subvalue, cards[i].subvalue);
  }
  pl_invalidate(d->w, NULL);
}

void infobar_clear(Infobar* d)
{
  if (!d) return;
  d->count = 0;
  pl_invalidate(d->w, NULL);
}