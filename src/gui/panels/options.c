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
#include <stdint.h>

#include "platform/platform.h"
#include "panels/options.h"
#include "core/gui_backend.h"
#include "core/config.h"
#include "core/i18n.h"
#include "theme/theme.h"
#include "core/virustotal.h"
#ifdef _WIN32
#include <windows.h>
#endif

#ifndef _WIN32
#define _strdup strdup
#endif

#define HEAD    44
#undef NAV_H
#define NAV_H   38
#define ROW_H   34
#define MAXIT   40
#define NCH     22  

#define DD_ROW 26
#define DD_MAXVIS 8


enum { PG_SCAN, PG_DB, PG_OPT, PG_FONTS, PG_LANG, PG_VT, PG_CTX, PG_UPDATE, PG_ABOUT, PG_COUNT };

enum {
  IK_HEAD, IK_TEXT, IK_NOTE, IK_CHECK, IK_BTN, IK_ACCENT, IK_VALUE, IK_PATH, IK_FVAL, IK_LVAL,
  IK_PARA, IK_SCROLL 
};

enum { DD_FONT, DD_LANG };

enum {
  ID_NONE,
  ID_DEEP, ID_HEUR, ID_AGGR, ID_VERBOSE, ID_HIDE, ID_FORMAT, ID_MONITOR,
  ID_DBX, ID_DBC,
  ID_SSE2, ID_AVX2,
  ID_BUF_DEC, ID_BUF_INC, ID_FBUF_DEC, ID_FBUF_INC, ID_BUF_VAL, ID_FBUF_VAL,
  ID_PATH_MAIN, ID_PATH_EXTRA, ID_PATH_CUSTOM, ID_PATH_VT,
  ID_BROWSE_MAIN, ID_BROWSE_EXTRA, ID_BROWSE_CUSTOM, ID_PASTE_VT,
  ID_FCHOOSE0, ID_FCHOOSE1, ID_FCHOOSE2, ID_FCHOOSE3,
  ID_FDEF0, ID_FDEF1, ID_FDEF2, ID_FDEF3,
  ID_FVAL0, ID_FVAL1, ID_FVAL2, ID_FVAL3,
  ID_LANGVAL,
  ID_CTX_TOGGLE,
  ID_CHECK_UPDATE,
  ID_OPEN_SITE, ID_OPEN_GITHUB, ID_OPEN_RELEASES, ID_COPY_BOX,
  ID_OK, ID_CANCEL, ID_APPLY
};

typedef struct { int kind, id, enabled; const char* text; PlRect r; } Item;

typedef struct Opt {
  PlWidget* w;
  PlWindow* win;
  PlFont* font, * fontb;
  OptionsHost host;
  OptionsScan scan;
  OptionsOptimization opt;
  int have_opt;
  int W, H, page, hov, navhov, result;
  int navpg[PG_COUNT], nnav;
  PlRect r_nav, r_card, r_bar;
  Item it[MAXIT];
  int nit;
  char msg[160];
  char dbpath[4][1024];   /* [3] = VirusTotal API key */
  int  dbfocus;
  int  dbdirty;
  OptionsFont fonts[OPTIONS_FONT_ROLES];
  int  have_fonts, fontsdirty;
  int  have_ctx, ctxenabled, ctxactual, ctxdirty;
  char ctxmsg[96];
  char verline[96];
  char updmsg[160];
  char aboutmsg[96];
  char buildline[64], copyline[64];
  int  about_scroll, about_lines, about_vis;
  PlRect about_in;
  int  about_sel;
  char** fnames;
  int nfnames, capfnames, fonts_loaded;
  int dd_open, dd_kind, dd_role, dd_scroll, dd_hov;
  int nlang, caplang, langs_loaded;
  char** lcodes; char** lnames;
  char lang_cur[32];
  int langdirty;
  PlRect dd_r;
} Opt;

static Opt* s_opt;

static const char* s_title[PG_COUNT] =
{ "Scan", "Databases", "Optimization", "Fonts", "Language", "VirusTotal", "Context menu", "Update", "About" };
static const char* s_sub[PG_COUNT] = {
  "How files are scanned and how results are shown",
  "Signature databases used by the scan engine",
  "Buffer sizes and CPU acceleration",
  "Interface fonts",
  "Interface language",
  "API key for file reputation lookups",
  "Explorer integration",
  "Check for updates",
  "Version, license, and project info"
};

static PlRect mkr(int l, int t, int r, int b) { PlRect x; x.l = l; x.t = t; x.r = r; x.b = b; return x; }
static int inr(const PlRect* r, int x, int y) { return x >= r->l && x < r->r && y >= r->t && y < r->b; }
static int imax(int a, int b) { return a > b ? a : b; }

static int nav_yoff(Opt* o, int i)
{
  int pg = o->navpg[i];
  if (pg == PG_UPDATE || pg == PG_ABOUT) {
    int below = o->nnav - 1 - i;
    return (o->r_nav.b - o->r_nav.t) - 12 - (below + 1) * NAV_H;
  }
  return 12 + i * NAV_H;
}

static void redraw(Opt* o)
{
  PlRect r = mkr(0, 0, o->W, o->H);
  pl_invalidate(o->w, &r);
}

