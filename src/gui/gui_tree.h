/* Copyright (c) 2026 hors<horsicq@gmail.com> · SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_TREE_H
#define CDIE_GUI_TREE_H
#include <xxwidgets/xxwidgets_scan_panel.h>
#include "gui_scan_options.h"
/* Applies highlighting and all category colors to the current result tree and
 * subsequent scans, retaining row selection, expansion and signature marks. */
xxwidgets_status gui_tree_set_scan_preferences(const gui_scan_page_values *values);
/* Scan and replace the complete tree transactionally. Free type with gui_backend_free.
 *
 * The engine runs on a worker thread while xxwidgets_process_dialog, owned by
 * owner, keeps the UI responsive; its window opens only for a scan that takes
 * more than a second, shows the engine's progress levels and offers Cancel.
 * A cancelled scan returns 0 with *cancelled set (cancelled may be NULL).
 * Call outside xxwidgets input callbacks and polling; there, or without an
 * owner, it waits for the worker without a dialog. The backend's options and
 * databases must not change until it returns. */
int gui_tree_scan(xxwidgets_widget *owner, xxwidgets_scan_panel *panel, const char *path, int type_id, char **type, int *cancelled);

/* The rest of Detect It Easy's result panel. A scan also fills the file's
 * Endianness, Mode, Architecture and Type, the scan time and the Log, and
 * puts S and ? on result rows: S on rows found by a script, ? on every
 * detection. From the event callback, gui_tree_event takes a press of S, ?,
 * Signatures, Directory or Log in panel and returns 1; after
 * xxwidgets_app_poll returns, gui_tree_process shows it modally over owner:
 * Signatures lists every loaded signature in the editor (S opens it on the
 * row's script) and Run executes the edited copy on the scanned file; ? shows
 * the bundled description, else a web search; Directory scans a folder with
 * the panel's flags; Log shows the engine's errors and warnings. Returns a
 * line for the status bar, or NULL. Rows of a tree not filled by
 * gui_tree_scan do nothing. */
int gui_tree_event(xxwidgets_scan_panel *panel, const xxwidgets_event *event);
const char *gui_tree_process(xxwidgets_widget *owner);
/* Releases the last scan's rows, e.g. before exit. */
void gui_tree_reset(void);
#endif
