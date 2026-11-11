#ifndef CDIE_THEME_H
#define CDIE_THEME_H

#include "platform/platform.h"
#ifdef _WIN32
#include <windows.h>
#endif


#if defined(_MSC_VER)
#define THEME_SHARED __declspec(selectany)
#else
#define THEME_SHARED __attribute__((weak))
#endif

typedef struct {
  PlColor bg, sidebar_bg, sidebar_sel, sidebar_hover;
  PlColor panel, panel_border, panel_header, panel_body;
  PlColor accent, accent_hover, on_accent;
  PlColor text_primary, text_secondary, text_code, text_accent;
  PlColor highlight_bg, highlight_txt, separator, search_bg, logo_bg;
  PlColor btn_ghost, btn_ghost_hover, btn_ghost_bd;
  PlColor badge, badge_bg;

  PlColor ent_low, ent_mid, ent_high, byte_null, byte_ctrl;

  PlColor nt_zero, nt_raw, nt_row_alt, nt_row_sel, nt_colhdr, nt_colhdr_line,
    nt_group_line, nt_green, nt_amber, nt_chip_bg, nt_chip_border,
    nt_sb_thumb, nt_sb_hot;

  PlColor danger, warning, success, on_color;

  PlColor syn_string, syn_keyword, syn_function, syn_number, syn_object;

  PlColor status_ok_text, status_ok_fill, status_err_text;
  PlColor str_ascii, str_utf16;

  PlColor sec_pal[6];
} Theme;

#define SEC_PAL_INIT { PL_RGB(0x4C,0x82,0xF5), PL_RGB(0x2F,0xA6,0xA0), \
                       PL_RGB(0x8E,0x6B,0xD8), PL_RGB(0xD8,0x84,0x3A), \
                       PL_RGB(0x6C,0x7A,0x94), PL_RGB(0xD9,0x53,0x4F) }

static const Theme s_theme_dark = {
  .bg = PL_RGB(0x1F,0x21,0x2E), .sidebar_bg = PL_RGB(0x17,0x17,0x23),
  .sidebar_sel = PL_RGB(0x2B,0x2B,0x35), .sidebar_hover = PL_RGB(0x21,0x21,0x2C),
  .panel = PL_RGB(0x26,0x28,0x35), .panel_border = PL_RGB(0x33,0x35,0x42),
  .panel_header = PL_RGB(0x27,0x29,0x36), .panel_body = PL_RGB(0x28,0x2A,0x37),
  .accent = PL_RGB(0x4A,0x7F,0xFF), .accent_hover = PL_RGB(0x6B,0x97,0xFF),
  .on_accent = PL_RGB(0xFF,0xFF,0xFF),
  .text_primary = PL_RGB(0xEC,0xEC,0xF8), .text_secondary = PL_RGB(0x8A,0x89,0x97),
  .text_code = PL_RGB(0x9C,0x9B,0xA9), .text_accent = PL_RGB(0x85,0xAB,0xFE),
  .highlight_bg = PL_RGB(0x3E,0x2E,0x3B), .highlight_txt = PL_RGB(0xED,0x94,0xAA),
  .separator = PL_RGB(0x27,0x27,0x33), .search_bg = PL_RGB(0x25,0x25,0x2F),
  .logo_bg = PL_RGB(0x21,0x23,0x32),
  .btn_ghost = PL_RGB(0x2C,0x2C,0x39), .btn_ghost_hover = PL_RGB(0x38,0x39,0x4A),
  .btn_ghost_bd = PL_RGB(0x3E,0x40,0x54),
  .badge = PL_RGB(0xE0,0xAF,0x68), .badge_bg = PL_RGB(0x3A,0x34,0x30),

  .ent_low = PL_RGB(0x43,0xA0,0x47), .ent_mid = PL_RGB(0xFF,0xB3,0x00),
  .ent_high = PL_RGB(0xE5,0x39,0x35), .byte_null = PL_RGB(0x5C,0x9D,0xFF),
  .byte_ctrl = PL_RGB(0x9E,0x9E,0xB8),

  .nt_zero = PL_RGB(0x66,0x6A,0x8C), .nt_raw = PL_RGB(0x7D,0x83,0xA6),
  .nt_row_alt = PL_RGB(0x2C,0x2E,0x3D), .nt_row_sel = PL_RGB(0x34,0x38,0x53),
  .nt_colhdr = PL_RGB(0x23,0x25,0x32), .nt_colhdr_line = PL_RGB(0x32,0x35,0x47),
  .nt_group_line = PL_RGB(0x35,0x38,0x49), .nt_green = PL_RGB(0x7F,0xD6,0xA4),
  .nt_amber = PL_RGB(0xE6,0xC2,0x7A), .nt_chip_bg = PL_RGB(0x30,0x33,0x4A),
  .nt_chip_border = PL_RGB(0x5B,0x5F,0x7B), .nt_sb_thumb = PL_RGB(0x45,0x49,0x66),
  .nt_sb_hot = PL_RGB(0x5B,0x5F,0x7B),

  .danger = PL_RGB(0xD9,0x53,0x4F), .warning = PL_RGB(0xF0,0xAD,0x4E),
  .success = PL_RGB(0x5C,0xB8,0x5C), .on_color = PL_RGB(0xFF,0xFF,0xFF),

  .syn_string = PL_RGB(158,206,106), .syn_keyword = PL_RGB(122,162,247),
  .syn_function = PL_RGB(224,175,104), .syn_number = PL_RGB(255,158,100),
  .syn_object = PL_RGB(187,154,247),

  .status_ok_text = PL_RGB(123,224,165), .status_ok_fill = PL_RGB(46,158,106),
  .status_err_text = PL_RGB(242,154,147),
  .str_ascii = PL_RGB(98,219,196), .str_utf16 = PL_RGB(187,154,247),

  .sec_pal = SEC_PAL_INIT
};

