/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Data-only description of DIE_LOOK.md 1.4 format rows. */

#ifndef CDIE_GUI_FORMAT_ROWS_H
#define CDIE_GUI_FORMAT_ROWS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gui_format_kind {
    GUI_FORMAT_PE = 0,
    GUI_FORMAT_ELF,
    GUI_FORMAT_MACHO,
    GUI_FORMAT_MACHO_FAT,
    GUI_FORMAT_MSDOS,
    GUI_FORMAT_NE,
    GUI_FORMAT_LE_LX,
    GUI_FORMAT_DEX,
    GUI_FORMAT_ARCHIVE,
    GUI_FORMAT_COM,
    GUI_FORMAT_BINARY,
    GUI_FORMAT_KIND_COUNT
} gui_format_kind;

typedef enum gui_format_button_id {
    GUI_FORMAT_BUTTON_NONE = 0,
    GUI_FORMAT_BUTTON_MAIN,
    GUI_FORMAT_BUTTON_SYMBOLS,
    GUI_FORMAT_BUTTON_EXPORT,
    GUI_FORMAT_BUTTON_IMPORT,
    GUI_FORMAT_BUTTON_RESOURCES,
    GUI_FORMAT_BUTTON_DOTNET,
    GUI_FORMAT_BUTTON_TLS,
    GUI_FORMAT_BUTTON_OVERLAY,
    GUI_FORMAT_BUTTON_SECTIONS,
    GUI_FORMAT_BUTTON_PROGRAMS,
    GUI_FORMAT_BUTTON_COMMANDS,
    GUI_FORMAT_BUTTON_SEGMENTS,
    GUI_FORMAT_BUTTON_LIBRARIES,
    GUI_FORMAT_BUTTON_MANIFEST,
    GUI_FORMAT_BUTTON_VERSION,
    GUI_FORMAT_BUTTON_JAR_MANIFEST,
    GUI_FORMAT_BUTTON_ANDROID_MANIFEST,
    GUI_FORMAT_BUTTON_ARCHIVE_FILES,
    GUI_FORMAT_BUTTON_ARCHIVE_UNPACK
} gui_format_button_id;

/* FACT_* rules map one-for-one to GUI_FACTS_* flags in gui_backend.h.  The
 * TOOL_* rules ask the existing tool dispatcher whether that format is
 * supported.  LOADED needs only a loaded file; NEVER is a missing tool. */
typedef enum gui_format_enable_rule {
    GUI_FORMAT_ENABLE_LOADED = 0,
    GUI_FORMAT_ENABLE_FACT_EXPORTS,
    GUI_FORMAT_ENABLE_FACT_IMPORTS,
    GUI_FORMAT_ENABLE_FACT_RESOURCES,
    GUI_FORMAT_ENABLE_FACT_TLS,
    GUI_FORMAT_ENABLE_FACT_OVERLAY,
    GUI_FORMAT_ENABLE_FACT_SECTIONS,
    GUI_FORMAT_ENABLE_FACT_MANIFEST,
    GUI_FORMAT_ENABLE_FACT_VERSION,
    GUI_FORMAT_ENABLE_TOOL_IMPORTS,
    GUI_FORMAT_ENABLE_TOOL_EXPORTS,
    GUI_FORMAT_ENABLE_TOOL_SYMBOLS,
    GUI_FORMAT_ENABLE_NEVER
} gui_format_enable_rule;

typedef enum gui_format_availability {
    GUI_FORMAT_AVAILABLE = 0,
    GUI_FORMAT_MISSING_TOOL_DISABLED,
    GUI_FORMAT_HIDDEN
} gui_format_availability;

typedef enum gui_format_value_id {
    GUI_FORMAT_VALUE_SECTIONS = 0,
    GUI_FORMAT_VALUE_TIME_DATE_STAMP,
    GUI_FORMAT_VALUE_SIZE_OF_IMAGE,
    GUI_FORMAT_VALUE_PROGRAMS,
    GUI_FORMAT_VALUE_COMMANDS,
    GUI_FORMAT_VALUE_SEGMENTS,
    GUI_FORMAT_VALUE_LIBRARIES
} gui_format_value_id;

/* Names identify members of gui_backend_facts without depending on its large
 * engine-facing header. */
typedef enum gui_format_fact_field {
    GUI_FORMAT_FACT_MAIN_LABEL = 0,
    GUI_FORMAT_FACT_SECTIONS,
    GUI_FORMAT_FACT_TIME_DATE_STAMP_TEXT,
    GUI_FORMAT_FACT_SIZE_OF_IMAGE,
    GUI_FORMAT_FACT_PROGRAMS,
    GUI_FORMAT_FACT_COMMANDS,
    GUI_FORMAT_FACT_SEGMENTS,
    GUI_FORMAT_FACT_LIBRARIES
} gui_format_fact_field;

typedef struct gui_format_button_row {
    gui_format_button_id id;
    const char *label;   /* untranslated CDIE_N_; NULL means facts.main_label */
    const char *tooltip; /* untranslated CDIE_N_; NULL follows the dynamic label */
    gui_format_enable_rule enabled;
    gui_format_availability availability;
} gui_format_button_row;

typedef struct gui_format_value_row {
    gui_format_value_id id;
    const char *caption; /* untranslated CDIE_N_ */
    gui_format_fact_field fact;
    gui_format_button_id arrow; /* NONE when the value has no arrow */
} gui_format_value_row;

typedef struct gui_format_row {
    gui_format_kind kind;
    const char *name;
    const gui_format_button_row *buttons; /* first visible button is the page title */
    size_t button_count;
    const gui_format_value_row *values;
    size_t value_count;
} gui_format_row;

size_t gui_format_rows_count(void);
const gui_format_row *gui_format_rows_at(size_t index);
const gui_format_row *gui_format_rows_find(gui_format_kind kind);

#ifdef __cplusplus
}
#endif

#endif /* CDIE_GUI_FORMAT_ROWS_H */
