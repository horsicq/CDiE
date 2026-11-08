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
#ifndef CDIE_SIDEBAR_H
#define CDIE_SIDEBAR_H
#include "platform/platform.h"

enum { NAV_OVERVIEW, NAV_SECTIONS, NAV_SIGNATURES, NAV_STRINGS, NAV_HEX,
       NAV_DISASM, NAV_ENTROPY, NAV_VIRUSTOTAL, NAV_HEADERS, NAV_YARA, NAV_OPTIONS, NAV_COUNT };

typedef void (*SidebarCb)(void *user, int nav_id);
typedef struct Sidebar Sidebar;

Sidebar  *sidebar_create(void *parent_native, int x, int y, int w, int h,
                         SidebarCb cb, void *user);
void      sidebar_destroy(Sidebar *s);
PlWidget *sidebar_widget(const Sidebar *s);
void      sidebar_set_selected(Sidebar *s, int nav_id);
int       sidebar_get_selected(const Sidebar *s);

#endif
