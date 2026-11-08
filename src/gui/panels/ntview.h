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
#ifndef CDIE_NTVIEW_H
#define CDIE_NTVIEW_H

#include <stddef.h>
#include <stdint.h>
#include "platform/platform.h"

typedef struct NtView NtView;
typedef int (*NtRead)(void* user, uint64_t off, void* buf, size_t len);
typedef void (*NtGotoFn)(void* user, uint64_t offset, uint64_t length);
void ntview_set_callbacks(NtView* v, NtGotoFn on_goto, void* user);

NtView* ntview_create(void* parent_native, int x, int y, int w, int h);
PlWidget* ntview_widget(const NtView* v);
void      ntview_set_rect(NtView* v, int x, int y, int w, int h);
void      ntview_open(NtView* v, NtRead rd, void* user, uint64_t size);
void      ntview_close(NtView* v);

#endif