static const Theme s_theme_light = {
  .bg = PL_RGB(0xF4,0xF5,0xF9), .sidebar_bg = PL_RGB(0xE9,0xEB,0xF2),
  .sidebar_sel = PL_RGB(0xD9,0xDE,0xEE), .sidebar_hover = PL_RGB(0xE1,0xE4,0xEF),
  .panel = PL_RGB(0xFF,0xFF,0xFF), .panel_border = PL_RGB(0xD0,0xD4,0xE0),
  .panel_header = PL_RGB(0xEE,0xF0,0xF6), .panel_body = PL_RGB(0xFF,0xFF,0xFF),
  .accent = PL_RGB(0x2F,0x66,0xE8), .accent_hover = PL_RGB(0x4A,0x7F,0xF5),
  .on_accent = PL_RGB(0xFF,0xFF,0xFF),
  .text_primary = PL_RGB(0x1B,0x1D,0x2A), .text_secondary = PL_RGB(0x5E,0x62,0x75),
  .text_code = PL_RGB(0x4A,0x4E,0x63), .text_accent = PL_RGB(0x2F,0x66,0xE8),
  .highlight_bg = PL_RGB(0xFB,0xE3,0xE8), .highlight_txt = PL_RGB(0xB3,0x30,0x4F),
  .separator = PL_RGB(0xDD,0xE0,0xEA), .search_bg = PL_RGB(0xEC,0xEE,0xF4),
  .logo_bg = PL_RGB(0xE3,0xE6,0xF0),
  .btn_ghost = PL_RGB(0xEE,0xF0,0xF6), .btn_ghost_hover = PL_RGB(0xE2,0xE5,0xF0),
  .btn_ghost_bd = PL_RGB(0xC5,0xCA,0xDA),
  .badge = PL_RGB(0xB7,0x79,0x1F), .badge_bg = PL_RGB(0xFB,0xF1,0xDC),

  .ent_low = PL_RGB(0x2E,0x7D,0x32), .ent_mid = PL_RGB(0xE6,0x91,0x00),
  .ent_high = PL_RGB(0xC6,0x28,0x28), .byte_null = PL_RGB(0x2F,0x6F,0xDB),
  .byte_ctrl = PL_RGB(0x6B,0x6F,0x8A),

  .nt_zero = PL_RGB(0x8A,0x8F,0xAE), .nt_raw = PL_RGB(0x6B,0x71,0x94),
  .nt_row_alt = PL_RGB(0xF3,0xF4,0xF9), .nt_row_sel = PL_RGB(0xDD,0xE3,0xF7),
  .nt_colhdr = PL_RGB(0xE8,0xEA,0xF2), .nt_colhdr_line = PL_RGB(0xD3,0xD7,0xE4),
  .nt_group_line = PL_RGB(0xD0,0xD4,0xE2), .nt_green = PL_RGB(0x1E,0x8E,0x5A),
  .nt_amber = PL_RGB(0xA8,0x6B,0x00), .nt_chip_bg = PL_RGB(0xE4,0xE7,0xF4),
  .nt_chip_border = PL_RGB(0xB5,0xBA,0xD0), .nt_sb_thumb = PL_RGB(0xB8,0xBD,0xD3),
  .nt_sb_hot = PL_RGB(0x9A,0xA0,0xBC),

  .danger = PL_RGB(0xC9,0x30,0x2C), .warning = PL_RGB(0xD9,0x8A,0x12),
  .success = PL_RGB(0x2E,0x9E,0x4F), .on_color = PL_RGB(0xFF,0xFF,0xFF),

  .syn_string = PL_RGB(0x3F,0x7D,0x20), .syn_keyword = PL_RGB(0x2F,0x5F,0xD0),
  .syn_function = PL_RGB(0xA8,0x62,0x0A), .syn_number = PL_RGB(0xC2,0x41,0x0C),
  .syn_object = PL_RGB(0x7C,0x3A,0xED),

  .status_ok_text = PL_RGB(0x1E,0x8E,0x5A), .status_ok_fill = PL_RGB(0x2E,0x9E,0x6A),
  .status_err_text = PL_RGB(0xC0,0x39,0x2B),
  .str_ascii = PL_RGB(0x0F,0x8F,0x7A), .str_utf16 = PL_RGB(0x7C,0x3A,0xED),

  .sec_pal = SEC_PAL_INIT
};

