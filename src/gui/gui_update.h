/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_UPDATE_H
#define CDIE_GUI_UPDATE_H

/* The "Update database" action shared by the Windows, GTK and Cocoa
 * frontends: src/updater's check-and-install on a worker thread, behind
 * xxwidgets' progress dialog, followed by a reload of the backend.
 *
 * The folders updated are the backend's current ones. An explicitly chosen
 * folder is updated in place; a default one may move to the per-user folder
 * when it is read-only (an installed package, an AppImage, a macOS bundle),
 * which the backend's default lookup searches first. Default slots are then
 * reloaded from that lookup, also when the per-user copy was already there
 * and up to date (written by diec --update or die-update): frontends refresh
 * their tags and results whenever gui_backend_database_generation changed. */

#include <stddef.h>
#include <xxwidgets/xxwidgets.h>

typedef enum gui_update_result_e {
    GUI_UPDATE_UNAVAILABLE = 0, /* this build has no updater */
    GUI_UPDATE_CURRENT,         /* already up to date; nothing installed (the backend may have
                                   switched to the up-to-date per-user copy) */
    GUI_UPDATE_INSTALLED,       /* installed, and the backend reloaded it */
    GUI_UPDATE_RELOAD_FAILED,   /* installed, or up to date in a per-user copy the backend was not
                                   using, but it did not load; the old database stays active */
    GUI_UPDATE_FAILED,          /* nothing changed */
    GUI_UPDATE_CANCELLED        /* stopped from the progress dialog; nothing changed */
} gui_update_result;

/* Nonzero when this build can update the database. */
int gui_update_supported(void);

/* "unavailable", "current", "installed", "reload_failed", "failed" or
 * "cancelled": the result names --smoke-update prints and accepts. */
const char *gui_update_result_name(gui_update_result result);

/* Update from @p url (NULL or "" = the default source; the old
 * ".../releases/tag/db" default counts as unset). @p owner owns the progress
 * dialog, which appears after a second, shows the step ("[2/4] Downloading")
 * above the updater's own levels and offers Cancel; NULL runs without a
 * dialog (smoke tests). @p message receives a translated one-line result
 * for the status bar. With an owner, FAILED and RELOAD_FAILED are also shown
 * whole in a text dialog over it before this returns. Call it from the main
 * loop, never from an input callback, and only while no scan is running.
 *
 * The environment variable CDIE_UPDATE_URL replaces @p url and
 * CDIE_UPDATE_NOPROXY (any value) connects directly; both exist for tests. */
gui_update_result gui_update_run(xxwidgets_widget *owner, const char *url, char *message, size_t message_size);

#endif
