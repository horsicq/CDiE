#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "platform/platform.h"
#include "theme/theme.h"
#include "controls/hexviewer.h"

#define TB_H        44      /* toolbar height */
#define EDIT_H      28
#define ROW_H       20
#define HEAD_H      34      /* column header band (separator line sits at its bottom) */
#define HEAD_TOP    8       /* top padding above the header text */
#define HEAD_GAP    4       /* gap between header separator and first row */
#define INSP_W      250     /* data inspector width */
#define INSP_ROW    28
#define INSP_TITLE  22      /* advance after a section title */
#define INSP_SECT   14      /* extra gap between sections */
#define LEG_ROW     23
#define PAD         20      /* toolbar side padding */
#define GRID_PAD    16      /* hex grid left padding */
#define EDGE        16      /* gap between grid and the inspector / scrollbar */
#define SB_W        6
#define GOTO_W      104
#define BPR_W       48
#define ENC_W       74
#define COPY_W      54
#define NAV_W       24
#define COPY_MAX    (16u * 1024u * 1024u)

enum { F_VIEW, F_GOTO, F_FIND };
enum { H_NONE = 0, H_GOTO, H_FIND, H_PREV, H_NEXT, H_BPR, H_ENC, H_COPY };

static const unsigned s_cols[] = { 8, 16, 24, 32, 48, 64 };
static const char* s_enc[] = { "ANSI", "ASCII" };

typedef struct { char* name; uint64_t off, len; PlColor bg, fg; } Reg;

struct HexViewer {
  PlWidget* w;
  PlFont* mono, * ui;
  int own_mono;
  int W, H;
  const unsigned char* mem;
  HexReadFn read;
  void* ruser;
  uint64_t size, base;
  unsigned cols;
  int enc;
  uint64_t scroll, cursor, anchor;
  int has_cursor;
  int cw, addr_digits;
  int x_off, x_hex, x_asc, x_end, hex_right, y0, vis_rows, show_insp;
  int x_lbl_goto, x_lbl_find, x_lbl_bpr, x_lbl_enc;
  PlRect r_goto, r_find, r_prev, r_next, r_bpr, r_enc, r_copy, r_sb, r_insp;
  int focus, hov, over;
  char goto_txt[32], find_txt[128];
  unsigned char* buf;
  size_t bufcap;
  Reg* reg;
  size_t nreg;
  HexSelectFn on_select;
  HexCopyFn on_copy;
  void* cb_user;
  HexStatusFn on_status;
  void* st_user;
  HexStatusFn on_info;
  void* info_user;
  int caret_on;
  unsigned long blink_t;
};

static PlRect mkr(int l, int t, int r, int b) { PlRect x; x.l = l; x.t = t; x.r = r; x.b = b; return x; }
static int inr(const PlRect* r, int x, int y) { return x >= r->l && x < r->r && y >= r->t && y < r->b; }
static uint64_t u64min(uint64_t a, uint64_t b) { return a < b ? a : b; }
static unsigned long long ull(uint64_t v) { return (unsigned long long)v; }

