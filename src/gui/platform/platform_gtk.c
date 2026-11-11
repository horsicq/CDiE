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
#if !defined(_WIN32) && !defined(__APPLE__)
#include <gtk/gtk.h>
#include <stdlib.h>
#include "platform/platform.h"

struct PlCanvas { cairo_t *cr; };
struct PlFont   { PangoFontDescription *desc; };
struct PlWidget { GtkWidget *da; PlWidgetOps ops; void *user; };

static void set_col(cairo_t *cr, PlColor c)
{
    cairo_set_source_rgb(cr, ((c >> 16) & 255) / 255.0, ((c >> 8) & 255) / 255.0, (c & 255) / 255.0);
}

static void rr_path(cairo_t *cr, double x, double y, double w, double h, double r)
{
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r,     y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r,     y + r,     r, G_PI, 3 * G_PI / 2);
    cairo_close_path(cr);
}

static GtkWidget *find_fixed(GtkWidget *w)
{
    GList *kids, *it;
    GtkWidget *found = NULL;
    if (!w) return NULL;
    if (GTK_IS_FIXED(w)) return w;
    if (!GTK_IS_CONTAINER(w)) return NULL;
    kids = gtk_container_get_children(GTK_CONTAINER(w));
    for (it = kids; it && !found; it = it->next)
        found = find_fixed(GTK_WIDGET(it->data));
    g_list_free(kids);
    return found;
}

static gboolean on_draw(GtkWidget *da, cairo_t *cr, gpointer p)
{
    PlWidget *w = (PlWidget *)p;
    PlCanvas c;
    c.cr = cr;
    if (w->ops.paint)
        w->ops.paint(w->user, &c, gtk_widget_get_allocated_width(da),
                     gtk_widget_get_allocated_height(da));
    return FALSE;
}

static gboolean on_motion(GtkWidget *da, GdkEventMotion *ev, gpointer p)
{
    PlWidget *w = (PlWidget *)p;
    GdkWindow *gw = gtk_widget_get_window(da);
    if (w->ops.hand_cursor_at && gw) {
        GdkCursor *cur = NULL;
        if (w->ops.hand_cursor_at(w->user, (int)ev->x, (int)ev->y))
            cur = gdk_cursor_new_from_name(gdk_window_get_display(gw), "pointer");
        gdk_window_set_cursor(gw, cur);
        if (cur) g_object_unref(cur);
    }
    if (w->ops.mouse_move) w->ops.mouse_move(w->user, (int)ev->x, (int)ev->y);
    return FALSE;
}

static gboolean on_leave(GtkWidget *da, GdkEventCrossing *ev, gpointer p)
{
    PlWidget *w = (PlWidget *)p;
    (void)da; (void)ev;
    if (w->ops.mouse_leave) w->ops.mouse_leave(w->user);
    return FALSE;
}

static gboolean on_button(GtkWidget *da, GdkEventButton *ev, gpointer p)
{
    PlWidget *w = (PlWidget *)p;
    if (w->ops.key) gtk_widget_grab_focus(da);
    if (ev->button == 1 && w->ops.mouse_down)
        w->ops.mouse_down(w->user, (int)ev->x, (int)ev->y);
    return TRUE;
}

static int map_key(guint kv)
{
  switch (kv) {
  case GDK_KEY_Left: case GDK_KEY_KP_Left:   return PL_KEY_LEFT;
  case GDK_KEY_Right: case GDK_KEY_KP_Right: return PL_KEY_RIGHT;
  case GDK_KEY_Up: case GDK_KEY_KP_Up:       return PL_KEY_UP;
  case GDK_KEY_Down: case GDK_KEY_KP_Down:   return PL_KEY_DOWN;
  case GDK_KEY_Page_Up:   return PL_KEY_PGUP;
  case GDK_KEY_Page_Down: return PL_KEY_PGDN;
  case GDK_KEY_Home: return PL_KEY_HOME;
  case GDK_KEY_End:  return PL_KEY_END;
  case GDK_KEY_BackSpace: return PL_KEY_BACK;
  case GDK_KEY_Delete:    return PL_KEY_DEL;
  case GDK_KEY_Tab:       return PL_KEY_TAB;
  case GDK_KEY_Return: case GDK_KEY_KP_Enter: return PL_KEY_ENTER;
  case GDK_KEY_Escape: return PL_KEY_ESC;
  case GDK_KEY_F3:     return PL_KEY_F3;
  default: return -1;
  }
}