/* ------------------------------------------------------------------ */
/*  Runtime switching                                                  */
/* ------------------------------------------------------------------ */

enum { THEME_DARK = 0, THEME_LIGHT = 1 };

THEME_SHARED int   g_theme_mode = THEME_DARK;
THEME_SHARED void (*g_theme_listener)(void) = 0;   /* set by main window */

static __inline const Theme* theme_cur(void)
{
  return g_theme_mode == THEME_LIGHT ? &s_theme_light : &s_theme_dark;
}

static __inline void theme_set(int mode)
{
  if (mode == g_theme_mode) return;
  g_theme_mode = mode;
  if (g_theme_listener) g_theme_listener();   /* repaint every view */
}

/* ------------------------------------------------------------------ */
/*  Colour names (same names as before, so call sites don't change)    */
/* ------------------------------------------------------------------ */

#define CLR_BG              (theme_cur()->bg)
#define CLR_SIDEBAR_BG      (theme_cur()->sidebar_bg)
#define CLR_SIDEBAR_SEL     (theme_cur()->sidebar_sel)
#define CLR_SIDEBAR_HOVER   (theme_cur()->sidebar_hover)
#define CLR_PANEL           (theme_cur()->panel)
#define CLR_PANEL_BORDER    (theme_cur()->panel_border)
#define CLR_PANEL_HEADER    (theme_cur()->panel_header)
#define CLR_PANEL_BODY      (theme_cur()->panel_body)
#define CLR_ACCENT          (theme_cur()->accent)
#define CLR_ACCENT_HOVER    (theme_cur()->accent_hover)
#define CLR_ON_ACCENT       (theme_cur()->on_accent)
#define CLR_TEXT_PRIMARY    (theme_cur()->text_primary)
#define CLR_TEXT_SECONDARY  (theme_cur()->text_secondary)
#define CLR_TEXT_CODE       (theme_cur()->text_code)
#define CLR_TEXT_ACCENT     (theme_cur()->text_accent)
#define CLR_HIGHLIGHT_BG    (theme_cur()->highlight_bg)
#define CLR_HIGHLIGHT_TXT   (theme_cur()->highlight_txt)
#define CLR_SEPARATOR       (theme_cur()->separator)
#define CLR_SEARCH_BG       (theme_cur()->search_bg)
#define CLR_LOGO_BG         (theme_cur()->logo_bg)
#define CLR_BTN_GHOST       (theme_cur()->btn_ghost)
#define CLR_BTN_GHOST_HOVER (theme_cur()->btn_ghost_hover)
#define CLR_BTN_GHOST_BD    (theme_cur()->btn_ghost_bd)
#define CLR_BADGE           (theme_cur()->badge)
#define CLR_BADGE_BG        (theme_cur()->badge_bg)

