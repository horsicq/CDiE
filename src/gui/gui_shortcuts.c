/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_shortcuts.h"
#include "app/cdie_translation.h"
#include <string.h>

typedef struct gui_shortcut_row {
    gui_shortcut_action action;
    const char *name;                               /* CDIE_N_ */
    const char *keys[GUI_SHORTCUTS_PLATFORM_COUNT]; /* Windows, Linux, macOS */
} gui_shortcut_row;

/* DIE_LOOK.md 2.4. The tools keep their keys in both views; on macOS the
 * File menu binds them ('@': shown in the dialog, not bound again). Qt's Ctrl+E full screen is left out: Ctrl+E is
 * Entropy here. Tool names are gui_tools' titles, in gui_tool_id order. */
static const gui_shortcut_row rows[] = {
    {GUI_SHORTCUT_OPEN, CDIE_N_("Open file"), {"Ctrl+O", "Ctrl+O", "Cmd+O"}},
    {GUI_SHORTCUT_EXIT, CDIE_N_("Exit"), {"Alt+X", "Alt+X;Ctrl+Q", "@Cmd+Q"}},
    {GUI_SHORTCUT_SCAN, CDIE_N_("Scan"), {"F5", "F5", "F5"}},
    {GUI_SHORTCUT_RECENT, CDIE_N_("Recent files"), {"Ctrl+R", "Ctrl+R", "Cmd+R"}},
    {GUI_SHORTCUT_REPORT, CDIE_N_("Scan report"), {"Ctrl+Shift+R", "Ctrl+Shift+R", "Cmd+Shift+R"}},
    {GUI_SHORTCUT_ABOUT, CDIE_N_("About"), {"F1", "F1", "F1;@Cmd+?"}},
    {GUI_SHORTCUT_OPTIONS, CDIE_N_("Options"), {"F2", "F2", "F2;@Cmd+,"}},
    {GUI_SHORTCUT_DATABASE_PATHS, CDIE_N_("Database folders"), {"Ctrl+Shift+D;F3", "Ctrl+Shift+D;F3", "Cmd+Shift+D"}},
    {GUI_SHORTCUT_FONTS, CDIE_N_("Fonts"), {"F4", "F4", ""}},
    {GUI_SHORTCUT_UPDATE, CDIE_N_("Update database"), {"Ctrl+U", "Ctrl+U", "Cmd+U"}},
    {GUI_SHORTCUT_TOOL_FIRST + 0, CDIE_N_("File Information"), {"Ctrl+I", "Ctrl+I", "@Cmd+I"}},
    {GUI_SHORTCUT_TOOL_FIRST + 1, CDIE_N_("Entropy"), {"Ctrl+E", "Ctrl+E", "@Cmd+E"}},
    {GUI_SHORTCUT_TOOL_FIRST + 2, CDIE_N_("Hash"), {"Ctrl+H", "Ctrl+H", "@Cmd+Shift+H"}},
    {GUI_SHORTCUT_TOOL_FIRST + 3, CDIE_N_("Strings"), {"Ctrl+Shift+S", "Ctrl+Shift+S", "@Cmd+Shift+S"}},
    {GUI_SHORTCUT_TOOL_FIRST + 4, CDIE_N_("Hex View"), {"Ctrl+Shift+X", "Ctrl+Shift+X", "@Cmd+Shift+X"}},
    {GUI_SHORTCUT_TOOL_FIRST + 5, CDIE_N_("Disasm"), {"Ctrl+Shift+A", "Ctrl+Shift+A", "@Cmd+Shift+A"}},
    {GUI_SHORTCUT_TOOL_FIRST + 6, CDIE_N_("Memory Map"), {"Ctrl+M", "Ctrl+M", "@Cmd+Shift+M"}},
    {GUI_SHORTCUT_TOOL_FIRST + 7, CDIE_N_("Imports"), {"Ctrl+Shift+I", "Ctrl+Shift+I", "@Cmd+Shift+I"}},
    {GUI_SHORTCUT_TOOL_FIRST + 8, CDIE_N_("Exports"), {"Ctrl+Shift+E", "Ctrl+Shift+E", "@Cmd+Shift+E"}},
    {GUI_SHORTCUT_TOOL_FIRST + 9, CDIE_N_("Resources"), {"Ctrl+Shift+U", "Ctrl+Shift+U", "@Cmd+Shift+U"}},
    {GUI_SHORTCUT_TOOL_FIRST + 10, CDIE_N_("Symbols"), {"Ctrl+Shift+Y", "Ctrl+Shift+Y", "@Cmd+Shift+Y"}},
    {GUI_SHORTCUT_TOOL_FIRST + 11, CDIE_N_("Extractor"), {"Ctrl+Shift+T", "Ctrl+Shift+T", "@Cmd+Shift+T"}},
};

#define ROW_COUNT (sizeof(rows) / sizeof(rows[0]))

