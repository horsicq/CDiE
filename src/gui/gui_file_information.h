/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_FILE_INFORMATION_H
#define CDIE_GUI_FILE_INFORMATION_H
#include <xxwidgets/xxwidgets_file_information.h>

typedef struct gui_file_information_data {
    char *path, *file_type;
    int64_t size;
    xxwidgets_data_struct_item *items;
    size_t count, capacity, bytes, field_capacity;
} gui_file_information_data;

/* Worker-safe native DataStructs collection, independent of signature state.
 * No widget calls. The result owns ordinary C allocations; error, when set,
 * is released with gui_backend_free. Failures/cancellation return no model. */
int gui_file_information_read(const char *path, xx_pd_struct *progress,
    gui_file_information_data **data, char **error);
/* As above with the slice of a fat (universal) Mach-O to open: its fat_arch
 * index, or -1 (CDIE_FILE_INFO_SLICE_AUTO, what gui_file_information_read
 * and _show pass) for the host architecture's slice, else the first. */
int gui_file_information_read_slice(const char *path, int slice, xx_pd_struct *progress,
    gui_file_information_data **data, char **error);
void gui_file_information_free(gui_file_information_data *data);

/* UI thread, outside input dispatch. Loads with cancellable worker progress,
 * then displays the reusable xxwidgets modal browser. */
xxwidgets_status gui_file_information_show(xxwidgets_widget *owner, const char *path);
xxwidgets_status gui_file_information_show_slice(xxwidgets_widget *owner, const char *path, int slice);
/* As gui_file_information_show, opened at the first structure named selector
 * (a DataStruct name such as "IMAGE_SECTION_HEADER", or a gui_backend_facts
 * *_selector); the first structure when the file has none of that name. */
xxwidgets_status gui_file_information_show_at(xxwidgets_widget *owner, const char *path, const char *selector);
#endif
