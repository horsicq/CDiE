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
#include <stdio.h>
#include <string.h>
#include "panels/header.h"
#include "theme/theme.h"
#include "core/i18n.h"

#define MAX_HDR_STR 4096
#define BULLETS     "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2"   /* ••• (UTF-8) */

#define TXT_LEFT (PL_TEXT_LEFT | PL_TEXT_ELLIPSIS)

struct Header {
  PlWidget* w;
  HeaderBrowseCb cb;
  void* user;
  char           folder[MAX_HDR_STR];
  char           filename[MAX_HDR_STR];
  int            has_file;
  char           timestr[32];
  int            width, height;
  PlFont* fontCrumb, * fontFilename, * fontBrowse;
};

static void browse_rect(const Header* d, PlRect* r)
{
  r->r = d->width - 14;
  r->l = r->r - 46;
  r->t = (d->height - 30) / 2;
  r->b = r->t + 30;
}

static int in_browse(const Header* d, int x, int y)
{
  PlRect r;
  browse_rect(d, &r);
  return x >= r.l && x < r.r && y >= r.t && y < r.b;
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
  Header* d = (Header*)u;
  PlRect br;

  d->width = w; d->height = h;
  pl_fill(c, 0, 0, w, h, CLR_BG);

  if (d->has_file) {
    PlRect crumb = { CONTENT_PAD, 10, w - 70, 28 };
    PlRect name = { CONTENT_PAD, 30, w - 70, h - 6 };
    pl_text(c, d->fontCrumb, crumb, d->folder,
      CLR_TEXT_SECONDARY, TXT_LEFT);
    pl_text(c, d->fontFilename, name, d->filename,
      CLR_TEXT_PRIMARY, TXT_LEFT);
    if (d->timestr[0]) {
      int nw = pl_text_width(d->fontFilename, d->filename);
      int tx = CONTENT_PAD + nw + 12;
      PlRect tr = { tx, 30, w - 70, h - 6 };
      if (tx < w - 70 - 40)
        pl_text(c, d->fontCrumb, tr, d->timestr,
          CLR_TEXT_SECONDARY, TXT_LEFT);
    }
  }
  else {
    PlRect r = { CONTENT_PAD, 0, w - 70, h };
    pl_text(c, d->fontCrumb, r,
      translate("Drop a file here, or click " BULLETS " to open"),
      CLR_TEXT_SECONDARY, TXT_LEFT);
  }

  browse_rect(d, &br);
  pl_round_rect(c, br, CLR_PANEL, CLR_BTN_GHOST_BD, 6);
  pl_text(c, d->fontBrowse, br, BULLETS, CLR_TEXT_SECONDARY, PL_TEXT_CENTER);

  pl_line(c, 0, h - 1, w, h - 1, CLR_SEPARATOR);
}

static void on_resize(void* u, int w, int h)
{
  Header* d = (Header*)u;
  d->width = w; d->height = h;
  pl_invalidate(d->w, NULL);
}

static void on_mouse_move(void* u, int x, int y) { (void)u; (void)x; (void)y; }
static void on_mouse_leave(void* u) { (void)u; }

static void on_mouse_down(void* u, int x, int y)
{
  Header* d = (Header*)u;
  if (in_browse(d, x, y) && d->cb) d->cb(d->user);
}

static int on_hand(void* u, int x, int y)
{
  return in_browse((Header*)u, x, y);
}

static void on_destroy(void* u)
{
  Header* d = (Header*)u;
  pl_font_destroy(d->fontCrumb);
  pl_font_destroy(d->fontFilename);
  pl_font_destroy(d->fontBrowse);
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

Header* header_create(void* parent_native, int x, int y, int w, int h,
  HeaderBrowseCb cb, void* user)
{
  Header* d = (Header*)calloc(1, sizeof * d);
  if (!d) return NULL;
  d->cb = cb; d->user = user; d->width = w; d->height = h;
  d->fontCrumb = pl_font_create(9, 0);
  d->fontFilename = pl_font_create(18, 1);
  d->fontBrowse = pl_font_create(8, 1);
  d->w = pl_widget_create(parent_native, &s_ops, d, x, y, w, h);
  if (!d->w) {
    pl_font_destroy(d->fontCrumb);
    pl_font_destroy(d->fontFilename);
    pl_font_destroy(d->fontBrowse);
    free(d);
    return NULL;
  }
  return d;
}

void      header_destroy(Header* d) { if (d) pl_widget_destroy(d->w); }
PlWidget* header_widget(const Header* d) { return d->w; }

void header_set_path(Header* d, const char* folder, const char* filename)
{
  if (!d) return;
  copy_str(d->folder, sizeof d->folder, folder);
  copy_str(d->filename, sizeof d->filename, filename);
  d->has_file = (filename && filename[0]) ? 1 : 0;
  d->timestr[0] = 0;                 
  pl_invalidate(d->w, NULL);
}

void header_set_time(Header* d, unsigned int msec)
{
  if (!d) return;
  snprintf(d->timestr, sizeof d->timestr, "%u ms", msec);
  pl_invalidate(d->w, NULL);
}

void header_clear(Header* d) { header_set_path(d, NULL, NULL); }