static void redraw(HexViewer* hv)
{
  PlRect r = mkr(0, 0, hv->W, hv->H);
  pl_invalidate(hv->w, &r);
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

static void status(HexViewer* hv, const char* fmt, ...)
{
  char t[200];
  va_list ap;
  if (!hv->on_status) return;
  va_start(ap, fmt);
  vsnprintf(t, sizeof(t), fmt, ap);
  va_end(ap);
  hv->on_status(t, hv->st_user);
}

static void info(HexViewer* hv)
{
  char t[96];
  if (!hv->on_info) return;
  if (hv->size)
    snprintf(t, sizeof(t), "Range 0x%llX \xE2\x80\x93 0x%llX \xC2\xB7 %s",
      ull(hv->base), ull(hv->base + hv->size - 1), s_enc[hv->enc]);
  else
    snprintf(t, sizeof(t), "No data \xC2\xB7 %s", s_enc[hv->enc]);
  hv->on_info(t, hv->info_user);
}

static int hexval(int ch)
{
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
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

static int glyph(const HexViewer* hv, unsigned char b, char out[4])
{
  if (b >= 32 && b <= 126) { out[0] = (char)b; out[1] = 0; return 1; }
  if (hv->enc == 0 && b >= 0xA1) {
    out[0] = (char)(0xC0 | (b >> 6)); out[1] = (char)(0x80 | (b & 63)); out[2] = 0;
    return 1;
  }
  out[0] = '.'; out[1] = 0;
  return 0;
}

static int fetch(HexViewer* hv, uint64_t off, void* dst, size_t len)
{
  if (!len) return 1;
  if (off > hv->size || len > hv->size - off) return 0;
  if (hv->mem) { memcpy(dst, hv->mem + off, len); return 1; }
  if (hv->read) return hv->read(hv->ruser, off, dst, len);
  return 0;
}

static uint64_t total_rows(const HexViewer* hv) { return (hv->size + hv->cols - 1) / hv->cols; }

static uint64_t max_scroll(const HexViewer* hv)
{
  uint64_t t = total_rows(hv);
  return t > (uint64_t)hv->vis_rows ? t - (uint64_t)hv->vis_rows : 0;
}

static void clamp_scroll(HexViewer* hv)
{
  uint64_t m = max_scroll(hv);
  if (hv->scroll > m) hv->scroll = m;
}

static int reg_at(const HexViewer* hv, uint64_t idx)
{
  int found = -1;
  size_t i;
  for (i = 0; i < hv->nreg; ++i)
    if (idx >= hv->reg[i].off && idx - hv->reg[i].off < hv->reg[i].len) found = (int)i;
  return found;
}

static int cell_w(PlFont* f)
{
  static const char hexd[] = "0123456789ABCDEF";
  int i, m = 1;
  char s[2] = { 0, 0 };
  for (i = 0; hexd[i]; ++i) {
    int w;
    s[0] = hexd[i];
    w = pl_text_width(f, s);
    if (w > m) m = w;
  }
  return m;
}

static int hx(const HexViewer* hv, unsigned i)
{
  return hv->x_hex + (int)i * 3 * hv->cw + (int)(i / 8) * hv->cw;
}

static int ax(const HexViewer* hv, unsigned i) { return hv->x_asc + (int)i * hv->cw; }

static void layout(HexViewer* hv)
{
  int W = hv->W, H = hv->H, ty = (TB_H - EDIT_H) / 2, gap = 8;
  int x, lx, fx, fw, right_limit, need;
  int wl_goto, wl_find, wl_bpr, wl_enc;

  hv->cw = cell_w(hv->mono);
  if (hv->cw < 1) hv->cw = 8;
  wl_goto = pl_text_width(hv->ui, "Go to");
  wl_find = pl_text_width(hv->ui, "Find");
  wl_bpr = pl_text_width(hv->ui, "Bytes / row");
  wl_enc = pl_text_width(hv->ui, "Encoding");

  x = W - PAD;
  hv->r_copy = mkr(x - COPY_W, ty, x, ty + EDIT_H); x -= COPY_W + gap;
  hv->r_enc = mkr(x - ENC_W, ty, x, ty + EDIT_H);   x -= ENC_W + gap;
  hv->x_lbl_enc = x - wl_enc;                       x = hv->x_lbl_enc - 12;
  hv->r_bpr = mkr(x - BPR_W, ty, x, ty + EDIT_H);   x -= BPR_W + gap;
  hv->x_lbl_bpr = x - wl_bpr;
  right_limit = hv->x_lbl_bpr - 16;

  lx = PAD;
  hv->x_lbl_goto = lx; lx += wl_goto + gap;
  hv->r_goto = mkr(lx, ty, lx + GOTO_W, ty + EDIT_H); lx += GOTO_W + 16;
  hv->x_lbl_find = lx; lx += wl_find + gap;
  fx = lx;
  fw = right_limit - fx - gap - (2 * NAV_W + gap);
  if (fw < 60) fw = 60;
  hv->r_find = mkr(fx, ty, fx + fw, ty + EDIT_H);
  hv->r_prev = mkr(fx + fw + gap, ty, fx + fw + gap + NAV_W, ty + EDIT_H);
  hv->r_next = mkr(hv->r_prev.r + gap, ty, hv->r_prev.r + gap + NAV_W, ty + EDIT_H);

  hv->addr_digits = (hv->base + hv->size > 0xFFFFFFFFULL) ? 16 : 8;
  hv->x_off = GRID_PAD;
  hv->x_hex = hv->x_off + (hv->addr_digits + 4) * hv->cw;
  hv->x_asc = hx(hv, hv->cols - 1) + 2 * hv->cw + (5 * hv->cw) / 2;
  need = hv->x_asc + (int)hv->cols * hv->cw + EDGE;
  hv->show_insp = (W - INSP_W >= need);
  hv->hex_right = hv->show_insp ? W - INSP_W : W;
  hv->x_end = hv->hex_right - EDGE;     
  hv->r_insp = mkr(W - INSP_W, TB_H + 1, W, H);
  hv->y0 = TB_H + 1 + HEAD_H + HEAD_GAP;
  hv->vis_rows = (H - hv->y0 - 4) / ROW_H;
  if (hv->vis_rows < 1) hv->vis_rows = 1;
  hv->r_sb = mkr(hv->hex_right - SB_W - 4, hv->y0, hv->hex_right - 4, H - 4);
  clamp_scroll(hv);
}

static int hit_ctrl(const HexViewer* hv, int x, int y)
{
  if (y >= TB_H) return H_NONE;
  if (inr(&hv->r_goto, x, y)) return H_GOTO;
  if (inr(&hv->r_find, x, y)) return H_FIND;
  if (inr(&hv->r_prev, x, y)) return H_PREV;
  if (inr(&hv->r_next, x, y)) return H_NEXT;
  if (inr(&hv->r_bpr, x, y)) return H_BPR;
  if (inr(&hv->r_enc, x, y)) return H_ENC;
  if (inr(&hv->r_copy, x, y)) return H_COPY;
  return H_NONE;
}

static int hit_byte(HexViewer* hv, int x, int y, uint64_t* off)
{
  int row, col = -1;
  unsigned i;
  uint64_t o;
  if (y < hv->y0) return 0;
  row = (y - hv->y0) / ROW_H;
  if (row >= hv->vis_rows) return 0;
  if (x >= hv->x_asc && x < hv->x_asc + (int)hv->cols * hv->cw)
    col = (x - hv->x_asc) / hv->cw;
  else
    for (i = 0; i < hv->cols; ++i)
      if (x >= hx(hv, i) - hv->cw / 2 && x < hx(hv, i) + 2 * hv->cw + hv->cw / 2) { col = (int)i; break; }
  if (col < 0) return 0;
  o = (hv->scroll + (uint64_t)row) * hv->cols + (uint64_t)col;
  if (o >= hv->size) return 0;
  *off = o;
  return 1;
}

static PlRect sb_hit_rect(const HexViewer* hv)
{
  return mkr(hv->r_sb.l - 4, hv->r_sb.t, hv->r_sb.r + 4, hv->r_sb.b);
}

static void sel_range(const HexViewer* hv, uint64_t* lo, uint64_t* hi)
{
  *lo = hv->anchor < hv->cursor ? hv->anchor : hv->cursor;
  *hi = hv->anchor < hv->cursor ? hv->cursor : hv->anchor;
}

static void notify(HexViewer* hv)
{
  uint64_t lo, hi;
  int ri;
  char t[200];
  if (!hv->has_cursor) {
    if (hv->on_select) hv->on_select(hv->cb_user, 0, 0);
    return;
  }
  sel_range(hv, &lo, &hi);
  if (hv->focus != F_GOTO)
    snprintf(hv->goto_txt, sizeof(hv->goto_txt), "0x%08llX", ull(hv->base + lo));
  if (hv->on_select) hv->on_select(hv->cb_user, lo, hi - lo + 1);
  if (hv->on_status) {
    ri = reg_at(hv, lo);
    snprintf(t, sizeof(t), "%llu byte%s selected at 0x%08llX%s%s%s.",
      ull(hi - lo + 1), hi == lo ? "" : "s", ull(hv->base + lo),
      ri >= 0 && hv->reg[ri].name[0] ? " (" : "",
      ri >= 0 ? hv->reg[ri].name : "",
      ri >= 0 && hv->reg[ri].name[0] ? ")" : "");
    hv->on_status(t, hv->st_user);
  }
}

static void ensure_visible(HexViewer* hv, uint64_t off)
{
  uint64_t row = off / hv->cols;
  if (row < hv->scroll) hv->scroll = row;
  else if (row >= hv->scroll + (uint64_t)hv->vis_rows)
    hv->scroll = row - (uint64_t)hv->vis_rows + 1;
  clamp_scroll(hv);
}

static void reveal(HexViewer* hv, uint64_t off)
{
  uint64_t row = off / hv->cols;
  if (row < hv->scroll || row >= hv->scroll + (uint64_t)hv->vis_rows) {
    uint64_t half = (uint64_t)hv->vis_rows / 2;
    hv->scroll = row > half ? row - half : 0;
  }
  clamp_scroll(hv);
}

static void move_to(HexViewer* hv, uint64_t off, int extend)
{
  if (!hv->size) return;
  if (off >= hv->size) off = hv->size - 1;
  hv->cursor = off;
  if (!extend || !hv->has_cursor) hv->anchor = off;
  hv->has_cursor = 1;
  ensure_visible(hv, off);
  notify(hv);
  redraw(hv);
}

static int parse_offset(const char* s, uint64_t* out)
{
  uint64_t v = 0;
  unsigned base = 10;
  int digits = 0;
  while (*s == ' ') ++s;
  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
  for (; *s && *s != ' '; ++s) {
    int d = hexval(*s);
    if (d < 0 || (unsigned)d >= base) return 0;
    if (v > (UINT64_MAX - (unsigned)d) / base) return 0;
    v = v * base + (unsigned)d;
    digits = 1;
  }
  while (*s == ' ') ++s;
  if (!digits || *s) return 0;
  *out = v;
  return 1;
}

static void goto_offset(HexViewer* hv, uint64_t off)
{
  size_t i;
  uint64_t len = 1;
  if (off >= hv->size) return;
  for (i = 0; i < hv->nreg; ++i)
    if (hv->reg[i].off == off && hv->reg[i].len && hv->reg[i].len <= hv->size - off)
      len = hv->reg[i].len;
  reveal(hv, off);
  hexview_set_selection(hv, off, len);
}

static void do_goto(HexViewer* hv)
{
  uint64_t v;
  if (!parse_offset(hv->goto_txt, &v) || v < hv->base || v - hv->base >= hv->size) {
    status(hv, "Invalid offset. Use decimal or 0x hexadecimal inside the file.");
    return;
  }
  hv->focus = F_VIEW;
  goto_offset(hv, v - hv->base);
  redraw(hv);
}

static size_t parse_pattern(const char* s, unsigned char* out, size_t cap)
{
  size_t n = 0, m = 0;
  const char* p = s, * q;
  int ok = 1;
  while (*p == ' ') ++p;
  if (*p == '"') {
    ++p;
    while (*p && *p != '"' && n < cap) out[n++] = (unsigned char)*p++;
    return n;
  }
  q = s;
  while (*q) {
    int h, l;
    while (*q == ' ') ++q;
    if (!*q) break;
    h = hexval(*q++);
    if (!*q || h < 0) { ok = 0; break; }
    l = hexval(*q++);
    if (l < 0) { ok = 0; break; }
    if (m < cap) out[m++] = (unsigned char)((h << 4) | l);
  }
  if (ok && m) return m;
  n = 0;
  while (*p && n < cap) out[n++] = (unsigned char)*p++;
  return n;
}

static int find_fwd(HexViewer* hv, const unsigned char* pat, size_t pl,
  uint64_t from, uint64_t to, uint64_t* hit)
{
  enum { CH = 65536 };
  unsigned char* b;
  uint64_t pos = from;
  int found = 0;
  if (hv->size < pl) return 0;
  if (to > hv->size - pl + 1) to = hv->size - pl + 1;
  b = (unsigned char*)malloc(CH + pl);
  if (!b) return 0;
  while (pos < to && !found) {
    size_t npos = (size_t)u64min(CH, to - pos), len = npos + pl - 1, i;
    if (!fetch(hv, pos, b, len)) break;
    for (i = 0; i < npos; ++i)
      if (b[i] == pat[0] && !memcmp(b + i, pat, pl)) { *hit = pos + i; found = 1; break; }
    pos += npos;
  }
  free(b);
  return found;
}

static int find_bwd(HexViewer* hv, const unsigned char* pat, size_t pl,
  uint64_t lo, uint64_t hi, uint64_t* hit)
{
  enum { CH = 65536 };
  unsigned char* b;
  uint64_t end;
  int found = 0;
  if (hv->size < pl) return 0;
  if (hi > hv->size - pl + 1) hi = hv->size - pl + 1;
  end = hi;
  b = (unsigned char*)malloc(CH + pl);
  if (!b) return 0;
  while (end > lo && !found) {
    uint64_t start = (end - lo > CH) ? end - CH : lo;
    size_t npos = (size_t)(end - start), len = npos + pl - 1, i;
    if (!fetch(hv, start, b, len)) break;
    for (i = npos; i-- > 0;)
      if (b[i] == pat[0] && !memcmp(b + i, pat, pl)) { *hit = start + i; found = 1; break; }
    end = start;
  }
  free(b);
  return found;
}

static void search(HexViewer* hv, int dir)
{
  unsigned char pat[128];
  size_t pl;
  uint64_t lo = 0, hi = 0, hit = 0;
  int found;
  pl = parse_pattern(hv->find_txt, pat, sizeof(pat));
  if (!pl || !hv->size) { status(hv, "Enter hex bytes or text to find."); return; }
  if (hv->has_cursor) sel_range(hv, &lo, &hi);
  if (dir > 0) {
    uint64_t from = hv->has_cursor ? lo + 1 : 0;
    found = find_fwd(hv, pat, pl, from, hv->size, &hit) ||
      find_fwd(hv, pat, pl, 0, from, &hit);
  }
  else {
    uint64_t to = hv->has_cursor ? lo : hv->size;
    found = find_bwd(hv, pat, pl, 0, to, &hit) ||
      find_bwd(hv, pat, pl, to, hv->size, &hit);
  }
  if (!found) { status(hv, "Not found."); redraw(hv); return; }
  reveal(hv, hit);
  hexview_set_selection(hv, hit, pl);
}

static void draw_edit(HexViewer* hv, PlCanvas* c, PlRect r, const char* s,
  const char* hint, int focused, int hover)
{
  PlRect tr = mkr(r.l + 10, r.t, r.r - 8, r.b);
  pl_round_rect(c, r, CLR_SEARCH_BG,
    focused ? CLR_ACCENT : (hover ? CLR_BTN_GHOST_BD : CLR_PANEL_BORDER), 6);
  if (s[0]) pl_text(c, hv->mono, tr, s, CLR_TEXT_PRIMARY, PL_TEXT_ELLIPSIS);
  else pl_text(c, hv->ui, tr, hint, CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
  if (focused && hv->caret_on) {
    int cx = tr.l + pl_text_width(hv->mono, s);
    if (cx > tr.r) cx = tr.r;
    pl_line(c, cx, r.t + 7, cx, r.b - 7, CLR_TEXT_PRIMARY);
  }
}

static void draw_btn(HexViewer* hv, PlCanvas* c, PlRect r, const char* t, int hover)
{
  PlColor fill = hover ? CLR_BTN_GHOST_HOVER : CLR_BTN_GHOST;
  pl_round_rect(c, r, fill, CLR_BTN_GHOST_BD, 6);
  pl_text(c, hv->ui, r, t, CLR_TEXT_PRIMARY, PL_TEXT_CENTER);
}

static void draw_drop(HexViewer* hv, PlCanvas* c, PlRect r, const char* t, int hover)
{
  pl_round_rect(c, r, CLR_SEARCH_BG, hover ? CLR_ACCENT : CLR_PANEL_BORDER, 6);
  pl_text(c, hv->ui, mkr(r.l + 10, r.t, r.r - 8, r.b), t, CLR_TEXT_PRIMARY, PL_TEXT_LEFT);
}

static void label(HexViewer* hv, PlCanvas* c, int x, const char* t)
{
  pl_text(c, hv->ui, mkr(x, 0, x + pl_text_width(hv->ui, t) + 4, TB_H), t,
    CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
}

static void paint_toolbar(HexViewer* hv, PlCanvas* c)
{
  char bpr[16];
  label(hv, c, hv->x_lbl_goto, "Go to");
  draw_edit(hv, c, hv->r_goto, hv->goto_txt, "0x00000000", hv->focus == F_GOTO, hv->hov == H_GOTO);
  label(hv, c, hv->x_lbl_find, "Find");
  draw_edit(hv, c, hv->r_find, hv->find_txt, "Hex bytes or text", hv->focus == F_FIND, hv->hov == H_FIND);
  draw_btn(hv, c, hv->r_prev, "\xE2\x80\xB9", hv->hov == H_PREV);
  draw_btn(hv, c, hv->r_next, "\xE2\x80\xBA", hv->hov == H_NEXT);
  label(hv, c, hv->x_lbl_bpr, "Bytes / row");
  snprintf(bpr, sizeof(bpr), "%u", hv->cols);
  draw_drop(hv, c, hv->r_bpr, bpr, hv->hov == H_BPR);
  label(hv, c, hv->x_lbl_enc, "Encoding");
  draw_drop(hv, c, hv->r_enc, s_enc[hv->enc], hv->hov == H_ENC);
  draw_btn(hv, c, hv->r_copy, "Copy", hv->hov == H_COPY);
  pl_line(c, 1, TB_H, hv->W - 1, TB_H, CLR_SEPARATOR);
}

static void paint_header(HexViewer* hv, PlCanvas* c)
{
  int y = TB_H + 1, ty = y + HEAD_TOP;
  unsigned i;
  pl_text(c, hv->mono, mkr(hv->x_off, ty, hv->x_hex, y + HEAD_H), "Offset",
    CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
  for (i = 0; i < hv->cols; ++i) {
    char s[8];
    int x = hx(hv, i);
    snprintf(s, sizeof(s), "%02X", i & 0xFF);
    pl_text(c, hv->mono, mkr(x, ty, x + 2 * hv->cw, y + HEAD_H), s,
      CLR_TEXT_SECONDARY, PL_TEXT_CENTER);
  }
  pl_text(c, hv->mono, mkr(hv->x_asc, ty, hv->x_end, y + HEAD_H), "Decoded text",
    CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
  pl_line(c, hv->x_off, y + HEAD_H, hv->x_end, y + HEAD_H, CLR_SEPARATOR);
}

static void fill_bg(PlCanvas* c, PlRect r, PlColor col)
{
  if (col == CLR_ACCENT) pl_round_rect(c, r, col, col, 3);
  else pl_fill(c, r.l, r.t, r.r, r.b, col);
}

static void paint_rows(HexViewer* hv, PlCanvas* c)
{
  uint64_t trows = total_rows(hv), block_off, block_len, lo = 0, hi = 0;
  size_t need;
  int r, ok = 1, has = hv->has_cursor;
  char line[48];

  if (!hv->size) {
    pl_text(c, hv->ui, mkr(hv->x_off, hv->y0 + 8, hv->x_end, hv->y0 + 8 + ROW_H),
      "No data", CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
    return;
  }
  if (has) sel_range(hv, &lo, &hi);

  block_off = hv->scroll * hv->cols;
  block_len = u64min((uint64_t)hv->vis_rows * hv->cols, hv->size - block_off);
  need = (size_t)block_len;
  if (need > hv->bufcap) {
    unsigned char* nb = (unsigned char*)realloc(hv->buf, need);
    if (nb) { hv->buf = nb; hv->bufcap = need; }
  }
  if (need > hv->bufcap || !fetch(hv, block_off, hv->buf, need)) ok = 0;

  for (r = 0; r < hv->vis_rows; ++r) {
    uint64_t row = hv->scroll + (uint64_t)r, off = row * hv->cols;
    int y = hv->y0 + r * ROW_H, n, i, cur_row;
    const unsigned char* rb;
    PlColor bg[64], fh[64], fa[64];

    if (row >= trows) break;
    n = (int)u64min(hv->cols, hv->size - off);
    cur_row = has && (hv->cursor / hv->cols == row);

    if (cur_row)
      pl_fill(c, hv->x_off, y, hv->x_end, y + ROW_H, CLR_SIDEBAR_HOVER);

    snprintf(line, sizeof(line), "%0*llX", hv->addr_digits, ull(hv->base + off));
    pl_text(c, hv->mono, mkr(hv->x_off, y, hv->x_hex, y + ROW_H), line,
      cur_row ? CLR_ACCENT : CLR_TEXT_SECONDARY, PL_TEXT_LEFT);

    if (!ok) {
      pl_text(c, hv->ui, mkr(hv->x_hex, y, hv->x_end, y + ROW_H), "(read error)",
        CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
      continue;
    }
    rb = hv->buf + (size_t)(off - block_off);

    for (i = 0; i < n; ++i) {
      uint64_t idx = off + (uint64_t)i;
      int sel = has && idx >= lo && idx <= hi;
      int ri = reg_at(hv, idx), pr;
      char g[4];
      PlColor rf = (!sel && ri >= 0) ? hv->reg[ri].fg : PL_NONE;
      pr = glyph(hv, rb[i], g);
      bg[i] = sel ? CLR_ACCENT : (ri >= 0 ? hv->reg[ri].bg : PL_NONE);
      fh[i] = sel ? CLR_TEXT_PRIMARY : rf != PL_NONE ? rf : (rb[i] == 0 ? CLR_TEXT_SECONDARY : CLR_TEXT_PRIMARY);
      fa[i] = sel ? CLR_TEXT_PRIMARY : rf != PL_NONE ? rf : (pr ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY);
    }

    for (i = 0; i < n;) {
      int j = i + 1;
      if (bg[i] == PL_NONE) { ++i; continue; }
      while (j < n && bg[j] == bg[i] && (j % 8) != 0) ++j;
      fill_bg(c, mkr(hx(hv, (unsigned)i) - hv->cw / 2, y,
        hx(hv, (unsigned)(j - 1)) + 2 * hv->cw + hv->cw / 2, y + ROW_H), bg[i]);
      i = j;
    }
    for (i = 0; i < n;) {
      int j = i + 1;
      if (bg[i] == PL_NONE) { ++i; continue; }
      while (j < n && bg[j] == bg[i]) ++j;
      fill_bg(c, mkr(ax(hv, (unsigned)i), y, ax(hv, (unsigned)(j - 1)) + hv->cw, y + ROW_H), bg[i]);
      i = j;
    }

    for (i = 0; i < n; ++i) {
      char hb[4], gl[4];
      int hxp = hx(hv, (unsigned)i), axp = ax(hv, (unsigned)i);
      snprintf(hb, sizeof(hb), "%02X", rb[i]);
      glyph(hv, rb[i], gl);
      pl_text(c, hv->mono, mkr(hxp, y, hxp + 2 * hv->cw, y + ROW_H), hb, fh[i], PL_TEXT_CENTER);
      pl_text(c, hv->mono, mkr(axp, y, axp + hv->cw, y + ROW_H), gl, fa[i], PL_TEXT_CENTER);
    }
  }
}

static void paint_scroll(HexViewer* hv, PlCanvas* c)
{
  uint64_t trows = total_rows(hv);
  int track, th, ty;
  double frac;
  if (!hv->over || trows <= (uint64_t)hv->vis_rows) return;
  track = hv->r_sb.b - hv->r_sb.t;
  th = (int)((double)track * hv->vis_rows / (double)trows);
  if (th < 18) th = 18;
  frac = max_scroll(hv) ? (double)hv->scroll / (double)max_scroll(hv) : 0.0;
  ty = hv->r_sb.t + (int)((track - th) * frac);
  pl_round_rect(c, mkr(hv->r_sb.l, ty, hv->r_sb.r, ty + th), CLR_BTN_GHOST_BD, CLR_BTN_GHOST_BD, 3);
}

static void insp_title(HexViewer* hv, PlCanvas* c, int x, int r, int y, const char* t)
{
  pl_text(c, hv->ui, mkr(x, y, r, y + INSP_TITLE), t, CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
}

static void insp_row(HexViewer* hv, PlCanvas* c, int x, int r, int y, const char* lbl, const char* val)
{
  int w = pl_text_width(hv->mono, val);
  pl_text(c, hv->ui, mkr(x, y, x + 110, y + INSP_ROW), lbl, CLR_TEXT_SECONDARY, PL_TEXT_LEFT);
  pl_text(c, hv->mono, mkr(r - w, y, r, y + INSP_ROW), val, CLR_TEXT_PRIMARY, PL_TEXT_LEFT);
  pl_line(c, x, y + INSP_ROW, r, y + INSP_ROW, CLR_SEPARATOR);
}

static void paint_inspector(HexViewer* hv, PlCanvas* c)
{
  int x = hv->r_insp.l + 16, r = hv->r_insp.r - 16, y = hv->r_insp.t + 10;
  unsigned char d[4] = { 0, 0, 0, 0 };
  size_t m = 0, i, nleg = 0;
  uint64_t lo = 0, hi = 0;
  int has = hv->has_cursor, ri = -1;
  char v[64];
  const char* seen[8];

  pl_line(c, hv->r_insp.l, hv->r_insp.t, hv->r_insp.l, hv->r_insp.b - 1, CLR_SEPARATOR);
  if (has) {
    sel_range(hv, &lo, &hi);
    m = (size_t)u64min(4, hv->size - lo);
    if (!fetch(hv, lo, d, m)) m = 0;
    ri = reg_at(hv, lo);
  }

  insp_title(hv, c, x, r, y, "DATA INSPECTOR"); y += INSP_TITLE;

  if (m >= 1) snprintf(v, sizeof(v), "%u", d[0]); else strcpy(v, "-");
  insp_row(hv, c, x, r, y, "Uint8", v); y += INSP_ROW;
  if (m >= 2) snprintf(v, sizeof(v), "%u", (unsigned)(d[0] | (d[1] << 8))); else strcpy(v, "-");
  insp_row(hv, c, x, r, y, "Uint16 LE", v); y += INSP_ROW;
  if (m >= 4) snprintf(v, sizeof(v), "%llu", ull((uint64_t)d[0] | ((uint64_t)d[1] << 8) | ((uint64_t)d[2] << 16) | ((uint64_t)d[3] << 24)));
  else strcpy(v, "-");
  insp_row(hv, c, x, r, y, "Uint32 LE", v); y += INSP_ROW;
  if (m >= 4) snprintf(v, sizeof(v), "%llu", ull((uint64_t)d[3] | ((uint64_t)d[2] << 8) | ((uint64_t)d[1] << 16) | ((uint64_t)d[0] << 24)));
  else strcpy(v, "-");
  insp_row(hv, c, x, r, y, "Uint32 BE", v); y += INSP_ROW;

  v[0] = 0;
  for (i = 0; i < m; ++i) {
    char t[8];
    snprintf(t, sizeof(t), "%s%02X", i ? " " : "", d[i]);
    strcat(v, t);
  }
  if (!m) strcpy(v, "-");
  insp_row(hv, c, x, r, y, "Hex (4 B)", v); y += INSP_ROW;

  v[0] = 0;
  for (i = 0; i < m; ++i) { char g[4]; glyph(hv, d[i], g); strcat(v, g); }
  if (!m) strcpy(v, "-");
  insp_row(hv, c, x, r, y, "Text", v); y += INSP_ROW + INSP_SECT;

  insp_title(hv, c, x, r, y, "SELECTION"); y += INSP_TITLE;
  if (has) snprintf(v, sizeof(v), "0x%08llX", ull(hv->base + lo)); else strcpy(v, "-");
  insp_row(hv, c, x, r, y, "Offset", v); y += INSP_ROW;
  if (has) snprintf(v, sizeof(v), "%llu byte%s", ull(hi - lo + 1), hi == lo ? "" : "s"); else strcpy(v, "-");
  insp_row(hv, c, x, r, y, "Length", v); y += INSP_ROW;
  insp_row(hv, c, x, r, y, "Region", ri >= 0 && hv->reg[ri].name[0] ? hv->reg[ri].name : "-");
  y += INSP_ROW + INSP_SECT;

  insp_title(hv, c, x, r, y, "LEGEND"); y += INSP_TITLE;
  for (i = 0; i < hv->nreg && nleg < 8; ++i) {
    size_t k;
    int dup = 0;
    if (hv->reg[i].bg == PL_NONE || !hv->reg[i].name[0]) continue;
    for (k = 0; k < nleg; ++k) if (!strcmp(seen[k], hv->reg[i].name)) dup = 1;
    if (dup) continue;
    seen[nleg++] = hv->reg[i].name;
    pl_round_rect(c, mkr(x, y + 5, x + 14, y + 19), hv->reg[i].bg, hv->reg[i].bg, 4);
    pl_text(c, hv->ui, mkr(x + 22, y, r, y + 24), hv->reg[i].name, CLR_TEXT_PRIMARY, PL_TEXT_ELLIPSIS);
    y += LEG_ROW;
  }
  pl_round_rect(c, mkr(x, y + 5, x + 14, y + 19), CLR_ACCENT, CLR_ACCENT, 4);
  pl_text(c, hv->ui, mkr(x + 22, y, r, y + 24), "Selection", CLR_TEXT_PRIMARY, PL_TEXT_LEFT);
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
  HexViewer* hv = (HexViewer*)u;
  PlRect frame;
  hv->W = w; hv->H = h;
  layout(hv);
  pl_fill(c, 0, 0, w, h, CLR_BG);
  frame = mkr(0, 0, w - 1, h - 1);
  pl_round_rect(c, frame, CLR_PANEL_BODY, CLR_PANEL_BORDER, 12);
  paint_toolbar(hv, c);
  paint_header(hv, c);
  paint_rows(hv, c);
  paint_scroll(hv, c);
  if (hv->show_insp) paint_inspector(hv, c);
}

static void cycle_bpr(HexViewer* hv)
{
  size_t i, n = sizeof(s_cols) / sizeof(s_cols[0]), cur = 1;
  for (i = 0; i < n; ++i) if (s_cols[i] == hv->cols) cur = i;
  hexview_set_columns(hv, s_cols[(cur + 1) % n]);
}

static void on_mouse_move(void* u, int x, int y)
{
  HexViewer* hv = (HexViewer*)u;
  int h = hit_ctrl(hv, x, y);
  int o = (x < hv->hex_right && y >= hv->y0);
  if (h != hv->hov || o != hv->over) { hv->hov = h; hv->over = o; redraw(hv); }
}

static void on_mouse_leave(void* u)
{
  HexViewer* hv = (HexViewer*)u;
  if (hv->hov != H_NONE || hv->over) { hv->hov = H_NONE; hv->over = 0; redraw(hv); }
}

static void on_mouse_down(void* u, int x, int y)
{
  HexViewer* hv = (HexViewer*)u;
  int h = hit_ctrl(hv, x, y);
  uint64_t off;
  PlRect sb;

  hv->caret_on = 1; hv->blink_t = blink_now();
  if (h == H_GOTO) { hv->focus = F_GOTO; redraw(hv); return; }
  if (h == H_FIND) { hv->focus = F_FIND; redraw(hv); return; }
  hv->focus = F_VIEW;

  switch (h) {
  case H_PREV: search(hv, -1); return;
  case H_NEXT: search(hv, 1); return;
  case H_BPR:  cycle_bpr(hv); return;
  case H_ENC:  hv->enc = (hv->enc + 1) % 2; info(hv); redraw(hv); return;
  case H_COPY:
    if (hexview_copy(hv, HEX_COPY_HEX)) status(hv, "Copied the selection to the clipboard.");
    return;
  default: break;
  }

  sb = sb_hit_rect(hv);
  if (inr(&sb, x, y) && total_rows(hv) > (uint64_t)hv->vis_rows) {
    double f = (double)(y - hv->r_sb.t) / (double)(hv->r_sb.b - hv->r_sb.t);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    hv->scroll = (uint64_t)(f * (double)max_scroll(hv));
    clamp_scroll(hv);
    redraw(hv);
    return;
  }
  if (hit_byte(hv, x, y, &off)) move_to(hv, off, 0);
  else redraw(hv);
}

static int on_hand(void* u, int x, int y)
{
  HexViewer* hv = (HexViewer*)u;
  uint64_t off;
  PlRect sb = sb_hit_rect(hv);
  return hit_ctrl(hv, x, y) != H_NONE || inr(&sb, x, y) || hit_byte(hv, x, y, &off);
}

static void on_wheel(void* u, int x, int y, int delta, int mods)
{
  HexViewer* hv = (HexViewer*)u;
  (void)x; (void)y; (void)mods;
  if (!delta) return;
  if (delta > 0) hv->scroll = hv->scroll > 3 ? hv->scroll - 3 : 0;
  else hv->scroll += 3;
  clamp_scroll(hv);
  redraw(hv);
}

static void edit_key(HexViewer* hv, int key, uint32_t ch, int mods)
{
  char* s = hv->focus == F_GOTO ? hv->goto_txt : hv->find_txt;
  size_t cap = hv->focus == F_GOTO ? sizeof(hv->goto_txt) : sizeof(hv->find_txt);

  if (key == PL_KEY_ESC) hv->focus = F_VIEW;
  else if (key == PL_KEY_ENTER) {
    if (hv->focus == F_GOTO) do_goto(hv);
    else search(hv, (mods & PL_MOD_SHIFT) ? -1 : 1);
  }
  else if (key == PL_KEY_TAB) hv->focus = hv->focus == F_GOTO ? F_FIND : F_VIEW;
  else if (key == PL_KEY_BACK || ch == 8) edit_back(s);
  else if (ch >= 32 && ch != 127) edit_append(s, cap, ch);
  redraw(hv);
}

static void on_key(void* u, int key, uint32_t ch, int mods)
{
  HexViewer* hv = (HexViewer*)u;
  int ext = (mods & PL_MOD_SHIFT) != 0, ctrl = (mods & PL_MOD_CTRL) != 0;
  uint64_t at, cols = hv->cols, page = (uint64_t)hv->vis_rows;

  hv->caret_on = 1; hv->blink_t = blink_now();
  if (ch == 6) { hv->focus = F_FIND; redraw(hv); return; }   /* Ctrl+F */
  if (ch == 7) { hv->focus = F_GOTO; redraw(hv); return; }   /* Ctrl+G */
  if (hv->focus != F_VIEW) { edit_key(hv, key, ch, mods); return; }
  if (key == PL_KEY_TAB) { hv->focus = F_GOTO; redraw(hv); return; }

  if (ch == 3) { hexview_copy(hv, HEX_COPY_HEX); return; }   /* Ctrl+C */
  if (ch == 1 && hv->size) {                                  /* Ctrl+A */
    hv->anchor = 0; hv->cursor = hv->size - 1; hv->has_cursor = 1;
    notify(hv); redraw(hv); return;
  }
  if (key == PL_KEY_F3) { search(hv, ext ? -1 : 1); return; }
  if (!hv->size) return;
  at = hv->has_cursor ? hv->cursor : 0;

  switch (key) {
  case PL_KEY_LEFT:  move_to(hv, at ? at - 1 : 0, ext); break;
  case PL_KEY_RIGHT: move_to(hv, at + 1, ext); break;
  case PL_KEY_UP:    move_to(hv, at >= cols ? at - cols : at % cols, ext); break;
  case PL_KEY_DOWN:  move_to(hv, at + cols < hv->size ? at + cols : hv->size - 1, ext); break;
  case PL_KEY_PGUP:  move_to(hv, at >= cols * page ? at - cols * page : at % cols, ext); break;
  case PL_KEY_PGDN:
    move_to(hv, hv->size - at > cols * page ? at + cols * page : hv->size - 1, ext); break;
  case PL_KEY_HOME:  move_to(hv, ctrl ? 0 : at / cols * cols, ext); break;
  case PL_KEY_END:
    move_to(hv, ctrl ? hv->size - 1 : u64min(at / cols * cols + cols - 1, hv->size - 1), ext);
    break;
  default: break;
  }
}

static void on_resize(void* u, int w, int h)
{
  HexViewer* hv = (HexViewer*)u;
  hv->W = w; hv->H = h;
  layout(hv);
  redraw(hv);
}

static void free_regions(HexViewer* hv)
{
  size_t i;
  for (i = 0; i < hv->nreg; ++i) free(hv->reg[i].name);
  free(hv->reg);
  hv->reg = NULL;
  hv->nreg = 0;
}

static void on_destroy(void* u)
{
  HexViewer* hv = (HexViewer*)u;
  if (hv->own_mono && hv->mono) pl_font_destroy(hv->mono);
  if (hv->ui) pl_font_destroy(hv->ui);
  free_regions(hv);
  free(hv->buf);
  free(hv);
}

static const PlWidgetOps s_ops = {
  on_paint, on_mouse_move, on_mouse_leave, on_mouse_down,
  on_resize, on_hand, on_destroy,
  on_key, on_wheel
};

HexViewer* hexview_create(void* parent_native, PlFont* mono_font, int x, int y, int w, int h)
{
  HexViewer* hv = (HexViewer*)calloc(1, sizeof(*hv));
  if (!hv) return NULL;
  hv->cols = 16;
  hv->W = w; hv->H = h;
  hv->hov = H_NONE;
  hv->caret_on = 1;
  snprintf(hv->goto_txt, sizeof(hv->goto_txt), "0x00000000");

  hv->ui = pl_font_create(9, 0);
  if (mono_font) hv->mono = mono_font;
  else {
#ifdef PL_HAS_FONT_FAMILY
#if defined(_WIN32)
    hv->mono = pl_font_create_family("Consolas", 11, 0);
#elif defined(__APPLE__)
    hv->mono = pl_font_create_family("Menlo", 11, 0);
#else
    hv->mono = pl_font_create_family("DejaVu Sans Mono", 11, 0);
#endif
    if (!hv->mono) hv->mono = pl_font_create(11, 0);
#else
    hv->mono = pl_font_create(11, 0);   /* proportional fallback: cells keep it aligned */
#endif
    hv->own_mono = 1;
  }
  if (!hv->ui || !hv->mono) {
    if (hv->own_mono && hv->mono) pl_font_destroy(hv->mono);
    if (hv->ui) pl_font_destroy(hv->ui);
    free(hv);
    return NULL;
  }
  layout(hv);
  hv->w = pl_widget_create(parent_native, &s_ops, hv, x, y, w, h);
  if (!hv->w) {
    if (hv->own_mono) pl_font_destroy(hv->mono);
    pl_font_destroy(hv->ui);
    free(hv);
    return NULL;
  }
  return hv;
}

void hexview_destroy(HexViewer* hv)
{
  if (hv && hv->w) pl_widget_destroy(hv->w);  
}

PlWidget* hexview_widget(HexViewer* hv) { return hv ? hv->w : NULL; }

void hexview_set_rect(HexViewer* hv, int x, int y, int w, int h)
{
  if (!hv) return;
  hv->W = w; hv->H = h;
  pl_widget_set_rect(hv->w, x, y, w, h);
  layout(hv);
  redraw(hv);
}

static void reset_view(HexViewer* hv)
{
  hv->scroll = 0;
  hv->cursor = hv->anchor = 0;
  hv->has_cursor = 0;
  hv->focus = F_VIEW;
  snprintf(hv->goto_txt, sizeof(hv->goto_txt), "0x%08llX", ull(hv->base));
  layout(hv);
  notify(hv);
  info(hv);
  redraw(hv);
}

void hexview_set_memory(HexViewer* hv, const void* data, uint64_t size, uint64_t base)
{
  if (!hv) return;
  hv->mem = (const unsigned char*)data;
  hv->read = NULL; hv->ruser = NULL;
  hv->size = data ? size : 0;
  hv->base = base;
  reset_view(hv);
}

void hexview_set_source(HexViewer* hv, uint64_t size, HexReadFn read, void* user, uint64_t base)
{
  if (!hv) return;
  hv->mem = NULL;
  hv->read = read; hv->ruser = user;
  hv->size = read ? size : 0;
  hv->base = base;
  reset_view(hv);
}

int hexview_set_columns(HexViewer* hv, unsigned columns)
{
  uint64_t first;
  if (!hv || (columns != 8 && columns != 16 && columns != 24 &&
    columns != 32 && columns != 48 && columns != 64)) return 0;
  first = hv->scroll * hv->cols;
  hv->cols = columns;
  hv->scroll = first / columns;
  layout(hv);
  if (hv->has_cursor) ensure_visible(hv, hv->cursor);
  redraw(hv);
  return 1;
}

void hexview_set_selection(HexViewer* hv, uint64_t offset, uint64_t length)
{
  if (!hv) return;
  if (!length || offset >= hv->size || length > hv->size - offset) {
    hv->has_cursor = 0;
    notify(hv); redraw(hv);
    return;
  }
  hv->anchor = offset;
  hv->cursor = offset + length - 1;
  hv->has_cursor = 1;
  ensure_visible(hv, hv->cursor);
  notify(hv); redraw(hv);
}

void hexview_goto(HexViewer* hv, uint64_t offset)
{
  if (!hv) return;
  goto_offset(hv, offset);
}

int hexview_get_selection(const HexViewer* hv, uint64_t* offset, uint64_t* length)
{
  uint64_t lo, hi;
  if (!hv || !offset || !length) return 0;
  if (!hv->has_cursor) { *offset = 0; *length = 0; return 1; }
  sel_range(hv, &lo, &hi);
  *offset = lo; *length = hi - lo + 1;
  return 1;
}

void hexview_set_regions(HexViewer* hv, const HexRegion* regions, size_t count)
{
  size_t i;
  Reg* nr = NULL;
  if (!hv) return;
  if (count && regions) {
    nr = (Reg*)calloc(count, sizeof(*nr));
    if (!nr) return;
    for (i = 0; i < count; ++i) {
      const char* nm = regions[i].name ? regions[i].name : "";
      size_t len = strlen(nm) + 1;
      nr[i].name = (char*)malloc(len);
      if (!nr[i].name) {
        while (i--) free(nr[i].name);
        free(nr);
        return;
      }
      memcpy(nr[i].name, nm, len);
      nr[i].off = regions[i].offset; nr[i].len = regions[i].length;
      nr[i].bg = regions[i].bg; nr[i].fg = regions[i].fg;
    }
  }
  free_regions(hv);
  hv->reg = nr;
  hv->nreg = nr ? count : 0;
  redraw(hv);
}

void hexview_set_callbacks(HexViewer* hv, HexSelectFn on_select, HexCopyFn on_copy, void* user)
{
  if (!hv) return;
  hv->on_select = on_select;
  hv->on_copy = on_copy;
  hv->cb_user = user;
}

void hexview_set_status_callback(HexViewer* hv, HexStatusFn fn, void* user)
{
  if (!hv) return;
  hv->on_status = fn;
  hv->st_user = user;
}

void hexview_set_info_callback(HexViewer* hv, HexStatusFn fn, void* user)
{
  if (!hv) return;
  hv->on_info = fn;
  hv->info_user = user;
  info(hv);
}

int hexview_copy(HexViewer* hv, int mode)
{
  static const char digits[] = "0123456789ABCDEF";
  uint64_t lo, hi, len, done = 0;
  unsigned char chunk[4096];
  char* text;
  size_t p = 0;
  int ok;

  if (!hv || !hv->on_copy || !hv->has_cursor) return 0;
  sel_range(hv, &lo, &hi);
  len = hi - lo + 1;
  if (len > COPY_MAX) len = COPY_MAX;
  text = (char*)malloc((size_t)len * 3 + 1);
  if (!text) return 0;

  while (done < len) {
    size_t n = (size_t)u64min(sizeof(chunk), len - done), i;
    if (!fetch(hv, lo + done, chunk, n)) { free(text); return 0; }
    for (i = 0; i < n; ++i) {
      if (mode == HEX_COPY_TEXT) {
        text[p++] = (chunk[i] >= 32 && chunk[i] <= 126) ? (char)chunk[i] : '.';
      }
      else {
        if (p) text[p++] = ' ';
        text[p++] = digits[chunk[i] >> 4];
        text[p++] = digits[chunk[i] & 15];
      }
    }
    done += n;
  }
  text[p] = 0;
  ok = hv->on_copy(hv->cb_user, text);
  free(text);
  return ok;
}

void hexview_key(HexViewer* hv, int key, uint32_t ch, int mods)
{
  if (!hv) return;
  on_key(hv, key, ch, mods);
}

void hexview_tick(HexViewer* hv)
{
  unsigned long now;
  if (!hv || hv->W <= 0 || hv->H <= 0) return;
  if (hv->focus != F_GOTO && hv->focus != F_FIND) return;
  now = blink_now();
  if (now - hv->blink_t >= blink_period()) {
    hv->blink_t = now;
    hv->caret_on = !hv->caret_on;
    redraw(hv);
  }
}