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
#ifndef CDIE_PLATFORM_H
#define CDIE_PLATFORM_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t PlColor;                       /* 0xRRGGBB */
#define PL_RGB(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define PL_NONE 0xFFFFFFFFu                     /* no fill / no border */

  typedef struct { int l, t, r, b; } PlRect;
  typedef struct PlCanvas PlCanvas;               /* opaque */
  typedef struct PlFont   PlFont;                 /* opaque */
  typedef struct PlWidget PlWidget;               /* opaque */
  typedef struct PlWindow PlWindow;               /* opaque */

  enum { PL_TEXT_LEFT = 0, PL_TEXT_CENTER = 1, PL_TEXT_ELLIPSIS = 2, PL_TEXT_RIGHT = 4 };

  enum { PL_MOD_CTRL = 1, PL_MOD_SHIFT = 2 };

  enum {
    PL_KEY_NONE = 0, PL_KEY_CHAR,
    PL_KEY_BACK, PL_KEY_DEL, PL_KEY_TAB, PL_KEY_ENTER, PL_KEY_ESC,
    PL_KEY_LEFT, PL_KEY_RIGHT, PL_KEY_UP, PL_KEY_DOWN,
    PL_KEY_HOME, PL_KEY_END, PL_KEY_PGUP, PL_KEY_PGDN, PL_KEY_F3
  };

  typedef struct PlWidgetOps {
    void (*paint)(void* u, PlCanvas* c, int w, int h);
    void (*mouse_move)(void* u, int x, int y);
    void (*mouse_leave)(void* u);
    void (*mouse_down)(void* u, int x, int y);
    void (*resize)(void* u, int w, int h);
    int  (*hand_cursor_at)(void* u, int x, int y);
    void (*destroy)(void* u);
    void (*key)(void* u, int key, uint32_t ch, int mods);
    void (*wheel)(void* u, int x, int y, int delta, int mods);
    void (*mouse_up)(void* u, int x, int y);
  } PlWidgetOps;

  /* ---- widgets ---- */
  PlWidget* pl_widget_create(void* parent_native, const PlWidgetOps* ops, void* user, int x, int y, int w, int h);
  void pl_widget_destroy(PlWidget* w);
  void pl_widget_set_rect(PlWidget* w, int x, int y, int width, int height);
  int  pl_parent_size(PlWidget* w, int* width, int* height);
  void pl_invalidate(PlWidget* w, const PlRect* r);



  /* ---- fonts ---- */
  PlFont* pl_font_create(int pt, int bold);
  void    pl_font_destroy(PlFont* f);
  int     pl_text_width(PlFont* f, const char* utf8);
  PlFont* pl_font_create_family(const char* family, int pt, int bold);  /* NULL if unavailable */
#ifdef _WIN32
#define PL_HAS_FONT_FAMILY 1
#endif

  /* ---- drawing ---- */
  void pl_fill(PlCanvas* c, int l, int t, int r, int b, PlColor col);
  void pl_line(PlCanvas* c, int x1, int y1, int x2, int y2, PlColor col);
  void pl_round_rect(PlCanvas* c, PlRect r, PlColor fill, PlColor border, int radius);
  void pl_ellipse(PlCanvas* c, int l, int t, int r, int b, PlColor fill, PlColor border);
  void pl_text(PlCanvas* c, PlFont* f, PlRect r, const char* utf8, PlColor col, int flags);
  void pl_image(PlCanvas* c, int x, int y, int w, int h, int src_w, int src_h, const unsigned char* rgba);
  /* call last in paint: rounds the four corners of the w x h area (anti-aliased) and draws the 1px border */
  void pl_frame_corners(PlCanvas* c, int w, int h, int radius, PlColor bg, PlColor border);

  /* ---- top-level window (modal dialogs) ---- */
  PlWindow* pl_window_create(void* owner_native, const char* title, int w, int h, int min_w, int min_h,
  void (*on_size)(void* u, int w, int h),
  void (*on_close)(void* u), void* user);
  void* pl_window_client(PlWindow* win);          
  void  pl_window_run_modal(PlWindow* win);       
  void  pl_window_close(PlWindow* win);
  void  pl_window_destroy(PlWindow* win);        

#ifdef __cplusplus
}
#endif
#endif