#define CLR_ENT_LOW         (theme_cur()->ent_low)
#define CLR_ENT_MID         (theme_cur()->ent_mid)
#define CLR_ENT_HIGH        (theme_cur()->ent_high)
#define CLR_BYTE_NULL       (theme_cur()->byte_null)
#define CLR_BYTE_CTRL       (theme_cur()->byte_ctrl)
#define COL_LOW   CLR_ENT_LOW
#define COL_MID   CLR_ENT_MID
#define COL_HIGH  CLR_ENT_HIGH

#define CLR_NT_ZERO         (theme_cur()->nt_zero)
#define CLR_NT_RAW          (theme_cur()->nt_raw)
#define CLR_NT_ROW_ALT      (theme_cur()->nt_row_alt)
#define CLR_NT_ROW_SEL      (theme_cur()->nt_row_sel)
#define CLR_NT_COLHDR       (theme_cur()->nt_colhdr)
#define CLR_NT_COLHDR_LINE  (theme_cur()->nt_colhdr_line)
#define CLR_NT_GROUP_LINE   (theme_cur()->nt_group_line)
#define CLR_NT_GREEN        (theme_cur()->nt_green)
#define CLR_NT_AMBER        (theme_cur()->nt_amber)
#define CLR_NT_CHIP_BG      (theme_cur()->nt_chip_bg)
#define CLR_NT_CHIP_BORDER  (theme_cur()->nt_chip_border)
#define CLR_NT_SB_THUMB     (theme_cur()->nt_sb_thumb)
#define CLR_NT_SB_HOT       (theme_cur()->nt_sb_hot)

#define CLR_DANGER          (theme_cur()->danger)
#define CLR_WARNING         (theme_cur()->warning)
#define CLR_SUCCESS         (theme_cur()->success)
#define CLR_ON_COLOR        (theme_cur()->on_color)

#define CLR_SYN_STRING      (theme_cur()->syn_string)
#define CLR_SYN_KEYWORD     (theme_cur()->syn_keyword)
#define CLR_SYN_FUNCTION    (theme_cur()->syn_function)
#define CLR_SYN_NUMBER      (theme_cur()->syn_number)
#define CLR_SYN_OBJECT      (theme_cur()->syn_object)

#define CLR_STATUS_OK_TEXT  (theme_cur()->status_ok_text)
#define CLR_STATUS_OK_FILL  (theme_cur()->status_ok_fill)
#define CLR_STATUS_ERR_TEXT (theme_cur()->status_err_text)
#define CLR_STR_ASCII       (theme_cur()->str_ascii)
#define CLR_STR_UTF16       (theme_cur()->str_utf16)

#define CLR_SEC_PAL_COUNT   6
#define CLR_ROW_SEL_MIX_PCT   16
#define CLR_ROW_HOVER_MIX_PCT 7

/* Syntax class -> colour. Order matches CL_TXT..CL_OBJ in signatures.c */
static __inline PlColor theme_syn(int cls)
{
  const Theme* t = theme_cur();
  switch (cls) {
  case 1:  return t->text_secondary;   /* CL_COM */
  case 2:  return t->syn_string;       /* CL_STR */
  case 3:  return t->syn_keyword;      /* CL_KEY */
  case 4:  return t->syn_function;     /* CL_FN  */
  case 5:  return t->syn_number;       /* CL_NUM */
  case 6:  return t->syn_object;       /* CL_OBJ */
  default: return t->text_primary;     /* CL_TXT */
  }
}

static __inline PlColor theme_sec_pal(int k)
{
  return theme_cur()->sec_pal[(unsigned)k % CLR_SEC_PAL_COUNT];
}

