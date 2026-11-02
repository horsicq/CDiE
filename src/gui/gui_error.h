/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_ERROR_H
#define CDIE_GUI_ERROR_H
/* Failure text for people, shared by the tool dialogs, Options and the result
 * panel: translated sentences in place of status names such as "operation
 * unavailable during callback or polling", and for a file a tool cannot read,
 * its path and what the file system says about it.
 *
 * Header-only, so every tool and its probe still compile just their own
 * source. The catalog extractor must read this header too: its globs take
 * the .c files of src/gui only. */
#include "app/cdie_translation.h"
#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/io/xx_io.h>
#include <xxfclib/rt/xx_rt.h>
#include <xxwidgets/xxwidgets.h>

/* A sentence for a dialog or action that failed with status. */
static inline const char *gui_error_status_text(xxwidgets_status status)
{
    switch (status) {
        case XXWIDGETS_BUSY: return CDIE_TR("Another window is still open. Close it and try again.");
        case XXWIDGETS_UNAVAILABLE: return CDIE_TR("This is not available on this system.");
        case XXWIDGETS_OUT_OF_MEMORY: return CDIE_TR("There is not enough memory.");
        case XXWIDGETS_INVALID_ARGUMENT: return CDIE_TR("The request is not valid.");
        case XXWIDGETS_BUFFER_TOO_SMALL: return CDIE_TR("The text is too long.");
        default: return CDIE_TR("The system reported an error.");
    }
}

/* What the file system says about a file a tool could not read: a sentence,
 * or NULL when the file opens and has data (the reader's detail then says
 * what is wrong with its content). */
static inline const char *gui_error_file_reason(const char *path)
{
    xx_io_device *device;
    int64_t size;
    if (!path || !path[0]) return CDIE_TR("No file is selected.");
    if (xx_fs_is_dir(path)) return CDIE_TR("This is a folder, not a file.");
    if (!xx_fs_exists(path)) return CDIE_TR("The file does not exist. It may have been moved, renamed or deleted.");
    device = xx_io_file_open(path, "rb");
    if (!device) return CDIE_TR("The file cannot be opened. Another program may be using it, or you may not have permission to read it.");
    size = xx_io_total_size(device);
    xx_io_close(device);
    if (size < 0) return CDIE_TR("The size of the file cannot be read.");
    return size ? NULL : CDIE_TR("The file is empty.");
}

/* The status line for a file that cannot be scanned: one sentence on every
 * platform, with what the file system says when it says anything. */
static inline void gui_error_open_status(char *buffer, size_t capacity, const char *path)
{
    const char *reason = gui_error_file_reason(path);
    /* Without a reason, the older whole sentence: every catalog translates it. */
    if (reason) cdie_translation_snprintf(buffer, capacity, CDIE_N_("Cannot open this file. %s"), reason);
    else cdie_translation_snprintf(buffer, capacity, "%s", CDIE_TR("Cannot open this file. Check the path and permissions."));
}

/* "headline\n\nreason\n\nDetails: detail" for a tool that cannot open or read
 * path. headline is an untranslated format, marked with CDIE_N_, with one %s
 * for the path; detail is the reader's own message (NULL or empty for none).
 * Free with xx_rt_free; NULL when out of memory. */
static inline char *gui_error_file_text(const char *headline, const char *path, const char *detail)
{
    const char *reason = gui_error_file_reason(path), *details = detail && detail[0] ? CDIE_TR(detail) : NULL;
    int head, tail = 0;
    size_t size, used;
    char *text;
    if (!path) path = "";
    head = cdie_translation_snprintf(NULL, 0, headline, path);
    if (details) tail = cdie_translation_snprintf(NULL, 0, CDIE_N_("Details: %s"), details);
    if (head < 0 || tail < 0) return NULL;
    size = (size_t)head + (reason ? xx_rt_strlen(reason) + 2 : 0) + (details ? (size_t)tail + 2 : 0) + 1;
    text = (char *)xx_rt_malloc(size);
    if (!text) return NULL;
    cdie_translation_snprintf(text, size, headline, path);
    used = xx_rt_strlen(text);
    if (reason) {
        xx_rt_memcpy(text + used, "\n\n", 2);
        xx_rt_memcpy(text + used + 2, reason, xx_rt_strlen(reason) + 1);
        used += xx_rt_strlen(text + used);
    }
    if (details) {
        xx_rt_memcpy(text + used, "\n\n", 3);
        used += 2;
        cdie_translation_snprintf(text + used, size - used, CDIE_N_("Details: %s"), details);
    }
    return text;
}

/* Shows gui_error_file_text in a text dialog titled title. A shown message is
 * the action's whole result: OK, so no second error reaches the status bar. */
static inline xxwidgets_status gui_error_show_file(xxwidgets_widget *owner, const char *title, const char *headline,
    const char *path, const char *detail)
{
    char *text = gui_error_file_text(headline, path, detail);
    xxwidgets_status status = xxwidgets_text_dialog(owner, title, text ? text : detail && detail[0] ? detail : title);
    xx_rt_free(text);
    return status;
}
#endif
