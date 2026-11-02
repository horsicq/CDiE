/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_OPTIONS_EXTRA_H
#define CDIE_GUI_OPTIONS_EXTRA_H
#include "gui_options.h"
typedef struct gui_options_extra_values {
    gui_options_page_id id;
    gui_options_values *values;
    const gui_options_values *defaults;
    const gui_options_host *host;
} gui_options_extra_values;
xxwidgets_status gui_options_extra_page(xxwidgets_options_page *page, const char *title, gui_options_extra_values *values);
/* host->browse on behalf of page (see gui_options_browse_page). */
xxwidgets_status gui_options_browse_for(const gui_options_host *host, gui_options_page_id page, xxwidgets_widget *dialog,
                                        const char *initial, char **path, int *accepted);
#endif