/* Blend: pct % of a, the rest of b. Works on any platform. */
static __inline PlColor theme_mix_pct(PlColor a, PlColor b, int pct)
{
  unsigned x = (unsigned)a, y = (unsigned)b, r = 0;
  int i;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  for (i = 0; i < 24; i += 8) {
    unsigned ca = (x >> i) & 255, cb = (y >> i) & 255;
    r |= ((ca * (unsigned)pct + cb * (unsigned)(100 - pct)) / 100) << i;
  }
  return (PlColor)r;
}

/* 0..256 blend toward b (used by the entropy ramp) */
static __inline PlColor theme_mix(PlColor a, PlColor b, int t)
{
  unsigned ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
  unsigned br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
  if (t < 0) t = 0;
  if (t > 256) t = 256;
  return (PlColor)((((ar * (256 - t) + br * t) >> 8) << 16) |
    (((ag * (256 - t) + bg * t) >> 8) << 8) |
    ((ab * (256 - t) + bb * t) >> 8));
}

#define ENT_T_PACKED    6.5
#define ENT_T_HIGH      7.2
#define FONT_UI_PT   9
/* ---- layout constants (were in the old header) ---- */
#define SIDEBAR_W      268
#define LOGO_H          50
#define SEARCH_H        36
#define SEARCH_MX       12
#define SEARCH_MY        8
#define NAV_H           40
#define HEADER_H        90
#define INFOBAR_H       84
#define TABBAR_H        46
#define BOTTOMBAR_H     56
#define CONTENT_PAD     20

/* ---- section palette names (were constants, now lookups) ---- */
#define CLR_SEC_PAL_0   theme_sec_pal(0)
#define CLR_SEC_PAL_1   theme_sec_pal(1)
#define CLR_SEC_PAL_2   theme_sec_pal(2)
#define CLR_SEC_PAL_3   theme_sec_pal(3)
#define CLR_SEC_PAL_4   theme_sec_pal(4)
#define CLR_SEC_PAL_5   theme_sec_pal(5)

static __inline PlColor theme_entropy_col(double e)
{
  if (e <= 4.5) return CLR_ENT_LOW;
  if (e < ENT_T_PACKED)
    return theme_mix(CLR_ENT_LOW, CLR_ENT_MID,
      (int)((e - 4.5) / (ENT_T_PACKED - 4.5) * 256));
  if (e < ENT_T_HIGH)
    return theme_mix(CLR_ENT_MID, CLR_ENT_HIGH,
      (int)((e - ENT_T_PACKED) / (ENT_T_HIGH - ENT_T_PACKED) * 256));
  return CLR_ENT_HIGH;
}

static __inline int theme_entropy_lvl(double e)
{
  return e >= ENT_T_HIGH ? 2 : (e >= ENT_T_PACKED ? 1 : 0);
}

#ifdef _WIN32
#define TH_CR(c) RGB(((c) >> 16) & 255, ((c) >> 8) & 255, (c) & 255)

static __inline void th_fill_cr(HDC dc, int l, int t, int r, int b, COLORREF c)
{
  RECT rc; HBRUSH br;
  rc.left = l; rc.top = t; rc.right = r; rc.bottom = b;
  br = CreateSolidBrush(c);
  FillRect(dc, &rc, br);
  DeleteObject(br);
}

static __inline void th_fill(HDC dc, int l, int t, int r, int b, PlColor c)
{
  th_fill_cr(dc, l, t, r, b, TH_CR(c));
}

static __inline void th_line(HDC dc, int x0, int y0, int x1, int y1, PlColor c)
{
  HPEN pen = CreatePen(PS_SOLID, 1, TH_CR(c));
  HPEN old = (HPEN)SelectObject(dc, pen);
  MoveToEx(dc, x0, y0, NULL); LineTo(dc, x1, y1);
  SelectObject(dc, old); DeleteObject(pen);
}

