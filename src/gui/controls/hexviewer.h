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
#ifndef CDIE_HEXVIEWER_H
#define CDIE_HEXVIEWER_H
#include <stddef.h>
#include <stdint.h>
#include "platform/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

  typedef struct HexViewer HexViewer;
  typedef int  (*HexReadFn)(void* user, uint64_t offset, void* buf, size_t len);
  typedef void (*HexSelectFn)(void* user, uint64_t offset, uint64_t length);
  typedef int  (*HexCopyFn)(void* user, const char* text);
  typedef void (*HexStatusFn)(const char* text, void* user);

  typedef struct {
    uint64_t offset, length;
    PlColor bg, fg;
    const char* name;
  } HexRegion;

  enum { HEX_COPY_HEX = 0, HEX_COPY_TEXT = 1 };

  HexViewer* hexview_create(void* parent_native, PlFont* mono_font, int x, int y, int w, int h);
  void       hexview_destroy(HexViewer* hv);
  PlWidget* hexview_widget(HexViewer* hv);
  void       hexview_set_rect(HexViewer* hv, int x, int y, int w, int h);

  void hexview_set_memory(HexViewer* hv, const void* data, uint64_t size, uint64_t base_address);
  void hexview_set_source(HexViewer* hv, uint64_t size, HexReadFn read, void* user, uint64_t base_address);

  int  hexview_set_columns(HexViewer* hv, unsigned columns);  
  void hexview_goto(HexViewer* hv, uint64_t offset);
  void hexview_set_selection(HexViewer* hv, uint64_t offset, uint64_t length);
  int  hexview_get_selection(const HexViewer* hv, uint64_t* offset, uint64_t* length);
  void hexview_set_regions(HexViewer* hv, const HexRegion* regions, size_t count);
  void hexview_set_callbacks(HexViewer* hv, HexSelectFn on_select, HexCopyFn on_copy, void* user);
  void hexview_set_status_callback(HexViewer* hv, HexStatusFn fn, void* user);
  void hexview_set_info_callback(HexViewer* hv, HexStatusFn fn, void* user);
  int  hexview_copy(HexViewer* hv, int mode);

  void hexview_key(HexViewer* hv, int key, uint32_t ch, int mods);
  void hexview_tick(HexViewer* hv);

#ifdef __cplusplus
}
#endif
#endif