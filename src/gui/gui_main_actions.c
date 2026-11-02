/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "gui_main_actions.h"
#include "gui_backend.h"
#include "gui_frontend_preferences.h"
#include "gui_options.h"
#include "gui_tools.h"
#include "gui_format_rows.h"
#include "gui_file_information.h"
#include "gui_hex_view.h"
#include "gui_disasm.h"
#include "gui_error.h"
#include "app/cdie_hash.h"
#include "app/cdie_translation.h"
#include <xxfclib/scan/xx_scan.h>
#include <xxfclib/strings/xx_string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- File type list ---------------------------------------------------- */

typedef struct file_types_collect {
    gui_main_file_types *types;
    int preferred_id; /* the first concrete type the backend emits */
    int overflow;
} file_types_collect;

/* XBinary::FT order in Qt DIE. xxfclib's compact scan IDs put ZIP before
 * Archive, while the Qt combo puts Archive before ZIP. */
static int qt_file_type_order(int type_id)
{
    switch (type_id) {
    case XFT_BINARY: return 4;
    case XFT_COM: return 8;
    case XFT_MSDOS: return 9;
    case XFT_NE: return 12;
    case XFT_LE: return 13;
    case XFT_LX: return 14;
    case XFT_PE32: return 16;
    case XFT_PE64: return 17;
    case XFT_ELF32: return 19;
    case XFT_ELF64: return 20;
    case XFT_MACHO32: return 22;
    case XFT_MACHO64: return 23;
    case GUI_FILE_TYPE_7ZIP: return 27;
    case XFT_APK: return 30;
    case XFT_ARCHIVE: return 42;
    case XFT_DEX: return 44;
    case XFT_IPA: return 51;
    case XFT_JAR: return 52;
    case XFT_MACHOFAT: return 54;
    case XFT_PDF: return 55;
    case XFT_RAR: return 59;
    case GUI_FILE_TYPE_TEXT: return 60;
    case GUI_FILE_TYPE_UNICODE: return 62;
    case XFT_ZIP: return 68;
    default: return 1000 + type_id;
    }
}

