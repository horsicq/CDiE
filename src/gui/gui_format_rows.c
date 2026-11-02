/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_format_rows.h"
#include "app/cdie_translation.h"

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))
#define BUTTON(id, label, rule) {id, CDIE_N_(label), CDIE_N_(label), rule, GUI_FORMAT_AVAILABLE}
#define ARROW(id, tip) {id, "", CDIE_N_(tip), GUI_FORMAT_ENABLE_LOADED, GUI_FORMAT_AVAILABLE}
#define VALUE(id, caption, fact, arrow) {id, CDIE_N_(caption), fact, arrow}

/* Row 1, then the arrow/text-dialog buttons belonging to Row 2.  MAIN is the
 * Info-icon page-title button.  A NULL MAIN label is facts.main_label. */
static const gui_format_button_row pe_buttons[] = {
    BUTTON(GUI_FORMAT_BUTTON_MAIN, "PE", GUI_FORMAT_ENABLE_LOADED),
    BUTTON(GUI_FORMAT_BUTTON_SYMBOLS, "Symbols", GUI_FORMAT_ENABLE_TOOL_SYMBOLS),
    BUTTON(GUI_FORMAT_BUTTON_EXPORT, "Export", GUI_FORMAT_ENABLE_FACT_EXPORTS),
    BUTTON(GUI_FORMAT_BUTTON_IMPORT, "Import", GUI_FORMAT_ENABLE_FACT_IMPORTS),
    BUTTON(GUI_FORMAT_BUTTON_RESOURCES, "Resources", GUI_FORMAT_ENABLE_FACT_RESOURCES),
    {GUI_FORMAT_BUTTON_DOTNET, CDIE_N_(".NET"), CDIE_N_("Not available in this version"),
        GUI_FORMAT_ENABLE_NEVER, GUI_FORMAT_MISSING_TOOL_DISABLED},
    BUTTON(GUI_FORMAT_BUTTON_TLS, "TLS", GUI_FORMAT_ENABLE_FACT_TLS),
    BUTTON(GUI_FORMAT_BUTTON_OVERLAY, "Overlay", GUI_FORMAT_ENABLE_FACT_OVERLAY),
    {GUI_FORMAT_BUTTON_SECTIONS, "", CDIE_N_("Sections"), GUI_FORMAT_ENABLE_FACT_SECTIONS, GUI_FORMAT_AVAILABLE},
    BUTTON(GUI_FORMAT_BUTTON_MANIFEST, "Manifest", GUI_FORMAT_ENABLE_FACT_MANIFEST),
    BUTTON(GUI_FORMAT_BUTTON_VERSION, "Version", GUI_FORMAT_ENABLE_FACT_VERSION)
};
static const gui_format_value_row pe_values[] = {
    VALUE(GUI_FORMAT_VALUE_SECTIONS, "Sections", GUI_FORMAT_FACT_SECTIONS, GUI_FORMAT_BUTTON_SECTIONS),
    VALUE(GUI_FORMAT_VALUE_TIME_DATE_STAMP, "Time date stamp", GUI_FORMAT_FACT_TIME_DATE_STAMP_TEXT, GUI_FORMAT_BUTTON_NONE),
    VALUE(GUI_FORMAT_VALUE_SIZE_OF_IMAGE, "Size of image", GUI_FORMAT_FACT_SIZE_OF_IMAGE, GUI_FORMAT_BUTTON_NONE)
};

static const gui_format_button_row elf_buttons[] = {
    BUTTON(GUI_FORMAT_BUTTON_MAIN, "ELF", GUI_FORMAT_ENABLE_LOADED),
    BUTTON(GUI_FORMAT_BUTTON_IMPORT, "Import", GUI_FORMAT_ENABLE_TOOL_IMPORTS),
    BUTTON(GUI_FORMAT_BUTTON_EXPORT, "Export", GUI_FORMAT_ENABLE_TOOL_EXPORTS),
    BUTTON(GUI_FORMAT_BUTTON_SYMBOLS, "Symbols", GUI_FORMAT_ENABLE_TOOL_SYMBOLS),
    ARROW(GUI_FORMAT_BUTTON_PROGRAMS, "Programs"),
    ARROW(GUI_FORMAT_BUTTON_SECTIONS, "Sections")
};
static const gui_format_value_row elf_values[] = {
    VALUE(GUI_FORMAT_VALUE_PROGRAMS, "Programs", GUI_FORMAT_FACT_PROGRAMS, GUI_FORMAT_BUTTON_PROGRAMS),
    VALUE(GUI_FORMAT_VALUE_SECTIONS, "Sections", GUI_FORMAT_FACT_SECTIONS, GUI_FORMAT_BUTTON_SECTIONS)
};

static const gui_format_button_row macho_buttons[] = {
    BUTTON(GUI_FORMAT_BUTTON_MAIN, "Mach-O", GUI_FORMAT_ENABLE_LOADED),
    BUTTON(GUI_FORMAT_BUTTON_IMPORT, "Import", GUI_FORMAT_ENABLE_TOOL_IMPORTS),
    BUTTON(GUI_FORMAT_BUTTON_EXPORT, "Export", GUI_FORMAT_ENABLE_TOOL_EXPORTS),
    BUTTON(GUI_FORMAT_BUTTON_SYMBOLS, "Symbols", GUI_FORMAT_ENABLE_TOOL_SYMBOLS),
    ARROW(GUI_FORMAT_BUTTON_COMMANDS, "Commands"),
    ARROW(GUI_FORMAT_BUTTON_SEGMENTS, "Segments"),
    ARROW(GUI_FORMAT_BUTTON_SECTIONS, "Sections"),
    ARROW(GUI_FORMAT_BUTTON_LIBRARIES, "Libraries")
};
static const gui_format_value_row macho_values[] = {
    VALUE(GUI_FORMAT_VALUE_COMMANDS, "Commands", GUI_FORMAT_FACT_COMMANDS, GUI_FORMAT_BUTTON_COMMANDS),
    VALUE(GUI_FORMAT_VALUE_SEGMENTS, "Segments", GUI_FORMAT_FACT_SEGMENTS, GUI_FORMAT_BUTTON_SEGMENTS),
    VALUE(GUI_FORMAT_VALUE_SECTIONS, "Sections", GUI_FORMAT_FACT_SECTIONS, GUI_FORMAT_BUTTON_SECTIONS),
    VALUE(GUI_FORMAT_VALUE_LIBRARIES, "Libraries", GUI_FORMAT_FACT_LIBRARIES, GUI_FORMAT_BUTTON_LIBRARIES)
};

