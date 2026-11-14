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
#ifndef CDIE_DISASMVIEW_H
#define CDIE_DISASMVIEW_H
#include <stddef.h>
#include <stdint.h>
#include "platform/platform.h"
#include "core/disasm_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

  typedef struct DisasmView DisasmView;
  typedef void (*DisasmStatusFn)(const char* text, void* user);

  DisasmView* disasmview_create(void* parent_native, PlFont* mono_font, int x, int y, int w, int h);
  void        disasmview_destroy(DisasmView* dv);
  PlWidget* disasmview_widget(DisasmView* dv);
  void        disasmview_set_rect(DisasmView* dv, int x, int y, int w, int h);

  /* Parses the PE through rd and jumps to the entry point. Returns 0 when the
   * file cannot be disassembled (the view then shows a short message). */
  int         disasmview_open(DisasmView* dv, DasmRead rd, void* user, uint64_t file_size);
  void        disasmview_close(DisasmView* dv);

  void        disasmview_goto_offset(DisasmView* dv, uint64_t file_offset);
  void        disasmview_goto_entry(DisasmView* dv);
  void        disasmview_set_status_callback(DisasmView* dv, DisasmStatusFn fn, void* user);

#ifdef __cplusplus
}
#endif
#endif
