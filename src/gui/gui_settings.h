/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_SETTINGS_H
#define CDIE_GUI_SETTINGS_H

#include "gui_options.h"
#include <xxfclib/settings/xx_settings.h>

/* The Options settings between runs, in Detect It Easy's own store and under
 * its key names, so cdie, die-update and the Qt program share them.
 * Store: the INI file named by CDIE_SETTINGS; else die.ini beside the
 * executable when a file named "PORTABLE" or "portable" is there; else the
 * platform store NTInfo/die (HKCU\Software\NTInfo\die, ~/.config/NTInfo/die.conf,
 * the biz.ntinfo.die preferences on macOS), selected by
 * xx_settings_create_application. Portable storage never falls back to native.
 * The first start after an upgrade copies what the store lacks from the
 * settings older versions kept (cdie.ini, NTInfo/cdie), once: both stores get
 * cdie/Migrated, so resetting Detect It Easy's settings does not copy them
 * again; the old settings stay as they are. memory_only (smoke tests) never
 * touches disk. *loaded is 0 when stored settings could not be read; the
 * frontend then continues with defaults in memory and the store is never
 * overwritten. */
xx_settings *gui_settings_open(int memory_only, int *loaded);
/* Use this, not xx_settings_destroy, for a store gui_settings_load read. */
void gui_settings_close(xx_settings *store);

/* Applies the stored backend settings (database folders, CPU optimization,
 * scan modes) and fills values and *page with the frontend's. Missing or
 * malformed keys keep what values and *page already hold; booleans and
 * numbers are accepted in Qt's text form or typed. Feature/ReadBufferSize
 * and Feature/FileBufferSize take effect clamped to the Optimization page's
 * range, 1 KiB to 1 GiB (64 MiB in a 32-bit build); 0 is the default size,
 * and so is a number past INT32_MAX, which Detect It Easy reads as its
 * default; the stored value stays as it is until the size is changed in
 * Options. Returns 0 when the
 * stored database folders do not load; the defaults stay active then. */
int gui_settings_load(xx_settings *store, gui_options_values *values, gui_options_page_id *page);

/* Records values, page and the backend state, then saves. After a
 * gui_settings_load of this store only settings that changed since are
 * written (compared with what the store holds, so a change kept by a failed
 * save is retried or undone), and values Detect It Easy stored keep their
 * form; without one every setting is written. Booleans become true/false and
 * numbers decimal text, fonts QFont strings with a family (a default font is
 * "", or a monospace string for the table and text views) and colors
 * "#rrggbb|#rrggbb"; default database folders are stored as $data/db and
 * $data/db_custom. The extra database keys of earlier versions (Extra/Path and
 * Extra/Enabled under Scan/Die/Database) are never read or written and stay
 * as they are. The language is cdie/Lang; View/Lang
 * changes only to "" (English) or "System", the values both programs read
 * alike. Detect It Easy's one color per scan category is cdie's light theme
 * color; the dark theme's stay in Scan/Color/Dark. View/Style, View/Qss,
 * File/Context and Detect It Easy's RecentFiles are never written. */
int gui_settings_save(xx_settings *store, const gui_options_values *values, gui_options_page_id page);

#endif
