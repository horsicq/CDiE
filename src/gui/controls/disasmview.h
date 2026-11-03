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
