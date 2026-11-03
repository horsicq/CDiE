#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
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
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define PSEP '\\'
#else
#include <dirent.h>
#include <sys/stat.h>
#define PSEP '/'
#endif

#include "platform/platform.h"
#include "panels/signatures.h"
#include "core/gui_backend.h"
#include "core/config.h" 
#include "core/updater.h"
#include "core/i18n.h"
#include "theme/theme.h"

#define SG_BG     CLR_BG
#define SG_PANEL  CLR_PANEL_HEADER      /* card headers, left panel, find box */
#define SG_CODE   CLR_PANEL_BODY        /* editor / output body               */
#define SG_BAR    CLR_BG                /* bottom bar (same as bottombar.c)   */
#define SG_BORDER CLR_PANEL_BORDER
#define SG_SEP    CLR_SEPARATOR
#define SG_TEXT   CLR_TEXT_PRIMARY
#define SG_TEXT2  CLR_TEXT_SECONDARY
#define SG_DIM    CLR_TEXT_CODE         /* line numbers, count, cue text      */
#define SG_ACCENT CLR_ACCENT
#define SG_SEL    CLR_SIDEBAR_SEL       /* tree selection + find match        */
#define SG_BTN    CLR_BTN_GHOST
#define SG_BTNB   CLR_BTN_GHOST_BD
#define SG_CHIP   CLR_PANEL_HEADER
#define SG_WHITE  CLR_TEXT_PRIMARY

static const PlColor s_cls_col[] = {
  CLR_TEXT_PRIMARY,        /* CL_TXT */
  CLR_TEXT_SECONDARY,      /* CL_COM */
  PL_RGB(158, 206, 106),   /* CL_STR */
  PL_RGB(122, 162, 247),   /* CL_KEY */
  PL_RGB(224, 175, 104),   /* CL_FN  */
  PL_RGB(255, 158, 100),   /* CL_NUM */
  PL_RGB(187, 154, 247)    /* CL_OBJ */
};

enum { CL_TXT, CL_COM, CL_STR, CL_KEY, CL_FN, CL_NUM, CL_OBJ };

#define BANNER_H 52
#define HEAD_ED  38
#define HEAD_OUT 34
#define NCHIPS   5
#define TROW     28          /* tree row height   */
#define LH       18          /* editor line height */
#define OLH      16          /* output line height */
#define MAXC     2048        /* max drawn columns per line */
#define OUT_MAX  1000

enum { K_NORMAL, K_ACCENT, K_CHIP, K_SWITCH, K_LINK, K_DROP };
enum {
  B_SAVE, B_RO, B_FIND, B_NEXT, B_CLEAR,
  B_CH0, B_CH1, B_CH2, B_CH3, B_CH4,
  B_DROP, B_RUN, B_CLOSE, B_UPD, NB
};
enum { F_TREE, F_ED, F_FILTER, F_FIND };

typedef struct { char s[256]; int len, caret; } Edit;
typedef struct { int kind, on, enabled; const char* text; PlRect r; } Btn;
typedef struct { char* name; char* path; int depth, isdir, open, parent; } Node;

typedef struct Sig {
  PlWidget* w;
  PlWindow* win;
  PlFont* font, * fontb, * mono;
  int W, H, done, focus, hov, popup, sp, aw[128];  

  PlRect r_left, r_filter, r_edcard, r_findbox, r_outcard, r_bar;
  PlRect r_tree, r_gut, r_ed, r_out;
  Btn b[NB];
  Edit filter, find;

  Node* nodes; int nn, ncap;
  int* vis; int nvis;
  int count, tsel, ttop;

  char* buf; size_t len, cap;
  size_t* ls; char* blk; int nl, lscap;
  size_t caret, ma, mb;
  int top, hs, crlf, dirty;
  char cur[1024], type[64];

  char* out[OUT_MAX]; int nout, oscroll;
  Updater upd;
  PlRect r_banner;
  int auto_done;
} Sig;

static Sig* s_sig;

static const char* s_chip_text[NCHIPS] =
{ "Recursive", "Deep scan", "Heuristic", "Verbose", "Profiling" };

static PlRect mkr(int l, int t, int r, int b) { PlRect x; x.l = l; x.t = t; x.r = r; x.b = b; return x; }
static int inr(const PlRect* r, int x, int y) { return x >= r->l && x < r->r && y >= r->t && y < r->b; }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static void redraw(Sig* s)
{
  PlRect r = mkr(0, 0, s->W, s->H);
  pl_invalidate(s->w, &r);
}

