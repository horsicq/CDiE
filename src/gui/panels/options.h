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
#ifndef CDIE_OPTIONS_H
#define CDIE_OPTIONS_H

#include <stddef.h>
#include <xxwidgets/xxwidgets.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPTIONS_FONT_ROLES 4   /* controls, table views, tree views, text edits */

enum {
  OPTIONS_PAGE_SCAN = 0,
  OPTIONS_PAGE_DATABASES,
  OPTIONS_PAGE_OPTIMIZATION,
  OPTIONS_PAGE_FONTS,
  OPTIONS_PAGE_LANGUAGE,
  OPTIONS_PAGE_CONTEXT,
  OPTIONS_PAGE_ABOUT
};

enum {
  OPTIONS_RESULT_NONE = 0,       /* closed with OK or Cancel */
  OPTIONS_RESULT_DATABASES,      /* host should open its database paths dialog */
  OPTIONS_RESULT_FONTS,          /* host should open its fonts dialog */
  OPTIONS_RESULT_CONTEXT         /* host should open its context menu dialog */
};

typedef struct OptionsScan {
  int deep, heuristic, verbose, aggressive, hide_unknown, format, monitor_downloads;
  int db_extra, db_custom;
} OptionsScan;

typedef struct OptionsOptimization {
  size_t buffer_size;            /* 0 = Default */
  size_t file_buffer_size;       /* 0 = Default */
  int use_sse2, use_avx2;
  int has_sse2, has_avx2;        /* host-reported CPU/OS support */
} OptionsOptimization;

typedef struct OptionsFont {
  char family[128];            /* empty = default family */
  unsigned int point_size;     /* 0 = default size */
  int bold, italic;
} OptionsFont;

typedef struct OptionsHost {
  void* user;
  void (*get_scan)(void* user, OptionsScan* out);
  int  (*set_scan)(void* user, const OptionsScan* in);
  int  (*get_optimization)(void* user, OptionsOptimization* out);
  int  (*set_optimization)(void* user, const OptionsOptimization* in);

  int  context_available;      /* 0 hides the Context menu page */
  int  updates_available;      /* 0 hides the Update page */
  int (*open_url)(void* user, const char* url);
  int (*copy_text)(void* user, const char* text);

  int  (*browse_folder)(void* user, char* out, size_t cap);
  void (*db_changed)(void* user);
  int  (*get_fonts)(void* user, OptionsFont out[OPTIONS_FONT_ROLES]);
  int  (*set_fonts)(void* user, const OptionsFont in[OPTIONS_FONT_ROLES]);
  int  (*choose_font)(void* user, int role, OptionsFont* font);
  int  (*context_get)(void* user, int* enabled);
  int  (*context_set)(void* user, int enabled);
  const char* (*get_version)(void* user);
  int  (*check_update)(void* user, char* out, size_t cap);
  int  (*list_fonts)(void* user, int fixed_only,
  void (*add)(const char* family, void* ud), void* ud);

  int  (*list_languages)(void* user,
    void (*add)(const char* code, const char* name, void* ud), void* ud);
  const char* (*get_language)(void* user);
  int  (*set_language)(void* user, const char* code);
  int (*set_theme)(void* user, int mode);
  int (*paste_text)(void* user, char* out, size_t cap);
} OptionsHost;


int options_show(xxwidgets_app* app, void* owner_native,
  const OptionsHost* host, int start_page);

#ifdef __cplusplus
}
#endif
#endif