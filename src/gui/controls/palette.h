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