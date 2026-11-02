/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_MAIN_ACTIONS_H
#define CDIE_GUI_MAIN_ACTIONS_H
/* What the main window's buttons do (DIE_LOOK.md 2.3), shared by the three
 * desktop frontends: they hand every action xxwidgets_die_main_panel reports,
 * and every shortcut, to gui_main_run_action / gui_main_run_shortcut. The
 * dialogs opened here are xxwidgets dialogs that behave alike everywhere;
 * the steps only a frontend can take (its file chooser, the recent-files
 * dialog, scanning, Options with the view re-applied, quitting, saving the
 * settings) are its hooks. */
#include <stddef.h>
#include <stdint.h>
#include <xxwidgets/xxwidgets.h>
#include <xxwidgets/xxwidgets_combobox.h> /* xx_meta_string */
#include <xxwidgets/xxwidgets_die_main_panel.h>
#include "gui_shortcuts.h"

/* The File type combo (DIE_LOOK.md 2.6): the file's concrete types, without
 * "Automatic", sorted by type id, with the most specific one (the type a
 * type-0 scan prefers) selected. While the user has not picked another
 * entry the scan still passes type id 0, so results stay those of the
 * "Automatic" scan; picking the preferred entry again returns to type 0. */
#define GUI_MAIN_FILE_TYPES_MAX 64
typedef struct gui_main_file_types {
    size_t count;
    int ids[GUI_MAIN_FILE_TYPES_MAX];
    const char *names[GUI_MAIN_FILE_TYPES_MAX]; /* the backend's static UTF-8 names */
    size_t preferred;                           /* index of the most specific type */
    size_t selected;                            /* the combo's selection */
} gui_main_file_types;

/* Reads path's types (gui_backend_file_types; needs gui_backend_init).
 * keep_choice keeps the user's pick when the type is still there (a rescan
 * of the same file); otherwise the preferred type is selected. Returns 0 on
 * a read failure or when the file has no concrete type: types is then empty. */
int gui_main_file_types_load(gui_main_file_types *types, const char *path, int keep_choice);
void gui_main_file_types_clear(gui_main_file_types *types);
/* The user picked record index; 0 when index is out of range. */
int gui_main_file_types_choose(gui_main_file_types *types, size_t index);
/* The type id to scan with: 0 unless the user picked a type other than the
 * preferred one. */
int gui_main_file_types_scan_id(const gui_main_file_types *types);
/* Combo records (UINT64 values = type ids) for xxwidgets_file_header_set_file_types
 * or xxwidgets_combobox_set_records; release with gui_main_file_types_free_records. */
xxwidgets_status gui_main_file_types_records(const gui_main_file_types *types, xx_meta_string **records);
void gui_main_file_types_free_records(xx_meta_string *records, size_t count);

/* Refreshes every file-dependent value in the Advanced view for type_id:
 * the header, engine row and per-format page.  type_id 0 resolves exactly as
 * the backend's automatic scan. */
xxwidgets_status gui_main_apply_file_facts(xxwidgets_die_main_panel *panel, const char *path, int type_id);
/* Restores/stores the Qt-compatible Scan/IncludeTags and Scan/ExcludeTags
 * values around set_available_tags. Store also applies them to the backend. */
xxwidgets_status gui_main_restore_tag_filters(xxwidgets_scan_panel *panel);
xxwidgets_status gui_main_store_tag_filters(xxwidgets_scan_panel *panel);

typedef struct gui_main_hooks {
    void *user;
    /* NULL hooks make their action do nothing. */
    void (*browse)(void *user);                  /* BROWSE, shortcut Open: the file chooser */
    void (*recent)(void *user);                  /* RECENT: the recent-files dialog, then open */
    void (*open_path)(void *user);               /* OPEN_PATH: open the typed path */
    void (*scan)(void *user, int type_id);       /* SCAN, a new File type or engine: scan the loaded file */
    void (*report)(void *user);                  /* REPORT */
    void (*signatures)(void *user);              /* the scan bar's Signatures */
    void (*directory)(void *user);
    void (*log)(void *user);
    void (*update)(void *user);                  /* only called with with_update */
    void (*options)(void *user, int page);       /* -1: the last page; else a gui_options_page_id */
    void (*about)(void *user);
    void (*exit)(void *user);                    /* the normal close path */
    void (*settings_changed)(void *user);        /* save the settings (changes only) */
    void (*error)(void *user, const char *text); /* NULL: a text dialog titled with the app name */
    int with_update;                             /* an updater is built in */
    gui_main_file_types *file_types;             /* the File type list, or NULL */
} gui_main_hooks;

/* Carries out action for the loaded file path ("" or NULL: none). Returns 1
 * when the action was one of the shared ones (done, or deliberately
 * nothing), 0 when the frontend must handle it (PATH_EDITED and DEMANGLE). */
int gui_main_run_action(xxwidgets_widget *owner, xxwidgets_die_main_panel *panel, const char *path, const xxwidgets_die_action *action,
                        const gui_main_hooks *hooks);

/* A SHORTCUT event mapped with gui_shortcuts_action: the same steps as the
 * buttons; the tools work in both views, also those without a button. */
int gui_main_run_shortcut(xxwidgets_widget *owner, xxwidgets_die_main_panel *panel, const char *path, gui_shortcut_action shortcut,
                          const gui_main_hooks *hooks);

#endif
