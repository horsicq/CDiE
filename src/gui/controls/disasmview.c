#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "platform/platform.h"
#include "theme/theme.h"
#include "controls/disasmview.h"

#define ROW_H      20
#define HEAD_H     30
#define HEAD_TOP   6
#define HEAD_GAP   4
#define PAD        16
#define SB_W       6
#define MAX_ROWS   256
#define MAX_HIST   32
#define SHOW_BYTES 8

struct DisasmView {
  PlWidget* w;
  PlFont* mono, * ui;
  int own_mono;
  int W, H, cw;
  int open, has_sel;
  uint64_t size, lo, hi, top, sel;
  int x_addr, x_bytes, x_ins, x_end, y0, vis_rows, addr_digits;
  PlRect r_sb;
  uint64_t rows[MAX_ROWS];
  unsigned lens[MAX_ROWS];
  int nrows;
  int hov, over, dragging;
  uint64_t hist[MAX_HIST];
  int nhist;
  DisasmStatusFn on_status;
  void* st_user;
};

static PlRect mkr(int l, int t, int r, int b) { PlRect x; x.l = l; x.t = t; x.r = r; x.b = b; return x; }
static unsigned long long ull(uint64_t v) { return (unsigned long long)v; }

static void redraw(DisasmView* dv)
{
  PlRect r = mkr(0, 0, dv->W, dv->H);
  pl_invalidate(dv->w, &r);
}

static void status(DisasmView* dv, const char* t)
{
  if (dv->on_status) dv->on_status(t, dv->st_user);
}

static unsigned insn_len(DisasmView* dv, uint64_t off, DasmLine* l)
{
  DasmLine tmp;
  if (!l) l = &tmp;
  if (!dasm_decode_at(off, l)) { memset(l, 0, sizeof *l); l->len = 1; }
  if (l->len < 1) l->len = 1;
  if (off + l->len > dv->hi) l->len = (unsigned)(dv->hi > off ? dv->hi - off : 1);
  if (l->len < 1) l->len = 1;
  return l->len;
}

static uint64_t prev_start(DisasmView* dv, uint64_t at)
{
  unsigned k;
  if (at <= dv->lo) return dv->lo;
  for (k = 0; k < 16; ++k) {
    uint64_t base = (at > dv->lo + 48 + k) ? at - 48 - k : dv->lo;
    uint64_t p = base, last = base;
    while (p < at) { last = p; p += insn_len(dv, p, NULL); }
    if (p == at) return last;
    if (base == dv->lo) break;
  }
  return at - 1;
}

static void build_rows(DisasmView* dv)
{
  uint64_t p = dv->top;
  int i;
  dv->nrows = 0;
  if (!dv->open) return;
  for (i = 0; i < dv->vis_rows && i < MAX_ROWS && p < dv->hi; ++i) {
    unsigned n = insn_len(dv, p, NULL);
    dv->rows[i] = p; dv->lens[i] = n;
    p += n;
    dv->nrows = i + 1;
  }
}

static void scroll_by(DisasmView* dv, int n)
{
  while (n > 0) {
    uint64_t next = dv->top + insn_len(dv, dv->top, NULL);
    if (next >= dv->hi) break;
    dv->top = next; --n;
  }
  while (n < 0 && dv->top > dv->lo) { dv->top = prev_start(dv, dv->top); ++n; }
}

static void set_range_for(DisasmView* dv, uint64_t off)
{
  uint64_t lo, hi;
  if (dasm_section_of(off, &lo, &hi) && hi > lo) { dv->lo = lo; dv->hi = hi; }
  else { dv->lo = 0; dv->hi = dv->size; }
}

static void keep_sel_visible(DisasmView* dv)
{
  build_rows(dv);
  if (dv->sel < dv->top) { dv->top = dv->sel; return; }
  if (dv->nrows && dv->sel >= dv->rows[dv->nrows - 1] + dv->lens[dv->nrows - 1]) {
    uint64_t t = dv->sel;
    int i;
    for (i = 0; i < dv->vis_rows - 1; ++i) t = prev_start(dv, t);
    dv->top = t;
  }
}

static void select_off(DisasmView* dv, uint64_t off)
{
  if (off < dv->lo) off = dv->lo;
  if (off >= dv->hi) off = prev_start(dv, dv->hi);
  dv->sel = off;
  dv->has_sel = 1;
  keep_sel_visible(dv);
  redraw(dv);
}