static void txt(PlCanvas* c, PlFont* f, PlColor col, PlRect r, int al, const char* t)
{
  if (al == 1) { pl_text(c, f, r, t, col, PL_TEXT_CENTER); return; }
  if (al == 2) {
    int w = pl_text_width(f, t);
    if (w < r.r - r.l) r.l = r.r - w;
  }
  pl_text(c, f, r, t, col, al == 2 ? PL_TEXT_LEFT : PL_TEXT_ELLIPSIS);
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

static int ci_cmp(const char* a, const char* b)
{
  for (;; ++a, ++b) {
    int x = (unsigned char)*a, y = (unsigned char)*b;
    if (x >= 'A' && x <= 'Z') x += 32;
    if (y >= 'A' && y <= 'Z') y += 32;
    if (x != y) return x - y;
    if (!x) return 0;
  }
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
static char* dupstr(const char* s)
{
  size_t n = strlen(s) + 1;
  char* p = (char*)malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}
static char* join_path(const char* dir, const char* name)
{
  size_t a = strlen(dir), b = strlen(name);
  char* p = (char*)malloc(a + b + 2);
  if (!p) return NULL;
  memcpy(p, dir, a);
  p[a] = PSEP;
  memcpy(p + a + 1, name, b + 1);
  return p;
}

typedef struct { char* name; int isdir; } DEnt;

#ifdef _WIN32
static wchar_t* to_w(const char* s)
{
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  wchar_t* w;
  if (n <= 0) return NULL;
  w = (wchar_t*)malloc((size_t)n * sizeof(wchar_t));
  if (w) MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
  return w;
}
static char* to_u8(const wchar_t* w)
{
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  char* s;
  if (n <= 0) return NULL;
  s = (char*)malloc((size_t)n);
  if (s) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
  return s;
}
static FILE* fopen_u8(const char* path, const char* mode)
{
  wchar_t* w = to_w(path), m[8];
  FILE* f = NULL;
  int i = 0;
  if (!w) return NULL;
  while (mode[i] && i < 7) { m[i] = (wchar_t)mode[i]; ++i; }
  m[i] = 0;
  f = _wfopen(w, m);
  free(w);
  return f;
}
static int list_dir(const char* dir, DEnt** out)
{
  char* pat = join_path(dir, "*");
  wchar_t* wp = pat ? to_w(pat) : NULL;
  WIN32_FIND_DATAW fd;
  HANDLE h;
  int n = 0, cap = 0;
  DEnt* e = NULL;
  free(pat);
  *out = NULL;
  if (!wp) return 0;
  h = FindFirstFileW(wp, &fd);
  free(wp);
  if (h == INVALID_HANDLE_VALUE) return 0;
  do {
    if (fd.cFileName[0] == L'.' &&
      (!fd.cFileName[1] || (fd.cFileName[1] == L'.' && !fd.cFileName[2]))) continue;
    if (n == cap) {
      DEnt* ne = (DEnt*)realloc(e, (size_t)(cap = cap ? cap * 2 : 64) * sizeof(DEnt));
      if (!ne) break;
      e = ne;
    }
    e[n].name = to_u8(fd.cFileName);
    e[n].isdir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (e[n].name) ++n;
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  *out = e;
  return n;
}
#else

static FILE* fopen_u8(const char* path, const char* mode) { return fopen(path, mode); }

static int list_dir(const char* dir, DEnt** out)
{
  DIR* d = opendir(dir);
  struct dirent* de;
  int n = 0, cap = 0;
  DEnt* e = NULL;
  *out = NULL;
  if (!d) return 0;
  while ((de = readdir(d)) != NULL) {
    struct stat st;
    char* full;
    if (de->d_name[0] == '.' &&
      (!de->d_name[1] || (de->d_name[1] == '.' && !de->d_name[2]))) continue;
    if (n == cap) {
      DEnt* ne = (DEnt*)realloc(e, (size_t)(cap = cap ? cap * 2 : 64) * sizeof(DEnt));
      if (!ne) break;
      e = ne;
    }
    full = join_path(dir, de->d_name);
    e[n].isdir = (full && stat(full, &st) == 0) ? S_ISDIR(st.st_mode) : 0;
    free(full);
    e[n].name = dupstr(de->d_name);
    if (e[n].name) ++n;
  }
  closedir(d);
  *out = e;
  return n;
}
#endif

static int dent_cmp(const void* a, const void* b)
{
  return ci_cmp(((const DEnt*)a)->name, ((const DEnt*)b)->name);
}

static void out_log(Sig* s, const char* t)
{
  char* c = dupstr(t);
  if (!c) return;
  if (s->nout == OUT_MAX) {
    free(s->out[0]);
    memmove(s->out, s->out + 1, (OUT_MAX - 1) * sizeof(char*));
    --s->nout;
  }
  s->out[s->nout++] = c;
  s->oscroll = 0;
  redraw(s);
}
static void out_clear(Sig* s)
{
  int i;
  for (i = 0; i < s->nout; ++i) free(s->out[i]);
  s->nout = 0; s->oscroll = 0;
}

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

static Node* node_at(Sig* s, int i) { return &s->nodes[i]; }

static int node_add(Sig* s, int parent, int depth, const char* name, const char* path, int isdir)
{
  Node* n;
  if (s->nn == s->ncap) {
    int nc = s->ncap ? s->ncap * 2 : 256;
    Node* p = (Node*)realloc(s->nodes, (size_t)nc * sizeof(Node));
    if (!p) return -1;
    s->nodes = p; s->ncap = nc;
  }
  n = &s->nodes[s->nn];
  n->name = dupstr(name);
  n->path = isdir ? NULL : dupstr(path);
  n->depth = depth; n->isdir = isdir; n->open = 0; n->parent = parent;
  return s->nn++;
}
static void node_trunc(Sig* s, int idx)
{
  while (s->nn > idx) {
    --s->nn;
    free(s->nodes[s->nn].name);
    free(s->nodes[s->nn].path);
  }
}
static void rebuild_vis(Sig* s)
{
  char* vf;
  int i;
  free(s->vis);
  s->vis = (int*)malloc((size_t)(s->nn + 1) * sizeof(int));
  vf = (char*)calloc((size_t)s->nn + 1, 1);
  s->nvis = 0;
  if (!s->vis || !vf) { free(vf); return; }
  for (i = 0; i < s->nn; ++i) {
    Node* n = node_at(s, i);
    vf[i] = (n->parent < 0) || (vf[n->parent] && node_at(s, n->parent)->open);
    if (vf[i]) s->vis[s->nvis++] = i;
  }
  free(vf);
  if (s->ttop > imax(0, s->nvis - 1)) s->ttop = imax(0, s->nvis - 1);
}

static int add_dir(Sig* s, int parent, int depth, const char* dir, const char* flt)
{
  DEnt* e = NULL;
  int n = list_dir(dir, &e), pass, i, total = 0;
  if (n > 1) qsort(e, (size_t)n, sizeof(DEnt), dent_cmp);
  for (pass = 0; pass < 2; ++pass) {              
    for (i = 0; i < n; ++i) {
      int isdir = e[i].isdir, idx;
      char* full;
      if (isdir != (pass == 0)) continue;
      if (!isdir && flt[0] && !contains_ci(e[i].name, flt)) continue;
      full = join_path(dir, e[i].name);
      if (!full) continue;
      idx = node_add(s, parent, depth, e[i].name, full, isdir);
      if (idx >= 0) {
        if (isdir) {
          int c = add_dir(s, idx, depth + 1, full, flt);
          if (flt[0] && c == 0) node_trunc(s, idx);
          else { total += c; if (flt[0]) node_at(s, idx)->open = 1; }
        }
        else ++total;
      }
      free(full);
    }
  }
  for (i = 0; i < n; ++i) free(e[i].name);
  free(e);
  return total;
}

static void rebuild_tree(Sig* s)
{
  const char* root = config_db_required() ? NULL : gui_backend_main_db_path();
  node_trunc(s, 0);
  s->tsel = -1; s->ttop = 0;
  s->count = (root && root[0]) ? add_dir(s, -1, 0, root, s->filter.s) : 0;
  rebuild_vis(s);
  if (!root || !root[0])
    out_log(s, translate("Signature folder not found. Use Database paths in the main window."));
  redraw(s);
}

static size_t line_end(Sig* s, int i) { return (i + 1 < s->nl) ? s->ls[i + 1] - 1 : s->len; }

static int find_line(Sig* s, size_t pos)
{
  int lo = 0, hi = s->nl - 1;
  while (lo < hi) {
    int mid = (lo + hi + 1) / 2;
    if (s->ls[mid] <= pos) lo = mid; else hi = mid - 1;
  }
  return lo;
}

static void reindex(Sig* s)
{
  size_t i, p, e;
  int n = 1, k = 1, l, inb = 0;
  for (i = 0; i < s->len; ++i) if (s->buf[i] == '\n') ++n;
  if (n > s->lscap) {
    size_t* nl = (size_t*)realloc(s->ls, (size_t)n * sizeof(size_t));
    char* nb = (char*)realloc(s->blk, (size_t)n);
    if (!nl || !nb) { if (nl) s->ls = nl; if (nb) s->blk = nb; return; }
    s->ls = nl; s->blk = nb; s->lscap = n;
  }
  s->nl = n;
  s->ls[0] = 0;
  for (i = 0; i < s->len; ++i) if (s->buf[i] == '\n') s->ls[k++] = i + 1;

  for (l = 0; l < s->nl; ++l) {
    p = s->ls[l]; e = line_end(s, l);
    s->blk[l] = (char)inb;
    while (p < e) {
      char ch = s->buf[p];
      if (inb) {
        if (ch == '*' && p + 1 < e && s->buf[p + 1] == '/') { inb = 0; p += 2; }
        else ++p;
      }
      else if (ch == '/' && p + 1 < e && s->buf[p + 1] == '/') break;
      else if (ch == '/' && p + 1 < e && s->buf[p + 1] == '*') { inb = 1; p += 2; }
      else if (ch == '"' || ch == '\'') {
        char q = ch;
        ++p;
        while (p < e && s->buf[p] != q) { if (s->buf[p] == '\\') ++p; ++p; }
        ++p;
      }
      else ++p;
    }
  }
}

static int vcol_of(Sig* s, int line, size_t pos)
{
  size_t p = s->ls[line];
  int c = 0;
  while (p < pos && p < s->len) {
    unsigned char ch = (unsigned char)s->buf[p];
    if (ch == '\t') c += 4 - c % 4;
    else if ((ch & 0xC0) != 0x80) ++c;
    ++p;
  }
  return c;
}
static size_t pos_of_vcol(Sig* s, int line, int vc)
{
  size_t p = s->ls[line], e = line_end(s, line);
  int c = 0;
  while (p < e && c < vc) {
    if (s->buf[p] == '\t') { c += 4 - c % 4; ++p; }
    else { ++c; p = u8next(s->buf, p, e); }
  }
  return p;
}

static int ed_rows(Sig* s) { return imax(1, (s->r_ed.b - s->r_ed.t) / LH); }
static int glyph_w(Sig* s, const char* p, size_t n)
{
  if (n == 1) { unsigned char ch = (unsigned char)*p; return (ch >= 32 && ch < 127) ? s->aw[ch] : s->sp; }
  { char t[8]; if (n > 7) n = 7; memcpy(t, p, n); t[n] = 0; return pl_text_width(s->font, t); }
}

static int vx(Sig* s, int line, int vc)
{
  size_t p = s->ls[line], e = line_end(s, line);
  int c = 0, x = 0;
  while (p < e && c < vc) {
    if (s->buf[p] == '\t') {
      int k = 4 - c % 4;
      while (k-- && c < vc) { x += s->sp; ++c; }
      ++p;
    }
    else {
      size_t q = u8next(s->buf, p, e);
      x += glyph_w(s, s->buf + p, q - p); ++c; p = q;
    }
  }
  if (c < vc) x += (vc - c) * s->sp;
  return x;
}

static int vc_at_x(Sig* s, int line, int xrel)
{
  size_t p = s->ls[line], e = line_end(s, line);
  int c = 0, acc = 0;
  while (p < e) {
    int w, step = 1;
    size_t q;
    if (s->buf[p] == '\t') { w = s->sp; q = p; if (c % 4 == 3) q = p + 1; }
    else { q = u8next(s->buf, p, e); w = glyph_w(s, s->buf + p, q - p); }
    if (acc + w / 2 > xrel) return c;
    acc += w; c += step;
    if (s->buf[p] == '\t') { if (c % 4 == 0) p = q; else q = p; }
    else p = q;
  }
  return c + (xrel > acc ? (xrel - acc + s->sp / 2) / s->sp : 0);
}

static void ensure_visible(Sig* s)
{
  int l, vr, vc, avail, xc;
  if (!s->cur[0] || !s->buf || !s->ls || s->nl <= 0) return;
  l = find_line(s, s->caret);
  vr = ed_rows(s);
  vc = vcol_of(s, l, s->caret);
  avail = s->r_ed.r - s->r_ed.l - 12 - 10;
  if (l < s->top) s->top = l;
  else if (l >= s->top + vr) s->top = l - vr + 1;
  if (vc < s->hs) s->hs = vc;
  else {
    xc = vx(s, l, vc);
    while (s->hs < vc && xc - vx(s, l, s->hs) > avail - 8) ++s->hs;
  }
  if (s->hs < 0) s->hs = 0;
}

static void set_save_state(Sig* s)
{
  s->b[B_SAVE].enabled = !s->b[B_RO].on && s->dirty && s->cur[0] &&
    s->upd.phase != UPD_RUNNING;
}

static int buf_reserve(Sig* s, size_t need)
{
  if (need + 1 > s->cap) {
    size_t nc = s->cap ? s->cap : 4096;
    char* p;
    while (nc < need + 1) nc *= 2;
    p = (char*)realloc(s->buf, nc);
    if (!p) return 0;
    s->buf = p; s->cap = nc;
  }
  return 1;
}
static void buf_ins(Sig* s, size_t pos, const char* t, size_t n)
{
  if (!buf_reserve(s, s->len + n)) return;
  memmove(s->buf + pos + n, s->buf + pos, s->len - pos);
  memcpy(s->buf + pos, t, n);
  s->len += n;
}
static void buf_del(Sig* s, size_t pos, size_t n)
{
  memmove(s->buf + pos, s->buf + pos + n, s->len - pos - n);
  s->len -= n;
}
static void ed_changed(Sig* s)
{
  reindex(s);
  s->dirty = 1;
  s->ma = s->mb = 0;
  set_save_state(s);
  ensure_visible(s);
}

static void parse_type(Sig* s)
{
  const char* t = s->buf ? s->buf : "";
  char* z = (char*)malloc(s->len + 1);
  const char* p;
  int i = 0;
  s->type[0] = 0;
  if (!z) return;
  memcpy(z, t, s->len); z[s->len] = 0;
  p = strstr(z, "meta(");
  if (!p) p = strstr(z, "init(");
  if (p && (p = strchr(p, '"')) != NULL) {
    ++p;
    while (*p && *p != '"' && *p != '\n' && *p != '\r' && i < 63) s->type[i++] = *p++;
    s->type[i] = 0;
  }
  free(z);
}

static void load_file(Sig* s, const char* path)
{
  FILE* f = fopen_u8(path, "rb");
  long size;
  char* b;
  size_t got, i, j, skip = 0;

  if (!f) return;
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size < 0 || size > 8L * 1024 * 1024) { fclose(f); return; }
  b = (char*)malloc((size_t)size + 1);
  if (!b) { fclose(f); return; }
  got = fread(b, 1, (size_t)size, f);
  fclose(f);
  if (got >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB &&
    (unsigned char)b[2] == 0xBF) skip = 3;

  s->crlf = 0;
  for (i = skip, j = 0; i < got; ++i) {
    if (b[i] == '\r') {
      if (i + 1 < got && b[i + 1] == '\n') { ++i; s->crlf = 1; }
      b[j++] = '\n';
    }
    else b[j++] = b[i];
  }
  if (!buf_reserve(s, j)) { free(b); return; }
  memcpy(s->buf, b, j);                     
  s->len = j;
  free(b);

  strncpy(s->cur, path, sizeof(s->cur) - 1);
  s->cur[sizeof(s->cur) - 1] = 0;
  s->caret = 0; s->top = 0; s->hs = 0; s->ma = s->mb = 0;
  reindex(s);
  parse_type(s);
  s->dirty = 0;
  set_save_state(s);
  redraw(s);
}

static void save_file(Sig* s)
{
  FILE* f;
  size_t i, run = 0;
  int ok = 1;
  char msg[1100];

  if (!s->cur[0]) return;
  f = fopen_u8(s->cur, "wb");
  if (!f) { out_log(s, translate("Cannot save the file.")); return; }
  for (i = 0; i <= s->len && ok; ++i) {
    if (i == s->len || s->buf[i] == '\n') {
      if (i > run && fwrite(s->buf + run, 1, i - run, f) != i - run) ok = 0;
      if (i < s->len) {
        if (s->crlf) ok = ok && fwrite("\r\n", 1, 2, f) == 2;
        else ok = ok && fwrite("\n", 1, 1, f) == 1;
      }
      run = i + 1;
    }
  }
  if (fclose(f) != 0) ok = 0;
  if (ok) {
    s->dirty = 0;
    snprintf(msg, sizeof(msg), translate("Saved %s"), s->cur);
    out_log(s, msg);
  }
  else out_log(s, translate("Cannot save the file."));
  set_save_state(s);
  redraw(s);
}

static void do_find(Sig* s, int from_start)
{
  const char* needle = s->find.s;
  size_t nl = strlen(needle), i, k, start;
  if (!nl || !s->buf) return;
  start = from_start ? 0 : (s->mb > s->ma ? s->mb : s->caret);
  for (i = start; i + nl <= s->len; ++i) {
    for (k = 0; k < nl; ++k) {
      int x = (unsigned char)s->buf[i + k], y = (unsigned char)needle[k];
      if (x >= 'A' && x <= 'Z') x += 32;
      if (y >= 'A' && y <= 'Z') y += 32;
      if (x != y) break;
    }
    if (k == nl) goto found;
  }
  if (!from_start && start > 0) {                  
    for (i = 0; i < start && i + nl <= s->len; ++i) {
      for (k = 0; k < nl; ++k) {
        int x = (unsigned char)s->buf[i + k], y = (unsigned char)needle[k];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) break;
      }
      if (k == nl) goto found;
    }
  }
  out_log(s, translate("Text not found."));
  return;
found:
  s->ma = i; s->mb = i + nl; s->caret = s->mb;
  ensure_visible(s);
  redraw(s);
}

static char s_xl[MAXC * 4 + 8];
static int  s_xo[MAXC + 2];
static unsigned char s_cls[MAXC + 2];
static int s_px[MAXC + 2];
#define CH(i) ((unsigned char)s_xl[s_xo[i]])

static int expand_line(Sig* s, int line)
{
  size_t p = s->ls[line], e = line_end(s, line);
  int n = 0, off = 0;
  while (p < e && n < MAXC) {
    if (s->buf[p] == '\t') {
      int k = 4 - n % 4;
      while (k-- && n < MAXC) { s_xo[n] = off; s_xl[off++] = ' '; ++n; }
      ++p;
    }
    else {
      size_t q = u8next(s->buf, p, e);
      s_xo[n] = off;
      memcpy(s_xl + off, s->buf + p, q - p);
      off += (int)(q - p); p = q; ++n;
    }
  }
  s_xo[n] = off;
  return n;
}

static int is_kw(const char* w)
{
  static const char* kw[] = {
    "function", "var", "let", "const", "if", "else", "return", "true", "false",
    "null", "undefined", "for", "while", "do", "switch", "case", "break",
    "continue", "new", "typeof", "in", "try", "catch", "throw", "this"
  };
  size_t i;
  for (i = 0; i < sizeof(kw) / sizeof(kw[0]); ++i)
    if (strcmp(kw[i], w) == 0) return 1;
  return 0;
}
static void setc(int a, int b, int cl) { while (a < b) s_cls[a++] = (unsigned char)cl; }
static int isid(int c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || c >= 0x80; }

static void classify(int n, int inblk)
{
  int i = 0, j;
  memset(s_cls, CL_TXT, (size_t)n + 1);
  while (i < n) {
    int c = CH(i);
    if (inblk) {
      j = i;
      while (j < n && !(CH(j) == '*' && j + 1 < n && CH(j + 1) == '/')) ++j;
      if (j < n) { j += 2; inblk = 0; }
      setc(i, j, CL_COM); i = j;
    }
    else if (c == '/' && i + 1 < n && CH(i + 1) == '/') { setc(i, n, CL_COM); i = n; }
    else if (c == '/' && i + 1 < n && CH(i + 1) == '*') {
      j = i + 2;
      while (j < n && !(CH(j) == '*' && j + 1 < n && CH(j + 1) == '/')) ++j;
      if (j < n) j += 2; else inblk = 1;
      setc(i, j, CL_COM); i = j;
    }
    else if (c == '"' || c == '\'') {
      j = i + 1;
      while (j < n && CH(j) != c) { if (CH(j) == '\\' && j + 1 < n) ++j; ++j; }
      if (j < n) ++j;
      setc(i, j, CL_STR); i = j;
    }
    else if (c >= '0' && c <= '9') {
      j = i;
      while (j < n && (isid(CH(j)) || CH(j) == '.')) ++j;
      setc(i, j, CL_NUM); i = j;
    }
    else if (isid(c)) {
      char w[32];
      int k, len;
      j = i;
      while (j < n && isid(CH(j))) ++j;
      len = imin(j - i, 31);
      for (k = 0; k < len; ++k) w[k] = (char)CH(i + k);
      w[len] = 0;
      k = j;
      while (k < n && (CH(k) == ' ' || CH(k) == '\t')) ++k;
      if (j - i < 32 && is_kw(w)) setc(i, j, CL_KEY);
      else if (k < n && CH(k) == '(') setc(i, j, CL_FN);
      else if (j < n && CH(j) == '.' && c >= 'A' && c <= 'Z') setc(i, j, CL_OBJ);
      i = j;
    }
    else ++i;
  }
}

static void card(PlCanvas* c, PlRect r, int headH)
{
  pl_round_rect(c, r, SG_CODE, SG_BORDER, 12);
  pl_round_rect(c, mkr(r.l + 1, r.t + 1, r.r - 1, r.t + headH + 12), SG_PANEL, SG_PANEL, 11);
  pl_fill(c, r.l + 1, r.t + headH, r.r - 1, r.t + headH + 12, SG_CODE);
  pl_line(c, r.l + 1, r.t + headH, r.r - 1, r.t + headH, SG_SEP);
}

static void paint_btn(Sig* s, PlCanvas* c, int i)
{
  Btn bb = s->b[i];
  Btn* b = &bb;
  PlRect r = b->r, tr = r;
  int hov = s->hov == i, en = b->enabled, on = b->on, w = r.r - r.l, h = r.b - r.t;
  PlColor fill, bd, fg;

  if (r.r <= r.l) return;
  bb.text = translate(bb.text);

  switch (b->kind) {
  case K_ACCENT:
    fill = hov ? CLR_ACCENT_HOVER : SG_ACCENT;
    pl_round_rect(c, r, fill, fill, 8);
    txt(c, s->font, SG_WHITE, tr, 1, b->text);
    break;
  case K_NORMAL: case K_DROP:
    fill = !en ? SG_BTN : hov ? CLR_BTN_GHOST_HOVER : SG_BTN;
    bd = SG_BTNB;
    fg = en ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY;
    pl_round_rect(c, r, fill, bd, 8);
    if (b->kind == K_DROP) {
      int cx = r.r - 18, cy = r.t + h / 2;
      tr.l = r.l + 14;
      txt(c, s->font, fg, tr, 0, b->text);
      pl_line(c, cx - 4, cy - 2, cx, cy + 2, fg);
      pl_line(c, cx, cy + 2, cx + 4, cy - 2, fg);
    }
    else txt(c, s->font, fg, tr, 1, b->text);
    break;
  case K_CHIP:
    pl_round_rect(c, r, hov ? CLR_BTN_GHOST : SG_CHIP, SG_BTNB, h / 2);
    pl_ellipse(c, r.l + 12, r.t + h / 2 - 4, r.l + 20, r.t + h / 2 + 4,
      on ? SG_ACCENT : SG_BTNB, on ? SG_ACCENT : SG_BTNB);
    tr.l = r.l + 28;
    txt(c, s->font, CLR_TEXT_PRIMARY, tr, 0, b->text);
    break;
  case K_SWITCH: {
    int top = r.t + (h - 20) / 2, kx = r.l + (on ? 34 - 3 - 14 : 3);
    pl_round_rect(c, mkr(r.l, top, r.l + 34, top + 20), on ? SG_ACCENT : SG_BTNB,
      on ? SG_ACCENT : SG_BTNB, 10);
    pl_ellipse(c, kx, top + 3, kx + 14, top + 17, SG_WHITE, SG_WHITE);
    tr.l = r.l + 44;
    txt(c, s->font, SG_TEXT, tr, 0, b->text);
    break;
  }
  case K_LINK:
    txt(c, s->font, hov ? CLR_TEXT_PRIMARY : SG_TEXT2, tr, 2, b->text);
    break;
  }
  (void)w;
}

static void paint_edit(Sig* s, PlCanvas* c, Edit* e, PlRect tr, int focused, const char* cue)
{
  if (e->len == 0) {
    txt(c, s->font, SG_DIM, tr, 0, translate(cue));
  }
  else txt(c, s->font, SG_TEXT, tr, 0, e->s);
  if (focused) {
    char tmp[260];
    int x;
    memcpy(tmp, e->s, (size_t)e->caret); tmp[e->caret] = 0;
    x = tr.l + (e->caret ? pl_text_width(s->font, tmp) : 0);
    if (x < tr.r) pl_line(c, x, tr.t + 3, x, tr.b - 3, SG_TEXT);
  }
}

static void paint_tree(Sig* s, PlCanvas* c)
{
  int rows = imax(1, (s->r_tree.b - s->r_tree.t) / TROW), i;
  for (i = 0; i < rows; ++i) {
    int vi = s->ttop + i, y = s->r_tree.t + i * TROW, ni, x, cy;
    Node* n;
    PlColor col;
    PlRect tr;
    if (vi >= s->nvis) break;
    ni = s->vis[vi];
    n = node_at(s, ni);
    if (ni == s->tsel)
      pl_round_rect(c, mkr(s->r_tree.l + 2, y, s->r_tree.r - 2, y + TROW), SG_SEL, SG_SEL, 6);
    x = s->r_tree.l + 10 + n->depth * 18;
    cy = y + TROW / 2;
    col = ni == s->tsel ? SG_WHITE : n->isdir ? CLR_TEXT_PRIMARY : CLR_TEXT_PRIMARY;
    if (n->isdir) {
      if (n->open) {
        pl_line(c, x - 4, cy - 2, x, cy + 2, SG_TEXT2);
        pl_line(c, x, cy + 2, x + 4, cy - 2, SG_TEXT2);
      }
      else {
        pl_line(c, x - 2, cy - 4, x + 2, cy, SG_TEXT2);
        pl_line(c, x + 2, cy, x - 2, cy + 4, SG_TEXT2);
      }
    }
    tr = mkr(x + 12, y, s->r_tree.r - 6, y + TROW);
    txt(c, s->font, col, tr, 0, n->name);
  }

  if (s->nvis > rows) {
    int th = s->r_tree.b - s->r_tree.t, bh = imax(24, th * rows / s->nvis);
    int by = s->r_tree.t + (th - bh) * s->ttop / imax(1, s->nvis - rows);
    pl_round_rect(c, mkr(s->r_tree.r - 5, by, s->r_tree.r - 2, by + bh), SG_BTNB, SG_BTNB, 1);
  }
}

static void paint_editor(Sig* s, PlCanvas* c)
{
  int rows = ed_rows(s), i, textx = s->r_ed.l + 12, right = s->r_ed.r - 10;
  int cl = s->nl ? find_line(s, s->caret) : 0;

  pl_line(c, s->r_gut.r - 1, s->r_gut.t, s->r_gut.r - 1, s->r_gut.b, SG_SEP);
  if (!s->cur[0] || !s->buf) return;

  for (i = 0; i < rows; ++i) {
    int line = s->top + i, y = s->r_ed.t + i * LH, n, a, b, j, ox;
    char num[16];
    if (line >= s->nl) break;

    snprintf(num, sizeof(num), "%d", line + 1);
    txt(c, s->font, SG_DIM, mkr(s->r_gut.l, y, s->r_gut.r - 12, y + LH), 2, num);

    n = expand_line(s, line);
    classify(n, s->blk[line]);
    s_px[0] = 0;
    for (j = 0; j < n; ++j)
      s_px[j + 1] = s_px[j] + glyph_w(s, s_xl + s_xo[j], (size_t)(s_xo[j + 1] - s_xo[j]));
    ox = vx(s, line, s->hs);

    if (s->mb > s->ma && s->ma >= s->ls[line] && s->ma <= line_end(s, line)) {
      int xa = textx + vx(s, line, vcol_of(s, line, s->ma)) - ox;
      int xb = textx + vx(s, line, vcol_of(s, line, s->mb)) - ox;
      xa = imax(xa, textx); xb = imin(xb, right);
      if (xa < xb) pl_fill(c, xa, y, xb, y + LH, SG_SEL);
    }

    j = 0;
    while (j < n) {
      int k = j, x;
      while (k < n && s_cls[k] == s_cls[j]) ++k;
      a = imax(j, s->hs); b = k;
      x = textx + s_px[imin(a, n)] - ox;
      if (x >= right) break;
      if (a < b) {
        static char tmp[MAXC * 4 + 8];
        int len = s_xo[b] - s_xo[a];
        memcpy(tmp, s_xl + s_xo[a], (size_t)len);
        tmp[len] = 0;
        pl_text(c, s->font, mkr(x, y, right, y + LH), tmp, s_cls_col[s_cls[j]], PL_TEXT_LEFT);
      }
      j = k;
    }

    if (s->focus == F_ED && line == cl) {
      int vc = vcol_of(s, line, s->caret);
      int x = textx + vx(s, line, vc) - ox;
      if (vc >= s->hs && x < right) pl_line(c, x, y + 2, x, y + LH - 2, SG_TEXT);
    }
  }
  if (s->nl > rows) {
    int th = s->r_ed.b - s->r_ed.t, bh = imax(24, th * rows / s->nl);
    int by = s->r_ed.t + (th - bh) * s->top / imax(1, s->nl - rows);
    pl_round_rect(c, mkr(s->r_ed.r - 5, by, s->r_ed.r - 2, by + bh), SG_BTNB, SG_BTNB, 1);
  }
}

static void upd_notify(void* u)      
{
  redraw((Sig*)u);
}

static void upd_poll(Sig* s)
{
  if (s->upd.db_changed) { s->upd.db_changed = 0; rebuild_tree(s); }
#ifdef SIG_AUTO_UPDATE_TEST
  if (s->upd.phase == UPD_READY && !s->auto_done) {
    s->auto_done = 1;
    updater_install(&s->upd);
  }
#endif
}

static void upd_sync_btn(Sig* s)
{
  Btn* b = &s->b[B_UPD];
  switch (s->upd.phase) {
  case UPD_READY:    b->kind = K_ACCENT; b->text = "Update";      b->enabled = 1; break;
  case UPD_RUNNING:  b->kind = K_NORMAL; b->text = "Cancel";      b->enabled = 1; break;
  case UPD_CHECKING: b->kind = K_NORMAL; b->text = "Checking";    b->enabled = 0; break;
  case UPD_ERROR:    b->kind = K_NORMAL; b->text = "Retry";       b->enabled = 1; break;
  default:           b->kind = K_NORMAL; b->text = "Check again"; b->enabled = 1; break;
  }
  set_save_state(s);
}

static void paint_banner(Sig* s, PlCanvas* c)
{
  Updater* u = &s->upd;
  PlRect r = s->r_banner;
  int cy = (r.t + r.b) / 2, x = r.l + 16, ph = u->phase, bl = s->b[B_UPD].r.l - 16;
  char t[300];
  const char* title = "Signature database";
  PlColor green = PL_RGB(123, 224, 165), red = PL_RGB(242, 154, 147);
  PlColor dcol = SG_TEXT2;

  pl_round_rect(c, r, SG_PANEL, SG_BORDER, 12);

  if (ph == UPD_RUNNING) {
    int bx = x + 214, bw = bl - 56 - bx, fw = bw * u->pct / 100;
    snprintf(t, sizeof(t), "%s...", translate(u->label[0] ? u->label : "Updating"));
    txt(c, s->fontb, SG_TEXT, mkr(x, r.t, x + 204, r.b), 0, t);
    if (bw > 20) {
      pl_round_rect(c, mkr(bx, cy - 4, bx + bw, cy + 4), SG_BTNB, SG_BTNB, 4);
      if (fw > 8) pl_round_rect(c, mkr(bx, cy - 4, bx + fw, cy + 4), SG_ACCENT, SG_ACCENT, 4);
      snprintf(t, sizeof(t), "%d%%", u->pct);
      txt(c, s->font, SG_TEXT, mkr(bx + bw + 6, r.t, bl, r.b), 2, t);
    }
    return;
  }

  if (ph == UPD_UPTODATE || ph == UPD_DONE) {
    pl_ellipse(c, x, cy - 11, x + 22, cy + 11, PL_RGB(46, 158, 106), PL_RGB(46, 158, 106));
    pl_line(c, x + 6, cy, x + 10, cy + 4, SG_WHITE);
    pl_line(c, x + 10, cy + 4, x + 16, cy - 4, SG_WHITE);
    x += 34;
    dcol = SG_TEXT2;
    if (ph == UPD_DONE) {
      title = "Database updated";
      snprintf(t, sizeof(t), translate("Now at %s  \xC2\xB7  %d added, %d changed  \xC2\xB7  your db_custom was not changed"),
        u->installed, u->added, u->changed);
    }
    else {
      title = "Signature database is up to date";
      snprintf(t, sizeof(t), translate("Version %s"), u->installed);
    }
  }
  else {
    PlColor d = ph == UPD_ERROR ? red : ph == UPD_READY ? SG_ACCENT : SG_BTNB;
    pl_ellipse(c, x, cy - 4, x + 9, cy + 5, d, d);
    x += 23;
    if (ph == UPD_READY)
      snprintf(t, sizeof(t), translate("Installed %s  \xE2\x86\x92  latest %s"),
        u->installed[0] ? u->installed : translate("unknown"), u->latest);
    else if (ph == UPD_ERROR) { snprintf(t, sizeof(t), "%s", translate(u->error)); dcol = red; }
    else if (ph == UPD_CHECKING) snprintf(t, sizeof(t), "%s", translate("Checking for updates..."));
    else snprintf(t, sizeof(t), "%s", translate("Not checked yet"));
  }
  {
    const char* ttl = translate(title);
    int tw = pl_text_width(s->fontb, ttl);
    txt(c, s->fontb, SG_TEXT, mkr(x, r.t, x + tw + 4, r.b), 0, ttl);
    txt(c, s->font, ph == UPD_DONE ? green : dcol, mkr(x + tw + 16, r.t, bl, r.b), 0, t);
  }
}

static void on_paint(void* u, PlCanvas* c, int w, int h) {
  Sig* s = (Sig*)u;
  char t[96];
  const char* name;
  PlRect tr;
  int i;

  s->W = w; s->H = h;
  upd_poll(s);
  upd_sync_btn(s);
  pl_fill(c, 0, 0, w, h, SG_BG);

  pl_round_rect(c, s->r_left, SG_PANEL, SG_BORDER, 12);
  pl_round_rect(c, s->r_filter, SG_BG, s->focus == F_FILTER ? SG_ACCENT : SG_BORDER, 8);
  {
    int fx = s->r_filter.l, fy = s->r_filter.t;
    pl_ellipse(c, fx + 12, fy + 10, fx + 22, fy + 20, SG_BG, SG_TEXT2);
    pl_line(c, fx + 21, fy + 19, fx + 26, fy + 24, SG_TEXT2);
  }
  paint_edit(s, c, &s->filter,
    mkr(s->r_filter.l + 34, s->r_filter.t, s->r_filter.r - 56, s->r_filter.b),
    s->focus == F_FILTER, "Filter signatures");
  snprintf(t, sizeof(t), "%d", s->count);
  txt(c, s->font, SG_DIM, mkr(s->r_filter.r - 60, s->r_filter.t, s->r_filter.r - 10, s->r_filter.b), 2, t);
  paint_tree(s, c);

  pl_round_rect(c, s->r_findbox, SG_PANEL, s->focus == F_FIND ? SG_ACCENT : SG_BORDER, 8);
  paint_edit(s, c, &s->find,
    mkr(s->r_findbox.l + 12, s->r_findbox.t, s->r_findbox.r - 12, s->r_findbox.b),
    s->focus == F_FIND, "Find in script");

  card(c, s->r_edcard, HEAD_ED);
  name = s->cur[0] ? strrchr(s->cur, PSEP) : NULL;
  name = name ? name + 1 : (s->cur[0] ? s->cur : translate("No file selected"));
  {
    int nx = s->r_edcard.l + 14, nw = pl_text_width(s->fontb, name);
    txt(c, s->fontb, SG_TEXT, mkr(nx, s->r_edcard.t, nx + nw + 4, s->r_edcard.t + HEAD_ED), 0, name);
    if (s->cur[0] && s->type[0]) {
      int bw = pl_text_width(s->font, s->type) + 18;
      PlRect badge = mkr(nx + nw + 12, s->r_edcard.t + 9, nx + nw + 12 + bw, s->r_edcard.t + HEAD_ED - 9);
      pl_round_rect(c, badge, CLR_SIDEBAR_SEL, CLR_SIDEBAR_SEL, 10);
      txt(c, s->font, CLR_TEXT_ACCENT, badge, 1, s->type);
    }
    if (s->cur[0]) {
      snprintf(t, sizeof(t), translate("%d lines  \xC2\xB7  UTF-8"), s->nl);
      tr = mkr(s->r_edcard.r - 220, s->r_edcard.t, s->r_edcard.r - 14, s->r_edcard.t + HEAD_ED);
      txt(c, s->font, SG_TEXT2, tr, 2, t);
    }
  }
  paint_editor(s, c);

  pl_fill(c, s->r_bar.l, s->r_bar.t, s->r_bar.r, s->r_bar.b, SG_BAR);
  pl_line(c, s->r_bar.l, s->r_bar.t, s->r_bar.r, s->r_bar.t, SG_SEP);

  paint_banner(s, c);
  for (i = 0; i < NB; ++i) paint_btn(s, c, i);
  if (s->upd.phase == UPD_RUNNING)
    txt(c, s->font, SG_TEXT2, mkr(s->b[B_RO].r.r + 14, s->b[B_RO].r.t, s->r_findbox.l - 10, s->b[B_RO].r.b),
      0, translate("Editing is locked while the database updates"));

  if (s->popup) {
    PlRect p = mkr(s->b[B_DROP].r.l, s->b[B_DROP].r.t - 38, s->b[B_DROP].r.r, s->b[B_DROP].r.t - 4);
    pl_round_rect(c, p, SG_PANEL, SG_BORDER, 8);
    pl_ellipse(c, p.l + 12, (p.t + p.b) / 2 - 3, p.l + 18, (p.t + p.b) / 2 + 3, SG_ACCENT, SG_ACCENT);
    txt(c, s->font, SG_TEXT, mkr(p.l + 28, p.t, p.r - 8, p.b), 0, "detect");
  }
}

static void layout(Sig* s)
{
  const int m = 12, lw = 300, barH = 56, bh = 34, gap = 12;
  const int nextW = 70, findW = 70, boxW = 260;
  int W = s->W, H = s->H, bot = H - barH - m, rx, rw, y, top, gy, gh, by, ty;

  s->r_banner = mkr(m, m, W - m, m + BANNER_H);
  s->b[B_UPD].r = mkr(W - m - 16 - 110, m + 9, W - m - 16, m + 9 + bh);
  ty = m + BANNER_H + gap;

  s->r_left = mkr(m, ty, m + lw, bot);
  s->r_filter = mkr(m + 12, ty + 12, m + lw - 12, ty + 12 + bh);
  s->r_tree = mkr(m + 8, s->r_filter.b + 8, m + lw - 8, bot - 8);

  rx = m + lw + gap;
  rw = W - rx - m;
  if (rw < 300) rw = 300;
  y = ty;
  s->b[B_SAVE].r = mkr(rx, y, rx + 80, y + bh);
  s->b[B_RO].r = mkr(rx + 92, y, rx + 202, y + bh);
  s->b[B_NEXT].r = mkr(rx + rw - nextW, y, rx + rw, y + bh);
  s->b[B_FIND].r = mkr(rx + rw - nextW - 8 - findW, y, rx + rw - nextW - 8, y + bh);
  s->r_findbox = mkr(rx + rw - nextW - 8 - findW - 8 - boxW, y,
    rx + rw - nextW - 8 - findW - 8, y + bh);

  top = ty + bh + gap;
  s->r_edcard = mkr(rx, top, rx + rw, bot);

  gy = s->r_edcard.t + HEAD_ED + 8;
  gh = s->r_edcard.b - 8 - gy;
  s->r_gut = mkr(rx + 1, gy, rx + 53, gy + gh);
  s->r_ed = mkr(rx + 53, gy, rx + rw - 10, gy + gh);

  s->r_bar = mkr(0, H - barH, W, H);
  by = H - barH;
  s->b[B_CLOSE].r = mkr(W - 12 - 80, by + 11, W - 12, by + 11 + bh);
}

static void on_resize(void* u, int w, int h)
{
  Sig* s = (Sig*)u;
  s->W = w; s->H = h;
  layout(s);
  ensure_visible(s);
}

static int hit_btn(Sig* s, int x, int y)
{
  int i;
  for (i = 0; i < NB; ++i) if (inr(&s->b[i].r, x, y)) return i;
  return -1;
}

static void run_cmd(Sig* s, int id)
{
  switch (id) {
  case B_SAVE: if (s->b[B_SAVE].enabled) save_file(s); break;
  case B_RO: set_save_state(s); break;                 
  case B_FIND: do_find(s, 1); break;
  case B_NEXT: do_find(s, 0); break;
  case B_CLEAR: out_clear(s); break;
  case B_RUN: out_log(s, translate("Run is not connected to the scan engine yet.")); break;
  case B_DROP: s->popup = 1; break;
  case B_UPD:
    if (s->upd.phase == UPD_READY) updater_install(&s->upd);
    else if (s->upd.phase == UPD_RUNNING) updater_cancel(&s->upd);
    else updater_check(&s->upd);
    break;
  case B_CLOSE: s->done = 1; pl_window_close(s->win); break;
  default: break;                                      
  }
}

static void tree_select_pos(Sig* s, int pos)
{
  int rows = imax(1, (s->r_tree.b - s->r_tree.t) / TROW);
  if (pos < 0 || pos >= s->nvis) return;
  s->tsel = s->vis[pos];
  if (pos < s->ttop) s->ttop = pos;
  else if (pos >= s->ttop + rows) s->ttop = pos - rows + 1;
  if (!node_at(s, s->tsel)->isdir) load_file(s, node_at(s, s->tsel)->path);
}
static int tree_pos_of(Sig* s, int node)
{
  int i;
  for (i = 0; i < s->nvis; ++i) if (s->vis[i] == node) return i;
  return -1;
}

static void on_mouse_move(void* u, int x, int y)
{
  Sig* s = (Sig*)u;
  int h = hit_btn(s, x, y);
  if (h != s->hov) { s->hov = h; redraw(s); }
}
static void on_mouse_leave(void* u)
{
  Sig* s = (Sig*)u;
  if (s->hov != -1) { s->hov = -1; redraw(s); }
}

static void on_mouse_down(void* u, int x, int y)
{
  Sig* s = (Sig*)u;
  int h;

  if (s->popup) { s->popup = 0; redraw(s); return; } 

  h = hit_btn(s, x, y);
  if (h >= 0) {
    Btn* b = &s->b[h];
    if (!b->enabled) return;
    if (b->kind == K_CHIP || b->kind == K_SWITCH) b->on = !b->on;
    run_cmd(s, h);
    redraw(s);
    return;
  }
  if (inr(&s->r_filter, x, y)) { s->focus = F_FILTER; s->filter.caret = s->filter.len; }
  else if (inr(&s->r_findbox, x, y)) { s->focus = F_FIND; s->find.caret = s->find.len; }
  else if (inr(&s->r_tree, x, y)) {
    int row = s->ttop + (y - s->r_tree.t) / TROW;
    s->focus = F_TREE;
    if (row >= 0 && row < s->nvis) {
      Node* n = node_at(s, s->vis[row]);
      if (n->isdir) { n->open = !n->open; s->tsel = s->vis[row]; rebuild_vis(s); }
      else tree_select_pos(s, row);
    }
  }
  else if (inr(&s->r_ed, x, y) && s->cur[0]) {
    int line = imin(s->nl - 1, s->top + (y - s->r_ed.t) / LH);
    int vc = vc_at_x(s, line, x - (s->r_ed.l + 12) + vx(s, line, s->hs));
    s->focus = F_ED;
    s->caret = pos_of_vcol(s, line, imax(0, vc));
    s->ma = s->mb = 0;
  }
  redraw(s);
}

static int on_hand(void* u, int x, int y)
{
  Sig* s = (Sig*)u;
  int h = hit_btn(s, x, y);
  if (h >= 0) return s->b[h].enabled;
  if (inr(&s->r_tree, x, y)) return (s->ttop + (y - s->r_tree.t) / TROW) < s->nvis;
  return 0;
}

static void on_wheel(void* u, int x, int y, int delta, int mods)
{
  Sig* s = (Sig*)u;
  int n = -delta * 3;
  if (inr(&s->r_tree, x, y)) {
    int rows = imax(1, (s->r_tree.b - s->r_tree.t) / TROW);
    s->ttop = imax(0, imin(s->ttop + n, imax(0, s->nvis - rows)));
  }
  else if (inr(&s->r_ed, x, y) || inr(&s->r_gut, x, y)) {
    if (mods & PL_MOD_SHIFT) s->hs = imax(0, s->hs + n * 2);
    else s->top = imax(0, imin(s->top + n, imax(0, s->nl - ed_rows(s))));
  }
  else if (inr(&s->r_out, x, y)) {
    int rows = imax(1, (s->r_out.b - s->r_out.t) / OLH);
    s->oscroll = imax(0, imin(s->oscroll - n, imax(0, s->nout - rows)));
  }
  redraw(s);
}

static void ed_key(Sig* s, int key, uint32_t ch, int mods)
{
  int ro = s->b[B_RO].on, l, vc;
  size_t p;
  if (!s->cur[0] || !s->buf) return;
  if (s->upd.phase == UPD_RUNNING) return;          
  l = find_line(s, s->caret);

  switch (key) {
  case PL_KEY_LEFT:  if (s->caret > 0) s->caret = u8prev(s->buf, s->caret); break;
  case PL_KEY_RIGHT: s->caret = u8next(s->buf, s->caret, s->len); break;
  case PL_KEY_UP:
  case PL_KEY_DOWN:
  case PL_KEY_PGUP:
  case PL_KEY_PGDN: {
    int d = key == PL_KEY_UP ? -1 : key == PL_KEY_DOWN ? 1 : key == PL_KEY_PGUP ? -ed_rows(s) : ed_rows(s);
    int nlx = imax(0, imin(s->nl - 1, l + d));
    vc = vcol_of(s, l, s->caret);
    s->caret = pos_of_vcol(s, nlx, vc);
    break;
  }
  case PL_KEY_HOME: s->caret = s->ls[l]; break;
  case PL_KEY_END:  s->caret = line_end(s, l); break;
  case PL_KEY_CHAR:
    if (ro || (mods & PL_MOD_CTRL) || ch < 32) return;
    { char t[4]; int n = u8enc(ch, t); buf_ins(s, s->caret, t, (size_t)n); s->caret += (size_t)n; ed_changed(s); }
    break;
  case PL_KEY_TAB:
    if (ro) return;
    buf_ins(s, s->caret, "  ", 2); s->caret += 2; ed_changed(s);
    break;
  case PL_KEY_ENTER: {
    char t[256];
    size_t k = 0, q = s->ls[l];
    if (ro) return;
    t[k++] = '\n';                                    
    while (q < s->caret && k < 250 && (s->buf[q] == ' ' || s->buf[q] == '\t')) t[k++] = s->buf[q++];
    buf_ins(s, s->caret, t, k); s->caret += k; ed_changed(s);
    break;
  }
  case PL_KEY_BACK:
    if (ro || s->caret == 0) return;
    p = u8prev(s->buf, s->caret);
    buf_del(s, p, s->caret - p); s->caret = p; ed_changed(s);
    break;
  case PL_KEY_DEL:
    if (ro || s->caret >= s->len) return;
    p = u8next(s->buf, s->caret, s->len);
    buf_del(s, s->caret, p - s->caret); ed_changed(s);
    break;
  default: return;
  }
  ensure_visible(s);
}

static void on_key(void* u, int key, uint32_t ch, int mods)
{
  Sig* s = (Sig*)u;

  if (key == PL_KEY_ESC) {
    if (s->popup) s->popup = 0;
    else { s->done = 1; pl_window_close(s->win); return; }
    redraw(s); return;
  }
  if (key == PL_KEY_F3) { do_find(s, 0); return; }
  if (key == PL_KEY_CHAR && (mods & PL_MOD_CTRL)) {
    if (ch == 's' || ch == 'S') { if (s->b[B_SAVE].enabled) save_file(s); return; }
    if (ch == 'f' || ch == 'F') { s->focus = F_FIND; s->find.caret = s->find.len; redraw(s); return; }
  }

  if (s->focus == F_FILTER) {
    if (e_key(&s->filter, key, ch, mods)) rebuild_tree(s);   
  }
  else if (s->focus == F_FIND) {
    if (key == PL_KEY_ENTER) do_find(s, 0);
    else e_key(&s->find, key, ch, mods);
  }
  else if (s->focus == F_ED) ed_key(s, key, ch, mods);
  else {                                                      
    int pos = s->tsel >= 0 ? tree_pos_of(s, s->tsel) : -1;
    Node* n = pos >= 0 ? node_at(s, s->tsel) : NULL;
    if (key == PL_KEY_DOWN) tree_select_pos(s, pos < 0 ? 0 : pos + 1);
    else if (key == PL_KEY_UP) tree_select_pos(s, pos < 0 ? 0 : pos - 1);
    else if (n && n->isdir && (key == PL_KEY_RIGHT || key == PL_KEY_ENTER)) { n->open = !n->open || key == PL_KEY_RIGHT; rebuild_vis(s); }
    else if (n && n->isdir && key == PL_KEY_LEFT) { n->open = 0; rebuild_vis(s); }
  }
  redraw(s);
}

static void on_destroy(void* u)
{
  Sig* s = (Sig*)u;
  updater_shutdown(&s->upd);               
  node_trunc(s, 0);
  free(s->nodes); free(s->vis);
  free(s->buf); free(s->ls); free(s->blk);
  out_clear(s);
  if (s->font) pl_font_destroy(s->font);
  if (s->fontb) pl_font_destroy(s->fontb);
  
  s_sig = NULL;
  free(s);
}

static const PlWidgetOps s_ops = {
  on_paint, on_mouse_move, on_mouse_leave, on_mouse_down,
  on_resize, on_hand, on_destroy,
  on_key, on_wheel
};

static void on_win_size(void* u, int w, int h)
{
  Sig* s = (Sig*)u;
  s->W = w; s->H = h;
  pl_widget_set_rect(s->w, 0, 0, w, h);
}
static void on_win_close(void* u)
{
  Sig* s = (Sig*)u;
  s->done = 1;
  pl_window_close(s->win);
}

static void init_btn(Sig* s, int id, int kind, const char* text, int on, int enabled)
{
  s->b[id].kind = kind; s->b[id].text = text; s->b[id].on = on; s->b[id].enabled = enabled;
}

int signatures_handle_event(const xxwidgets_event* event)
{
  (void)event;
  return 0;   
}

void signatures_show(xxwidgets_app* app, xxwidgets_widget* owner)
{
  void* ow = xxwidgets_widget_native_handle(owner);
  Sig* s;
  PlWindow* win;
  int i;
  (void)app;

  if (s_sig) return;
  s = (Sig*)calloc(1, sizeof(*s));
  if (!s) return;

  s->W = 1280; s->H = 780; s->hov = -1; s->tsel = -1; s->focus = F_TREE;
  s->font = pl_font_create(9, 0);
  s->fontb = pl_font_create(10, 1);
  if (!s->font || !s->fontb) { on_destroy(s); return; }
  {
    int ci;
    for (ci = 32; ci < 127; ++ci) { char t[2]; t[0] = (char)ci; t[1] = 0; s->aw[ci] = pl_text_width(s->font, t); }
    s->sp = imax(1, s->aw[' ']);
    for (ci = 0; ci < 32; ++ci) s->aw[ci] = s->sp;
    s->aw[127] = s->sp;
  }

  init_btn(s, B_SAVE, K_NORMAL, translate("Save"), 0, 0);
  init_btn(s, B_RO, K_SWITCH, translate("Readonly"), 1, 1);
  init_btn(s, B_FIND, K_NORMAL, translate("Find"), 0, 1);
  init_btn(s, B_NEXT, K_NORMAL, translate("Next"), 0, 1);
  init_btn(s, B_CLEAR, K_LINK, translate("Clear result"), 0, 1);
  for (i = 0; i < NCHIPS; ++i) init_btn(s, B_CH0 + i, K_CHIP, s_chip_text[i], i < 4, 1);
  init_btn(s, B_DROP, K_DROP, translate("detect"), 0, 1);
  init_btn(s, B_RUN, K_ACCENT, translate("Run"), 0, 1);
  init_btn(s, B_CLOSE, K_NORMAL, translate("Close"), 0, 1);
  init_btn(s, B_UPD, K_NORMAL, translate("Checking"), 0, 0);

  win = pl_window_create(ow, translate("Signatures"), 1280, 780, 1000, 640, on_win_size, on_win_close, s);
  if (!win) { on_destroy(s); return; }
  s->win = win;
  s->w = pl_widget_create(pl_window_client(win), &s_ops, s, 0, 0, 1280, 780);
  if (!s->w) { pl_window_destroy(win); on_destroy(s); return; }
  s_sig = s;
  updater_init(&s->upd, upd_notify, s);
  updater_check(&s->upd);                   

  layout(s);
  rebuild_tree(s);
  pl_window_run_modal(win);              

  pl_widget_destroy(s->w);               
  pl_window_destroy(win);
}