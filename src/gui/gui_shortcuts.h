/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_SHORTCUTS_H
#define CDIE_GUI_SHORTCUTS_H
/* The main window's keyboard shortcuts, one table for the three desktop
 * frontends (DIE_LOOK.md 2.4): each action with its translatable name and its
 * keys on Windows, Linux and macOS (Ctrl becomes Cmd there). Frontends bind
 * the keys with xxwidgets_window_set_shortcuts; the Shortcuts button shows
 * the same table read-only (xxwidgets_shortcuts_dialog). */
#include <stddef.h>
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_shortcuts_dialog.h>

typedef enum gui_shortcut_action {
    GUI_SHORTCUT_NONE = 0,
    GUI_SHORTCUT_OPEN,           /* the file chooser */
    GUI_SHORTCUT_EXIT,
    GUI_SHORTCUT_SCAN,
    GUI_SHORTCUT_RECENT,
    GUI_SHORTCUT_REPORT,
    GUI_SHORTCUT_ABOUT,
    GUI_SHORTCUT_OPTIONS,
    GUI_SHORTCUT_DATABASE_PATHS, /* Options on the Scan page's database folders */
    GUI_SHORTCUT_FONTS,          /* Options on the Fonts page */
    GUI_SHORTCUT_UPDATE,         /* bound only when gui_update_supported() */
    GUI_SHORTCUT_TOOL_FIRST,     /* + gui_tool_id: File Information ... Extractor */
    GUI_SHORTCUT_TOOL_LAST = GUI_SHORTCUT_TOOL_FIRST + 11,
    GUI_SHORTCUT_COUNT
} gui_shortcut_action;

typedef enum gui_shortcut_platform {
    GUI_SHORTCUTS_WINDOWS = 0,
    GUI_SHORTCUTS_LINUX,
    GUI_SHORTCUTS_MACOS,
    GUI_SHORTCUTS_PLATFORM_COUNT
} gui_shortcut_platform;

/* The platform this build runs on. */
gui_shortcut_platform gui_shortcuts_platform(void);

/* Untranslated (CDIE_N_) name of an action; NULL for NONE or out of range. */
const char *gui_shortcuts_name(gui_shortcut_action action);

/* The keys of an action on a platform, separated by ';' ("" for none). A key
 * that starts with '@' belongs to the macOS menu bar: the dialog shows it,
 * bindings skip it. */
const char *gui_shortcuts_keys(gui_shortcut_action action, gui_shortcut_platform platform);

/* Fills out[0..capacity) with the bindings of a platform, action =
 * action_base + gui_shortcut_action, in table order; an action with two keys
 * gets two bindings. with_update 0 leaves Update database out. The sequences
 * point into storage (storage_size bytes). Returns how many were written, or
 * 0 when out or storage is too small. */
size_t gui_shortcuts_bindings(gui_shortcut_platform platform, int action_base, int with_update, xxwidgets_shortcut *out, size_t capacity,
                              char *storage, size_t storage_size);

/* Maps a SHORTCUT event value back: the action, or NONE when value is not
 * action_base + a valid action. */
gui_shortcut_action gui_shortcuts_action(int value, int action_base);

/* The rows of the Shortcuts dialog for a platform: translated names (through
 * the cdie catalog) and the keys as shown, several joined with ", ". with_update
 * 0 leaves Update database out. Strings point into storage. Returns the row
 * count, 0 when out or storage is too small. */
size_t gui_shortcuts_dialog_entries(gui_shortcut_platform platform, int with_update, xxwidgets_shortcut_entry *out, size_t capacity,
                                    char *storage, size_t storage_size);

/* Opens the read-only Shortcuts dialog over owner for this platform. */
xxwidgets_status gui_shortcuts_show(xxwidgets_widget *owner, int with_update);

#endif
