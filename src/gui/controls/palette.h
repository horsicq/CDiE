#ifndef PALETTE_H
#define PALETTE_H

#include "platform/platform.h"

typedef struct Palette Palette;
typedef void (*PaletteCb)(void* user, int id);

Palette* palette_create(void* parent_native, PaletteCb cb, void* user);
void     palette_destroy(Palette* p);
PlWidget* palette_widget(const Palette* p);
void     palette_add(Palette* p, const char* label, const char* kind, int id);
void     palette_open(Palette* p, int ax, int ay, int aw, int ah);
void     palette_close(Palette* p);
int      palette_is_open(const Palette* p);
void     palette_key(Palette* p, int key, uint32_t ch, int mods);
int      palette_get_rect(const Palette* p, int* x, int* y, int* w, int* h);
#endif