static gboolean on_key_ev(GtkWidget* da, GdkEventKey* ev, gpointer p)
{
  PlWidget* w = (PlWidget*)p;
  int mods = 0, key;
  uint32_t ch;
  (void)da;
  if (!w->ops.key) return FALSE;
  if (ev->state & GDK_CONTROL_MASK) mods |= PL_MOD_CTRL;
  if (ev->state & GDK_SHIFT_MASK)   mods |= PL_MOD_SHIFT;
  key = map_key(ev->keyval);
  if (key >= 0) { w->ops.key(w->user, key, 0, mods); return TRUE; }
  ch = gdk_keyval_to_unicode(ev->keyval);
  if (ch) { w->ops.key(w->user, PL_KEY_CHAR, ch, mods); return TRUE; }
  return FALSE;
}

static gboolean on_scroll_ev(GtkWidget* da, GdkEventScroll* ev, gpointer p)
{
  PlWidget* w = (PlWidget*)p;
  int delta = 0, mods = 0;
  (void)da;
  if (!w->ops.wheel) return FALSE;
  if (ev->direction == GDK_SCROLL_UP) delta = 1;
  else if (ev->direction == GDK_SCROLL_DOWN) delta = -1;
  else if (ev->direction == GDK_SCROLL_SMOOTH) delta = ev->delta_y < 0 ? 1 : ev->delta_y > 0 ? -1 : 0;
  if (!delta) return FALSE;
  if (ev->state & GDK_SHIFT_MASK) mods |= PL_MOD_SHIFT;
  w->ops.wheel(w->user, (int)ev->x, (int)ev->y, delta, mods);
  return TRUE;
}

static void on_alloc(GtkWidget* da, GdkRectangle* a, gpointer p)
{
    PlWidget *w = (PlWidget *)p;
    (void)da;
    if (w->ops.resize) w->ops.resize(w->user, a->width, a->height);
}

static void on_destroy(GtkWidget *da, gpointer p)
{
    PlWidget *w = (PlWidget *)p;
    (void)da;
    if (w->ops.destroy) w->ops.destroy(w->user);
    free(w);
}

PlWidget *pl_widget_create(void *parent_native, const PlWidgetOps *ops, void *user,
                           int x, int y, int width, int height)
{
    GtkWidget *fixed = find_fixed((GtkWidget *)parent_native);
    PlWidget *w;
    if (!fixed) return NULL;
    w = (PlWidget *)calloc(1, sizeof *w);
    if (!w) return NULL;
    w->ops = *ops; w->user = user;

    w->da = gtk_drawing_area_new();
    gtk_widget_set_size_request(w->da, width, height);
    gtk_widget_add_events(w->da, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK |
      GDK_LEAVE_NOTIFY_MASK | GDK_KEY_PRESS_MASK |
      GDK_SCROLL_MASK);
    if (w->ops.key) gtk_widget_set_can_focus(w->da, TRUE);
    g_signal_connect(w->da, "key-press-event", G_CALLBACK(on_key_ev), w);
    g_signal_connect(w->da, "scroll-event", G_CALLBACK(on_scroll_ev), w);
    g_signal_connect(w->da, "draw", G_CALLBACK(on_draw), w);
    g_signal_connect(w->da, "motion-notify-event", G_CALLBACK(on_motion), w);
    g_signal_connect(w->da, "leave-notify-event", G_CALLBACK(on_leave), w);
    g_signal_connect(w->da, "button-press-event", G_CALLBACK(on_button), w);
    g_signal_connect(w->da, "size-allocate", G_CALLBACK(on_alloc), w);
    g_signal_connect(w->da, "destroy", G_CALLBACK(on_destroy), w);
    gtk_fixed_put(GTK_FIXED(fixed), w->da, x, y);
    gtk_widget_show(w->da);
    return w;
}

void pl_window_set_caption(PlWindow* win, int dark, PlColor bg, PlColor fg)
{
  (void)win; (void)dark; (void)bg; (void)fg;
}

void pl_widget_destroy(PlWidget *w) { if (w) gtk_widget_destroy(w->da); }