gui_shortcut_platform gui_shortcuts_platform(void)
{
#if defined(_WIN32)
    return GUI_SHORTCUTS_WINDOWS;
#elif defined(__APPLE__)
    return GUI_SHORTCUTS_MACOS;
#else
    return GUI_SHORTCUTS_LINUX;
#endif
}

static const gui_shortcut_row *row_of(gui_shortcut_action action)
{
    size_t i;
    for (i = 0; i < ROW_COUNT; ++i)
        if (rows[i].action == action) return &rows[i];
    return NULL;
}

const char *gui_shortcuts_name(gui_shortcut_action action)
{
    const gui_shortcut_row *row = row_of(action);
    return row ? row->name : NULL;
}

const char *gui_shortcuts_keys(gui_shortcut_action action, gui_shortcut_platform platform)
{
    const gui_shortcut_row *row = row_of(action);
    if (!row || (int)platform < 0 || platform >= GUI_SHORTCUTS_PLATFORM_COUNT) return "";
    return row->keys[platform];
}

/* Copies text into storage at *used; NULL when it does not fit. */
static char *store_text(char *storage, size_t storage_size, size_t *used, const char *text, size_t length)
{
    char *copy;
    if (length >= storage_size - *used) return NULL;
    copy = storage + *used;
    memcpy(copy, text, length);
    copy[length] = 0;
    *used += length + 1;
    return copy;
}

size_t gui_shortcuts_bindings(gui_shortcut_platform platform, int action_base, int with_update, xxwidgets_shortcut *out, size_t capacity,
                              char *storage, size_t storage_size)
{
    size_t i, count = 0, used = 0;
    if (!out || !storage || !storage_size || (int)platform < 0 || platform >= GUI_SHORTCUTS_PLATFORM_COUNT) return 0;
    for (i = 0; i < ROW_COUNT; ++i) {
        const char *key = rows[i].keys[platform];
        if (rows[i].action == GUI_SHORTCUT_UPDATE && !with_update) continue;
        while (*key) {
            const char *end = strchr(key, ';');
            size_t length = end ? (size_t)(end - key) : strlen(key);
            if (length && key[0] != '@') {
                char *copy;
                if (count >= capacity) return 0;
                copy = store_text(storage, storage_size, &used, key, length);
                if (!copy) return 0;
                out[count].sequence = copy;
                out[count].action = action_base + (int)rows[i].action;
                ++count;
            }
            key += length;
            if (*key == ';') ++key;
        }
    }
    return count;
}

gui_shortcut_action gui_shortcuts_action(int value, int action_base)
{
    long long action = (long long)value - action_base;
    if (action <= GUI_SHORTCUT_NONE || action >= GUI_SHORTCUT_COUNT) return GUI_SHORTCUT_NONE;
    return (gui_shortcut_action)action;
}

size_t gui_shortcuts_dialog_entries(gui_shortcut_platform platform, int with_update, xxwidgets_shortcut_entry *out, size_t capacity,
                                    char *storage, size_t storage_size)
{
    size_t i, count = 0, used = 0;
    if (!out || !storage || !storage_size || (int)platform < 0 || platform >= GUI_SHORTCUTS_PLATFORM_COUNT) return 0;
    for (i = 0; i < ROW_COUNT; ++i) {
        const char *key = rows[i].keys[platform];
        char *shown = storage + used;
        size_t pieces = 0;
        if (rows[i].action == GUI_SHORTCUT_UPDATE && !with_update) continue;
        if (count >= capacity) return 0;
        /* "Ctrl+Shift+D;F3" is shown as "Ctrl+Shift+D, F3"; '@' marks a
         * menu key, shown like the others. */
        while (*key) {
            const char *end = strchr(key, ';');
            size_t length = end ? (size_t)(end - key) : strlen(key);
            const char *text = key;
            key += length;
            if (*key == ';') ++key;
            if (length && text[0] == '@') { ++text; --length; }
            if (!length) continue;
            /* Room for the separator, the key and the terminator. */
            if (length + (pieces ? 2u : 0u) >= storage_size - used) return 0;
            if (pieces) { storage[used++] = ','; storage[used++] = ' '; }
            memcpy(storage + used, text, length);
            used += length;
            ++pieces;
        }
        if (used >= storage_size) return 0;
        storage[used++] = 0;
        out[count].name = CDIE_TR(rows[i].name);
        out[count].key = shown;
        ++count;
    }
    return count;
}

xxwidgets_status gui_shortcuts_show(xxwidgets_widget *owner, int with_update)
{
    xxwidgets_shortcut_entry entries[ROW_COUNT];
    char storage[1024];
    size_t count = gui_shortcuts_dialog_entries(gui_shortcuts_platform(), with_update, entries, ROW_COUNT, storage, sizeof(storage));
    if (!count) return XXWIDGETS_OUT_OF_MEMORY;
    return xxwidgets_shortcuts_dialog(owner, entries, count);
}
