/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_STRINGS_SOURCE_H
#define CDIE_GUI_STRINGS_SOURCE_H
#include <xxwidgets/xxwidgets_strings.h>
typedef struct gui_strings_source gui_strings_source;
/* Worker entry points. Real-file metadata is streamed to temporary disk-backed
 * indexes and merged by absolute offset with bounded RAM. The generated case
 * really scans count * 5 bytes, then uses its known pattern for lazy pages. */
xxwidgets_status gui_strings_source_open(const char *path, xx_pd_struct *progress, gui_strings_source **out, char **error);
/* Configured extraction follows DIE's ANSI alphabet and UTF-16LE Unicode.
 * Configuration searches build a private candidate and publish it atomically;
 * the public source/model addresses and user pointer stay stable. */
xxwidgets_status gui_strings_source_open_options(const char *path, const xxwidgets_strings_search_options *options, xx_pd_struct *progress, gui_strings_source **out,
                                                 char **error);
const char *gui_strings_source_error(const gui_strings_source *source);
xxwidgets_status gui_strings_source_generated(uint64_t count, xx_pd_struct *progress, gui_strings_source **out, char **error);
xxwidgets_strings_source *gui_strings_source_model(gui_strings_source *source);
uint64_t gui_strings_source_size(const gui_strings_source *source);
void gui_strings_source_free(gui_strings_source *source);
#endif
