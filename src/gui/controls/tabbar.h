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
#ifndef CDIE_TABBAR_H
#define CDIE_TABBAR_H
#include "platform/platform.h"

enum { TAB_SCAN, TAB_MEMORY, TAB_DISASM, TAB_HEX, TAB_STRINGS,
       TAB_HASH, TAB_ENTROPY, TAB_MORE, TAB_COUNT };

typedef void (*TabbarCb)(void *user, int tab_id);
typedef struct Tabbar Tabbar;

Tabbar   *tabbar_create(void *parent_native, int x, int y, int w, int h,
                        TabbarCb cb, void *user);
void      tabbar_destroy(Tabbar *t);
PlWidget *tabbar_widget(const Tabbar *t);
void      tabbar_set_active(Tabbar *t, int tab_id);
int       tabbar_get_active(const Tabbar *t);
void tabbar_relayout(Tabbar* t);


#endif