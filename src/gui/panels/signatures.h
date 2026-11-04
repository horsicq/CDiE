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
#ifndef CDIE_SIGNATURES_H
#define CDIE_SIGNATURES_H

#include <xxwidgets/xxwidgets.h>

#ifdef __cplusplus
extern "C" {
#endif

void signatures_show(xxwidgets_app* app, xxwidgets_widget* owner);
int signatures_handle_event(const xxwidgets_event* event);

typedef struct Sig SigView;
SigView* sigview_create(void* parent_native, int x, int y, int w, int h);
void     sigview_set_rect(SigView* s, int x, int y, int w, int h);
void     sigview_activate(SigView* s);
void     sigview_reload(SigView* s);
void		 sigview_key(SigView* s, int key, uint32_t ch, int mods);
void		 sigview_tick(SigView* s);

#ifdef __cplusplus
}
#endif

#endif /* CDIE_SIGNATURES_H */