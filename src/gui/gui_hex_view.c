/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "app/cdie_translation.h"
#include "gui_hex_view.h"
#include "gui_error.h"
#include "../app/cdie_app.h"
#include <xxfclib/rt/xx_rt.h>

static xxwidgets_hex_options g_hex_options;
static int g_hex_options_set;

void gui_hex_view_get_options(xxwidgets_hex_options *options)
{
    if (!options) return;
    if (!g_hex_options_set) { xxwidgets_hex_options_init(&g_hex_options); g_hex_options_set = 1; }
    *options = g_hex_options;
}

void gui_hex_view_set_options(const xxwidgets_hex_options *options)
{
    if (!options) return;
    g_hex_options = *options;
    g_hex_options.editable = 0;
    g_hex_options_set = 1;
}

int gui_hex_view_open(const char *path, xx_io_device **out, char **error)
{
    xx_io_device *device;
    if (!out || !error) return 0;
    *out = NULL; *error = NULL;
    device = path && *path ? xx_io_file_open(path, "rb") : NULL;
    if (!device) { *error = cdie_strdup(CDIE_N_("Cannot open file for Hex View")); return 0; }
    if (xx_io_total_size(device) < 0) {
        xx_io_close(device); *error = cdie_strdup(CDIE_N_("Cannot determine file size")); return 0;
    }
    *out = device; return 1;
}
static xxwidgets_status show_hex(xxwidgets_widget *owner, const char *path, int at, uint64_t offset, uint64_t length);

xxwidgets_status gui_hex_view_show(xxwidgets_widget *owner, const char *path)
{
    return show_hex(owner, path, 0, 0, 0);
}

xxwidgets_status gui_hex_view_show_at(xxwidgets_widget *owner, const char *path, uint64_t offset, uint64_t length)
{
    return show_hex(owner, path, 1, offset, length);
}

static xxwidgets_status show_hex(xxwidgets_widget *owner, const char *path, int at, uint64_t offset, uint64_t length)
{
    xx_io_device *device = NULL;
    char *error = NULL;
    xxwidgets_status status;
    if (!owner || !path || !*path) return XXWIDGETS_INVALID_ARGUMENT;
    if (!gui_hex_view_open(path, &device, &error)) {
        /* The file system's reason says it better than the opener's detail. */
        status = gui_error_show_file(owner, CDIE_TR("Hex View"), CDIE_N_("Cannot open %s in the Hex View."), path,
            gui_error_file_reason(path) ? NULL : error);
        xx_rt_free(error); return status;
    }
    {
        xxwidgets_hex_options options;
        gui_hex_view_get_options(&options);
        /* A range the file no longer holds (it shrank since the facts were
         * read) opens at the start rather than not at all. */
        uint64_t size = (uint64_t)xx_io_total_size(device);
        if (at && offset <= size && length <= size - offset)
            status = xxwidgets_hexview_dialog_at(owner, CDIE_TR("Hex View"), path, device, &options, offset, length);
        else status = xxwidgets_hexview_dialog_with_options(owner, CDIE_TR("Hex View"), path, device, &options);
    }
    xx_io_close(device);
    return status;
}