static void layout(DisasmView* dv)
{
  int cw = pl_text_width(dv->mono, "0");
  dv->cw = cw < 1 ? 8 : cw;
  dv->addr_digits = (dasm_arch() == DASM_ARCH_X86_64) ? 16 : 8;
  dv->x_addr = PAD;
  dv->x_bytes = dv->x_addr + (dv->addr_digits + 3) * dv->cw;
  dv->x_ins = dv->x_bytes + (SHOW_BYTES * 3 + 3) * dv->cw;
  dv->x_end = dv->W - PAD - SB_W - 4;
  dv->y0 = HEAD_H + HEAD_GAP;
  dv->vis_rows = (dv->H - dv->y0 - 4) / ROW_H;
  if (dv->vis_rows < 1) dv->vis_rows = 1;
  if (dv->vis_rows > MAX_ROWS) dv->vis_rows = MAX_ROWS;
  dv->r_sb = mkr(dv->W - SB_W - 4, dv->y0, dv->W - 4, dv->H - 4);
}

static void paint_header(DisasmView* dv, PlCanvas* c)
{
  pl_text(c, dv->mono, mkr(dv->x_addr, HEAD_TOP, dv->x_bytes, HEAD_H), "Address", CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
  pl_text(c, dv->mono, mkr(dv->x_bytes, HEAD_TOP, dv->x_ins, HEAD_H), "Bytes", CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
  pl_text(c, dv->mono, mkr(dv->x_ins, HEAD_TOP, dv->x_end, HEAD_H), "Instruction", CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
  pl_line(c, dv->x_addr, HEAD_H, dv->x_end, HEAD_H, CLR_SEPARATOR);
}

static void paint_rows(DisasmView* dv, PlCanvas* c)
{
  int r;
  if (!dv->open) {
    pl_text(c, dv->ui, mkr(dv->x_addr, dv->y0 + 8, dv->x_end, dv->y0 + 8 + ROW_H),
      "Scan an x86 or x64 PE file to see its disassembly.", CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
    return;
  }
  build_rows(dv);
  for (r = 0; r < dv->nrows; ++r) {
    DasmLine l;
    char addr[24], bytes[64], ins[200], tgt[32];
    unsigned i, shown;
    int y = dv->y0 + r * ROW_H;
    int is_sel = dv->has_sel && dv->rows[r] == dv->sel;
    int is_branch;
    const char* ops;
    size_t pos = 0;

    insn_len(dv, dv->rows[r], &l);
    if (is_sel) pl_fill(c, PAD / 2, y, dv->x_end + 4, y + ROW_H, CLR_SIDEBAR_SEL);
    else if (dv->hov == r) pl_fill(c, PAD / 2, y, dv->x_end + 4, y + ROW_H, CLR_SIDEBAR_HOVER);

    snprintf(addr, sizeof addr, "%0*llX", dv->addr_digits, ull(l.va));
    pl_text(c, dv->mono, mkr(dv->x_addr, y, dv->x_bytes, y + ROW_H), addr,
      is_sel ? CLR_ACCENT : CLR_TEXT_SECONDARY, PL_TEXT_LEFT);

    shown = l.len < SHOW_BYTES ? l.len : SHOW_BYTES;
    for (i = 0; i < shown; ++i)
      pos += (size_t)snprintf(bytes + pos, sizeof bytes - pos, "%s%02X", i ? " " : "", l.bytes[i]);
    if (l.len > SHOW_BYTES) snprintf(bytes + pos, sizeof bytes - pos, "..");
    pl_text(c, dv->mono, mkr(dv->x_bytes, y, dv->x_ins, y + ROW_H), bytes,
      CLR_TEXT_SECONDARY, PL_TEXT_ELLIPSIS);

    ops = l.operands;
    if (!ops[0] && l.target) { snprintf(tgt, sizeof tgt, "0x%llX", ull(l.target)); ops = tgt; }
    is_branch = l.target != 0;
    snprintf(ins, sizeof ins, "%-7s %s", l.mnemonic, ops);
    pl_text(c, dv->mono, mkr(dv->x_ins, y, dv->x_end, y + ROW_H), ins,
      !l.valid ? CLR_TEXT_SECONDARY : (is_branch ? CLR_ACCENT : CLR_TEXT_PRIMARY), PL_TEXT_ELLIPSIS);
  }
}

static void paint_scroll(DisasmView* dv, PlCanvas* c)
{
  int track, th, ty;
  uint64_t range;
  double frac;
  if (!dv->open || !dv->over) return;
  range = dv->hi - dv->lo;
  if (range < 2) return;
  track = dv->r_sb.b - dv->r_sb.t;
  th = (int)((double)track * (double)(dv->vis_rows * 4) / (double)range);
  if (th < 18) th = 18;
  if (th > track) th = track;
  frac = (double)(dv->top - dv->lo) / (double)range;
  ty = dv->r_sb.t + (int)((track - th) * frac);
  pl_round_rect(c, mkr(dv->r_sb.l, ty, dv->r_sb.r, ty + th), CLR_BTN_GHOST_BD, CLR_BTN_GHOST_BD, 3);
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
  DisasmView* dv = (DisasmView*)u;
  dv->W = w; dv->H = h;
  layout(dv);
  pl_fill(c, 0, 0, w, h, CLR_BG);
  paint_header(dv, c);
  paint_rows(dv, c);
  paint_scroll(dv, c);
}

static int row_at(DisasmView* dv, int x, int y)
{
  int r;
  if (!dv->open || y < dv->y0 || x >= dv->r_sb.l) return -1;
  r = (y - dv->y0) / ROW_H;
  return (r >= 0 && r < dv->nrows) ? r : -1;
}

static void scroll_to_y(DisasmView* dv, int y)
{
  double f = (double)(y - dv->r_sb.t) / (double)(dv->r_sb.b - dv->r_sb.t);
  uint64_t off;
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  off = dv->lo + (uint64_t)(f * (double)(dv->hi - dv->lo));
  if (off >= dv->hi) off = dv->hi - 1;
  dv->top = prev_start(dv, off + 1);
  redraw(dv);
}

static void on_mouse_move(void* u, int x, int y)
{
  DisasmView* dv = (DisasmView*)u;
  int h, o;
  if (dv->dragging) {
#ifdef _WIN32
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) dv->dragging = 0;
#endif
    if (dv->dragging) { scroll_to_y(dv, y); return; }
  }
  h = row_at(dv, x, y);
  o = (y >= dv->y0);
  if (h != dv->hov || o != dv->over) { dv->hov = h; dv->over = o; redraw(dv); }
}

static void on_mouse_leave(void* u)
{
  DisasmView* dv = (DisasmView*)u;
  if (dv->hov != -1 || dv->over) { dv->hov = -1; dv->over = 0; redraw(dv); }
}

static void on_mouse_down(void* u, int x, int y)
{
  DisasmView* dv = (DisasmView*)u;
  int r;
  if (!dv->open) return;
  dv->dragging = 0;
  if (x >= dv->r_sb.l - 4 && y >= dv->r_sb.t && y < dv->r_sb.b) {
    dv->dragging = 1;
    scroll_to_y(dv, y);
    return;
  }
  r = row_at(dv, x, y);
  if (r >= 0) select_off(dv, dv->rows[r]);
}

static int on_hand(void* u, int x, int y)
{
  DisasmView* dv = (DisasmView*)u;
  return row_at(dv, x, y) >= 0;
}

static void on_wheel(void* u, int x, int y, int delta, int mods)
{
  DisasmView* dv = (DisasmView*)u;
  (void)x; (void)y; (void)mods;
  if (!dv->open || !delta) return;
  scroll_by(dv, delta > 0 ? -3 : 3);
  redraw(dv);
}

static void follow_branch(DisasmView* dv)
{
  DasmLine l;
  uint64_t off;
  if (!dv->has_sel) return;
  insn_len(dv, dv->sel, &l);
  if (!l.target) { status(dv, "This instruction has no branch target."); return; }
  if (!dasm_va_to_off(l.target, &off)) { status(dv, "Branch target is outside the file image."); return; }
  if (dv->nhist == MAX_HIST) {
    memmove(dv->hist, dv->hist + 1, (MAX_HIST - 1) * sizeof dv->hist[0]);
    dv->nhist = MAX_HIST - 1;
  }
  dv->hist[dv->nhist++] = dv->sel;
  disasmview_goto_offset(dv, off);
}

static void go_back(DisasmView* dv)
{
  if (dv->nhist <= 0) return;
  disasmview_goto_offset(dv, dv->hist[--dv->nhist]);
}

static void on_key(void* u, int key, uint32_t ch, int mods)
{
  DisasmView* dv = (DisasmView*)u;
  uint64_t at;
  int i;
  (void)ch; (void)mods;
  if (!dv->open) return;
  at = dv->has_sel ? dv->sel : dv->top;

  switch (key) {
  case PL_KEY_UP:   select_off(dv, prev_start(dv, at)); break;
  case PL_KEY_DOWN: select_off(dv, at + insn_len(dv, at, NULL)); break;
  case PL_KEY_PGUP:
    for (i = 0; i < dv->vis_rows; ++i) at = prev_start(dv, at);
    select_off(dv, at); break;
  case PL_KEY_PGDN:
    for (i = 0; i < dv->vis_rows && at < dv->hi; ++i) at += insn_len(dv, at, NULL);
    select_off(dv, at); break;
  case PL_KEY_HOME: dv->top = dv->lo; select_off(dv, dv->lo); break;
  case PL_KEY_END:  select_off(dv, prev_start(dv, dv->hi)); break;
  case PL_KEY_ENTER: follow_branch(dv); break;
  case PL_KEY_BACK:  go_back(dv); break;
  default: break;
  }
}

static void on_resize(void* u, int w, int h)
{
  DisasmView* dv = (DisasmView*)u;
  dv->W = w; dv->H = h;
  layout(dv);
  redraw(dv);
}

static void on_destroy(void* u)
{
  DisasmView* dv = (DisasmView*)u;
  if (dv->own_mono && dv->mono) pl_font_destroy(dv->mono);
  if (dv->ui) pl_font_destroy(dv->ui);
  dasm_close();
  free(dv);
}

static const PlWidgetOps s_ops = {
  on_paint, on_mouse_move, on_mouse_leave, on_mouse_down,
  on_resize, on_hand, on_destroy,
  on_key, on_wheel
};

/* ---- public API ---- */

DisasmView* disasmview_create(void* parent_native, PlFont* mono_font, int x, int y, int w, int h)
{
  DisasmView* dv = (DisasmView*)calloc(1, sizeof(*dv));
  if (!dv) return NULL;
  dv->W = w; dv->H = h; dv->hov = -1;
  dv->ui = pl_font_create(9, 0);
  if (mono_font) dv->mono = mono_font;
  else {
#ifdef PL_HAS_FONT_FAMILY
#if defined(_WIN32)
    dv->mono = pl_font_create_family("Consolas", 11, 0);
#elif defined(__APPLE__)
    dv->mono = pl_font_create_family("Menlo", 11, 0);
#else
    dv->mono = pl_font_create_family("DejaVu Sans Mono", 11, 0);
#endif
    if (!dv->mono) dv->mono = pl_font_create(11, 0);
#else
    dv->mono = pl_font_create(11, 0);
#endif
    dv->own_mono = 1;
  }
  if (!dv->ui || !dv->mono) {
    if (dv->own_mono && dv->mono) pl_font_destroy(dv->mono);
    if (dv->ui) pl_font_destroy(dv->ui);
    free(dv);
    return NULL;
  }
  layout(dv);
  dv->w = pl_widget_create(parent_native, &s_ops, dv, x, y, w, h);
  if (!dv->w) {
    if (dv->own_mono) pl_font_destroy(dv->mono);
    pl_font_destroy(dv->ui);
    free(dv);
    return NULL;
  }
  return dv;
}

void disasmview_destroy(DisasmView* dv)
{
  if (dv && dv->w) pl_widget_destroy(dv->w);
}

PlWidget* disasmview_widget(DisasmView* dv) { return dv ? dv->w : NULL; }

void disasmview_set_rect(DisasmView* dv, int x, int y, int w, int h)
{
  if (!dv) return;
  dv->W = w; dv->H = h;
  pl_widget_set_rect(dv->w, x, y, w, h);
  layout(dv);
  redraw(dv);
}

void disasmview_close(DisasmView* dv)
{
  if (!dv) return;
  dasm_close();
  dv->open = 0; dv->has_sel = 0; dv->nrows = 0; dv->nhist = 0;
  dv->size = dv->lo = dv->hi = dv->top = dv->sel = 0;
  redraw(dv);
}

void disasmview_goto_offset(DisasmView* dv, uint64_t off)
{
  if (!dv || !dv->open || off >= dv->size) return;
  if (off < dv->lo || off >= dv->hi) set_range_for(dv, off);
  dv->sel = off; dv->has_sel = 1;
  dv->top = off;
  /* Show a few instructions of context above the target. */
  { int i; for (i = 0; i < 3 && dv->top > dv->lo; ++i) dv->top = prev_start(dv, dv->top); }
  redraw(dv);
}

void disasmview_goto_entry(DisasmView* dv)
{
  if (dv && dv->open) disasmview_goto_offset(dv, dasm_entry_offset());
}

int disasmview_open(DisasmView* dv, DasmRead rd, void* user, uint64_t file_size)
{
  if (!dv) return 0;
  disasmview_close(dv);
  if (!dasm_open(rd, user, file_size)) {
    status(dv, "Disassembly supports x86 and x64 PE files only.");
    return 0;
  }
  dv->size = file_size;
  dv->lo = 0; dv->hi = file_size;
  dv->open = 1;
  layout(dv);
  disasmview_goto_entry(dv);
  return 1;
}

void disasmview_set_status_callback(DisasmView* dv, DisasmStatusFn fn, void* user)
{
  if (!dv) return;
  dv->on_status = fn;
  dv->st_user = user;
}