static void update_ctxmsg(Opt* o)
{
  snprintf(o->ctxmsg, sizeof(o->ctxmsg), "%s",
    o->ctxactual ? "Currently registered for the current user."
    : "Currently not registered.");
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

#define ABOUT_INFO \
  "Bugreports: horsicq@gmail.com\n" \
  "Website: http://ntinfo.biz\n" \
  "Source code: https://github.com/horsicq/CDiE\n" \
  "GitHub: https://github.com/horsicq\n" \
  "Twitter: https://twitter.com/horsicq\n" \
  "YouTube: https://www.youtube.com/@funreverseengineering\n" \
  "Donate (PayPal): ntinfo.re@gmail.com\n" \
  "Donate (BTC): 3DqddVBX9PKqMvNPXZ3gPHBNNRtD9CnmJo"
#define ABOUT_LIBS \
  "xxfclib: formats, scan engine and runtime\n" \
  "xxwidgets: native widgets and dialogs\n" \
  "cdisasm: disassembler"
#define ABOUT_LICENSE \
  "MIT License\n\n" \
  "Permission is hereby granted, free of charge, to any person obtaining a copy " \
  "of this software and associated documentation files (the Software), to deal " \
  "in the Software without restriction, including without limitation the rights " \
  "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell " \
  "copies of the Software, and to permit persons to whom the Software is " \
  "furnished to do so, subject to the following conditions:\n\n" \
  "The above copyright notice and this permission notice shall be included in all " \
  "copies or substantial portions of the Software.\n\n" \
  "THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR " \
  "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, " \
  "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE " \
  "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER " \
  "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, " \
  "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE."
#define ABOUT_BOX \
  "INFO\n" ABOUT_INFO "\n\nLIBRARIES\n" ABOUT_LIBS "\n\nLICENSE\n" ABOUT_LICENSE

static int s_pick = -1;
static char s_pick_buf[512];

static void emit_line(PlCanvas* c, PlFont* f, PlColor col, PlRect r, int lh,
  int skip, int draw, int* lines, const char* s)
{
  if (s_pick >= 0 && *lines == s_pick)
    snprintf(s_pick_buf, sizeof(s_pick_buf), "%s", s);
  if (draw && *lines >= skip) {
    int y = r.t + (*lines - skip) * lh;
    if (y + lh <= r.b && s[0])
      txt(c, f, col, mkr(r.l, y, r.r, y + lh), 0, s);
  }
  ++ * lines;
}

static int wrap_text(PlCanvas* c, PlFont* f, PlColor col, PlRect r,
  const char* text, int lh, int skip, int draw)
{
  char line[512], word[256], cand[768];
  int width = r.r - r.l, lines = 0;
  const char* p = text;
  line[0] = 0;
  for (;;) {
    if (*p == '\n' || *p == 0) {
      emit_line(c, f, col, r, lh, skip, draw, &lines, line);
      line[0] = 0;
      if (*p == 0) break;
      ++p;
      continue;
    }
    if (*p == ' ') { ++p; continue; }
    {
      size_t wl = 0;
      while (*p && *p != ' ' && *p != '\n' && wl < sizeof(word) - 1) word[wl++] = *p++;
      word[wl] = 0;
      if (line[0]) snprintf(cand, sizeof(cand), "%s %s", line, word);
      else snprintf(cand, sizeof(cand), "%s", word);
      if (line[0] && pl_text_width(f, cand) > width) {
        emit_line(c, f, col, r, lh, skip, draw, &lines, line);
        snprintf(line, sizeof(line), "%s", word);
      }
      else {
        snprintf(line, sizeof(line), "%s", cand);
      }
    }
  }
  return lines;
}

static void to_crlf(const char* s, char* d, size_t cap)
{
  size_t n = 0;
  while (*s && n + 3 < cap) {
    if (*s == '\n') d[n++] = '\r';
    d[n++] = *s++;
  }
  d[n] = 0;
}

static int about_line_at(Opt* o, int x, int y, char* out, size_t cap)
{
  PlRect in = o->about_in;
  int line, lines;
  out[0] = 0;
  if (o->page != PG_ABOUT || in.r <= in.l ||
    x < in.l || x >= in.r || y < in.t || y >= in.b) return -1;
  line = o->about_scroll + (y - in.t) / 18;
  s_pick = line;
  s_pick_buf[0] = 0;
  lines = wrap_text(NULL, o->font, CLR_TEXT_SECONDARY, in, ABOUT_BOX, 18, 0, 0);
  s_pick = -1;
  if (line >= lines) return -1;
  snprintf(out, cap, "%s", s_pick_buf);
  return line;
}

static void card(PlCanvas* c, PlRect r, int headH)
{
  pl_round_rect(c, r, CLR_PANEL_BODY, CLR_PANEL_BORDER, 12);
  pl_round_rect(c, mkr(r.l + 1, r.t + 1, r.r - 1, r.t + headH + 12), CLR_PANEL_HEADER, CLR_PANEL_HEADER, 11);
  pl_fill(c, r.l + 1, r.t + headH, r.r - 1, r.t + headH + 12, CLR_PANEL_BODY);
  pl_line(c, r.l + 1, r.t + headH, r.r - 1, r.t + headH, CLR_SEPARATOR);
}

static unsigned long long choice(int i)
{
  return i == 0 ? 0ULL : (1024ULL << (i - 1));
}

static size_t size_step(size_t v, int dir)
{
  unsigned long long cur = (unsigned long long)v;
  int i;
  if (dir > 0) {
    for (i = 0; i < NCH; ++i) if (choice(i) > cur) return (size_t)choice(i);
  }
  else {
    for (i = NCH - 1; i >= 0; --i) if (choice(i) < cur) return (size_t)choice(i);
  }
  return v;
}

static void size_label(size_t v, char* out, size_t cap)
{
  unsigned long long n = (unsigned long long)v;
  if (n == 0) snprintf(out, cap, "Default (64 KiB)");
  else if (n % (1ULL << 30) == 0) snprintf(out, cap, "%llu GiB", n >> 30);
  else if (n % (1ULL << 20) == 0) snprintf(out, cap, "%llu MiB", n >> 20);
  else if (n % 1024ULL == 0) snprintf(out, cap, "%llu KiB", n >> 10);
  else snprintf(out, cap, "%llu bytes", n);
}

static void add(Opt* o, int kind, int id, const char* text, int enabled,
  int l, int t, int r, int b);
static void add_stepper(Opt* o, const char* label, int dec, int inc, int val,
  int x, int w, int y);
static void paste_append(char* s, size_t cap);

static int* chk_ptr(Opt* o, int id)
{
  switch (id) {
  case ID_DEEP:    return &o->scan.deep;
  case ID_HEUR:    return &o->scan.heuristic;
  case ID_AGGR:    return &o->scan.aggressive;
  case ID_VERBOSE: return &o->scan.verbose;
  case ID_HIDE:    return &o->scan.hide_unknown;
  case ID_FORMAT:  return &o->scan.format;
  case ID_MONITOR: return &o->scan.monitor_downloads;
  case ID_DBX:     return &o->scan.db_extra;
  case ID_DBC:     return &o->scan.db_custom;
  case ID_SSE2:    return &o->opt.use_sse2;
  case ID_AVX2:    return &o->opt.use_avx2;
  case ID_CTX_TOGGLE: return &o->ctxenabled;
  default:         return NULL;
  }
}

static void build_items(Opt* o)
{
  int x = o->r_card.l + 24;
  int w = o->r_card.r - o->r_card.l - 48;
  int y = o->r_card.t + HEAD + 16;
  int by = o->H - 56 + 11;

  o->nit = 0;
  switch (o->page) {
  case PG_SCAN:
    add(o, IK_HEAD, 0, "SCAN BEHAVIOR", 1, x, y, x + w, y + 20); y += 28;
    add(o, IK_CHECK, ID_DEEP, "Deep scan", 1, x, y, x + w, y + ROW_H); y += ROW_H;
    add(o, IK_CHECK, ID_HEUR, "Heuristic scan", 1, x, y, x + w, y + ROW_H); y += ROW_H;
    add(o, IK_CHECK, ID_AGGR, "Aggressive scan", 1, x, y, x + w, y + ROW_H); y += ROW_H + 14;
    add(o, IK_HEAD, 0, "RESULTS", 1, x, y, x + w, y + 20); y += 28;
    add(o, IK_CHECK, ID_VERBOSE, "Verbose output", 1, x, y, x + w, y + ROW_H); y += ROW_H;
    add(o, IK_CHECK, ID_HIDE, "Hide unknown", 1, x, y, x + w, y + ROW_H); y += ROW_H;
    add(o, IK_CHECK, ID_FORMAT, "Format result", 1, x, y, x + w, y + ROW_H); y += ROW_H + 14;
    add(o, IK_HEAD, 0, "WINDOW", 1, x, y, x + w, y + 20); y += 28;
    add(o, IK_CHECK, ID_MONITOR, "Monitor downloads", 1, x, y, x + w, y + ROW_H);
    break;

  case PG_DB:
    add(o, IK_HEAD, 0, "OPTIONAL DATABASES", 1, x, y, x + w, y + 20); y += 28;
    add(o, IK_CHECK, ID_DBX, "Extra database", 1, x, y, x + w, y + ROW_H); y += ROW_H;
    add(o, IK_CHECK, ID_DBC, "Custom database", 1, x, y, x + w, y + ROW_H); y += ROW_H;
    add(o, IK_NOTE, 0, "The main database is always loaded.", 1, x, y, x + w, y + 22); y += 36;
    add(o, IK_HEAD, 0, "FOLDERS", 1, x, y, x + w, y + 20); y += 28;
    add(o, IK_TEXT, 0, "Main", 1, x, y, x + 70, y + 30);
    add(o, IK_PATH, ID_PATH_MAIN, "", 1, x + 80, y, x + w - 90, y + 30);
    add(o, IK_BTN, ID_BROWSE_MAIN, "Browse", 1, x + w - 82, y, x + w, y + 30); y += 38;
    add(o, IK_TEXT, 0, "Extra", 1, x, y, x + 70, y + 30);
    add(o, IK_PATH, ID_PATH_EXTRA, "", 1, x + 80, y, x + w - 90, y + 30);
    add(o, IK_BTN, ID_BROWSE_EXTRA, "Browse", 1, x + w - 82, y, x + w, y + 30); y += 38;
    add(o, IK_TEXT, 0, "Custom", 1, x, y, x + 70, y + 30);
    add(o, IK_PATH, ID_PATH_CUSTOM, "", 1, x + 80, y, x + w - 90, y + 30);
    add(o, IK_BTN, ID_BROWSE_CUSTOM, "Browse", 1, x + w - 82, y, x + w, y + 30);
    break;

  case PG_OPT:
    if (!o->have_opt) {
      add(o, IK_NOTE, 0, "Optimization settings are not available.", 1, x, y, x + w, y + 22);
      break;
    }
    add(o, IK_HEAD, 0, "BUFFERS", 1, x, y, x + w, y + 20); y += 32;
    add_stepper(o, "Buffer size", ID_BUF_DEC, ID_BUF_INC, ID_BUF_VAL, x, w, y); y += 42;
    add_stepper(o, "File buffer size", ID_FBUF_DEC, ID_FBUF_INC, ID_FBUF_VAL, x, w, y); y += 54;
    add(o, IK_HEAD, 0, "ACCELERATION", 1, x, y, x + w, y + 20); y += 28;
    add(o, IK_CHECK, ID_SSE2, o->opt.has_sse2 ? "SSE2" : "SSE2 (not supported on this system)",
      o->opt.has_sse2, x, y, x + w, y + ROW_H); y += ROW_H;
    add(o, IK_CHECK, ID_AVX2, o->opt.has_avx2 ? "AVX2" : "AVX2 (not supported on this system)",
      o->opt.has_avx2, x, y, x + w, y + ROW_H);
    break;

  case PG_FONTS: {
    static const char* rn[OPTIONS_FONT_ROLES] =
    { "Controls", "Table views", "Tree views", "Text edits" };
    int k;
    if (!o->have_fonts) {
      add(o, IK_NOTE, 0, "Font settings are not available.", 1, x, y, x + w, y + 22);
      break;
    }
    add(o, IK_HEAD, 0, "INTERFACE FONTS", 1, x, y, x + w, y + 20); y += 32;
    for (k = 0; k < OPTIONS_FONT_ROLES; ++k) {
      add(o, IK_TEXT, 0, rn[k], 1, x, y, x + 110, y + 30);
      add(o, IK_FVAL, ID_FVAL0 + k, "", 1, x + 120, y, x + w - 88, y + 30);
      add(o, IK_BTN, ID_FDEF0 + k, "Default", 1, x + w - 80, y, x + w, y + 30);
      y += 42;
    }
    y += 8;
    add(o, IK_NOTE, 0, "Fonts are applied when you press Apply or OK.", 1, x, y, x + w, y + 22);
    break;
  }
  case PG_LANG:
    add(o, IK_HEAD, 0, translate("INTERFACE LANGUAGE"), 1, x, y, x + w, y + 20); y += 32;
    add(o, IK_TEXT, 0, translate("Language"), 1, x, y, x + 150, y + 30);
    add(o, IK_LVAL, ID_LANGVAL, "", 1, x + 160, y, x + w, y + 30); y += 50;
    add(o, IK_NOTE, 0, translate("Language files are loaded from the \"lang\" folder."), 1, x, y, x + w, y + 22);
    break;

  case PG_VT:
    add(o, IK_HEAD, 0, "API KEY", 1, x, y, x + w, y + 20); y += 32;
    add(o, IK_TEXT, 0, "API key", 1, x, y, x + 110, y + 30);
    add(o, IK_PATH, ID_PATH_VT, "", 1, x + 120, y, x + w - 90, y + 30);
    add(o, IK_BTN, ID_PASTE_VT, "Paste", 1, x + w - 82, y, x + w, y + 30); y += 46;
    add(o, IK_NOTE, 0, "Free key: virustotal.com > profile > API key. Click the box and type or press Ctrl+V.", 1, x, y, x + w, y + 22); y += 26;
    add(o, IK_NOTE, 0, "Only the SHA-256 hash is sent, never the file.", 1, x, y, x + w, y + 22);
    break;

  case PG_CTX:
    add(o, IK_TEXT, 0, translate("Add or remove the \"Scan with cdie\" entry in the Explorer file menu."),
      1, x, y, x + w, y + 24); y += 44;
    if (!o->have_ctx) {
      add(o, IK_NOTE, 0, translate("Explorer integration is not available."), 1, x, y, x + w, y + 22);
      break;
    }
    add(o, IK_CHECK, ID_CTX_TOGGLE, translate("Show \"Scan with cdie\" in the Explorer menu"),
      1, x, y, x + w, y + ROW_H); y += ROW_H + 8;
    add(o, IK_NOTE, 0, o->ctxmsg, 1, x, y, x + w, y + 22);
    break;

  case PG_UPDATE:
    add(o, IK_HEAD, 0, translate("VERSION"), 1, x, y, x + w, y + 20); y += 28;
    add(o, IK_TEXT, 0, o->verline, 1, x, y, x + w, y + 26); y += 44;
    add(o, IK_BTN, ID_CHECK_UPDATE, translate("Check for updates"), 1, x, y, x + 170, y + 34); y += 46;
    add(o, IK_NOTE, 0, o->updmsg, 1, x, y, x + w, y + 22);
    break;

  case PG_ABOUT: {
    int bh;
    add(o, IK_HEAD, 0, translate("DETECT IT EASY - CDIE"), 1, x, y, x + w, y + 20); y += 26;
    add(o, IK_TEXT, 0, o->verline, 1, x, y, x + w, y + 22); y += 22;
    add(o, IK_NOTE, 0, o->buildline, 1, x, y, x + w, y + 22); y += 22;
    add(o, IK_NOTE, 0, o->copyline, 1, x, y, x + w, y + 22); y += 30;
    add(o, IK_BTN, ID_OPEN_SITE, translate("Website"), 1, x, y, x + 84, y + 32);
    add(o, IK_BTN, ID_OPEN_GITHUB, translate("GitHub"), 1, x + 92, y, x + 176, y + 32);
    add(o, IK_BTN, ID_OPEN_RELEASES, translate("Releases"), 1, x + 184, y, x + 268, y + 32);
    add(o, IK_BTN, ID_COPY_BOX, translate("Copy text"), 1, x + 276, y, x + 360, y + 32); y += 36;
    add(o, IK_NOTE, 0, o->aboutmsg, 1, x, y, x + w, y + 20); y += 24;
    bh = o->r_card.b - 16 - y;
    if (bh < 60) bh = 60;
    add(o, IK_SCROLL, 0, "", 1, x, y, x + w, y + bh);
    break;
  }

  default: break;
  }

  add(o, IK_ACCENT, ID_OK, translate("OK"), 1, o->W - 12 - 80, by, o->W - 12, by + 34);
  add(o, IK_BTN, ID_APPLY, translate("Apply"), 1, o->W - 12 - 80 - 8 - 80, by, o->W - 12 - 80 - 8, by + 34);
  add(o, IK_BTN, ID_CANCEL, translate("Cancel"), 1, o->W - 12 - 3 * 80 - 16, by, o->W - 12 - 2 * 80 - 16, by + 34);
}

static int apply_all(Opt* o)
{
  int ok = 1;
  o->msg[0] = 0;

  {
    CdieConfig cfg;
    int k;
    config_load(&cfg);
    snprintf(cfg.main_db, sizeof(cfg.main_db), "%s", o->dbpath[0]);
    snprintf(cfg.extra_db, sizeof(cfg.extra_db), "%s", o->dbpath[1]);
    snprintf(cfg.custom_db, sizeof(cfg.custom_db), "%s", o->dbpath[2]);
    snprintf(cfg.vt_key, sizeof(cfg.vt_key), "%s", o->dbpath[3]);
    cfg.has_prefs = 1;
    snprintf(cfg.language, sizeof(cfg.language), "%s", o->lang_cur);
    cfg.deep = o->scan.deep;           cfg.heuristic = o->scan.heuristic;
    cfg.verbose = o->scan.verbose;     cfg.aggressive = o->scan.aggressive;
    cfg.hide_unknown = o->scan.hide_unknown; cfg.format = o->scan.format;
    cfg.db_extra = o->scan.db_extra;   cfg.db_custom = o->scan.db_custom;
    cfg.monitor_downloads = o->scan.monitor_downloads;
    if (o->have_opt) {
      cfg.use_sse2 = o->opt.use_sse2;  cfg.use_avx2 = o->opt.use_avx2;
      cfg.buffer_size = (unsigned long long)o->opt.buffer_size;
      cfg.file_buffer_size = (unsigned long long)o->opt.file_buffer_size;
    }
    if (o->have_fonts)
      for (k = 0; k < OPTIONS_FONT_ROLES && k < CDIE_FONT_ROLES; ++k) {
        snprintf(cfg.font_family[k], sizeof(cfg.font_family[k]), "%s", o->fonts[k].family);
        cfg.font_size[k] = o->fonts[k].point_size;
        cfg.font_bold[k] = o->fonts[k].bold;
        cfg.font_italic[k] = o->fonts[k].italic;
      }
    if (!config_save(&cfg)) {
      const char* cp = config_file_path();
      snprintf(o->msg, sizeof(o->msg), translate("Could not save settings to %s"), cp ? cp : "(unknown path)");
      return 0;
    }
  }

  vt_set_api_key(o->dbpath[3]);
  if (o->host.set_scan && !o->host.set_scan(o->host.user, &o->scan)) {
    snprintf(o->msg, sizeof(o->msg), translate("Saved, but could not apply the scan options."));
    ok = 0;
  }
  if (o->have_opt && o->host.set_optimization &&
    !o->host.set_optimization(o->host.user, &o->opt)) {
    snprintf(o->msg, sizeof(o->msg), translate("Saved, but could not apply the optimization settings."));
    ok = 0;
  }
  if (o->dbdirty) {
    if (!gui_backend_set_db_paths(o->dbpath[0], o->dbpath[1], o->dbpath[2])) {
      snprintf(o->msg, sizeof(o->msg), translate("Cannot load the main signature database. Check its folder."));
      return 0;
    }
    o->dbdirty = 0;
    if (o->host.db_changed) o->host.db_changed(o->host.user);
  }
  if (o->fontsdirty && o->host.set_fonts) {
    if (!o->host.set_fonts(o->host.user, o->fonts)) {
      snprintf(o->msg, sizeof(o->msg), translate("Saved, but could not apply the fonts."));
      ok = 0;
    }
    else o->fontsdirty = 0;
  }
  if (o->langdirty && o->host.set_language) {
    if (!o->host.set_language(o->host.user, o->lang_cur)) { 
      snprintf(o->msg, sizeof(o->msg), translate("Could not load the language file."));
      ok = 0;
    }
    else {
      o->langdirty = 0;
      build_items(o);  
    }
  }
  if (o->ctxdirty && o->host.context_set) {
    if (!o->host.context_set(o->host.user, o->ctxenabled)) {
      int actual = 0;
      if (o->host.context_get && o->host.context_get(o->host.user, &actual))
        o->ctxactual = o->ctxenabled = actual;
      update_ctxmsg(o);
      snprintf(o->msg, sizeof(o->msg), translate("Could not update the Explorer menu."));
      ok = 0;
    }
    else {
      o->ctxactual = o->ctxenabled;
      o->ctxdirty = 0;
      update_ctxmsg(o);
    }
  }
  if (ok) snprintf(o->msg, sizeof(o->msg), translate("Settings applied."));
  return ok;
}

static void finish(Opt* o, int result)
{
  o->result = result;
  pl_window_close(o->win);
}

static void add(Opt* o, int kind, int id, const char* text, int enabled,
  int l, int t, int r, int b)
{
  Item* it;
  if (o->nit >= MAXIT) return;
  it = &o->it[o->nit++];
  it->kind = kind; it->id = id; it->enabled = enabled;
  it->text = (text && text[0]) ? translate(text) : text;
  it->r = mkr(l, t, r, b);
}

static void add_stepper(Opt* o, const char* label, int dec, int inc, int val, int x, int w, int y)
{
  add(o, IK_TEXT, 0, label, 1, x, y, x + 170, y + 30);
  add(o, IK_BTN, dec, "<", 1, x + 180, y, x + 212, y + 30);
  add(o, IK_VALUE, val, "", 1, x + 220, y, x + 220 + 190, y + 30);
  add(o, IK_BTN, inc, ">", 1, x + 220 + 198, y, x + 220 + 230, y + 30);
  (void)w;
}


static void layout(Opt* o)
{
  const int m = 12, navw = 190, barH = 56;
  int bot = o->H - barH - m;
  o->r_nav = mkr(m, m, m + navw, bot);
  o->r_card = mkr(m + navw + 12, m, o->W - m, bot);
  o->r_bar = mkr(0, o->H - barH, o->W, o->H);
  build_items(o);
}

static void set_page(Opt* o, int page)
{
  o->page = page;
  o->about_scroll = 0;
  o->dd_open = 0;
  o->hov = -1;
  o->dbfocus = -1;
  build_items(o);
  redraw(o);
}

static void lang_add(const char* code, const char* name, void* ud)
{
  Opt* o = (Opt*)ud;
  if (o->nlang == o->caplang) {
    int nc = o->caplang ? o->caplang * 2 : 16;
    char** c = (char**)realloc(o->lcodes, (size_t)nc * sizeof(*c));
    char** n;
    if (!c) return;
    o->lcodes = c;
    n = (char**)realloc(o->lnames, (size_t)nc * sizeof(*n));
    if (!n) return;
    o->lnames = n; o->caplang = nc;
  }
  o->lcodes[o->nlang] = _strdup(code);
  o->lnames[o->nlang] = _strdup(name);
  if (o->lcodes[o->nlang] && o->lnames[o->nlang]) o->nlang++;
  else { free(o->lcodes[o->nlang]); free(o->lnames[o->nlang]); }
}

static void load_languages(Opt* o)
{
  if (o->langs_loaded) return;
  o->langs_loaded = 1;
  lang_add("", translate("English"), o);
  if (o->host.list_languages) o->host.list_languages(o->host.user, lang_add, o);
}

static const char* lang_name(Opt* o)
{
  int i;
  for (i = 0; i < o->nlang; ++i)
    if (!strcmp(o->lcodes[i], o->lang_cur)) return o->lnames[i];
  return o->lang_cur[0] ? o->lang_cur : translate("English");
}

static void paint_item(Opt* o, PlCanvas* c, int i)
{
  Item* it = &o->it[i];
  PlRect r = it->r, trc;
  int hov = (o->hov == i), h = r.b - r.t;
  char buf[96];
  const char* p;
  PlColor fill, fg;

  switch (it->kind) {
  case IK_HEAD:
    txt(c, o->fontb, CLR_TEXT_SECONDARY, r, 0, it->text);
    break;
  case IK_TEXT:
    txt(c, o->font, CLR_TEXT_PRIMARY, r, 0, it->text);
    break;
  case IK_NOTE:
    txt(c, o->font, CLR_TEXT_CODE, r, 0, it->text);
    break;

  case IK_CHECK: {
    int* v = chk_ptr(o, it->id);
    int on = v && *v && it->enabled;
    int bx = r.l, by = r.t + (h - 16) / 2;
    if (hov && it->enabled)
      pl_round_rect(c, mkr(r.l - 6, r.t + 2, r.r, r.b - 2), CLR_SIDEBAR_SEL, CLR_SIDEBAR_SEL, 6);
    pl_round_rect(c, mkr(bx + 0, by, bx + 16, by + 16),
      on ? CLR_ACCENT : CLR_BG, on ? CLR_ACCENT : CLR_BTN_GHOST_BD, 4);
    if (on) {
      pl_line(c, bx + 3, by + 8, bx + 6, by + 11, CLR_TEXT_PRIMARY);
      pl_line(c, bx + 6, by + 11, bx + 12, by + 5, CLR_TEXT_PRIMARY);
    }
    trc = mkr(r.l + 26, r.t, r.r - 6, r.b);
    txt(c, o->font, it->enabled ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY, trc, 0, it->text);
    break;
  }

  case IK_VALUE:
    pl_round_rect(c, r, CLR_PANEL_HEADER, CLR_PANEL_BORDER, 8);
    size_label(it->id == ID_BUF_VAL ? o->opt.buffer_size : o->opt.file_buffer_size, buf, sizeof(buf));
    txt(c, o->font, CLR_TEXT_PRIMARY, mkr(r.l + 12, r.t, r.r - 12, r.b), 1, buf);
    break;

  case IK_PATH: {
    int n = it->id - ID_PATH_MAIN;
    int foc = (o->dbfocus == n);
    const char* p = o->dbpath[n][0] ? o->dbpath[n] : (foc ? "" : translate("(not set)"));
    pl_round_rect(c, r, CLR_PANEL_HEADER, foc ? CLR_ACCENT : CLR_PANEL_BORDER, 8);
    txt(c, o->font, o->dbpath[n][0] ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY,
      mkr(r.l + 12, r.t, r.r - 12, r.b), 0, p);
    if (foc) {
      int cx = r.l + 12 + pl_text_width(o->font, o->dbpath[n]);
      if (cx > r.r - 12) cx = r.r - 12;
      pl_line(c, cx, r.t + 7, cx, r.b - 7, CLR_TEXT_PRIMARY);
    }
    break;
  }

  case IK_FVAL: {
    const OptionsFont* f = &o->fonts[it->id - ID_FVAL0];
    int cx = r.r - 18, cy = (r.t + r.b) / 2;
    pl_round_rect(c, r, CLR_PANEL_HEADER, hov ? CLR_ACCENT : CLR_PANEL_BORDER, 8);
    pl_line(c, cx - 4, cy - 2, cx, cy + 2, CLR_TEXT_SECONDARY);
    pl_line(c, cx, cy + 2, cx + 4, cy - 2, CLR_TEXT_SECONDARY);
    if (f->point_size)
      snprintf(buf, sizeof(buf), "%.40s, %u pt%s%s",
        f->family[0] ? f->family : "Default", f->point_size,
        f->bold ? ", Bold" : "", f->italic ? ", Italic" : "");
    else
      snprintf(buf, sizeof(buf), "%.40s%s%s",
        f->family[0] ? f->family : "Default",
        f->bold ? ", Bold" : "", f->italic ? ", Italic" : "");
    txt(c, o->font, CLR_TEXT_PRIMARY, mkr(r.l + 12, r.t, r.r - 30, r.b), 0, buf);
    break;
  }

  case IK_LVAL: {
    int cx = r.r - 18, cy = (r.t + r.b) / 2;
    pl_round_rect(c, r, CLR_PANEL_HEADER, hov ? CLR_ACCENT : CLR_PANEL_BORDER, 8);
    pl_line(c, cx - 4, cy - 2, cx, cy + 2, CLR_TEXT_SECONDARY);
    pl_line(c, cx, cy + 2, cx + 4, cy - 2, CLR_TEXT_SECONDARY);
    load_languages(o);
    txt(c, o->font, CLR_TEXT_PRIMARY, mkr(r.l + 12, r.t, r.r - 30, r.b), 0, lang_name(o));
    break;
  }

  case IK_PARA:
    wrap_text(c, o->font, CLR_TEXT_PRIMARY, r, it->text, 18, 0, 1);
    break;

  case IK_SCROLL: {
    PlRect in = mkr(r.l + 12, r.t + 8, r.r - 20, r.b - 8);
    int lh = 18, vis = (in.b - in.t) / lh, total;
    pl_round_rect(c, r, CLR_PANEL_HEADER, CLR_PANEL_BORDER, 8);
    o->about_in = in;
    if (o->about_sel >= o->about_scroll && o->about_sel < o->about_scroll + vis) {
      int sy = in.t + (o->about_sel - o->about_scroll) * lh;
      pl_round_rect(c, mkr(in.l - 4, sy, in.r + 4, sy + lh),
        CLR_SIDEBAR_SEL, CLR_SIDEBAR_SEL, 4);
    }
    total = wrap_text(c, o->font, CLR_TEXT_SECONDARY, in, ABOUT_BOX, lh, o->about_scroll, 1);
    o->about_lines = total;
    o->about_vis = vis;
    if (total > vis) {
      int track = r.b - r.t - 16;
      int th = imax(18, track * vis / total);
      int ty = r.t + 8 + (track - th) * o->about_scroll / (total - vis);
      pl_round_rect(c, mkr(r.r - 11, ty, r.r - 7, ty + th),
        CLR_BTN_GHOST_BD, CLR_BTN_GHOST_BD, 2);
    }
    break;
  }

  case IK_ACCENT:
    fill = hov ? CLR_ACCENT_HOVER : CLR_ACCENT;
    pl_round_rect(c, r, fill, fill, 8);
    txt(c, o->font, CLR_TEXT_PRIMARY, r, 1, it->text);
    break;

  case IK_BTN:
    fill = hov ? CLR_BTN_GHOST_HOVER : CLR_BTN_GHOST;
    fg = CLR_TEXT_PRIMARY;
    pl_round_rect(c, r, fill, CLR_BTN_GHOST_BD, 8);
    txt(c, o->font, fg, r, 1, it->text);
    break;

  default: break;
  }
}

static void font_add(const char* family, void* ud)
{
  Opt* o = (Opt*)ud;
  size_t len = strlen(family) + 1;
  char* copy;
  if (o->nfnames == o->capfnames) {
    int nc = o->capfnames ? o->capfnames * 2 : 128;
    char** n = (char**)realloc(o->fnames, (size_t)nc * sizeof(*n));
    if (!n) return;
    o->fnames = n; o->capfnames = nc;
  }
  copy = (char*)malloc(len);
  if (!copy) return;
  memcpy(copy, family, len);
  o->fnames[o->nfnames++] = copy;
}

static void load_fonts(Opt* o)
{
  if (o->fonts_loaded) return;
  o->fonts_loaded = 1;
  font_add(translate("Default"), o);
  if (o->host.list_fonts) o->host.list_fonts(o->host.user, 0, font_add, o);
}


static int dd_count(Opt* o) { return o->dd_kind == DD_LANG ? o->nlang : o->nfnames; }

static const char* dd_label(Opt* o, int i)
{
  return o->dd_kind == DD_LANG ? o->lnames[i] : o->fnames[i];
}

static int dd_is_current(Opt* o, int i)
{
  if (o->dd_kind == DD_LANG) return !strcmp(o->lcodes[i], o->lang_cur);
  {
    const char* cur = o->fonts[o->dd_role].family;
    return i == 0 ? cur[0] == 0 : !strcmp(o->fnames[i], cur);
  }
}

static int dd_visible(Opt* o)
{
  return dd_count(o) < DD_MAXVIS ? dd_count(o) : DD_MAXVIS;
}

static void dd_clamp(Opt* o)
{
  int max = dd_count(o) - dd_visible(o);
  if (o->dd_scroll > max) o->dd_scroll = max;
  if (o->dd_scroll < 0) o->dd_scroll = 0;
}

static int dd_hit(Opt* o, int x, int y)
{
  int idx;
  if (!inr(&o->dd_r, x, y)) return -1;
  idx = o->dd_scroll + (y - o->dd_r.t - 4) / DD_ROW;
  if (y < o->dd_r.t + 4 || idx >= dd_count(o) ||
    idx >= o->dd_scroll + dd_visible(o)) return -1;
  return idx;
}

static void open_dropdown(Opt* o, int kind, int anchor_id, int role)
{
  PlRect b = mkr(0, 0, 0, 0);
  int i, vis, h, cur = 0;
  for (i = 0; i < o->nit; ++i)
    if (o->it[i].id == anchor_id) b = o->it[i].r;
  o->dd_kind = kind;
  o->dd_role = role;
  if (kind == DD_LANG) load_languages(o); else load_fonts(o);
  if (dd_count(o) <= 0) return;
  vis = dd_visible(o);
  h = vis * DD_ROW + 8;
  o->dd_r = mkr(b.l, b.b + 2, b.r, b.b + 2 + h);
  if (o->dd_r.b > o->H - 4)
    o->dd_r = mkr(b.l, b.t - 2 - h, b.r, b.t - 2);
  for (i = 0; i < dd_count(o); ++i)
    if (dd_is_current(o, i)) { cur = i; break; }
  o->dd_scroll = cur > 2 ? cur - 2 : 0;
  dd_clamp(o);
  o->dd_hov = -1;
  o->hov = -1;
  o->dd_open = 1;
}


static void pick_font(Opt* o, int idx)
{
  OptionsFont* f = &o->fonts[o->dd_role];
  if (idx == 0) f->family[0] = 0;
  else snprintf(f->family, sizeof(f->family), "%s", o->fnames[idx]);
  o->fontsdirty = 1;
}

static void dd_pick(Opt* o, int idx)
{
  if (o->dd_kind == DD_LANG) {
    snprintf(o->lang_cur, sizeof(o->lang_cur), "%s", o->lcodes[idx]);
    o->langdirty = 1;
  }
  else pick_font(o, idx);
}

static void paint_dropdown(Opt* o, PlCanvas* c)
{
  PlRect r = o->dd_r;
  int vis = dd_visible(o), i;
  int sb = dd_count(o) > vis;

  pl_round_rect(c, r, CLR_PANEL_BODY, CLR_PANEL_BORDER, 8);
  for (i = 0; i < vis; ++i) {
    int idx = o->dd_scroll + i;
    PlRect ir = mkr(r.l + 4, r.t + 4 + i * DD_ROW,
      r.r - (sb ? 12 : 4), r.t + 4 + (i + 1) * DD_ROW);
    int sel = dd_is_current(o, idx);    if (idx == o->dd_hov)
      pl_round_rect(c, ir, CLR_SIDEBAR_SEL, CLR_SIDEBAR_SEL, 6);
    txt(c, o->font, sel ? CLR_ACCENT : CLR_TEXT_PRIMARY,
      mkr(ir.l + 10, ir.t, ir.r - 6, ir.b), 0, dd_label(o, idx));
  }
  if (sb) {
    int track = vis * DD_ROW;
    int th = imax(18, track * vis / dd_count(o));
    int ty = r.t + 4 + (track - th) * o->dd_scroll / (dd_count(o) - vis);
    pl_round_rect(c, mkr(r.r - 9, ty, r.r - 5, ty + th),
      CLR_BTN_GHOST_BD, CLR_BTN_GHOST_BD, 2);
  }
}

static void on_paint(void* u, PlCanvas* c, int w, int h)
{
  Opt* o = (Opt*)u;
  PlRect hd;
  int i;

  o->W = w; o->H = h;
  pl_fill(c, 0, 0, w, h, CLR_BG);

  pl_round_rect(c, o->r_nav, CLR_PANEL_HEADER, CLR_PANEL_BORDER, 12);
  for (i = 0; i < o->nnav; ++i) {
    int yoff = nav_yoff(o, i);

    PlRect nr = mkr(o->r_nav.l + 8, o->r_nav.t + yoff,
      o->r_nav.r - 8, o->r_nav.t + yoff + NAV_H - 2);
    int pg = o->navpg[i];
    if (pg == o->page)
      pl_round_rect(c, nr, CLR_SIDEBAR_SEL, CLR_SIDEBAR_SEL, 6);
    else if (i == o->navhov)
      pl_round_rect(c, nr, CLR_BTN_GHOST_HOVER, CLR_BTN_GHOST_HOVER, 6);
    txt(c, o->font, pg == o->page ? CLR_TEXT_PRIMARY : CLR_TEXT_SECONDARY,
      mkr(nr.l + 14, nr.t, nr.r - 8, nr.b), 0, translate(s_title[pg]));
  }

  card(c, o->r_card, HEAD);
  hd = mkr(o->r_card.l + 20, o->r_card.t, o->r_card.r - 20, o->r_card.t + HEAD);
  txt(c, o->fontb, CLR_TEXT_PRIMARY, mkr(hd.l, hd.t, hd.l + 170, hd.b), 0, translate(s_title[o->page]));
  txt(c, o->font, CLR_TEXT_SECONDARY, mkr(hd.l + 180, hd.t, hd.r, hd.b), 2, translate(s_sub[o->page]));

  for (i = 0; i < o->nit; ++i) paint_item(o, c, i);

  pl_fill(c, o->r_bar.l, o->r_bar.t, o->r_bar.r, o->r_bar.b, CLR_BG);
  pl_line(c, o->r_bar.l, o->r_bar.t, o->r_bar.r, o->r_bar.t, CLR_SEPARATOR);
  if (o->msg[0])
    txt(c, o->font, CLR_TEXT_SECONDARY,
      mkr(16, o->r_bar.t, o->W - 3 * 80 - 40, o->r_bar.b), 0, o->msg);

  for (i = 0; i < o->nit; ++i) paint_item(o, c, i);

  if (o->dd_open) paint_dropdown(o, c);
}

static int hit_item(Opt* o, int x, int y)
{
  int i;
  for (i = 0; i < o->nit; ++i) {
    Item* it = &o->it[i];
    if ((it->kind == IK_CHECK || it->kind == IK_BTN || it->kind == IK_ACCENT ||
      it->kind == IK_PATH || it->kind == IK_FVAL || it->kind == IK_LVAL) &&
      inr(&it->r, x, y)) return i;
  }
  return -1;
}

static int hit_nav(Opt* o, int x, int y)
{
  int i;
  for (i = 0; i < o->nnav; ++i) {
    int yoff = nav_yoff(o, i);

    PlRect nr = mkr(o->r_nav.l + 8, o->r_nav.t + yoff,
      o->r_nav.r - 8, o->r_nav.t + yoff + NAV_H - 2);
    if (inr(&nr, x, y)) return i;
  }
  return -1;
}

static void activate(Opt* o, int id)
{
  int* v;
  switch (id) {
  case ID_BUF_DEC:  o->opt.buffer_size = size_step(o->opt.buffer_size, -1); break;
  case ID_BUF_INC:  o->opt.buffer_size = size_step(o->opt.buffer_size, 1); break;
  case ID_FBUF_DEC: o->opt.file_buffer_size = size_step(o->opt.file_buffer_size, -1); break;
  case ID_FBUF_INC: o->opt.file_buffer_size = size_step(o->opt.file_buffer_size, 1); break;
  case ID_PATH_MAIN: case ID_PATH_EXTRA: case ID_PATH_CUSTOM: case ID_PATH_VT:
    o->dbfocus = id - ID_PATH_MAIN; break;
  case ID_BROWSE_MAIN: case ID_BROWSE_EXTRA: case ID_BROWSE_CUSTOM: {
    int n = id - ID_BROWSE_MAIN;
    char tmp[1024];
    if (o->host.browse_folder && o->host.browse_folder(o->host.user, tmp, sizeof(tmp))) {
      snprintf(o->dbpath[n], sizeof(o->dbpath[n]), "%s", tmp);
      o->dbdirty = 1;
    }
    break;
  }
  case ID_PASTE_VT:
    o->dbpath[3][0] = 0;                       /* replace, not append */
    paste_append(o->dbpath[3], sizeof(o->dbpath[3]));
    break;

  case ID_OK:       if (apply_all(o)) finish(o, OPTIONS_RESULT_NONE); break;
  case ID_APPLY:    apply_all(o); break;
  case ID_CANCEL:   finish(o, OPTIONS_RESULT_NONE); break;
  case ID_CHECK_UPDATE:
    o->updmsg[0] = 0;
    if (!o->host.check_update ||
      !o->host.check_update(o->host.user, o->updmsg, sizeof(o->updmsg))) {
      if (!o->updmsg[0])
        snprintf(o->updmsg, sizeof(o->updmsg), translate("Could not check for updates."));
    }
    break;
  case ID_CTX_TOGGLE:
    if (o->have_ctx) { o->ctxenabled = !o->ctxenabled; o->ctxdirty = 1; }
    break;
  case ID_OPEN_SITE: case ID_OPEN_GITHUB: case ID_OPEN_RELEASES: {
    const char* url =
      id == ID_OPEN_SITE ? "http://ntinfo.biz" :
      id == ID_OPEN_GITHUB ? "https://github.com/horsicq" :
      "https://github.com/horsicq/CDiE/releases";
    if (!o->host.open_url || !o->host.open_url(o->host.user, url))
      snprintf(o->aboutmsg, sizeof(o->aboutmsg), translate("Could not open the browser."));
    else
      o->aboutmsg[0] = 0;
    break;
  }
  case ID_COPY_BOX: {
    char src[4096], dst[6144];
    snprintf(src, sizeof(src), translate("Detect It Easy - cdie\n%s\n%s\n%s\n%s\n\n%s"),
      o->verline, o->buildline, o->copyline, ABOUT_BOX);
    to_crlf(src, dst, sizeof(dst));
    if (o->host.copy_text && o->host.copy_text(o->host.user, dst))
      snprintf(o->aboutmsg, sizeof(o->aboutmsg), translate("Copied to the clipboard."));
    else
      snprintf(o->aboutmsg, sizeof(o->aboutmsg), translate("Could not copy to the clipboard."));
    break;
  }
  default:
    if (id == ID_LANGVAL) {
      open_dropdown(o, DD_LANG, ID_LANGVAL, 0);
    }
    else if (id >= ID_FVAL0 && id <= ID_FVAL3) {
      open_dropdown(o, DD_FONT, id, id - ID_FVAL0);
    }
    else if (id >= ID_FDEF0 && id <= ID_FDEF3) {
      memset(&o->fonts[id - ID_FDEF0], 0, sizeof(o->fonts[0]));
      o->fontsdirty = 1;
    }
    else {
      v = chk_ptr(o, id);
      if (v) *v = !*v;
    }
    break;
  }
}

static void on_mouse_move(void* u, int x, int y)
{
  Opt* o = (Opt*)u;
  int h, n;
  if (o->dd_open) {
    int idx = dd_hit(o, x, y);
    if (idx != o->dd_hov) { o->dd_hov = idx; redraw(o); }
    return;
  }
  h = hit_item(o, x, y); n = hit_nav(o, x, y);
  if (h != o->hov || n != o->navhov) { o->hov = h; o->navhov = n; redraw(o); }
}

static void on_mouse_leave(void* u)
{
  Opt* o = (Opt*)u;
  if (o->hov != -1 || o->navhov != -1) { o->hov = -1; o->navhov = -1; redraw(o); }
}

static void on_mouse_down(void* u, int x, int y)
{
  Opt* o = (Opt*)u;
  int n, h;
  if (o->dd_open) {                      
    int idx = dd_hit(o, x, y);
    if (idx >= 0) dd_pick(o, idx);
    o->dd_open = 0;
    redraw(o);
    return;
  }
  n = hit_nav(o, x, y);
  if (n >= 0) {
    if (o->navpg[n] != o->page) set_page(o, o->navpg[n]);
    return;
  }
  h = hit_item(o, x, y);
  if (h < 0 || o->it[h].kind != IK_PATH) o->dbfocus = -1;
  if (h >= 0 && o->it[h].enabled) {
    activate(o, o->it[h].id);
    redraw(o);
  }
}

static int on_hand(void* u, int x, int y)
{
  Opt* o = (Opt*)u;
  int h;
  if (o->dd_open) return dd_hit(o, x, y) >= 0;
  h = hit_item(o, x, y);
  if (h >= 0) return o->it[h].enabled;
  return 0;
}

static void on_wheel(void* u, int x, int y, int delta, int mods)
{
  Opt* o = (Opt*)u;
  (void)mods;
  if (!delta) return;
  if (o->dd_open) {
    o->dd_scroll += delta > 0 ? -3 : 3;
    dd_clamp(o);
    o->dd_hov = dd_hit(o, x, y);
    redraw(o);
  }
  else if (o->page == PG_ABOUT) {
    int max = o->about_lines - o->about_vis;
    o->about_scroll += delta > 0 ? -3 : 3;
    if (o->about_scroll > max) o->about_scroll = max;
    if (o->about_scroll < 0) o->about_scroll = 0;
    redraw(o);
  }
}

static int ctrl_down(void)
{
#ifdef _WIN32
  return (GetKeyState(VK_CONTROL) & 0x8000) != 0;
#else
  return 0;
#endif
}

static void paste_append(char* s, size_t cap)
{
#ifdef _WIN32
  if (!OpenClipboard(NULL)) return;
  {
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    const WCHAR* w = h ? (const WCHAR*)GlobalLock(h) : NULL;
    if (w) {
      char u8[512];
      int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, u8, sizeof(u8), NULL, NULL);
      GlobalUnlock(h);
      if (len > 0) {
        size_t m = strlen(u8), n = strlen(s);
        while (m && (u8[m - 1] == '\r' || u8[m - 1] == '\n' || u8[m - 1] == ' ')) u8[--m] = 0;
        if (n + m + 1 < cap) memcpy(s + n, u8, m + 1);
      }
    }
  }
  CloseClipboard();
#else
  (void)s; (void)cap;
#endif
}

