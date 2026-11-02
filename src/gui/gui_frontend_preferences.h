/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_FRONTEND_PREFERENCES_H
#define CDIE_GUI_FRONTEND_PREFERENCES_H
#include "gui_options.h"

void gui_frontend_get_preferences(gui_options_values *values);
xxwidgets_status gui_frontend_apply_preferences(xxwidgets_app *app, const gui_options_values *values);
int gui_frontend_scan_after_open(void);
/* View/Advanced, shared with Detect It Easy: the main window's Advanced view.
 * set_advanced changes the in-memory preferences only; the frontend's
 * gui_settings_save writes the key when it differs from the store, so
 * toggling twice writes nothing. */
int gui_frontend_advanced(void);
void gui_frontend_set_advanced(int advanced);
void gui_frontend_remember_file(const char *path);
xxwidgets_status gui_frontend_recent_dialog(xxwidgets_widget *owner, char *path, size_t capacity, int *accepted);
char *gui_frontend_stored_language(int memory_only); /* free() by caller; NULL = System */
char *gui_frontend_executable_directory(void); /* free() by caller */
/* Single-application setting applies at startup. Zero means another instance
 * owns this installation. A negative return means the instance lock failed. */
int gui_frontend_instance_start(xxwidgets_widget *window, const char *startup_file);
void gui_frontend_instance_close(void);
/* POSIX: a later launch of this installation that found the instance lock
 * sends its absolute file path over a per-user AF_UNIX socket (mode 0600, in
 * XDG_RUNTIME_DIR or the macOS per-user temporary folder, else /tmp) and waits
 * up to 3 s for a one-byte answer; when the lock comes free meanwhile it takes
 * over as the owner. Call from the main loop: returns 1 and a malloc'd UTF-8 path
 * (free() by caller) when one arrived, else 0 with *path_out = NULL. The path
 * is "" for a launch without a file: only bring the window to the front, as
 * Windows does. Never blocks. Always 0 on Windows, where WM_COPYDATA carries
 * the path. */
int gui_frontend_instance_poll(xxwidgets_widget *window, char **path_out);
#define GUI_FRONTEND_COPYDATA_ID 0x43444945u
/* Windows user PATH: inspect/apply the exact application-directory component.
 * No process is launched and unrelated PATH components are preserved. */
int gui_frontend_environment_contains(const char *directory);
xxwidgets_status gui_frontend_environment_apply(const char *directory, int enabled);
#endif