static __inline void th_aa_corner(HDC dc, int x0, int y0, int cx, int cy,
  int rr, COLORREF color)
{
  int px, py, i, j;
  int r8 = rr * 8, cx8 = cx * 8, cy8 = cy * 8;
  int sr = GetRValue(color), sg = GetGValue(color), sb = GetBValue(color);

  for (py = y0; py < y0 + rr; ++py) {
    for (px = x0; px < x0 + rr; ++px) {
      int cov = 0;
      COLORREF d;
      for (j = 0; j < 4; ++j) {
        for (i = 0; i < 4; ++i) {
          int dx = px * 8 + 2 * i + 1 - cx8;
          int dy = py * 8 + 2 * j + 1 - cy8;
          if (dx * dx + dy * dy <= r8 * r8) ++cov;
        }
      }
      if (cov == 0) continue;
      if (cov == 16) { SetPixelV(dc, px, py, color); continue; }
      d = GetPixel(dc, px, py);
      if (d == CLR_INVALID) continue;
      SetPixelV(dc, px, py, RGB(
        (sr * cov + GetRValue(d) * (16 - cov) + 8) / 16,
        (sg * cov + GetGValue(d) * (16 - cov) + 8) / 16,
        (sb * cov + GetBValue(d) * (16 - cov) + 8) / 16));
    }
  }
}

static __inline void th_aa_shape(HDC dc, int l, int t, int r, int b,
  int rr, COLORREF color)
{
  int w = r - l, h = b - t;
  if (w <= 0 || h <= 0) return;
  if (rr > w / 2) rr = w / 2;
  if (rr > h / 2) rr = h / 2;
  if (rr <= 0) { th_fill_cr(dc, l, t, r, b, color); return; }

  th_fill_cr(dc, l + rr, t, r - rr, b, color);
  th_fill_cr(dc, l, t + rr, l + rr, b - rr, color);
  th_fill_cr(dc, r - rr, t + rr, r, b - rr, color);
  th_aa_corner(dc, l, t, l + rr, t + rr, rr, color);
  th_aa_corner(dc, r - rr, t, r - rr, t + rr, rr, color);
  th_aa_corner(dc, l, b - rr, l + rr, b - rr, rr, color);
  th_aa_corner(dc, r - rr, b - rr, r - rr, b - rr, rr, color);
}

static __inline void th_round_rect(HDC dc, int l, int t, int r, int b,
  PlColor fillc, PlColor borderc, int radius)
{
  int rr = radius / 2;
  COLORREF fill = TH_CR(fillc), border = TH_CR(borderc);
  if (border != fill) {
    th_aa_shape(dc, l, t, r, b, rr, border);
    th_aa_shape(dc, l + 1, t + 1, r - 1, b - 1, rr > 0 ? rr - 1 : 0, fill);
  }
  else {
    th_aa_shape(dc, l, t, r, b, rr, fill);
  }
}

typedef struct {
  HDC     target;
  HDC     dc;
  HBITMAP bmp;
  HBITMAP old;
  int     w, h;
} ThBuf;

static __inline HDC th_buf_begin(ThBuf* b, HDC target, int w, int h)
{
  b->target = target; b->dc = target; b->bmp = NULL; b->old = NULL;
  b->w = w; b->h = h;
  if (w <= 0 || h <= 0) return target;
  b->dc = CreateCompatibleDC(target);
  if (b->dc) {
    b->bmp = CreateCompatibleBitmap(target, w, h);
    if (b->bmp) {
      b->old = (HBITMAP)SelectObject(b->dc, b->bmp);
      return b->dc;
    }
    DeleteDC(b->dc);
  }
  b->dc = target;
  return target;
}

static __inline void th_buf_end(ThBuf* b)
{
  if (b->bmp) {
    BitBlt(b->target, 0, 0, b->w, b->h, b->dc, 0, 0, SRCCOPY);
    SelectObject(b->dc, b->old);
    DeleteObject(b->bmp);
    DeleteDC(b->dc);
    b->bmp = NULL;
  }
}

static __inline void th_dot(HDC dc, int cx, int cy, int radius, PlColor color)
{
  th_aa_shape(dc, cx - radius, cy - radius, cx + radius, cy + radius,
    radius, TH_CR(color));
}

static __inline HFONT th_font(int pt, int bold, const WCHAR* face)
{
  HDC screen = GetDC(NULL);
  int height = -MulDiv(pt, GetDeviceCaps(screen, LOGPIXELSY), 72);
  ReleaseDC(NULL, screen);
  return CreateFontW(height, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
    0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
    DEFAULT_PITCH | FF_SWISS, face);
}
#endif /* _WIN32 */


#endif /* THEME_H */