static void on_key(void* u, int key, uint32_t ch, int mods)
{
  Opt* o = (Opt*)u;
  int i, cur = 0;
  (void)ch; (void)mods;

  if (o->dd_open) {
    if (key == PL_KEY_ESC) { o->dd_open = 0; redraw(o); }
    return;
  }
  if (o->page == PG_ABOUT && ch == 3) {      /* Ctrl+C */
    activate(o, ID_COPY_BOX);
    redraw(o);
    return;
  }
  if (key == PL_KEY_ESC) { finish(o, OPTIONS_RESULT_NONE); return; }

  if ((o->page == PG_DB || o->page == PG_VT) && o->dbfocus >= 0) {
    char* s = o->dbpath[o->dbfocus];
    size_t n = strlen(s);
    if (key == PL_KEY_ENTER) { o->dbfocus = -1; redraw(o); return; }
    if (ch == 22 || ((ch == 'v' || ch == 'V' || key == 'V') && ctrl_down())) {   /* Ctrl+V */
      paste_append(s, sizeof(o->dbpath[0]));
      if (o->dbfocus < 3) o->dbdirty = 1;
      redraw(o); return;
    }
    if (ch == 8) {
      while (n > 0 && ((unsigned char)s[n - 1] & 0xC0) == 0x80) --n;
      if (n > 0) s[n - 1] = 0;
      if (o->dbfocus < 3) o->dbdirty = 1;
      redraw(o); return;
    }
    if (ch >= 32 && ch != 127 && n + 4 < (o->dbfocus == 3 ? 120 : sizeof(o->dbpath[0]))) {
      if (ch < 0x80) { s[n++] = (char)ch; }
      else if (ch < 0x800) { s[n++] = (char)(0xC0 | (ch >> 6)); s[n++] = (char)(0x80 | (ch & 63)); }
      else if (ch < 0x10000) { s[n++] = (char)(0xE0 | (ch >> 12)); s[n++] = (char)(0x80 | ((ch >> 6) & 63)); s[n++] = (char)(0x80 | (ch & 63)); }
      else { s[n++] = (char)(0xF0 | (ch >> 18)); s[n++] = (char)(0x80 | ((ch >> 12) & 63)); s[n++] = (char)(0x80 | ((ch >> 6) & 63)); s[n++] = (char)(0x80 | (ch & 63)); }
      s[n] = 0; if (o->dbfocus < 3) o->dbdirty = 1; redraw(o); return;
    }
    return;
  }
  if (key == PL_KEY_ENTER) { activate(o, ID_OK); redraw(o); return; }
  if (key == PL_KEY_UP || key == PL_KEY_DOWN) {
    for (i = 0; i < o->nnav; ++i) if (o->navpg[i] == o->page) cur = i;
    cur += key == PL_KEY_DOWN ? 1 : -1;
    if (cur >= 0 && cur < o->nnav) set_page(o, o->navpg[cur]);
  }
}

