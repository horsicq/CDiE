/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef CDIE_GUI_TOOLS_H
#define CDIE_GUI_TOOLS_H
/* The 12 tool dialogs of the main window, in the order of the buttons and of
 * DIE's menu, with the wording every frontend uses for them: one title, one
 * hint for "no file chosen", and "<Tool>: <reason>" for a failure. */
#include <xxwidgets/xxwidgets.h>

typedef enum gui_tool_id {
    GUI_TOOL_FILE_INFORMATION = 0,
    GUI_TOOL_ENTROPY,
    GUI_TOOL_HASH,
    GUI_TOOL_STRINGS,
    GUI_TOOL_HEX_VIEW,
    GUI_TOOL_DISASM,
    GUI_TOOL_MEMORY_MAP,
    GUI_TOOL_IMPORTS,
    GUI_TOOL_EXPORTS,
    GUI_TOOL_RESOURCES,
    GUI_TOOL_SYMBOLS,
    GUI_TOOL_EXTRACTOR,
    GUI_TOOL_COUNT
} gui_tool_id;

typedef struct gui_tool {
    const char *title; /* untranslated (CDIE_N_): the button's label */
    const char *hint;  /* untranslated: the status line when no file is chosen */
    xxwidgets_status (*show)(xxwidgets_widget *owner, const char *path);
} gui_tool;

extern const gui_tool gui_tools[GUI_TOOL_COUNT];

/* Opens tool id for path over owner (the main window) and waits for it. NULL
 * when it ran (its own dialogs explain a file it cannot read); otherwise the
 * translated status-line text: the tool's hint for an empty path (focus the
 * path field then), or "<Tool>: <reason>". The text stays valid until the
 * next call. */
const char *gui_tool_run(xxwidgets_widget *owner, gui_tool_id id, const char *path);

#endif
