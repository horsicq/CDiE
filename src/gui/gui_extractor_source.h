/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_EXTRACTOR_SOURCE_H
#define CDIE_GUI_EXTRACTOR_SOURCE_H
#include <xxwidgets/xxwidgets_extractor.h>

typedef struct gui_extractor_source gui_extractor_source;
/* Opens a read-only source and selector metadata; the reusable browser runs
 * the initial scan on its cancellable worker. Every subsequent scan builds a
 * private disk-backed index and publishes it only on success. */
xxwidgets_status gui_extractor_source_open(const char *path, xx_pd_struct *progress, gui_extractor_source **out, char **error);
xxwidgets_extractor_source *gui_extractor_source_model(gui_extractor_source *source);
const char *gui_extractor_source_error(const gui_extractor_source *source);
void gui_extractor_source_free(gui_extractor_source *source);
#endif