static void on_resize(void* u, int w, int h)
{
  Opt* o = (Opt*)u;
  o->W = w; o->H = h;
  o->dd_open = 0;
  layout(o);
  redraw(o);
}

static void on_destroy(void* u)
{
  Opt* o = (Opt*)u;
  if (o->font) pl_font_destroy(o->font);
  if (o->fontb) pl_font_destroy(o->fontb);
  if (o->fnames) {
    int i;
    for (i = 0; i < o->nfnames; ++i) free(o->fnames[i]);
    free(o->fnames);
  }
  if (o->lcodes || o->lnames) {
    int i;
    for (i = 0; i < o->nlang; ++i) { free(o->lcodes[i]); free(o->lnames[i]); }
    free(o->lcodes);
    free(o->lnames);
  }
  s_opt = NULL;
  free(o);
}

static const PlWidgetOps s_ops = {
  on_paint, on_mouse_move, on_mouse_leave, on_mouse_down,
  on_resize, on_hand, on_destroy,
  on_key, on_wheel
};

static void on_win_size(void* u, int w, int h)
{
  Opt* o = (Opt*)u;
  o->W = w; o->H = h;
  pl_widget_set_rect(o->w, 0, 0, w, h);
}

static void on_win_close(void* u)
{
  Opt* o = (Opt*)u;
  o->result = OPTIONS_RESULT_NONE;
  pl_window_close(o->win);
}

