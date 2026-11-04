#pragma once
#include <stdint.h>
#include <xxwidgets/xxwidgets.h>

typedef struct StringsView StringsView;

StringsView* stringsview_create(void* parent, int x, int y, int w, int h);
void stringsview_set_rect(StringsView* v, int x, int y, int w, int h);
void stringsview_set_file(StringsView* v, const char* path);
void stringsview_activate(StringsView* v);
void stringsview_key(StringsView* v, int key, uint32_t ch, int mods);
void stringsview_tick(StringsView* v);