static const gui_format_button_row macho_fat_buttons[] = {
    BUTTON(GUI_FORMAT_BUTTON_MAIN, "Binary", GUI_FORMAT_ENABLE_LOADED)
};
static const gui_format_button_row msdos_buttons[] = {
    BUTTON(GUI_FORMAT_BUTTON_MAIN, "MSDOS", GUI_FORMAT_ENABLE_LOADED),
    BUTTON(GUI_FORMAT_BUTTON_OVERLAY, "Overlay", GUI_FORMAT_ENABLE_FACT_OVERLAY)
};
static const gui_format_button_row ne_buttons[] = {
    BUTTON(GUI_FORMAT_BUTTON_MAIN, "NE", GUI_FORMAT_ENABLE_LOADED)
};
static const gui_format_button_row le_lx_buttons[] = {
    {GUI_FORMAT_BUTTON_MAIN, NULL, NULL, GUI_FORMAT_ENABLE_LOADED, GUI_FORMAT_AVAILABLE}
};
static const gui_format_button_row dex_buttons[] = {
    BUTTON(GUI_FORMAT_BUTTON_MAIN, "DEX", GUI_FORMAT_ENABLE_LOADED)
};
static const gui_format_button_row archive_buttons[] = {
    {GUI_FORMAT_BUTTON_MAIN, NULL, NULL, GUI_FORMAT_ENABLE_LOADED, GUI_FORMAT_AVAILABLE},
    {GUI_FORMAT_BUTTON_JAR_MANIFEST, CDIE_N_("MANIFEST.MF"), CDIE_N_("MANIFEST.MF"),
        GUI_FORMAT_ENABLE_NEVER, GUI_FORMAT_HIDDEN},
    {GUI_FORMAT_BUTTON_ANDROID_MANIFEST, CDIE_N_("AndroidManifest.xml"), CDIE_N_("AndroidManifest.xml"),
        GUI_FORMAT_ENABLE_NEVER, GUI_FORMAT_HIDDEN},
    {GUI_FORMAT_BUTTON_ARCHIVE_FILES, CDIE_N_("Files"), CDIE_N_("Files"),
        GUI_FORMAT_ENABLE_NEVER, GUI_FORMAT_HIDDEN},
    {GUI_FORMAT_BUTTON_ARCHIVE_UNPACK, CDIE_N_("Unpack"), CDIE_N_("Unpack"),
        GUI_FORMAT_ENABLE_NEVER, GUI_FORMAT_HIDDEN}
};
static const gui_format_button_row binary_buttons[] = {
    BUTTON(GUI_FORMAT_BUTTON_MAIN, "Binary", GUI_FORMAT_ENABLE_LOADED)
};

/* COM deliberately has no buttons or values.  FAT Mach-O deliberately uses
 * the Binary page because File Information cannot open a universal binary. */
static const gui_format_row rows[] = {
    {GUI_FORMAT_PE, "PE", pe_buttons, COUNT_OF(pe_buttons), pe_values, COUNT_OF(pe_values)},
    {GUI_FORMAT_ELF, "ELF", elf_buttons, COUNT_OF(elf_buttons), elf_values, COUNT_OF(elf_values)},
    {GUI_FORMAT_MACHO, "Mach-O", macho_buttons, COUNT_OF(macho_buttons), macho_values, COUNT_OF(macho_values)},
    {GUI_FORMAT_MACHO_FAT, "Mach-O FAT", macho_fat_buttons, COUNT_OF(macho_fat_buttons), NULL, 0},
    {GUI_FORMAT_MSDOS, "MSDOS", msdos_buttons, COUNT_OF(msdos_buttons), NULL, 0},
    {GUI_FORMAT_NE, "NE", ne_buttons, COUNT_OF(ne_buttons), NULL, 0},
    {GUI_FORMAT_LE_LX, "LE/LX", le_lx_buttons, COUNT_OF(le_lx_buttons), NULL, 0},
    {GUI_FORMAT_DEX, "DEX", dex_buttons, COUNT_OF(dex_buttons), NULL, 0},
    {GUI_FORMAT_ARCHIVE, "ZIP/JAR/APK", archive_buttons, COUNT_OF(archive_buttons), NULL, 0},
    {GUI_FORMAT_COM, "COM", NULL, 0, NULL, 0},
    {GUI_FORMAT_BINARY, "Binary", binary_buttons, COUNT_OF(binary_buttons), NULL, 0}
};

size_t gui_format_rows_count(void)
{
    return COUNT_OF(rows);
}

const gui_format_row *gui_format_rows_at(size_t index)
{
    return index < COUNT_OF(rows) ? &rows[index] : NULL;
}

const gui_format_row *gui_format_rows_find(gui_format_kind kind)
{
    size_t i;
    for (i = 0; i < COUNT_OF(rows); ++i)
        if (rows[i].kind == kind) return &rows[i];
    return NULL;
}