int options_show(xxwidgets_app* app, xxwidgets_widget* owner,
  const OptionsHost* host, int start_page)
{
  void* ow = owner ? xxwidgets_widget_native_handle(owner) : NULL;
  Opt* o;
  PlWindow* win;
  int result, i;
  (void)app;

  if (s_opt || !host) return OPTIONS_RESULT_NONE;
  o = (Opt*)calloc(1, sizeof(*o));
  if (!o) return OPTIONS_RESULT_NONE;

  o->host = *host;
  o->W = 760; o->H = 540; o->hov = -1; o->navhov = -1;
  o->dbfocus = -1;
  {
    CdieConfig kc;
    if (config_load(&kc)) snprintf(o->dbpath[3], sizeof(o->dbpath[3]), "%s", kc.vt_key);
  }
  {
    const char* ver = o->host.get_version ? o->host.get_version(o->host.user) : NULL;
    snprintf(o->verline, sizeof(o->verline), translate("Current version: %s"),
      ver && ver[0] ? ver : translate("unknown"));
  }
  snprintf(o->buildline, sizeof(o->buildline), translate("Build date: %s"), __DATE__);
  snprintf(o->copyline, sizeof(o->copyline), translate("Copyright (C) hors (2026) (core), devX-cipher(2026) (GUI)"), __DATE__ + 7);
  o->have_fonts = o->host.get_fonts &&
    o->host.get_fonts(o->host.user, o->fonts) ? 1 : 0;
  o->have_ctx = o->host.context_get &&
    o->host.context_get(o->host.user, &o->ctxenabled) ? 1 : 0;
  o->ctxactual = o->ctxenabled;
  update_ctxmsg(o);
  if (!config_db_required()) {          
    snprintf(o->dbpath[0], sizeof(o->dbpath[0]), "%s", gui_backend_main_db_path() ? gui_backend_main_db_path() : "");
    snprintf(o->dbpath[1], sizeof(o->dbpath[1]), "%s", gui_backend_extra_db_path() ? gui_backend_extra_db_path() : "");
    snprintf(o->dbpath[2], sizeof(o->dbpath[2]), "%s", gui_backend_custom_db_path() ? gui_backend_custom_db_path() : "");
  }

  o->scan.deep = 1; o->scan.heuristic = 1; o->scan.verbose = 1;
  o->scan.db_extra = 1; o->scan.db_custom = 1;
  if (o->host.get_scan) o->host.get_scan(o->host.user, &o->scan);
  o->have_opt = o->host.get_optimization &&
    o->host.get_optimization(o->host.user, &o->opt) ? 1 : 0;

  for (i = 0; i < PG_COUNT; ++i) {

    if (i == PG_CTX && !o->host.context_available)
      continue;

    if (i == PG_UPDATE && !o->host.updates_available)
      continue;

    if (i == PG_LANG && !o->host.list_languages)
      continue;

    o->navpg[o->nnav++] = i;
  }
  {
    const char* lc = o->host.get_language ? o->host.get_language(o->host.user) : NULL;
    snprintf(o->lang_cur, sizeof(o->lang_cur), "%s", lc ? lc : "");
  }
  o->page = PG_SCAN;
  for (i = 0; i < o->nnav; ++i)
    if (o->navpg[i] == start_page) o->page = start_page;

  o->font = pl_font_create(9, 0);
  o->fontb = pl_font_create(10, 1);
  if (!o->font || !o->fontb) { on_destroy(o); return OPTIONS_RESULT_NONE; }

  win = pl_window_create(ow, translate("Options"), 760, 540, 640, 500, on_win_size, on_win_close, o);
  if (!win) { on_destroy(o); return OPTIONS_RESULT_NONE; }
  o->win = win;
  o->w = pl_widget_create(pl_window_client(win), &s_ops, o, 0, 0, 760, 520);
  if (!o->w) { pl_window_destroy(win); on_destroy(o); return OPTIONS_RESULT_NONE; }
  s_opt = o;

  layout(o);
  pl_window_run_modal(win);

  result = o->result;                    
  pl_widget_destroy(o->w);
  pl_window_destroy(win);
  return result;
}