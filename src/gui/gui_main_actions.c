/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_main_actions.h"
#include "gui_backend.h"
#include "gui_frontend_preferences.h"
#include "gui_options.h"
#include "gui_tools.h"
#include "gui_error.h"
#include "app/cdie_translation.h"
#include <xxfclib/strings/xx_string.h>
#include <stdlib.h>
#include <string.h>

/* ---- File type list ---------------------------------------------------- */

typedef struct file_types_collect {
    gui_main_file_types *types;
    int preferred_id; /* the first concrete type the backend emits */
    int overflow;
} file_types_collect;

static void collect_type(int type_id, const char *name, void *user)
{
    file_types_collect *collect = (file_types_collect *)user;
    gui_main_file_types *types = collect->types;
    size_t i;
    if (type_id <= 0 || !name) return; /* "Automatic" is not an entry any more */
    for (i = 0; i < types->count; ++i)
        if (types->ids[i] == type_id) return;
    if (types->count >= GUI_MAIN_FILE_TYPES_MAX) { collect->overflow = 1; return; }
    if (!collect->preferred_id) collect->preferred_id = type_id;
    types->ids[types->count] = type_id;
    types->names[types->count] = name;
    ++types->count;
}

void gui_main_file_types_clear(gui_main_file_types *types)
{
    if (types) memset(types, 0, sizeof(*types));
}

int gui_main_file_types_load(gui_main_file_types *types, const char *path, int keep_choice)
{
    file_types_collect collect;
    int chosen_id = 0;
    size_t i, j;
    if (!types) return 0;
    if (keep_choice) chosen_id = gui_main_file_types_scan_id(types);
    gui_main_file_types_clear(types);
    if (!path || !path[0]) return 0;
    memset(&collect, 0, sizeof(collect));
    collect.types = types;
    if (!gui_backend_file_types(path, collect_type, &collect) || !types->count) {
        gui_main_file_types_clear(types);
        return 0;
    }
    /* Sorted by id, as Qt lists them (Binary, MSDOS, PE64). */
    for (i = 1; i < types->count; ++i)
        for (j = i; j > 0 && types->ids[j - 1] > types->ids[j]; --j) {
            int id = types->ids[j];
            const char *name = types->names[j];
            types->ids[j] = types->ids[j - 1];
            types->names[j] = types->names[j - 1];
            types->ids[j - 1] = id;
            types->names[j - 1] = name;
        }
    for (i = 0; i < types->count; ++i) {
        if (types->ids[i] == collect.preferred_id) types->preferred = i;
    }
    types->selected = types->preferred;
    for (i = 0; chosen_id && i < types->count; ++i)
        if (types->ids[i] == chosen_id) types->selected = i;
    return 1;
}

int gui_main_file_types_choose(gui_main_file_types *types, size_t index)
{
    if (!types || index >= types->count) return 0;
    types->selected = index;
    return 1;
}

int gui_main_file_types_scan_id(const gui_main_file_types *types)
{
    if (!types || !types->count || types->selected >= types->count || types->selected == types->preferred) return 0;
    return types->ids[types->selected];
}

void gui_main_file_types_free_records(xx_meta_string *records, size_t count)
{
    size_t i;
    if (!records) return;
    for (i = 0; i < count; ++i) {
        if (!records[i].meta_string) continue;
        if (records[i].meta_string->data) xx_str_wfree(records[i].meta_string->data);
        free(records[i].meta_string);
    }
    free(records);
}