void pl_widget_set_rect(PlWidget *w, int x, int y, int width, int height)
{
    GtkWidget *p = gtk_widget_get_parent(w->da);
    gtk_widget_set_size_request(w->da, width, height);
    if (p && GTK_IS_FIXED(p)) gtk_fixed_move(GTK_FIXED(p), w->da, x, y);
}

int pl_parent_size(PlWidget *w, int *width, int *height)
{
    GtkWidget *p = gtk_widget_get_parent(w->da);
    if (!p) return 0;
    *width = gtk_widget_get_allocated_width(p);
    *height = gtk_widget_get_allocated_height(p);
    return 1;
}

void pl_invalidate(PlWidget *w, const PlRect *r)
{
    if (r) gtk_widget_queue_draw_area(w->da, r->l, r->t, r->r - r->l, r->b - r->t);
    else   gtk_widget_queue_draw(w->da);
}

PlFont *pl_font_create(int pt, int bold)
{
    PlFont *f = (PlFont *)calloc(1, sizeof *f);
    if (!f) return NULL;
    f->desc = pango_font_description_new();
    pango_font_description_set_family(f->desc, "Segoe UI,Cantarell,Noto Sans,Sans");
    pango_font_description_set_size(f->desc, pt * PANGO_SCALE);
    pango_font_description_set_weight(f->desc, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
    return f;
}

void pl_font_destroy(PlFont *f)
{
    if (!f) return;
    pango_font_description_free(f->desc);
    free(f);
}

int pl_text_width(PlFont *f, const char *s)
{
    cairo_surface_t *sf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(sf);
    PangoLayout *l = pango_cairo_create_layout(cr);
    int w = 0, h = 0;
    pango_layout_set_font_description(l, f->desc);
    pango_layout_set_text(l, s, -1);
    pango_layout_get_pixel_size(l, &w, &h);
    g_object_unref(l);
    cairo_destroy(cr);
    cairo_surface_destroy(sf);
    return w;
}

void pl_fill(PlCanvas* c, int l, int t, int r, int b, PlColor col)
{
  cairo_save(c->cr);
  cairo_set_antialias(c->cr, CAIRO_ANTIALIAS_NONE);
  set_col(c->cr, col);
  cairo_rectangle(c->cr, l, t, r - l, b - t);
  cairo_fill(c->cr);
  cairo_restore(c->cr);
}

void pl_line(PlCanvas *c, int x1, int y1, int x2, int y2, PlColor col)
{
    set_col(c->cr, col);
    cairo_set_line_width(c->cr, 1.0);
    cairo_move_to(c->cr, x1 + 0.5, y1 + 0.5);
    cairo_line_to(c->cr, x2 + 0.5, y2 + 0.5);
    cairo_stroke(c->cr);
}

void pl_round_rect(PlCanvas *c, PlRect r, PlColor fill, PlColor border, int radius)
{
    if (fill != PL_NONE) {
        set_col(c->cr, fill);
        rr_path(c->cr, r.l, r.t, r.r - r.l, r.b - r.t, radius);
        cairo_fill(c->cr);
    }
    if (border != PL_NONE) {
        set_col(c->cr, border);
        cairo_set_line_width(c->cr, 1.0);
        rr_path(c->cr, r.l + 0.5, r.t + 0.5, r.r - r.l - 1, r.b - r.t - 1, radius);
        cairo_stroke(c->cr);
    }
}

static void ellipse_path(cairo_t *cr, double l, double t, double r, double b)
{
    cairo_save(cr);
    cairo_translate(cr, (l + r) / 2, (t + b) / 2);
    cairo_scale(cr, (r - l) / 2, (b - t) / 2);
    cairo_new_sub_path(cr);
    cairo_arc(cr, 0, 0, 1, 0, 2 * G_PI);
    cairo_restore(cr);
}

void pl_ellipse(PlCanvas *c, int l, int t, int r, int b, PlColor fill, PlColor border)
{
    if (fill != PL_NONE) {
        set_col(c->cr, fill);
        ellipse_path(c->cr, l, t, r, b);
        cairo_fill(c->cr);
    }
    if (border != PL_NONE) {
        set_col(c->cr, border);
        cairo_set_line_width(c->cr, 1.0);
        ellipse_path(c->cr, l + 0.5, t + 0.5, r - 0.5, b - 0.5);
        cairo_stroke(c->cr);
    }
}

void pl_image(PlCanvas* c, int x, int y, int w, int h,
  int sw, int sh, const unsigned char* rgba)
{
  cairo_surface_t* target, * s;
  double scx = 1.0, scy = 1.0;
  int dw, dh, stride, dx, dy;
  unsigned char* dst;

  if (!rgba || w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
  g_printerr("pl_image [built %s %s]: src %dx%d -> box %dx%d\n",
    __DATE__, __TIME__, sw, sh, w, h);

  target = cairo_get_target(c->cr);
  cairo_surface_get_device_scale(target, &scx, &scy);
  if (scx < 1.0) scx = 1.0;
  if (scy < 1.0) scy = 1.0;
  dw = (int)(w * scx + 0.5);
  dh = (int)(h * scy + 0.5);

  s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dw, dh);
  if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(s);
    return;
  }
  cairo_surface_flush(s);
  dst = cairo_image_surface_get_data(s);
  stride = cairo_image_surface_get_stride(s);

  for (dy = 0; dy < dh; ++dy) {
    int sy0 = dy * sh / dh;
    int sy1 = ((dy + 1) * sh + dh - 1) / dh;
    uint32_t* row = (uint32_t*)(dst + (size_t)dy * stride);
    if (sy1 <= sy0) sy1 = sy0 + 1;
    if (sy1 > sh) sy1 = sh;
    for (dx = 0; dx < dw; ++dx) {
      int sx0 = dx * sw / dw;
      int sx1 = ((dx + 1) * sw + dw - 1) / dw;
      unsigned long sa = 0, sr = 0, sg = 0, sb = 0;
      int n, sx, sy;
      if (sx1 <= sx0) sx1 = sx0 + 1;
      if (sx1 > sw) sx1 = sw;
      n = (sx1 - sx0) * (sy1 - sy0);
      for (sy = sy0; sy < sy1; ++sy)
        for (sx = sx0; sx < sx1; ++sx) {
          const unsigned char* p = rgba + ((size_t)sy * sw + sx) * 4;
          sa += p[3];
          sr += (unsigned long)p[0] * p[3];
          sg += (unsigned long)p[1] * p[3];
          sb += (unsigned long)p[2] * p[3];
        }
      /* Cairo ARGB32: premultiplied, native-endian */
      row[dx] = ((uint32_t)(sa / n) << 24)
        | ((uint32_t)(sr / (255UL * n)) << 16)
        | ((uint32_t)(sg / (255UL * n)) << 8)
        | (uint32_t)(sb / (255UL * n));
    }
  }
  cairo_surface_mark_dirty(s);
  cairo_surface_set_device_scale(s, scx, scy);

  cairo_save(c->cr);
  cairo_set_source_surface(c->cr, s, x, y);
  cairo_pattern_set_filter(cairo_get_source(c->cr), CAIRO_FILTER_NEAREST);
  cairo_rectangle(c->cr, x, y, w, h);
  cairo_fill(c->cr);
  cairo_restore(c->cr);
  cairo_surface_destroy(s);
}

