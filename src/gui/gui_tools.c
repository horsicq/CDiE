/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_tools.h"
#include "gui_error.h"
#include "gui_file_information.h"
#include "gui_entropy.h"
#include "gui_hash.h"
#include "gui_strings.h"
#include "gui_hex_view.h"
#include "gui_disasm.h"
#include "gui_memory_map.h"
#include "gui_imports.h"
#include "gui_exports.h"
#include "gui_resources.h"
#include "gui_symbols.h"
#include "gui_extractor.h"
#include "app/cdie_translation.h"

const gui_tool gui_tools[GUI_TOOL_COUNT] = {
    {CDIE_N_("File Information"), CDIE_N_("Choose a PE, ELF or Mach-O file to inspect."), gui_file_information_show},
    {CDIE_N_("Entropy"), CDIE_N_("Choose a file to calculate entropy."), gui_entropy_show},
    {CDIE_N_("Hash"), CDIE_N_("Choose a file to calculate hashes."), gui_hash_show},
    {CDIE_N_("Strings"), CDIE_N_("Choose a file to extract strings."), gui_strings_show},
    {CDIE_N_("Hex View"), CDIE_N_("Choose a file to view its bytes."), gui_hex_view_show},
    {CDIE_N_("Disasm"), CDIE_N_("Choose a file to disassemble."), gui_disasm_show},
    {CDIE_N_("Memory Map"), CDIE_N_("Choose a file to map."), gui_memory_map_show},
    {CDIE_N_("Imports"), CDIE_N_("Choose a file to inspect imports."), gui_imports_show},
    {CDIE_N_("Exports"), CDIE_N_("Choose a file to inspect exports."), gui_exports_show},
    {CDIE_N_("Resources"), CDIE_N_("Choose a file to inspect resources."), gui_resources_show},
    {CDIE_N_("Symbols"), CDIE_N_("Choose a file to inspect symbols."), gui_symbols_show},
    {CDIE_N_("Extractor"), CDIE_N_("Choose a file to extract embedded files."), gui_extractor_show},
};

const char *gui_tool_run(xxwidgets_widget *owner, gui_tool_id id, const char *path)
{
    static char message[512];
    xxwidgets_status status;
    if ((int)id < 0 || id >= GUI_TOOL_COUNT) return CDIE_TR("The request is not valid.");
    if (!path || !path[0]) return CDIE_TR(gui_tools[id].hint);
    status = gui_tools[id].show(owner, path);
    if (status == XXWIDGETS_OK) return NULL;
    cdie_translation_snprintf(message, sizeof(message), CDIE_N_("%s: %s"), CDIE_TR(gui_tools[id].title), gui_error_status_text(status));
    return message;
}