xxwidgets_status gui_main_file_types_records(const gui_main_file_types *types, xx_meta_string **records)
{
    xx_meta_string *list;
    size_t i;
    if (!types || !records) return XXWIDGETS_INVALID_ARGUMENT;
    *records = NULL;
    if (!types->count) return XXWIDGETS_OK;
    list = (xx_meta_string *)calloc(types->count, sizeof(*list));
    if (!list) return XXWIDGETS_OUT_OF_MEMORY;
    for (i = 0; i < types->count; ++i) {
        wchar_t *wide = xx_str_utf8_to_unicode(types->names[i]);
        size_t length = 0;
        list[i].meta_string = (xx_str_w_s *)calloc(1, sizeof(*list[i].meta_string));
        if (!wide || !list[i].meta_string) {
            if (wide) xx_str_wfree(wide);
            gui_main_file_types_free_records(list, types->count);
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        while (wide[length]) ++length;
        list[i].meta_string->data = wide;
        list[i].meta_string->length = length;
        list[i].meta_string->capacity = length + 1;
        list[i].meta_string->is_view = true;
        list[i].var.type = XX_VAR_TYPE_UINT64;
        list[i].var.val.u64 = (uint64_t)types->ids[i];
    }
    *records = list;
    return XXWIDGETS_OK;
}

/* ---- Actions ----------------------------------------------------------- */

static void report_error(xxwidgets_widget *owner, const gui_main_hooks *hooks, const char *text)
{
    if (!text || !text[0]) return;
    /* Errors that went to the status line become message dialogs (Q1). */
    if (hooks && hooks->error) hooks->error(hooks->user, text);
    else if (owner) (void)xxwidgets_text_dialog(owner, gui_backend_app_title(), text);
}

static int scan_type_id(const gui_main_hooks *hooks)
{
    return hooks ? gui_main_file_types_scan_id(hooks->file_types) : 0;
}

static void rescan(const gui_main_hooks *hooks, const char *path)
{
    if (hooks && hooks->scan && path && path[0]) hooks->scan(hooks->user, scan_type_id(hooks));
}

/* Scan/Engine, as Options > Scan sets it: 1 when it changed. */
static int set_engine(size_t index)
{
    gui_backend_scan_preferences scan;
    gui_options_values values;
    const char *id = gui_backend_engine_id(index);
    size_t length;
    if (!id || !gui_backend_get_scan_preferences(&scan) || !strcmp(scan.engine, id)) return 0;
    length = strlen(id);
    if (length >= sizeof(scan.engine) || length >= sizeof(values.scan.engine)) return 0;
    memcpy(scan.engine, id, length + 1);
    if (!gui_backend_set_scan_preferences(&scan)) return 0;
    gui_preferences_get(&values);
    memcpy(values.scan.engine, id, length + 1);
    gui_preferences_set(&values);
    return 1;
}

static void run_tool(xxwidgets_widget *owner, gui_tool_id tool, const char *path, const gui_main_hooks *hooks)
{
    report_error(owner, hooks, gui_tool_run(owner, tool, path ? path : ""));
}

/* The grid's tools that cdie has; the others are disabled buttons (D). */
static int grid_tool(int die_tool, gui_tool_id *tool)
{
    switch (die_tool) {
        case XXWIDGETS_DIE_TOOL_FILE_INFO: *tool = GUI_TOOL_FILE_INFORMATION; return 1;
        case XXWIDGETS_DIE_TOOL_MEMORY_MAP: *tool = GUI_TOOL_MEMORY_MAP; return 1;
        case XXWIDGETS_DIE_TOOL_DISASM: *tool = GUI_TOOL_DISASM; return 1;
        case XXWIDGETS_DIE_TOOL_HEX: *tool = GUI_TOOL_HEX_VIEW; return 1;
        case XXWIDGETS_DIE_TOOL_STRINGS: *tool = GUI_TOOL_STRINGS; return 1;
        case XXWIDGETS_DIE_TOOL_HASH: *tool = GUI_TOOL_HASH; return 1;
        case XXWIDGETS_DIE_TOOL_ENTROPY: *tool = GUI_TOOL_ENTROPY; return 1;
        case XXWIDGETS_DIE_TOOL_EXTRACTOR: *tool = GUI_TOOL_EXTRACTOR; return 1;
        default: return 0;
    }
}

#define CALL(hook) do { if (hooks && hooks->hook) hooks->hook(hooks->user); } while (0)

int gui_main_run_action(xxwidgets_widget *owner, xxwidgets_die_main_panel *panel, const char *path, const xxwidgets_die_action *action,
                        const gui_main_hooks *hooks)
{
    gui_tool_id tool;
    (void)panel; /* Phase 2: the type area's items read the panel */
    if (!action) return 0;
    switch (action->id) {
        case XXWIDGETS_DIE_ACTION_NONE: return 1;
        case XXWIDGETS_DIE_ACTION_RECENT: CALL(recent); return 1;
        case XXWIDGETS_DIE_ACTION_BROWSE: CALL(browse); return 1;
        case XXWIDGETS_DIE_ACTION_OPEN_PATH: CALL(open_path); return 1;
        case XXWIDGETS_DIE_ACTION_FILE_TYPE:
            /* Choosing another type rescans with it, as Qt does. */
            if (hooks && action->value >= 0 && gui_main_file_types_choose(hooks->file_types, (size_t)action->value)) rescan(hooks, path);
            return 1;
        case XXWIDGETS_DIE_ACTION_ENGINE:
            if (action->value >= 0 && set_engine((size_t)action->value)) {
                CALL(settings_changed);
                rescan(hooks, path);
            }
            return 1;
        case XXWIDGETS_DIE_ACTION_ADVANCED_CHANGED:
            /* The panel already shows the view; View/Advanced is written
             * through the changes-only path (DIE_LOOK.md 2.1). */
            gui_frontend_set_advanced(action->value != 0);
            CALL(settings_changed);
            return 1;
        case XXWIDGETS_DIE_ACTION_TOOL:
            if (grid_tool(action->value, &tool)) run_tool(owner, tool, path, hooks);
            else if (action->value == XXWIDGETS_DIE_TOOL_VIRUSTOTAL) return 0; /* Phase 2 */
            return 1;
        case XXWIDGETS_DIE_ACTION_SCAN:
            if (hooks && hooks->scan) hooks->scan(hooks->user, scan_type_id(hooks));
            return 1;
        case XXWIDGETS_DIE_ACTION_REPORT: CALL(report); return 1;
        case XXWIDGETS_DIE_ACTION_SIGNATURES: CALL(signatures); return 1;
        case XXWIDGETS_DIE_ACTION_DIRECTORY: CALL(directory); return 1;
        case XXWIDGETS_DIE_ACTION_LOG: CALL(log); return 1;
        case XXWIDGETS_DIE_ACTION_UPDATE:
            if (hooks && hooks->with_update) CALL(update);
            return 1;
        case XXWIDGETS_DIE_ACTION_SHORTCUTS: {
            xxwidgets_status status = gui_shortcuts_show(owner, hooks ? hooks->with_update : 0);
            if (status != XXWIDGETS_OK && status != XXWIDGETS_BUSY) {
                char message[256];
                cdie_translation_snprintf(message, sizeof(message), CDIE_N_("%s: %s"), CDIE_TR("Shortcuts"), gui_error_status_text(status));
                report_error(owner, hooks, message);
            }
            return 1;
        }
        case XXWIDGETS_DIE_ACTION_OPTIONS:
            if (hooks && hooks->options) hooks->options(hooks->user, -1);
            return 1;
        case XXWIDGETS_DIE_ACTION_ABOUT: CALL(about); return 1;
        case XXWIDGETS_DIE_ACTION_EXIT: CALL(exit); return 1;
        default: return 0; /* PATH_EDITED; ENTRY_POINT, FORMAT, FORMAT_ARROW, DEMANGLE (Phase 2/3) */
    }
}

int gui_main_run_shortcut(xxwidgets_widget *owner, xxwidgets_die_main_panel *panel, const char *path, gui_shortcut_action shortcut,
                          const gui_main_hooks *hooks)
{
    xxwidgets_die_action action;
    memset(&action, 0, sizeof(action));
    if (shortcut >= GUI_SHORTCUT_TOOL_FIRST && shortcut <= GUI_SHORTCUT_TOOL_LAST) {
        run_tool(owner, (gui_tool_id)(shortcut - GUI_SHORTCUT_TOOL_FIRST), path, hooks);
        return 1;
    }
    switch (shortcut) {
        case GUI_SHORTCUT_OPEN: action.id = XXWIDGETS_DIE_ACTION_BROWSE; break;
        case GUI_SHORTCUT_EXIT: action.id = XXWIDGETS_DIE_ACTION_EXIT; break;
        case GUI_SHORTCUT_SCAN: action.id = XXWIDGETS_DIE_ACTION_SCAN; break;
        case GUI_SHORTCUT_RECENT: action.id = XXWIDGETS_DIE_ACTION_RECENT; break;
        case GUI_SHORTCUT_REPORT: action.id = XXWIDGETS_DIE_ACTION_REPORT; break;
        case GUI_SHORTCUT_ABOUT: action.id = XXWIDGETS_DIE_ACTION_ABOUT; break;
        case GUI_SHORTCUT_OPTIONS: action.id = XXWIDGETS_DIE_ACTION_OPTIONS; break;
        case GUI_SHORTCUT_UPDATE: action.id = XXWIDGETS_DIE_ACTION_UPDATE; break;
        case GUI_SHORTCUT_DATABASE_PATHS:
            if (hooks && hooks->options) hooks->options(hooks->user, GUI_OPTIONS_SCAN);
            return 1;
        case GUI_SHORTCUT_FONTS:
            if (hooks && hooks->options) hooks->options(hooks->user, GUI_OPTIONS_FONTS);
            return 1;
        default: return 0;
    }
    return gui_main_run_action(owner, panel, path, &action, hooks);
}
