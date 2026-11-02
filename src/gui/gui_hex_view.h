/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_HEX_VIEW_H
#define CDIE_GUI_HEX_VIEW_H
#include <xxwidgets/xxwidgets_hexview_browser.h>
#include <xxfclib/io/xx_io.h>
/* Opens rb; no parser/signature state. Caller closes the returned device.
 * Error strings use xx_rt allocation (gui_backend_free/xx_rt_free). */
int gui_hex_view_open(const char *path, xx_io_device **device, char **error);
/* Opens the viewer with the session's Hex preferences. */
xxwidgets_status gui_hex_view_show(xxwidgets_widget *owner, const char *path);
/* As gui_hex_view_show, opened at offset with length bytes selected (0: just
 * there), e.g. a gui_backend_facts overlay; a range past the end of the file
 * opens at the start. */
xxwidgets_status gui_hex_view_show_at(xxwidgets_widget *owner, const char *path, uint64_t offset, uint64_t length);
/* Session Hex preferences: bytes per row, bold non-zero bytes and font. They
 * start at xxwidgets_hex_options_init; the viewer stays read-only. */
void gui_hex_view_get_options(xxwidgets_hex_options *options);
void gui_hex_view_set_options(const xxwidgets_hex_options *options);
#endif
