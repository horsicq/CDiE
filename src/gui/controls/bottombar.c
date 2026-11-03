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
#include "controls/bottombar.h"
#include "theme/theme.h"
#include "core/i18n.h"

#define BB_PAD 12
#define BB_VERSION "cdie 4.0.0 (x64)"

struct Bottombar {
  PlWidget* w;
  PlFont* font;
  char status[512];
  int cw, ch;
};

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
  Bottombar* d = (Bottombar*)u;
  const char* status = d->status[0] ? d->status : translate("Ready");
  PlRect left, right;
  int vw;

  d->cw = w;
  d->ch = h;

  pl_fill(c, 0, 0, w, h, CLR_BG);
  pl_line(c, 0, 0, w, 0, CLR_SEPARATOR);

  vw = pl_text_width(d->font, BB_VERSION);
  right.l = w - BB_PAD - vw;
  if (right.l < BB_PAD) right.l = BB_PAD;
  right.t = 0;
  right.r = w - BB_PAD;
  right.b = h;

  left.l = BB_PAD;
  left.t = 0;
  left.r = right.l - 8;
  if (left.r < left.l) left.r = left.l;
  left.b = h;

  pl_text(c, d->font, left, status, CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);
  pl_text(c, d->font, right, BB_VERSION, CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
}

static void on_resize(void* u, int w, int h)
{
  Bottombar* d = (Bottombar*)u;
  d->cw = w;
  d->ch = h;
}

static void on_mouse_move(void* u, int x, int y)
{
  (void)u; (void)x; (void)y;
}

static void on_mouse_leave(void* u)
{
  (void)u;
}

static void on_mouse_down(void* u, int x, int y)
{
  (void)u; (void)x; (void)y;
}

static int on_hand(void* u, int x, int y)
{
  (void)u; (void)x; (void)y;
  return 0;
}

static void on_destroy(void* u)
{
  Bottombar* d = (Bottombar*)u;
  if (d->font)
    pl_font_destroy(d->font);
  free(d);
}

static const PlWidgetOps s_ops = {
  on_paint,
  on_mouse_move,
  on_mouse_leave,
  on_mouse_down,
  on_resize,
  on_hand,
  on_destroy
};

Bottombar* bottombar_create(
  void* parent_native,
  int x, int y, int w, int h,
  BottombarCb cb,
  void* user)
{
  Bottombar* b;

  (void)cb;
  (void)user;

  b = (Bottombar*)calloc(1, sizeof(*b));
  if (!b)
    return NULL;

  b->cw = w;
  b->ch = h;

  b->font = pl_font_create(9, 0);
  if (!b->font) {
    free(b);
    return NULL;
  }

  b->w = pl_widget_create(parent_native, &s_ops, b, x, y, w, h);
  if (!b->w) {
    pl_font_destroy(b->font);
    free(b);
    return NULL;
  }

  return b;
}

void bottombar_destroy(Bottombar* b)
{
  if (b)
    pl_widget_destroy(b->w);
}

PlWidget* bottombar_widget(const Bottombar* b)
{
  return b->w;
}

void bottombar_set_status(Bottombar* b, const char* utf8)
{
  PlRect r;
  if (!b)
    return;

  strncpy(b->status, utf8 ? utf8 : "", sizeof(b->status) - 1);
  b->status[sizeof(b->status) - 1] = 0;

  r.l = 0;
  r.t = 0;
  r.r = b->cw;
  r.b = b->ch;
  pl_invalidate(b->w, &r);
}