static void collect_type(int type_id, const char *name, void *user)
{
    file_types_collect *collect = (file_types_collect *)user;
    gui_main_file_types *types = collect->types;
    size_t i;
    if (type_id <= 0 || !name) return; /* "Automatic" is not an entry any more */
    for (i = 0; i < types->count; ++i)
        if (types->ids[i] == type_id) return;
    if (types->count >= GUI_MAIN_FILE_TYPES_MAX) { collect->overflow = 1; return; }
    if (!collect->preferred_id ||
        ((collect->preferred_id == XFT_BINARY || collect->preferred_id == XFT_ARCHIVE) &&
         type_id != XFT_BINARY && type_id != XFT_ARCHIVE))
        collect->preferred_id = type_id;
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
    /* Sorted by the Qt FT order (Binary, Archive, ZIP, JAR). */
    for (i = 1; i < types->count; ++i)
        for (j = i; j > 0 && qt_file_type_order(types->ids[j - 1]) > qt_file_type_order(types->ids[j]); --j) {
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

/* ---- Advanced file facts ---------------------------------------------- */

static gui_format_kind facts_kind(const gui_backend_facts *facts)
{
    switch (facts->page) {
        case GUI_FACTS_PAGE_PE: return GUI_FORMAT_PE;
        case GUI_FACTS_PAGE_ELF: return GUI_FORMAT_ELF;
        case GUI_FACTS_PAGE_MACH: return !strcmp(facts->type_name, "Mach-O FAT") ? GUI_FORMAT_MACHO_FAT : GUI_FORMAT_MACHO;
        case GUI_FACTS_PAGE_MSDOS: return GUI_FORMAT_MSDOS;
        case GUI_FACTS_PAGE_NE: return GUI_FORMAT_NE;
        case GUI_FACTS_PAGE_LE: return GUI_FORMAT_LE_LX;
        case GUI_FACTS_PAGE_DEX: return GUI_FORMAT_DEX;
        case GUI_FACTS_PAGE_ARCHIVE: return GUI_FORMAT_ARCHIVE;
        case GUI_FACTS_PAGE_COM: return GUI_FORMAT_COM;
        default: return GUI_FORMAT_BINARY;
    }
}

static xxwidgets_format_item format_button_item(gui_format_button_id id, int page)
{
    switch (id) {
        case GUI_FORMAT_BUTTON_MAIN: return XXWIDGETS_FORMAT_ITEM_MAIN;
        case GUI_FORMAT_BUTTON_SYMBOLS: return XXWIDGETS_FORMAT_ITEM_EXTRA_SYMBOLS;
        case GUI_FORMAT_BUTTON_EXPORT: return page == GUI_FACTS_PAGE_PE ? XXWIDGETS_FORMAT_ITEM_PE_EXPORT : XXWIDGETS_FORMAT_ITEM_EXTRA_EXPORT;
        case GUI_FORMAT_BUTTON_IMPORT: return page == GUI_FACTS_PAGE_PE ? XXWIDGETS_FORMAT_ITEM_PE_IMPORT : XXWIDGETS_FORMAT_ITEM_EXTRA_IMPORT;
        case GUI_FORMAT_BUTTON_RESOURCES: return XXWIDGETS_FORMAT_ITEM_PE_RESOURCES;
        case GUI_FORMAT_BUTTON_DOTNET: return XXWIDGETS_FORMAT_ITEM_PE_DOTNET;
        case GUI_FORMAT_BUTTON_TLS: return XXWIDGETS_FORMAT_ITEM_PE_TLS;
        case GUI_FORMAT_BUTTON_OVERLAY: return page == GUI_FACTS_PAGE_MSDOS ? XXWIDGETS_FORMAT_ITEM_MSDOS_OVERLAY : XXWIDGETS_FORMAT_ITEM_PE_OVERLAY;
        case GUI_FORMAT_BUTTON_SECTIONS:
            return page == GUI_FACTS_PAGE_PE ? XXWIDGETS_FORMAT_ITEM_PE_SECTIONS :
                page == GUI_FACTS_PAGE_ELF ? XXWIDGETS_FORMAT_ITEM_ELF_SECTIONS : XXWIDGETS_FORMAT_ITEM_MACH_SECTIONS;
        case GUI_FORMAT_BUTTON_PROGRAMS: return XXWIDGETS_FORMAT_ITEM_ELF_PROGRAMS;
        case GUI_FORMAT_BUTTON_COMMANDS: return XXWIDGETS_FORMAT_ITEM_MACH_COMMANDS;
        case GUI_FORMAT_BUTTON_SEGMENTS: return XXWIDGETS_FORMAT_ITEM_MACH_SEGMENTS;
        case GUI_FORMAT_BUTTON_LIBRARIES: return XXWIDGETS_FORMAT_ITEM_MACH_LIBRARIES;
        case GUI_FORMAT_BUTTON_MANIFEST: return XXWIDGETS_FORMAT_ITEM_PE_MANIFEST;
        case GUI_FORMAT_BUTTON_VERSION: return XXWIDGETS_FORMAT_ITEM_PE_VERSION;
        case GUI_FORMAT_BUTTON_JAR_MANIFEST: return XXWIDGETS_FORMAT_ITEM_ARCHIVE_MANIFEST_MF;
        case GUI_FORMAT_BUTTON_ANDROID_MANIFEST: return XXWIDGETS_FORMAT_ITEM_ARCHIVE_ANDROID_MANIFEST;
        case GUI_FORMAT_BUTTON_ARCHIVE_FILES: return XXWIDGETS_FORMAT_ITEM_ARCHIVE_FILES;
        case GUI_FORMAT_BUTTON_ARCHIVE_UNPACK: return XXWIDGETS_FORMAT_ITEM_ARCHIVE_UNPACK;
        default: return XXWIDGETS_FORMAT_ITEM_COUNT;
    }
}

static int format_enabled(gui_format_enable_rule rule, unsigned int flags)
{
    switch (rule) {
        case GUI_FORMAT_ENABLE_FACT_EXPORTS: return (flags & GUI_FACTS_EXPORTS) != 0;
        case GUI_FORMAT_ENABLE_FACT_IMPORTS: return (flags & GUI_FACTS_IMPORTS) != 0;
        case GUI_FORMAT_ENABLE_FACT_RESOURCES: return (flags & GUI_FACTS_RESOURCES) != 0;
        case GUI_FORMAT_ENABLE_FACT_TLS: return (flags & GUI_FACTS_TLS) != 0;
        case GUI_FORMAT_ENABLE_FACT_OVERLAY: return (flags & GUI_FACTS_OVERLAY) != 0;
        case GUI_FORMAT_ENABLE_FACT_SECTIONS: return (flags & GUI_FACTS_SECTIONS) != 0;
        case GUI_FORMAT_ENABLE_FACT_MANIFEST: return (flags & GUI_FACTS_MANIFEST) != 0;
        case GUI_FORMAT_ENABLE_FACT_VERSION: return (flags & GUI_FACTS_VERSION) != 0;
        case GUI_FORMAT_ENABLE_NEVER: return 0;
        default: return 1;
    }
}

static xxwidgets_format_item format_value_item(gui_format_value_id id, int page)
{
    switch (id) {
        case GUI_FORMAT_VALUE_SECTIONS:
            return page == GUI_FACTS_PAGE_PE ? XXWIDGETS_FORMAT_ITEM_PE_SECTIONS :
                page == GUI_FACTS_PAGE_ELF ? XXWIDGETS_FORMAT_ITEM_ELF_SECTIONS : XXWIDGETS_FORMAT_ITEM_MACH_SECTIONS;
        case GUI_FORMAT_VALUE_TIME_DATE_STAMP: return XXWIDGETS_FORMAT_ITEM_PE_TIME_DATE_STAMP;
        case GUI_FORMAT_VALUE_SIZE_OF_IMAGE: return XXWIDGETS_FORMAT_ITEM_PE_SIZE_OF_IMAGE;
        case GUI_FORMAT_VALUE_PROGRAMS: return XXWIDGETS_FORMAT_ITEM_ELF_PROGRAMS;
        case GUI_FORMAT_VALUE_COMMANDS: return XXWIDGETS_FORMAT_ITEM_MACH_COMMANDS;
        case GUI_FORMAT_VALUE_SEGMENTS: return XXWIDGETS_FORMAT_ITEM_MACH_SEGMENTS;
        case GUI_FORMAT_VALUE_LIBRARIES: return XXWIDGETS_FORMAT_ITEM_MACH_LIBRARIES;
        default: return XXWIDGETS_FORMAT_ITEM_COUNT;
    }
}

static uint64_t format_value(const gui_backend_facts *facts, gui_format_value_id id)
{
    switch (id) {
        case GUI_FORMAT_VALUE_SECTIONS: return facts->sections;
        case GUI_FORMAT_VALUE_SIZE_OF_IMAGE: return facts->size_of_image;
        case GUI_FORMAT_VALUE_PROGRAMS: return facts->programs;
        case GUI_FORMAT_VALUE_COMMANDS: return facts->commands;
        case GUI_FORMAT_VALUE_SEGMENTS: return facts->segments;
        case GUI_FORMAT_VALUE_LIBRARIES: return facts->libraries;
        default: return 0;
    }
}

xxwidgets_status gui_main_apply_file_facts(xxwidgets_die_main_panel *panel, const char *path, int type_id)
{
    gui_backend_facts facts;
    xxwidgets_file_header *header;
    xxwidgets_scan_panel *scan;
    xxwidgets_format_panel *format;
    const gui_format_row *row;
    const char *main_label = NULL;
    xxwidgets_status status = XXWIDGETS_OK;
    size_t i;
    if (!panel || !path || !path[0]) return XXWIDGETS_INVALID_ARGUMENT;
    if (!gui_backend_file_facts(path, type_id, &facts)) return XXWIDGETS_UNAVAILABLE;
    header = xxwidgets_die_main_panel_file_header(panel);
    scan = xxwidgets_die_main_panel_scan_panel(panel);
    format = xxwidgets_die_main_panel_format_panel(panel);
    if (!header || !scan || !format) { gui_backend_facts_free(&facts); return XXWIDGETS_INVALID_ARGUMENT; }
    status = xxwidgets_file_header_set_file_size(header, facts.file_size);
    if (status == XXWIDGETS_OK && (facts.flags & GUI_FACTS_SEG_OFF))
        status = xxwidgets_file_header_set_address_seg_off(header, XXWIDGETS_FILE_HEADER_BASE_ADDRESS,
            (uint16_t)(facts.base_address >> 16), (uint16_t)facts.base_address);
    else if (status == XXWIDGETS_OK)
        status = xxwidgets_file_header_set_address(header, XXWIDGETS_FILE_HEADER_BASE_ADDRESS, facts.base_address,
            facts.base_bits ? facts.base_bits : 32);
    if (status == XXWIDGETS_OK && (facts.flags & GUI_FACTS_SEG_OFF))
        status = xxwidgets_file_header_set_address_seg_off(header, XXWIDGETS_FILE_HEADER_ENTRY_POINT,
            (uint16_t)(facts.entry_point >> 16), (uint16_t)facts.entry_point);
    else if (status == XXWIDGETS_OK)
        status = xxwidgets_file_header_set_address(header, XXWIDGETS_FILE_HEADER_ENTRY_POINT, facts.entry_point,
            facts.entry_bits ? facts.entry_bits : 32);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_set_file_info(scan, facts.endianness, facts.mode, facts.architecture, facts.type);
    for (i = 1; status == XXWIDGETS_OK && i < XXWIDGETS_FORMAT_ITEM_COUNT; ++i)
        status = xxwidgets_format_panel_set_item_shown(format, (xxwidgets_format_item)i, 0);
    if (facts.page == GUI_FACTS_PAGE_ARCHIVE ||
        (facts.page == GUI_FACTS_PAGE_LE && !strcmp(facts.main_label, "LX"))) main_label = facts.main_label;
    if (status == XXWIDGETS_OK)
        status = xxwidgets_format_panel_set_page(format, (xxwidgets_format_page)facts.page, main_label);
    row = gui_format_rows_find(facts_kind(&facts));
    for (i = 0; status == XXWIDGETS_OK && row && i < row->button_count; ++i) {
        xxwidgets_format_item item = format_button_item(row->buttons[i].id, facts.page);
        if (item == XXWIDGETS_FORMAT_ITEM_MAIN || item == XXWIDGETS_FORMAT_ITEM_COUNT ||
            row->buttons[i].availability == GUI_FORMAT_HIDDEN) continue;
        status = xxwidgets_format_panel_set_item_shown(format, item, 1);
        if (status == XXWIDGETS_OK) status = xxwidgets_format_panel_set_item_enabled(format, item,
            format_enabled(row->buttons[i].enabled, facts.flags));
    }
    for (i = 0; status == XXWIDGETS_OK && row && i < row->value_count; ++i) {
        xxwidgets_format_item item = format_value_item(row->values[i].id, facts.page);
        if (item == XXWIDGETS_FORMAT_ITEM_COUNT) continue;
        status = xxwidgets_format_panel_set_item_shown(format, item, 1);
        if (status == XXWIDGETS_OK) status = xxwidgets_format_panel_set_item_enabled(format, item, 1);
        if (status != XXWIDGETS_OK) break;
        if (row->values[i].id == GUI_FORMAT_VALUE_TIME_DATE_STAMP) {
            status = xxwidgets_format_panel_set_item_caption(format, item,
                (facts.flags & GUI_FACTS_NET_ID) ? CDIE_TR(".NET ID") : CDIE_TR("Time date stamp"));
            if (status == XXWIDGETS_OK && (facts.flags & GUI_FACTS_NET_ID))
                status = xxwidgets_format_panel_set_item_hex(format, item, facts.net_id, 8);
            else if (status == XXWIDGETS_OK)
                status = xxwidgets_format_panel_set_item_text(format, item, facts.time_date_stamp_text);
        } else {
            unsigned int digits = row->values[i].id == GUI_FORMAT_VALUE_SIZE_OF_IMAGE ? 8 : 4;
            status = xxwidgets_format_panel_set_item_hex(format, item, format_value(&facts, row->values[i].id), digits);
        }
    }
    if (status == XXWIDGETS_OK && facts.page == GUI_FACTS_PAGE_PE) {
        status = xxwidgets_format_panel_set_item_shown(format, XXWIDGETS_FORMAT_ITEM_PE_RESOURCES_GROUP, 1);
        if (status == XXWIDGETS_OK) status = xxwidgets_format_panel_set_item_enabled(format,
            XXWIDGETS_FORMAT_ITEM_PE_RESOURCES_GROUP, (facts.flags & GUI_FACTS_RESOURCES) != 0);
    }
    gui_backend_facts_free(&facts);
    return status;
}

xxwidgets_status gui_main_restore_tag_filters(xxwidgets_scan_panel *panel)
{
    gui_options_values values;
    xxwidgets_status status;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    gui_preferences_get(&values);
    status = xxwidgets_scan_panel_set_tag_filters(panel, values.scan.include_tags, values.scan.exclude_tags);
    if (status == XXWIDGETS_OK && !gui_backend_set_tag_filters(values.scan.include_tags, values.scan.exclude_tags))
        status = XXWIDGETS_OUT_OF_MEMORY;
    return status;
}

xxwidgets_status gui_main_store_tag_filters(xxwidgets_scan_panel *panel)
{
    gui_options_values values;
    size_t include_size = 0, exclude_size = 0;
    xxwidgets_status status;
    if (!panel) return XXWIDGETS_INVALID_ARGUMENT;
    gui_preferences_get(&values);
    status = xxwidgets_scan_panel_get_include_tags(panel, NULL, 0, &include_size);
    if (status == XXWIDGETS_OK) status = xxwidgets_scan_panel_get_exclude_tags(panel, NULL, 0, &exclude_size);
    if (status != XXWIDGETS_OK) return status;
    if (include_size > sizeof(values.scan.include_tags) || exclude_size > sizeof(values.scan.exclude_tags))
        return XXWIDGETS_BUFFER_TOO_SMALL;
    status = xxwidgets_scan_panel_get_include_tags(panel, values.scan.include_tags, sizeof(values.scan.include_tags), NULL);
    if (status == XXWIDGETS_OK)
        status = xxwidgets_scan_panel_get_exclude_tags(panel, values.scan.exclude_tags, sizeof(values.scan.exclude_tags), NULL);
    if (status == XXWIDGETS_OK && !gui_backend_set_tag_filters(values.scan.include_tags, values.scan.exclude_tags))
        status = XXWIDGETS_OUT_OF_MEMORY;
    if (status == XXWIDGETS_OK) gui_preferences_set(&values);
    return status;
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

static void report_status(xxwidgets_widget *owner, const gui_main_hooks *hooks, const char *title, xxwidgets_status status)
{
    char message[512];
    if (status == XXWIDGETS_OK || status == XXWIDGETS_BUSY) return;
    cdie_translation_snprintf(message, sizeof(message), CDIE_N_("%s: %s"), CDIE_TR(title), gui_error_status_text(status));
    report_error(owner, hooks, message);
}

static void run_virustotal(xxwidgets_widget *owner, const char *path, const gui_main_hooks *hooks)
{
    cdie_hash_data *data = NULL;
    char *error = NULL;
    char url[160];
    int i, md5 = -1;
    if (!path || !path[0]) { report_error(owner, hooks, CDIE_TR("Choose a file to calculate hashes.")); return; }
    if (!cdie_hash_read(path, NULL, &data, &error)) {
        report_error(owner, hooks, error ? error : CDIE_TR("Cannot read the file."));
        gui_backend_free(error);
        return;
    }
    for (i = 0; i < CDIE_HASH_COUNT; ++i) if (!strcmp(data->entries[i].algorithm, "MD5")) md5 = i;
    if (md5 >= 0) {
        (void)snprintf(url, sizeof(url), "https://www.virustotal.com/gui/file/%s", data->entries[md5].value);
        report_status(owner, hooks, "VirusTotal", xxwidgets_open_url(owner, url));
    }
    gui_backend_free(data);
}

static const char *format_selector(const gui_backend_facts *facts, int item)
{
    switch (item) {
        case XXWIDGETS_FORMAT_ITEM_MAIN: return facts->header_selector;
        case XXWIDGETS_FORMAT_ITEM_PE_TLS: return facts->tls_selector;
        case XXWIDGETS_FORMAT_ITEM_PE_SECTIONS:
        case XXWIDGETS_FORMAT_ITEM_ELF_SECTIONS:
        case XXWIDGETS_FORMAT_ITEM_MACH_SECTIONS: return facts->sections_selector;
        case XXWIDGETS_FORMAT_ITEM_ELF_PROGRAMS: return facts->programs_selector;
        case XXWIDGETS_FORMAT_ITEM_MACH_COMMANDS: return facts->commands_selector;
        case XXWIDGETS_FORMAT_ITEM_MACH_SEGMENTS: return facts->segments_selector;
        case XXWIDGETS_FORMAT_ITEM_MACH_LIBRARIES: return facts->libraries_selector;
        default: return NULL;
    }
}

static void run_entry_point(xxwidgets_widget *owner, const char *path, int type_id, const gui_main_hooks *hooks)
{
    gui_backend_facts facts;
    xxwidgets_status status;
    if (!path || !path[0]) { report_error(owner, hooks, CDIE_TR("Choose a file to disassemble.")); return; }
    if (!gui_backend_file_facts(path, type_id, &facts)) { report_error(owner, hooks, CDIE_TR("Cannot read the file.")); return; }
    if (facts.page == GUI_FACTS_PAGE_DEX && (facts.flags & GUI_FACTS_ENTRY_OFFSET))
        status = gui_hex_view_show_at(owner, path, (uint64_t)facts.entry_point_offset, 0);
    else
        status = gui_disasm_show(owner, path);
    report_status(owner, hooks, facts.page == GUI_FACTS_PAGE_DEX ? "Hex View" : "Disasm", status);
    gui_backend_facts_free(&facts);
}

static void run_format_action(xxwidgets_widget *owner, const char *path, int type_id, int item, const gui_main_hooks *hooks)
{
    gui_backend_facts facts;
    xxwidgets_status status = XXWIDGETS_OK;
    const char *selector;
    if (!path || !path[0] || !gui_backend_file_facts(path, type_id, &facts)) {
        report_error(owner, hooks, CDIE_TR("Cannot read the file."));
        return;
    }
    switch (item) {
        case XXWIDGETS_FORMAT_ITEM_PE_EXPORT: run_tool(owner, GUI_TOOL_EXPORTS, path, hooks); break;
        case XXWIDGETS_FORMAT_ITEM_PE_IMPORT: run_tool(owner, GUI_TOOL_IMPORTS, path, hooks); break;
        case XXWIDGETS_FORMAT_ITEM_PE_RESOURCES: run_tool(owner, GUI_TOOL_RESOURCES, path, hooks); break;
        case XXWIDGETS_FORMAT_ITEM_EXTRA_IMPORT: run_tool(owner, GUI_TOOL_IMPORTS, path, hooks); break;
        case XXWIDGETS_FORMAT_ITEM_EXTRA_EXPORT: run_tool(owner, GUI_TOOL_EXPORTS, path, hooks); break;
        case XXWIDGETS_FORMAT_ITEM_EXTRA_SYMBOLS: run_tool(owner, GUI_TOOL_SYMBOLS, path, hooks); break;
        case XXWIDGETS_FORMAT_ITEM_PE_OVERLAY:
        case XXWIDGETS_FORMAT_ITEM_MSDOS_OVERLAY:
            if (facts.overlay_offset >= 0 && facts.overlay_size > 0)
                status = gui_hex_view_show_at(owner, path, (uint64_t)facts.overlay_offset, (uint64_t)facts.overlay_size);
            break;
        case XXWIDGETS_FORMAT_ITEM_PE_MANIFEST:
            status = xxwidgets_text_dialog(owner, CDIE_TR("Manifest"), facts.manifest ? facts.manifest : "");
            break;
        case XXWIDGETS_FORMAT_ITEM_PE_VERSION:
            status = xxwidgets_text_dialog(owner, CDIE_TR("Version"), facts.version ? facts.version : "");
            break;
        case XXWIDGETS_FORMAT_ITEM_MAIN:
            selector = format_selector(&facts, item);
            status = selector ? gui_file_information_show_at(owner, path, selector) : gui_hex_view_show(owner, path);
            break;
        default:
            selector = format_selector(&facts, item);
            if (selector) status = gui_file_information_show_at(owner, path, selector);
            break;
    }
    report_status(owner, hooks, "File Information", status);
    gui_backend_facts_free(&facts);
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
    if (!action) return 0;
    switch (action->id) {
        case XXWIDGETS_DIE_ACTION_NONE: return 1;
        case XXWIDGETS_DIE_ACTION_RECENT: CALL(recent); return 1;
        case XXWIDGETS_DIE_ACTION_BROWSE: CALL(browse); return 1;
        case XXWIDGETS_DIE_ACTION_OPEN_PATH: CALL(open_path); return 1;
        case XXWIDGETS_DIE_ACTION_FILE_TYPE:
            /* Choosing another type rescans with it, as Qt does. */
            if (hooks && action->value >= 0 && gui_main_file_types_choose(hooks->file_types, (size_t)action->value)) {
                xxwidgets_status status = gui_main_apply_file_facts(panel, path, scan_type_id(hooks));
                report_status(owner, hooks, "File Information", status);
                rescan(hooks, path);
            }
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
            else if (action->value == XXWIDGETS_DIE_TOOL_VIRUSTOTAL) run_virustotal(owner, path, hooks);
            return 1;
        case XXWIDGETS_DIE_ACTION_ENTRY_POINT:
            run_entry_point(owner, path, scan_type_id(hooks), hooks);
            return 1;
        case XXWIDGETS_DIE_ACTION_FORMAT:
        case XXWIDGETS_DIE_ACTION_FORMAT_ARROW:
            run_format_action(owner, path, scan_type_id(hooks), action->value, hooks);
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