void pl_frame_corners(PlCanvas* c, int w, int h, int rr, PlColor bg, PlColor border)
{
  int W = w - 1, H = h - 1;
  int i;
  if (W < 2 * rr || H < 2 * rr) return;

  /* outer 1px strip, then the straight border edges */
  pl_fill(c, W, 0, w, h, bg);
  pl_fill(c, 0, H, w, h, bg);
  pl_fill(c, rr, 0, W - rr, 1, border);
  pl_fill(c, rr, H - 1, W - rr, H, border);
  pl_fill(c, 0, rr, 1, H - rr, border);
  pl_fill(c, W - 1, rr, W, H - rr, border);

  /* four corners: paint bg outside the arc, then the 1px border ring */
  for (i = 0; i < 4; ++i) {
    int cx = (i & 1) ? W - rr : 0;
    int cy = (i & 2) ? H - rr : 0;
    cairo_save(c->cr);
    cairo_rectangle(c->cr, cx, cy, rr, rr);
    cairo_clip(c->cr);

    cairo_set_fill_rule(c->cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_rectangle(c->cr, cx, cy, rr, rr);
    rr_path(c->cr, 0, 0, W, H, rr);
    set_col(c->cr, bg);
    cairo_fill(c->cr);

    set_col(c->cr, border);
    cairo_set_line_width(c->cr, 1.0);
    rr_path(c->cr, 0.5, 0.5, W - 1, H - 1, rr - 0.5);
    cairo_stroke(c->cr);
    cairo_restore(c->cr);
  }
}

void pl_text(PlCanvas *c, PlFont *f, PlRect r, const char *s, PlColor col, int flags)
{
    PangoLayout *l = pango_cairo_create_layout(c->cr);
    int tw = 0, th = 0, bw = r.r - r.l;
    double x, y;

    pango_layout_set_font_description(l, f->desc);
    pango_layout_set_text(l, s, -1);
    if (flags & PL_TEXT_ELLIPSIS) {
        pango_layout_set_width(l, bw * PANGO_SCALE);
        pango_layout_set_ellipsize(l, PANGO_ELLIPSIZE_END);
    }
    pango_layout_get_pixel_size(l, &tw, &th);
    x = r.l;
    if (flags & PL_TEXT_CENTER) x = r.l + (bw - tw) / 2.0;
    y = r.t + ((r.b - r.t) - th) / 2.0;
    set_col(c->cr, col);
    cairo_move_to(c->cr, x, y);
    pango_cairo_show_layout(c->cr, l);
    g_object_unref(l);
}
struct PlWindow {
  GtkWidget* win, * sw, * fixed;
  void (*on_size)(void*, int, int);
  void (*on_close)(void*);
  void* user;
  GMainLoop* loop;
};

static gboolean pl_win_delete(GtkWidget* w, GdkEvent* e, gpointer p)
{
  PlWindow* pw = (PlWindow*)p;
  (void)w; (void)e;
  if (pw->on_close) pw->on_close(pw->user); else pl_window_close(pw);
  return TRUE;
}

static void pl_win_alloc(GtkWidget* w, GdkRectangle* a, gpointer p)
{
  PlWindow* pw = (PlWindow*)p;
  (void)w;
  if (pw->on_size) pw->on_size(pw->user, a->width, a->height);
}

PlWindow* pl_window_create(void* owner, const char* title, int w, int h, int minw, int minh,
  void (*on_size)(void*, int, int), void (*on_close)(void*), void* user)
{
  PlWindow* pw = (PlWindow*)g_malloc0(sizeof * pw);
  pw->on_size = on_size; pw->on_close = on_close; pw->user = user;
  pw->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(pw->win), title ? title : "");
  gtk_window_set_default_size(GTK_WINDOW(pw->win), w, h);
  gtk_widget_set_size_request(pw->win, minw, minh);
  if (owner && GTK_IS_WINDOW((GtkWidget*)owner)) {
    gtk_window_set_transient_for(GTK_WINDOW(pw->win), GTK_WINDOW((GtkWidget*)owner));
    gtk_window_set_modal(GTK_WINDOW(pw->win), TRUE);
  }
  pw->sw = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pw->sw), GTK_POLICY_EXTERNAL, GTK_POLICY_EXTERNAL);
  pw->fixed = gtk_fixed_new();
  gtk_container_add(GTK_CONTAINER(pw->sw), pw->fixed);
  gtk_container_add(GTK_CONTAINER(pw->win), pw->sw);
  g_signal_connect(pw->win, "delete-event", G_CALLBACK(pl_win_delete), pw);
  g_signal_connect(pw->sw, "size-allocate", G_CALLBACK(pl_win_alloc), pw);
  return pw;
}

void* pl_window_client(PlWindow* pw) { return pw ? pw->fixed : NULL; }

void pl_window_run_modal(PlWindow* pw)
{
  if (!pw) return;
  gtk_widget_show_all(pw->win);
  pw->loop = g_main_loop_new(NULL, FALSE);
  g_main_loop_run(pw->loop);
  g_main_loop_unref(pw->loop);
  pw->loop = NULL;
}

void pl_window_close(PlWindow* pw)
{
  if (!pw) return;
  if (pw->loop && g_main_loop_is_running(pw->loop)) g_main_loop_quit(pw->loop);
  gtk_widget_hide(pw->win);
}

void pl_window_destroy(PlWindow* pw)
{
  if (!pw) return;
  if (pw->win) gtk_widget_destroy(pw->win);
  g_free(pw);
}

#endif /* !_WIN32 && !__APPLE__ */