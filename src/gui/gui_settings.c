/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Options persistence in Detect It Easy's own store, shared with the Qt
 * program. Values are written in QSettings' plain forms (true/false, decimal,
 * QFont::toString, "#rrggbb|#rrggbb"), only what the user changed is written,
 * and the keys whose meaning differs in Detect It Easy are translated or left
 * alone. Reading accepts the typed values older cdie versions wrote. */
#include "gui_settings.h"
#include "gui_backend.h"
#include "../global.h"
#include "app/cdie_app.h"
#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/global/xx_global.h>
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/strings/xx_string.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* cdie-only keys. Detect It Easy keeps RecentFiles as a Qt list, so cdie's
 * history has its own key; its View/Lang names a .qm file, so cdie's .po
 * path is cdie/Lang; Migrated records the one-time copy of the settings
 * older versions kept in cdie.ini / NTInfo/cdie (in both stores). */
#define KEY_RECENT "cdie/RecentFiles"
#define KEY_LANGUAGE "cdie/Lang"
#define KEY_MIGRATED "cdie/Migrated"
#define KEY_QT_RECENT "RecentFiles"
#define KEY_QT_LANGUAGE "View/Lang"
#define RECENT_CAPACITY sizeof(((gui_file_preferences *)0)->recent_files)
/* Feature/ReadBufferSize and Feature/FileBufferSize take effect within the
 * range of the Optimization page's choices (xxwidgets_optimization_options.c),
 * which diec's --buffersize and --filebuffersize accept too: 1 KiB to 1 GiB,
 * and to 64 MiB where a size_t has 32 bits (the library's 32-bit memory
 * budget, XX_DEFAULT_FILE_MEMORY_LIMIT). 0 is the default size, as in Detect
 * It Easy, and so is a number past BUFFER_SIZE_QT_MAX: Detect It Easy reads
 * these keys with QVariant::toInt(), so its own "2 GiB" and "4 GiB" choices
 * (and a REG_DWORD of 0x80000000 or more) mean its default size too. */
#define BUFFER_SIZE_MIN ((uint64_t)1024)
#if !defined(SIZE_MAX)
#error SIZE_MAX (stdint.h) is needed to choose the buffer size range
#elif SIZE_MAX > 0xFFFFFFFFu
#define BUFFER_SIZE_MAX ((uint64_t)1024 * 1024 * 1024)
#else
#define BUFFER_SIZE_MAX ((uint64_t)64 * 1024 * 1024)
#endif
#define BUFFER_SIZE_QT_MAX ((uint64_t)INT32_MAX)

enum setting_kind {
    SK_BOOL,           /* int member: "true"/"false" */
    SK_BIT,            /* group 0 scan flags, 1 the custom database, 2 scan modes */
    SK_TEXT,           /* char array member */
    SK_LANGUAGE,       /* cdie/Lang; View/Lang only where both programs agree */
    SK_LAST_DIRECTORY, /* empty while saving the last directory is off */
    SK_RECENT,         /* cdie's history; imports Detect It Easy's list while it has none (a
                        * one-entry INI list is Qt's @Variant(...), which is not read) */
    SK_ENGINE,         /* unknown engines mean auto here, the stored one is kept */
    SK_FONT,           /* QFont::toString; index 0-3 font roles, 4 Hex, 5 Disasm */
    SK_COLOR,          /* "#rrggbb|#rrggbb"; group 0 scan, 2 disassembly */
    SK_PLAIN_COLOR,    /* cdie-only decimal half; group 0 scan, 1 dark scan, 2 disassembly */
    SK_SYNTAX,         /* Detect It Easy's ATT/INTEL/...; 0 Intel, 1 AT&T */
    SK_DATABASE_PATH,  /* index = GUI_DB_* slot; "$data/db..." = default folder */
    SK_BUFFER,         /* index 0 read, 1 file buffer; 0 or past INT32_MAX = default size, else clamped */
    SK_FEATURE,        /* index = CPU feature */
    SK_BYTES_PER_ROW,
    SK_PAGE
};

typedef struct setting_spec {
    char key[64];
    char legacy[48]; /* fonts and colors: cdie's former sub-key prefix, read as a fallback */
    unsigned char kind, group, part;
    unsigned int bit;
    size_t offset, capacity, index;
} setting_spec;

/* The current state described in setting form. */
typedef struct settings_now {
    gui_options_page_id page;
    unsigned int modes;
    int have_optimization;
    gui_backend_optimization_options optimization;
    const char *paths[GUI_DB_COUNT]; /* "" for a default folder */
} settings_now;

/* What a load collects for the backend. */
typedef struct settings_load {
    gui_options_page_id *page;
    unsigned int modes;
    int have_optimization;
    gui_backend_optimization_options optimization;
    char *paths[GUI_DB_COUNT];
    size_t explicit_paths;
    int stored_paths;
} settings_load;

/* The settings as they were after gui_settings_load, per store: a save writes
 * a key only when the user changed it, so values Detect It Easy wrote, in its
 * own types, stay as they are. */
typedef struct settings_snapshot {
    const xx_settings *store;
    char **texts;
    struct settings_snapshot *next;
} settings_snapshot;

#define SPEC_CAPACITY (98 + 3 * XX_SCAN_COLOR_COUNT + 3 * XXWIDGETS_DISASM_COLOR_COUNT)
static setting_spec specs[SPEC_CAPACITY];
static size_t spec_count;
static gui_options_values defaults;
static settings_snapshot *snapshots;

static const char *const scan_color_names[XX_SCAN_COLOR_COUNT] = {
    "Unknown","Installer","Sfx","Archive","Protection","Petool","Apktool","Os","Vm","Platform","DosExtender","Format","Signtool",
    "Certificate","Licensing","Language","CorruptedData","PersonalData","Author","Virus","Trojan","Malware","Debug","DebugData","GameEngine",
    "Compiler","Compressor","Converter","Creator","Data","Database","Document","Generic","Image","InstallerData","Library","Linker","Loader",
    "Obfuscator","Overlay","Package","Player","Producer","ProtectorData","Rom","SfxData","SourceCode","Stub","Tool"
};
/* Detect It Easy's spelling; NULL keeps the category cdie-only. */
static const char *const qt_scan_color_names[XX_SCAN_COLOR_COUNT] = {
    NULL,"Installer","Sfx","Archive","Protection","PeTool","ApkTool","OS","VM","Platform","DosExtender","Format","SignTool",
    "Certificate","Licensing","Language","CorruptedData","PersonalData","Author","Virus","Trojan","Malware","Debug","DebugData","GameEngine",
    "Compiler","Compressor","Converter","Creator","Data","Database","Document","Generic","Image","InstallerData","Library","Linker","Loader",
    "Obfuscator","Overlay","Package","Player","Producer","ProtectorData","ROM","SfxData","SourceCode","Stub","Tool"
};
static const struct { const char *cdie, *qt; } qt_disasm_colors[] = {
    {"arrows", "Arrows"}, {"arrows_selected", "Arrows/Selected"}, {"regs", "Regs"}, {"numbers", "Numbers"}, {"opcode", "Opcode"},
    {"refs", "Refs"}, {"currentip", "CurrentIP"},
    {"x86_regs_general", "x86/Regs/General"}, {"x86_regs_stack", "x86/Regs/Stack"}, {"x86_regs_segment", "x86/Regs/Segment"},
    {"x86_regs_debug", "x86/Regs/Debug"}, {"x86_regs_ip", "x86/Regs/IP"}, {"x86_regs_flags", "x86/Regs/Flags"},
    {"x86_regs_fpu", "x86/Regs/Float"}, {"x86_regs_xmm", "x86/Regs/XMM"}, {"x86_regs_ymm", "x86/Regs/YMM"}, {"x86_regs_zmm", "x86/Regs/ZMM"},
    {"x86_opcode_call", "x86/Opcode/call"}, {"x86_opcode_ret", "x86/Opcode/ret"}, {"x86_opcode_nop", "x86/Opcode/nop"},
    {"x86_opcode_push", "x86/Opcode/push"}, {"x86_opcode_pop", "x86/Opcode/pop"}, {"x86_opcode_cond_jmp", "x86/Opcode/cond_jmp"},
    {"x86_opcode_jmp", "x86/Opcode/jmp"}, {"x86_opcode_int3", "x86/Opcode/int3"}, {"x86_opcode_syscall", "x86/Opcode/syscall"},
    {"arm_regs_general", "arm/Regs/General"}, {"arm_regs_stack", "arm/Regs/Stack"}, {"arm_opcode_branch", "arm/Opcode/b"},
    {"arm_opcode_branchlink", "arm/Opcode/bl"}, {"arm_opcode_ret", "arm/Opcode/ret"}, {"arm_opcode_push", "arm/Opcode/push"},
    {"arm_opcode_pop", "arm/Opcode/pop"}, {"arm_opcode_nop", "arm/Opcode/nop"}
};
static const char *const font_roles[4] = {"Controls", "TableViews", "TreeViews", "TextEdits"};
static const char *const database_names[GUI_DB_COUNT] = {"db", "db_custom"}; /* GUI_DB_MAIN, GUI_DB_CUSTOM */

static int *feature_field(gui_backend_optimization_options *options, size_t index)
{
    int *const fields[11] = {&options->use_sse2, &options->use_avx2, &options->use_sse42, &options->use_aes, &options->use_sha, &options->use_vpclmulqdq,
                             &options->use_neon, &options->use_arm_crc32, &options->use_arm_crypto, &options->use_arm_dotprod, &options->use_arm_pmull};
    return fields[index];
}

/* ------------------------------------------------------------------ text */

static char *text_copy(const char *text, size_t size)
{
    char *copy = (char *)xx_rt_malloc(size + 1);
    if (copy) {
        if (size) memcpy(copy, text, size);
        copy[size] = 0;
    }
    return copy;
}

/* strlen within a fixed buffer that may lack its NUL. */
static size_t bounded_length(const char *text, size_t capacity)
{
    const char *end = (const char *)memchr(text, 0, capacity);
    return end ? (size_t)(end - text) : capacity - 1;
}

static char *text_dup(const char *text)
{
    return text_copy(text, strlen(text));
}

static char *number_text(uint64_t number)
{
    char digits[24];
    size_t at = sizeof(digits);
    digits[--at] = 0;
    do digits[--at] = (char)('0' + number % 10); while ((number /= 10) != 0);
    return text_dup(digits + at);
}

/* The text of a STRING value without embedded NULs, else NULL. */
static const char *value_text(const xx_settings_value *value, size_t *size)
{
    if (!value || value->type != XX_SETTINGS_VALUE_STRING) return NULL;
    if (value->data.buffer.size && (!value->data.buffer.data || memchr(value->data.buffer.data, 0, value->data.buffer.size))) return NULL;
    *size = value->data.buffer.size;
    return value->data.buffer.size ? value->data.buffer.data : "";
}

static int same_text(const char *text, size_t size, const char *literal, int fold)
{
    size_t i;
    if (strlen(literal) != size) return 0;
    for (i = 0; i < size; ++i) {
        char a = text[i], b = literal[i];
        if (fold && a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (fold && b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

/* Digits only, overflow-checked. */
static int parse_decimal(const char *text, size_t size, uint64_t *number)
{
    uint64_t result = 0;
    size_t i;
    if (!size || size > 20) return 0;
    for (i = 0; i < size; ++i) {
        unsigned int digit = (unsigned int)((unsigned char)text[i] - '0');
        if (digit > 9 || result > (UINT64_MAX - digit) / 10) return 0;
        result = result * 10 + digit;
    }
    *number = result;
    return 1;
}

static int parse_hex(const char *text, size_t size, uint64_t *number)
{
    uint64_t result = 0;
    size_t i;
    if (!size || size > 16) return 0;
    for (i = 0; i < size; ++i) {
        char c = text[i];
        unsigned int digit = c >= '0' && c <= '9' ? (unsigned int)(c - '0') : c >= 'a' && c <= 'f' ? (unsigned int)(c - 'a' + 10) :
                             c >= 'A' && c <= 'F' ? (unsigned int)(c - 'A' + 10) : 16u;
        if (digit > 15) return 0;
        result = result * 16 + digit;
    }
    *number = result;
    return 1;
}

/* QSettings writes "true"/"false" and the registry or macOS may hold numbers;
 * older cdie versions wrote typed booleans. */
static int value_bool(const xx_settings_value *value, int *flag)
{
    const char *text;
    size_t size;
    uint64_t number;
    if (!value) return 0;
    if (value->type == XX_SETTINGS_VALUE_BOOL) *flag = value->data.boolean ? 1 : 0;
    else if (value->type == XX_SETTINGS_VALUE_UINT64) *flag = value->data.unsigned_integer != 0;
    else if (value->type == XX_SETTINGS_VALUE_INT64) *flag = value->data.integer != 0;
    else if ((text = value_text(value, &size)) == NULL) return 0;
    else if (same_text(text, size, "true", 1)) *flag = 1;
    else if (same_text(text, size, "false", 1)) *flag = 0;
    else if (parse_decimal(text, size, &number)) *flag = number != 0;
    else return 0;
    return 1;
}

static int value_uint(const xx_settings_value *value, uint64_t *number)
{
    const char *text;
    size_t size;
    if (!value) return 0;
    if (value->type == XX_SETTINGS_VALUE_UINT64) *number = value->data.unsigned_integer;
    else if (value->type == XX_SETTINGS_VALUE_INT64 && value->data.integer >= 0) *number = (uint64_t)value->data.integer;
    else if ((text = value_text(value, &size)) == NULL || !parse_decimal(text, size, number)) return 0;
    return 1;
}

static const xx_settings_value *stored_at(const xx_settings *store, const char *prefix, const char *suffix)
{
    char key[128];
    xx_rt_snprintf(key, sizeof(key), "%s/%s", prefix, suffix);
    return xx_settings_get(store, key);
}

static int write_text(xx_settings *store, const char *key, const char *text)
{
    xx_settings_value stored;
    memset(&stored, 0, sizeof(stored));
    stored.type = XX_SETTINGS_VALUE_STRING;
    stored.data.buffer.data = text;
    stored.data.buffer.size = strlen(text);
    return xx_settings_set(store, key, &stored) == XXFC_OK;
}

/* ----------------------------------------------------------------- fonts */

typedef struct text_fields {
    const char *start[24];
    size_t size[24], count;
} text_fields;

static int split_fields(const char *text, size_t size, text_fields *fields)
{
    size_t begin = 0, i;
    fields->count = 0;
    for (i = 0; i <= size; ++i) {
        if (i < size && text[i] != ',') continue;
        if (fields->count == sizeof(fields->start) / sizeof(fields->start[0])) return 0;
        fields->start[fields->count] = text + begin;
        fields->size[fields->count++] = i - begin;
        begin = i + 1;
    }
    return 1;
}

/* QFont stores a fractional point size, or -1 when the size is in pixels. */
static int parse_point_size(const char *text, size_t size, unsigned int *points)
{
    uint64_t whole = 0;
    size_t i = 0;
    int up = 0;
    if (same_text(text, size, "-1", 0)) { *points = 0; return 1; }
    while (i < size && text[i] >= '0' && text[i] <= '9' && whole <= 1000) whole = whole * 10 + (uint64_t)(text[i++] - '0');
    if (!i || whole > 1000) return 0;
    if (i < size && text[i] == '.') {
        up = ++i < size && text[i] >= '5' && text[i] <= '9';
        while (i < size && text[i] >= '0' && text[i] <= '9') ++i;
    }
    if (i != size) return 0;
    whole += (uint64_t)up;
    if (whole && (whole < 4 || whole > 96)) return 0;
    *points = (unsigned int)whole;
    return 1;
}

/* Qt 5: family,pointSize,pixelSize,styleHint,weight(50/75),style,... (10 or
 * 11 fields); Qt 6 has 16 or more, with weights 400/700; Qt 4 had 9. An empty
 * family is cdie's backend font. */
static int parse_qt_font(const char *text, size_t size, xxwidgets_font *font, text_fields *fields)
{
    uint64_t weight, style;
    memset(font, 0, sizeof(*font));
    if (!size || !split_fields(text, size, fields) || (fields->count > 2 && fields->count < 9) || fields->size[0] >= sizeof(font->family)) return 0;
    memcpy(font->family, fields->start[0], fields->size[0]);
    if (fields->count >= 2 && !parse_point_size(fields->start[1], fields->size[1], &font->point_size)) return 0;
    if (fields->count >= 9) {
        size_t at = fields->count == 9 ? 3 : 4;
        if (!parse_decimal(fields->start[at], fields->size[at], &weight) || !parse_decimal(fields->start[at + 1], fields->size[at + 1], &style)) return 0;
        font->bold = fields->count >= 12 ? weight >= 600 : weight >= 63;
        font->italic = style != 0;
    }
    return 1;
}

static int font_equal(const xxwidgets_font *a, const xxwidgets_font *b)
{
    return !strcmp(a->family, b->family) && a->point_size == b->point_size && !a->bold == !b->bold && !a->italic == !b->italic;
}

/* Canonical form: size,bold,italic,family. */
static char *font_text(const xxwidgets_font *font)
{
    char head[48];
    size_t head_size, family_size = bounded_length(font->family, sizeof(font->family));
    char *text;
    xx_rt_snprintf(head, sizeof(head), "%u,%d,%d,", font->point_size, font->bold ? 1 : 0, font->italic ? 1 : 0);
    head_size = strlen(head);
    text = (char *)xx_rt_malloc(head_size + family_size + 1);
    if (!text) return NULL;
    memcpy(text, head, head_size);
    memcpy(text + head_size, font->family, family_size);
    text[head_size + family_size] = 0;
    return text;
}

static int font_from_text(const char *text, xxwidgets_font *font)
{
    unsigned int size = 0;
    int bold = 0, italic = 0, fields = 0;
    const char *family = text;
    memset(font, 0, sizeof(*font));
    while (fields < 3) {
        const char *comma = strchr(family, ',');
        uint64_t number;
        if (!comma || !parse_decimal(family, (size_t)(comma - family), &number) || number > 1000) return 0;
        if (fields == 0) size = (unsigned int)number;
        else if (fields == 1) bold = number != 0;
        else italic = number != 0;
        family = comma + 1;
        ++fields;
    }
    if (strlen(family) >= sizeof(font->family)) return 0;
    strcpy(font->family, family);
    font->point_size = size;
    font->bold = bold;
    font->italic = italic;
    return 1;
}

static const xxwidgets_font *default_font(size_t index)
{
    return index < 4 ? &defaults.fonts.fonts[index] : index == 4 ? &defaults.hex.font : &defaults.disasm.font;
}

static xxwidgets_font *values_font(gui_options_values *values, size_t index)
{
    return index < 4 ? &values->fonts.fonts[index] : index == 4 ? &values->hex.font : &values->disasm.font;
}

/* QFont needs a family; cdie's "" is the backend's font for the role. */
#if defined(_WIN32)
#define QT_FONT_INTERFACE "Segoe UI"
#elif defined(__APPLE__)
#define QT_FONT_INTERFACE ".AppleSystemUIFont"
#else
#define QT_FONT_INTERFACE "Sans Serif"
#endif
/* The default font of the table and text roles, whose Detect It Easy
 * defaults are monospace: QFont reads it as a fixed-pitch font at the
 * application's size, cdie as its own default. */
#define QT_FONT_MONOSPACE "monospace,-1,-1,7,50,0,0,0,0,0"

static int font_is_alias(const char *family)
{
    return same_text(family, strlen(family), "monospace", 1);
}

/* Roles cdie's backends draw in a fixed-pitch font by default. */
static int font_role_fixed(size_t index)
{
    return index == XXWIDGETS_FONT_TABLE_VIEWS || index == XXWIDGETS_FONT_HEX_VIEWS || index == XXWIDGETS_FONT_DISASM_VIEWS;
}

static int font_role_monospace_default(size_t index)
{
    return index == XXWIDGETS_FONT_TABLE_VIEWS || index == XXWIDGETS_FONT_TEXT_EDITS;
}

/* The Qt font string for font. An existing string keeps every field cdie does
 * not edit. A default font is "" (Detect It Easy keeps its own), or
 * QT_FONT_MONOSPACE for the table and text roles. cdie's backend family ("")
 * becomes the stored family, else monospace for the fixed-pitch roles and
 * the platform's interface family, and the "monospace" alias gets QFont's
 * monospace style hint (7) so that Qt picks a fixed-pitch font. */
static char *qt_font_string(const char *existing, size_t existing_size, const xxwidgets_font *font, size_t index)
{
    xxwidgets_font parsed;
    text_fields fields;
    char size_text[16], *result, *at;
    const char *weight, *family = font->family;
    size_t i;
    int qt6, alias, have = existing && parse_qt_font(existing, existing_size, &parsed, &fields) && fields.count >= 10;
    if (font_equal(font, default_font(index))) return text_dup(font_role_monospace_default(index) ? QT_FONT_MONOSPACE : "");
    if (!family[0]) family = have && parsed.family[0] ? parsed.family : font_role_fixed(index) ? "monospace" : QT_FONT_INTERFACE;
    alias = font_is_alias(family);
    if (font->point_size) xx_rt_snprintf(size_text, sizeof(size_text), "%u", font->point_size);
    else strcpy(size_text, "-1");
    if (!have) {
        size_t needed = strlen(family) + 64;
        result = (char *)xx_rt_malloc(needed);
        if (result) xx_rt_snprintf(result, needed, "%s,%s,-1,%s,%s,%s,0,0,0,0", family, size_text, alias ? "7" : "5", font->bold ? "75" : "50", font->italic ? "1" : "0");
        return result;
    }
    qt6 = fields.count >= 12;
    weight = font->bold ? (qt6 ? "700" : "75") : (qt6 ? "400" : "50");
    result = (char *)xx_rt_malloc(existing_size + sizeof(font->family) + 64);
    if (!result) return NULL;
    at = result;
    for (i = 0; i < fields.count; ++i) {
        const char *text = fields.start[i];
        size_t size = fields.size[i];
        if (i == 0 && strcmp(parsed.family, family)) text = family, size = strlen(family);
        else if (i == 1 && parsed.point_size != font->point_size) text = size_text, size = strlen(size_text);
        else if (i == 2 && parsed.point_size != font->point_size) text = "-1", size = 2;
        else if (i == 3 && alias) text = "7", size = 1;
        else if (i == 4 && !parsed.bold != !font->bold) text = weight, size = strlen(weight);
        else if (i == 5 && !parsed.italic != !font->italic) text = font->italic ? "1" : "0", size = 1;
        if (i) *at++ = ',';
        memcpy(at, text, size);
        at += size;
    }
    *at = 0;
    return result;
}

/* ---------------------------------------------------------------- colors */

/* "" is the default color (UINT32_MAX). QColor also writes #aarrggbb: cdie's
 * colors are opaque, so a translucent one is blended over white, as Detect It
 * Easy draws it on a light row (its CurrentIP #5a00a000 is cdie's A5DDA5). */
static int parse_color_half(const char *text, size_t size, uint32_t *color)
{
    uint64_t value;
    if (!size) { *color = UINT32_MAX; return 1; }
    if (text[0] != '#' || !parse_hex(text + 1, size - 1, &value)) return 0;
    switch (size - 1) {
    case 3: *color = (uint32_t)(((value >> 8) & 0xF) * 0x110000u + ((value >> 4) & 0xF) * 0x1100u + (value & 0xF) * 0x11u); return 1;
    case 6: *color = (uint32_t)(value & 0xFFFFFFu); return 1;
    case 8: {
        uint32_t alpha = (uint32_t)(value >> 24) & 0xFFu, blended = 0;
        unsigned int shift;
        for (shift = 0; shift < 24; shift += 8)
            blended |= ((((uint32_t)(value >> shift) & 0xFFu) * alpha + 0xFFu * (0xFFu - alpha) + 0x7Fu) / 0xFFu) << shift;
        *color = blended;
        return 1;
    }
    case 9: *color = (uint32_t)((((value >> 28) & 0xFF) << 16) | (((value >> 16) & 0xFF) << 8) | ((value >> 4) & 0xFF)); return 1;
    case 12: *color = (uint32_t)((((value >> 40) & 0xFF) << 16) | (((value >> 24) & 0xFF) << 8) | ((value >> 8) & 0xFF)); return 1;
    default: return 0;
    }
}

/* The halves as QString::section('|', 0, 0) and section('|', 1, 1) see them. */
static void color_halves(const char *text, size_t size, size_t start[3], size_t length[3])
{
    const char *bar = (const char *)memchr(text, '|', size), *second;
    start[0] = 0;
    length[0] = bar ? (size_t)(bar - text) : size;
    start[1] = bar ? length[0] + 1 : size;
    second = bar ? (const char *)memchr(text + start[1], '|', size - start[1]) : NULL;
    length[1] = second ? (size_t)(second - text) - start[1] : size - start[1];
    start[2] = start[1] + length[1];
    length[2] = size - start[2];
}

static int parse_qt_color(const char *text, size_t size, uint32_t *foreground, uint32_t *background)
{
    size_t start[3], length[3];
    color_halves(text, size, start, length);
    return parse_color_half(text + start[0], length[0], foreground) && parse_color_half(text + start[1], length[1], background);
}

static int valid_color(uint64_t color)
{
    return color <= 0xFFFFFFu || color == UINT32_MAX;
}

static char *color_text(uint32_t foreground, uint32_t background)
{
    char text[24];
    xx_rt_snprintf(text, sizeof(text), "%08x,%08x", (unsigned int)foreground, (unsigned int)background);
    return text_dup(text);
}

static int color_from_text(const char *text, uint32_t *foreground, uint32_t *background)
{
    uint64_t first, second;
    if (strlen(text) != 17 || text[8] != ',' || !parse_hex(text, 8, &first) || !parse_hex(text + 9, 8, &second)) return 0;
    *foreground = (uint32_t)first;
    *background = (uint32_t)second;
    return 1;
}

/* Rewrites only the half that changed, so #aarrggbb and other spellings of
 * an unchanged half survive. */
static char *qt_color_string(const char *existing, size_t existing_size, uint32_t foreground, uint32_t background)
{
    size_t start[3] = {0, 0, 0}, length[3] = {0, 0, 0}, i, total;
    const uint32_t wanted[2] = {foreground, background};
    char halves[2][16], *result;
    const char *parts[2];
    size_t sizes[2];
    if (!existing) existing = "", existing_size = 0;
    color_halves(existing, existing_size, start, length);
    for (i = 0; i < 2; ++i) {
        uint32_t current;
        if (parse_color_half(existing + start[i], length[i], &current) && current == wanted[i]) {
            parts[i] = existing + start[i];
            sizes[i] = length[i];
        } else {
            if (wanted[i] == UINT32_MAX) halves[i][0] = 0;
            else xx_rt_snprintf(halves[i], sizeof(halves[i]), "#%06x", (unsigned int)wanted[i]);
            parts[i] = halves[i];
            sizes[i] = strlen(halves[i]);
        }
    }
    total = sizes[0] + 1 + sizes[1] + length[2];
    result = (char *)xx_rt_malloc(total + 1);
    if (!result) return NULL;
    memcpy(result, parts[0], sizes[0]);
    result[sizes[0]] = '|';
    memcpy(result + sizes[0] + 1, parts[1], sizes[1]);
    memcpy(result + sizes[0] + 1 + sizes[1], existing + start[2], length[2]);
    result[total] = 0;
    return result;
}

static uint32_t *values_color(gui_options_values *values, const setting_spec *spec, size_t part)
{
    if (spec->group == 2) return part ? &values->disasm.colors[spec->index].background : &values->disasm.colors[spec->index].foreground;
    {
        xx_scan_color_record *record = spec->group ? &values->scan.dark_colors.records[spec->index] : &values->scan.colors.records[spec->index];
        return part ? &record->background : &record->foreground;
    }
}

/* ---------------------------------------------------------------- recent */

/* Detect It Easy's list is oldest first; cdie's history is newest first, one
 * %-escaped path per line, at most 16. */
static char *recent_from_list(const xx_settings_value *value)
{
    char *result = (char *)xx_rt_malloc(RECENT_CAPACITY);
    size_t used = 0, kept = 0, i = value->data.list.count;
    if (!result) return NULL;
    result[0] = 0;
    while (i-- > 0 && kept < 16) {
        const char *item = value->data.list.items[i], *p;
        size_t length = item ? strlen(item) : 0, needed = 1;
        if (!length || length >= GUI_PREFERENCE_PATH_MAX) continue;
        for (p = item; *p; ++p) needed += (*p == '%' || *p == '\n' || *p == '\r') ? 3 : 1;
        if (needed >= RECENT_CAPACITY - used) break;
        for (p = item; *p; ++p) {
            const char *escape = *p == '%' ? "%25" : *p == '\n' ? "%0A" : *p == '\r' ? "%0D" : NULL;
            if (escape) { memcpy(result + used, escape, 3); used += 3; }
            else result[used++] = *p;
        }
        result[used++] = '\n';
        result[used] = 0;
        ++kept;
    }
    return result;
}

/* ------------------------------------------------------- database paths */

/* "$data" is the executable's folder, as in Detect It Easy and die-update. */
static char *expand_data_path(const char *text, size_t size)
{
    char *rest = text_copy(size > 6 ? text + 6 : "", size > 6 ? size - 6 : 0), *directory = cdie_app_dir(), *joined, *result = NULL;
    if (rest) {
        joined = xx_fs_path_join(directory ? directory : ".", rest);
        result = joined ? cdie_strdup(joined) : NULL;
        xx_str_free(joined);
    }
#ifdef _WIN32
    if (result) {
        char *p;
        for (p = result; *p; ++p) if (*p == '/') *p = '\\';
    }
#endif
    xx_rt_free(rest);
    xx_rt_free(directory);
    return result;
}

/* "" for a default folder (Detect It Easy's "$data/db" and friends). */
static char *stored_database_path(const xx_settings_value *value, size_t slot)
{
    size_t size;
    const char *text = value_text(value, &size);
    if (!text) return NULL;
    if (size >= 5 && !memcmp(text, "$data", 5) && (size == 5 || text[5] == '/' || text[5] == '\\')) {
        if (size > 6 && same_text(text + 6, size - 6, database_names[slot], 0)) return text_dup("");
        return expand_data_path(text, size);
    }
    return text_copy(text, size);
}

/* ------------------------------------------------------------- the table */

static int spec_overflow;

static setting_spec *spec_add(const char *key, int kind)
{
    static setting_spec overflow;
    setting_spec *spec = spec_count < SPEC_CAPACITY && strlen(key) < sizeof(spec->key) ? &specs[spec_count++] : &overflow;
    memset(spec, 0, sizeof(*spec));
    if (spec != &overflow) strcpy(spec->key, key);
    else spec_overflow = 1;
    spec->kind = (unsigned char)kind;
    return spec;
}

static void spec_member(const char *key, int kind, size_t offset, size_t capacity)
{
    setting_spec *spec = spec_add(key, kind);
    spec->offset = offset;
    spec->capacity = capacity;
}

#define MEMBER(member) offsetof(gui_options_values, member), sizeof(((gui_options_values *)0)->member)

static int specs_ready(void)
{
    static const struct { const char *key; size_t offset; } bools[] = {
        {"View/StayOnTop", offsetof(gui_options_values, appearance.stay_on_top)},
        {"View/SingleApplication", offsetof(gui_options_values, appearance.single_application)},
        {"View/Advanced", offsetof(gui_options_values, appearance.advanced)},
        {"File/SaveLastDirectory", offsetof(gui_options_values, file.save_last_directory)},
        {"File/SaveRecentFiles", offsetof(gui_options_values, file.save_recent_files)},
        {"File/SaveBackup", offsetof(gui_options_values, file.save_backup)},
        {"File/SetEnvironment", offsetof(gui_options_values, file.set_environment)},
        {"Scan/ScanAfterOpen", offsetof(gui_options_values, scan.scan_after_open)},
        {"Scan/Flag/Recursive", offsetof(gui_options_values, scan.recursive)},
        {"Scan/Log/Profiling", offsetof(gui_options_values, scan.profiling)},
        {"Scan/Sort", offsetof(gui_options_values, scan.sort)},
        {"Scan/UseCache", offsetof(gui_options_values, scan.use_cache)},
        {"Scan/Highlight", offsetof(gui_options_values, scan.highlight)},
        {"Hex/LocationColon", offsetof(gui_options_values, hex.location_colon)},
        {"Hex/BoldNonZero", offsetof(gui_options_values, hex.bold_nonzero)},
        {"Disasm/Uppercase", offsetof(gui_options_values, disasm.uppercase)},
        {"Disasm/LocationColon", offsetof(gui_options_values, disasm.location_colon)},
        {"Disasm/Highlight", offsetof(gui_options_values, disasm.highlight)}};
    static const struct { const char *key; unsigned char group; unsigned int bit; } bits[] = {
        {"Scan/Flag/Deep", 0, XXWIDGETS_SCAN_DEEP}, {"Scan/Flag/Heuristic", 0, XXWIDGETS_SCAN_HEURISTIC},
        {"Scan/Flag/Verbose", 0, XXWIDGETS_SCAN_VERBOSE}, {"Scan/Flag/Aggressive", 0, XXWIDGETS_SCAN_AGGRESSIVE},
        {"Scan/HideUnknown", 0, XXWIDGETS_SCAN_HIDE_UNKNOWN}, {"Scan/FormatResult", 0, XXWIDGETS_SCAN_FORMAT},
        {"Scan/Die/Database/Custom/Enabled", 1, XXWIDGETS_SCAN_DATABASE_CUSTOM},
        {"Scan/Flag/AllTypes", 2, GUI_SCAN_ALL_TYPES}, {"Scan/Flag/Overlay", 2, GUI_SCAN_OVERLAYS},
        {"Scan/Flag/Resources", 2, GUI_SCAN_RESOURCES}, {"Scan/Flag/Archives", 2, GUI_SCAN_ARCHIVES}};
    static const char *const feature_keys[11] = {"Feature/SSE2", "Feature/AVX2", "Feature/SSE42", "Feature/AES", "Feature/SHA", "Feature/VPCLMULQDQ",
                                                 "Feature/NEON", "Feature/ArmCRC32", "Feature/ArmCrypto", "Feature/ArmDotProd", "Feature/ArmPMULL"};
    /* Indexed by GUI_DB_*. The extra database of earlier versions is gone: its
     * Extra/Path and Extra/Enabled keys under Scan/Die/Database are not in this
     * table, so they are never read, written, removed or migrated. */
    static const char *const database_keys[GUI_DB_COUNT] = {"Scan/Die/Database/Main/Path", "Scan/Die/Database/Custom/Path"};
    static int ready;
    char key[128];
    size_t i, part;
    setting_spec *spec;
    if (ready) return ready > 0;
    gui_options_values_init(&defaults);
    for (i = 0; i < sizeof(bools) / sizeof(bools[0]); ++i) spec_member(bools[i].key, SK_BOOL, bools[i].offset, sizeof(int));
    for (i = 0; i < sizeof(bits) / sizeof(bits[0]); ++i) {
        spec = spec_add(bits[i].key, SK_BIT);
        spec->group = bits[i].group;
        spec->bit = bits[i].bit;
    }
    /* View/Style, View/Qss and File/Context are never written: cdie has no Qt
     * styles, and Detect It Easy's File/Context is a file-type pattern. */
    spec_member(KEY_LANGUAGE, SK_LANGUAGE, MEMBER(appearance.language));
    spec_member("Scan/Die/Database/Main/UpdateUrl", SK_TEXT, MEMBER(databases.main_update_url));
    spec_member("Scan/Peid/Database/Path", SK_TEXT, MEMBER(databases.peid_path));
    spec_member("Scan/Peid/Database/UpdateUrl", SK_TEXT, MEMBER(databases.peid_update_url));
    spec_member("Scan/Yara/Database/Path", SK_TEXT, MEMBER(databases.yara_path));
    spec_member("Scan/Yara/Database/UpdateUrl", SK_TEXT, MEMBER(databases.yara_update_url));
    spec_member("Signatures/Path", SK_TEXT, MEMBER(signatures.path));
    spec_member("OnlineTools/VirusTotalApi", SK_TEXT, MEMBER(online.virustotal_api_key));
    spec_member("Info/Path", SK_TEXT, MEMBER(info.path));
    spec_member("Info/Database/UpdateUrl", SK_TEXT, MEMBER(info.update_url));
    spec_member("LastDirectory", SK_LAST_DIRECTORY, MEMBER(file.last_directory));
    spec_member(KEY_RECENT, SK_RECENT, MEMBER(file.recent_files));
    spec_member("Scan/Engine", SK_ENGINE, MEMBER(scan.engine));
    spec_member("Scan/IncludeTags", SK_TEXT, MEMBER(scan.include_tags));
    spec_member("Scan/ExcludeTags", SK_TEXT, MEMBER(scan.exclude_tags));
    for (i = 0; i < 6; ++i) {
        if (i < 4) xx_rt_snprintf(key, sizeof(key), "View/Font/%s", font_roles[i]);
        spec = spec_add(i < 4 ? key : i == 4 ? "Hex/Font" : "Disasm/Font", SK_FONT);
        spec->index = i;
        xx_rt_snprintf(spec->legacy, sizeof(spec->legacy), "%s", spec->key);
    }
    for (i = 0; i < XX_SCAN_COLOR_COUNT; ++i) {
        if (qt_scan_color_names[i]) {
            xx_rt_snprintf(key, sizeof(key), "Scan/Color/%s", qt_scan_color_names[i]);
            spec = spec_add(key, SK_COLOR);
            spec->index = i;
            xx_rt_snprintf(spec->legacy, sizeof(spec->legacy), "Scan/Color/%s", scan_color_names[i]);
        } else for (part = 0; part < 2; ++part) {
            xx_rt_snprintf(key, sizeof(key), "Scan/Color/%s/%s", scan_color_names[i], part ? "Background" : "Foreground");
            spec = spec_add(key, SK_PLAIN_COLOR);
            spec->index = i;
            spec->part = (unsigned char)part;
        }
        for (part = 0; part < 2; ++part) {
            xx_rt_snprintf(key, sizeof(key), "Scan/Color/Dark/%s/%s", scan_color_names[i], part ? "Background" : "Foreground");
            spec = spec_add(key, SK_PLAIN_COLOR);
            spec->group = 1;
            spec->index = i;
            spec->part = (unsigned char)part;
        }
    }
    for (i = 0; i < XXWIDGETS_DISASM_COLOR_COUNT; ++i) {
        const char *name = xxwidgets_disasm_color_key((xxwidgets_disasm_color_id)i), *qt = NULL;
        size_t j;
        if (!name) { ready = -1; return 0; }
        for (j = 0; j < sizeof(qt_disasm_colors) / sizeof(qt_disasm_colors[0]); ++j)
            if (!strcmp(qt_disasm_colors[j].cdie, name)) qt = qt_disasm_colors[j].qt;
        if (qt) {
            xx_rt_snprintf(key, sizeof(key), "Disasm/Color/%s", qt);
            spec = spec_add(key, SK_COLOR);
            spec->group = 2;
            spec->index = i;
            xx_rt_snprintf(spec->legacy, sizeof(spec->legacy), "Disasm/Color/%s", name);
        } else for (part = 0; part < 2; ++part) {
            xx_rt_snprintf(key, sizeof(key), "Disasm/Color/%s/%s", name, part ? "Background" : "Foreground");
            spec = spec_add(key, SK_PLAIN_COLOR);
            spec->group = 2;
            spec->index = i;
            spec->part = (unsigned char)part;
        }
    }
    spec_add("Disasm/Syntax", SK_SYNTAX);
    for (i = 0; i < GUI_DB_COUNT; ++i) spec_add(database_keys[i], SK_DATABASE_PATH)->index = i;
    spec_add("Feature/ReadBufferSize", SK_BUFFER)->index = 0;
    spec_add("Feature/FileBufferSize", SK_BUFFER)->index = 1;
    for (i = 0; i < 11; ++i) spec_add(feature_keys[i], SK_FEATURE)->index = i;
    spec_add("Hex/BytesPerRow", SK_BYTES_PER_ROW);
    spec_add("Options/LastPage", SK_PAGE);
    /* Overflow means the table above outgrew SPEC_CAPACITY or a key buffer. */
    ready = spec_overflow ? -1 : 1;
    return ready > 0;
}

static int needs_optimization(const setting_spec *spec)
{
    return spec->kind == SK_BUFFER || spec->kind == SK_FEATURE;
}

/* --------------------------------------------------- reading and writing */

/* A stored setting in canonical form, or NULL when it is missing or invalid.
 * legacy reads a store of older cdie versions (RecentFiles was cdie's text).
 * base supplies the parts of a font or color missing from cdie's former
 * sub-keys. */
static char *stored_canonical(const xx_settings *store, const setting_spec *spec, int legacy, const gui_options_values *base)
{
    const xx_settings_value *value = xx_settings_get(store, spec->key);
    const char *text;
    size_t size;
    uint64_t number;
    int flag;
    switch (spec->kind) {
    case SK_BOOL: case SK_BIT: case SK_FEATURE:
        return value_bool(value, &flag) ? text_dup(flag ? "true" : "false") : NULL;
    case SK_TEXT: case SK_LAST_DIRECTORY:
        text = value_text(value, &size);
        return text && size < spec->capacity ? text_copy(text, size) : NULL;
    case SK_LANGUAGE:
        /* Older versions kept the .po path in View/Lang. Detect It Easy's own
         * value counts only where it means the same here: "" (English),
         * "System", or a .po file someone stored. */
        if (!value) {
            text = value_text(xx_settings_get(store, KEY_QT_LANGUAGE), &size);
            if (text && !legacy && size && !same_text(text, size, "System", 1) && !(size > 3 && same_text(text + size - 3, 3, ".po", 1))) text = NULL;
        } else text = value_text(value, &size);
        return text && size < spec->capacity ? text_copy(text, size) : NULL;
    case SK_RECENT:
        if (legacy) value = xx_settings_get(store, KEY_QT_RECENT);
        text = value_text(value, &size);
        if (text) return size < spec->capacity ? text_copy(text, size) : NULL;
        if (legacy || value) return NULL;
        value = xx_settings_get(store, KEY_QT_RECENT);
        return value && value->type == XX_SETTINGS_VALUE_STRING_LIST ? recent_from_list(value) : NULL;
    case SK_ENGINE:
        text = value_text(value, &size);
        return text ? text_dup(same_text(text, size, "die", 0) ? "die" : "auto") : NULL;
    case SK_FONT: {
        xxwidgets_font font;
        text_fields fields;
        text = value_text(value, &size);
        if (text && !size) return font_text(default_font(spec->index));
        if (text && parse_qt_font(text, size, &font, &fields)) {
            /* QT_FONT_MONOSPACE, as cdie writes its default for these roles. */
            if (font_role_monospace_default(spec->index) && font_is_alias(font.family) && !font.point_size && !font.bold && !font.italic)
                return font_text(default_font(spec->index));
            return font_text(&font);
        }
        {
            gui_options_values *source = (gui_options_values *)(base ? base : &defaults);
            const xx_settings_value *part;
            char family[sizeof(font.family)];
            int found = 0;
            font = *values_font(source, spec->index);
            part = stored_at(store, spec->legacy, "Family");
            if ((text = value_text(part, &size)) != NULL && size < sizeof(family)) {
                memset(font.family, 0, sizeof(font.family));
                memcpy(font.family, text, size);
                found = 1;
            }
            if (value_uint(stored_at(store, spec->legacy, "Size"), &number) && (number == 0 || (number >= 4 && number <= 96)))
                font.point_size = (unsigned int)number, found = 1;
            if (value_bool(stored_at(store, spec->legacy, "Bold"), &flag)) font.bold = flag, found = 1;
            if (value_bool(stored_at(store, spec->legacy, "Italic"), &flag)) font.italic = flag, found = 1;
            return found ? font_text(&font) : NULL;
        }
    }
    case SK_COLOR: {
        uint32_t foreground, background;
        gui_options_values *source = (gui_options_values *)(base ? base : &defaults);
        int found = 0;
        text = value_text(value, &size);
        if (text && parse_qt_color(text, size, &foreground, &background)) return color_text(foreground, background);
        foreground = *values_color(source, spec, 0);
        background = *values_color(source, spec, 1);
        if (value_uint(stored_at(store, spec->legacy, "Foreground"), &number) && valid_color(number)) foreground = (uint32_t)number, found = 1;
        if (value_uint(stored_at(store, spec->legacy, "Background"), &number) && valid_color(number)) background = (uint32_t)number, found = 1;
        return found ? color_text(foreground, background) : NULL;
    }
    case SK_PLAIN_COLOR:
        return value_uint(value, &number) && valid_color(number) ? number_text(number) : NULL;
    case SK_SYNTAX:
        if (value_uint(value, &number)) return number <= 1 ? text_dup(number ? "1" : "0") : NULL;
        text = value_text(value, &size);
        return text ? text_dup(same_text(text, size, "ATT", 1) ? "1" : "0") : NULL;
    case SK_DATABASE_PATH:
        return stored_database_path(value, spec->index);
    case SK_BUFFER:
        /* Clamped as a 64-bit number, before apply() narrows it to size_t,
         * so x86 never truncates it. The stored value is left as it is: a
         * save writes it only when the size changes in Options. */
        if (!value_uint(value, &number)) return NULL;
        if (!number || number > BUFFER_SIZE_QT_MAX)
            number = spec->index ? (uint64_t)XX_DEFAULT_FILE_BUFFER_SIZE : (uint64_t)XX_DEFAULT_BUFFER_SIZE;
        else if (number < BUFFER_SIZE_MIN) number = BUFFER_SIZE_MIN;
        else if (number > BUFFER_SIZE_MAX) number = BUFFER_SIZE_MAX;
        return number_text(number);
    case SK_BYTES_PER_ROW:
        return value_uint(value, &number) && (number == 8 || number == 16 || number == 24 || number == 32 || number == 48 || number == 64) ?
            number_text(number) : NULL;
    case SK_PAGE:
        return value_uint(value, &number) && number < GUI_OPTIONS_PAGE_COUNT ? number_text(number) : NULL;
    default:
        return NULL;
    }
}

/* The current state of one setting in canonical form; NULL without memory. */
static char *describe(const setting_spec *spec, const gui_options_values *values, const settings_now *now)
{
    gui_options_values *source = (gui_options_values *)values; /* read only */
    const unsigned char *member = (const unsigned char *)values + spec->offset;
    switch (spec->kind) {
    case SK_BOOL:
        return text_dup(*(const int *)member ? "true" : "false");
    case SK_BIT: {
        unsigned int bits = spec->group == 2 ? now->modes : spec->group ? values->scan.options.databases : values->scan.options.flags;
        return text_dup(bits & spec->bit ? "true" : "false");
    }
    case SK_TEXT: case SK_LANGUAGE:
        return text_copy((const char *)member, bounded_length((const char *)member, spec->capacity));
    case SK_LAST_DIRECTORY:
        return values->file.save_last_directory ? text_copy((const char *)member, bounded_length((const char *)member, spec->capacity)) : text_dup("");
    case SK_RECENT:
        return values->file.save_recent_files ? text_copy((const char *)member, bounded_length((const char *)member, spec->capacity)) : text_dup("");
    case SK_ENGINE:
        return text_dup(strcmp(values->scan.engine, "die") ? "auto" : "die");
    case SK_FONT:
        return font_text(values_font(source, spec->index));
    case SK_COLOR:
        return color_text(*values_color(source, spec, 0), *values_color(source, spec, 1));
    case SK_PLAIN_COLOR:
        return number_text(*values_color(source, spec, spec->part));
    case SK_SYNTAX:
        return text_dup(values->disasm.syntax == 1 ? "1" : "0");
    case SK_DATABASE_PATH:
        return text_dup(now->paths[spec->index]);
    case SK_BUFFER:
        return number_text(spec->index ? now->optimization.file_buffer_size : now->optimization.buffer_size);
    case SK_FEATURE:
        return text_dup(*feature_field((gui_backend_optimization_options *)&now->optimization, spec->index) ? "true" : "false");
    case SK_BYTES_PER_ROW:
        return number_text(values->hex.bytes_per_row);
    case SK_PAGE:
        return number_text((uint64_t)now->page);
    default:
        return NULL;
    }
}

static void copy_member(gui_options_values *values, const setting_spec *spec, const char *text)
{
    char *member = (char *)values + spec->offset;
    size_t size = strlen(text);
    if (size >= spec->capacity) return;
    memcpy(member, text, size + 1);
}

/* Applies a canonical setting to values or to what load hands the backend. */
static void apply(const setting_spec *spec, const char *text, gui_options_values *values, settings_load *load)
{
    uint64_t number = 0;
    int flag = !strcmp(text, "true");
    switch (spec->kind) {
    case SK_BOOL:
        *(int *)((unsigned char *)values + spec->offset) = flag;
        break;
    case SK_BIT: {
        unsigned int *bits = spec->group == 2 ? &load->modes : spec->group ? &values->scan.options.databases : &values->scan.options.flags;
        if (flag) *bits |= spec->bit;
        else *bits &= ~spec->bit;
        break;
    }
    case SK_TEXT: case SK_LANGUAGE: case SK_LAST_DIRECTORY: case SK_RECENT: case SK_ENGINE:
        copy_member(values, spec, text);
        break;
    case SK_FONT: {
        xxwidgets_font font;
        if (font_from_text(text, &font)) *values_font(values, spec->index) = font;
        break;
    }
    case SK_COLOR: {
        uint32_t foreground, background;
        if (color_from_text(text, &foreground, &background)) {
            *values_color(values, spec, 0) = foreground;
            *values_color(values, spec, 1) = background;
        }
        break;
    }
    case SK_PLAIN_COLOR:
        if (parse_decimal(text, strlen(text), &number)) *values_color(values, spec, spec->part) = (uint32_t)number;
        break;
    case SK_SYNTAX:
        values->disasm.syntax = text[0] == '1';
        break;
    case SK_DATABASE_PATH:
        xx_rt_free(load->paths[spec->index]);
        load->paths[spec->index] = text_dup(text);
        ++load->explicit_paths;
        if (text[0]) load->stored_paths = 1;
        break;
    case SK_BUFFER:
        if (load->have_optimization && parse_decimal(text, strlen(text), &number)) {
            if (spec->index) load->optimization.file_buffer_size = (size_t)number;
            else load->optimization.buffer_size = (size_t)number;
        }
        break;
    case SK_FEATURE:
        if (load->have_optimization) *feature_field(&load->optimization, spec->index) = flag;
        break;
    case SK_BYTES_PER_ROW:
        if (parse_decimal(text, strlen(text), &number)) values->hex.bytes_per_row = (unsigned int)number;
        break;
    case SK_PAGE:
        if (parse_decimal(text, strlen(text), &number)) *load->page = (gui_options_page_id)number;
        break;
    default:
        break;
    }
}

/* Writes a canonical setting in Detect It Easy's form. */
static int write_setting(xx_settings *store, const setting_spec *spec, const char *text)
{
    const xx_settings_value *existing = xx_settings_get(store, spec->key);
    const char *current;
    char *converted = NULL, path[24];
    size_t size = 0;
    int ok;
    switch (spec->kind) {
    case SK_LANGUAGE: {
        /* Detect It Easy loads View/Lang as a .qm name: only English ("") and
         * "System" mean the same to both programs; a .po file is cdie's alone. */
        size_t length = strlen(text);
        const char *shared = same_text(text, length, "System", 1) ? "System" : !length || same_text(text, length, "English", 1) ? "" : NULL;
        if (!write_text(store, spec->key, text)) return 0;
        if (!shared) return 1;
        current = value_text(xx_settings_get(store, KEY_QT_LANGUAGE), &size);
        return (current && same_text(current, size, shared, 0)) || write_text(store, KEY_QT_LANGUAGE, shared);
    }
    case SK_SYNTAX:
        return write_text(store, spec->key, text[0] == '1' ? "ATT" : "INTEL");
    case SK_DATABASE_PATH:
        if (text[0]) return write_text(store, spec->key, text);
        xx_rt_snprintf(path, sizeof(path), "$data/%s", database_names[spec->index]);
        return write_text(store, spec->key, path);
    case SK_FONT: {
        xxwidgets_font font;
        if (!font_from_text(text, &font)) return 0;
        current = value_text(existing, &size);
        converted = qt_font_string(current, size, &font, spec->index);
        break;
    }
    case SK_COLOR: {
        uint32_t foreground, background;
        if (!color_from_text(text, &foreground, &background)) return 0;
        current = value_text(existing, &size);
        converted = qt_color_string(current, size, foreground, background);
        break;
    }
    default:
        return write_text(store, spec->key, text);
    }
    ok = converted && write_text(store, spec->key, converted);
    xx_rt_free(converted);
    return ok;
}

/* ------------------------------------------------------------- snapshots */

static void texts_free(char **texts)
{
    size_t i;
    if (!texts) return;
    for (i = 0; i < spec_count; ++i) xx_rt_free(texts[i]);
    xx_rt_free(texts);
}

static settings_snapshot *snapshot_find(const xx_settings *store)
{
    settings_snapshot *snapshot;
    for (snapshot = snapshots; snapshot; snapshot = snapshot->next)
        if (snapshot->store == store) return snapshot;
    return NULL;
}

static void snapshot_forget(const xx_settings *store)
{
    settings_snapshot **link = &snapshots;
    while (*link) {
        settings_snapshot *snapshot = *link;
        if (snapshot->store == store) {
            *link = snapshot->next;
            texts_free(snapshot->texts);
            xx_rt_free(snapshot);
        } else link = &snapshot->next;
    }
}

/* Takes texts (NULL forgets the store's snapshot). */
static void snapshot_set(const xx_settings *store, char **texts)
{
    settings_snapshot *snapshot = snapshot_find(store);
    if (!texts) { snapshot_forget(store); return; }
    if (!snapshot) {
        snapshot = (settings_snapshot *)xx_rt_malloc(sizeof(*snapshot));
        if (!snapshot) { texts_free(texts); return; }
        snapshot->store = store;
        snapshot->texts = NULL;
        snapshot->next = snapshots;
        snapshots = snapshot;
    }
    texts_free(snapshot->texts);
    snapshot->texts = texts;
}

static void now_current(settings_now *now, gui_options_page_id page)
{
    memset(now, 0, sizeof(*now));
    now->page = page;
    now->modes = gui_backend_get_scan_modes();
    now->have_optimization = gui_backend_get_optimization_options(&now->optimization);
    now->paths[GUI_DB_MAIN] = gui_backend_db_path_is_default(GUI_DB_MAIN) ? "" : gui_backend_main_db_path();
    now->paths[GUI_DB_CUSTOM] = gui_backend_db_path_is_default(GUI_DB_CUSTOM) ? "" : gui_backend_custom_db_path();
}

static char **describe_all(const gui_options_values *values, const settings_now *now)
{
    char **texts = (char **)xx_rt_calloc(spec_count ? spec_count : 1, sizeof(char *));
    size_t i;
    if (!texts) return NULL;
    for (i = 0; i < spec_count; ++i)
        if (!needs_optimization(&specs[i]) || now->have_optimization) texts[i] = describe(&specs[i], values, now);
    return texts;
}

/* ------------------------------------------------------------- migration */

/* Settings of older versions (cdie.ini beside a portable executable, else the
 * native NTInfo/cdie store) fill what Detect It Easy's store does not hold
 * yet, once. The old store keeps its settings and gains only the marker, so
 * resetting Detect It Easy's settings (deleting die.ini, clearing NTInfo/die)
 * does not bring the old ones back. */
static void migrate_legacy(xx_settings *store, const char *directory)
{
    xx_settings *legacy;
    size_t i;
    if (!specs_ready() || xx_settings_get(store, KEY_MIGRATED)) return;
    legacy = xx_settings_create_application(directory, X_ORGANIZATIONNAME, "cdie", XX_SETTINGS_MODE_AUTO);
    if (!legacy) return;
    if (xx_settings_load(legacy) == XXFC_OK && xx_settings_count(legacy) && !xx_settings_get(legacy, KEY_MIGRATED)) {
        for (i = 0; i < spec_count; ++i) {
            char *text;
            uint64_t number;
            if (xx_settings_get(store, specs[i].key)) continue;
            /* A buffer size moves as it was stored, not as cdie clamps it, so
             * Detect It Easy reads what it would have read there. */
            if (specs[i].kind == SK_BUFFER)
                text = value_uint(xx_settings_get(legacy, specs[i].key), &number) ? number_text(number) : NULL;
            else
                text = stored_canonical(legacy, &specs[i], 1, NULL);
            if (text && specs[i].kind == SK_FONT) {
                /* Older versions stored every font; a default one leaves Detect It Easy its own. */
                char *plain = font_text(default_font(specs[i].index));
                if (!plain || !strcmp(plain, text)) xx_rt_free(text), text = NULL;
                xx_rt_free(plain);
            }
            /* Detect It Easy's own View/Lang is never replaced by a migration. */
            if (text) (void)(specs[i].kind == SK_LANGUAGE ? write_text(store, specs[i].key, text) : write_setting(store, &specs[i], text));
            xx_rt_free(text);
        }
        if (write_text(store, KEY_MIGRATED, "true") && xx_settings_save(store) == XXFC_OK && write_text(legacy, KEY_MIGRATED, "true"))
            (void)xx_settings_save(legacy);
    }
    xx_settings_destroy(legacy);
}

/* ------------------------------------------------------------------ API */

/* CDIE_SETTINGS names an INI file, for tests and side-by-side setups. */
static char *settings_override(void)
{
#ifdef _WIN32
    wchar_t *environment = _wgetenv(L"CDIE_SETTINGS");
    char *converted, *path;
    if (!environment || !environment[0]) return NULL;
    converted = xx_str_unicode_to_utf8(environment);
    path = converted ? cdie_strdup(converted) : NULL;
    xx_str_free(converted);
    return path;
#else
    const char *environment = getenv("CDIE_SETTINGS");
    return environment && environment[0] ? cdie_strdup(environment) : NULL;
#endif
}

xx_settings *gui_settings_open(int memory_only, int *loaded)
{
    xx_settings *store;
    char *path = NULL, *directory = NULL;
    if (loaded) *loaded = 1;
    if (memory_only) return xx_settings_create_memory();
    path = settings_override();
    if (path) store = xx_settings_create_ini(path);
    else {
        directory = cdie_app_dir();
        store = directory ? xx_settings_create_application(directory, X_SETTINGS_ORGANIZATION, X_SETTINGS_APPLICATION, XX_SETTINGS_MODE_AUTO) : NULL;
    }
    if (store) (void)xx_settings_set_qt_compatible(store, true);
    if (store && xx_settings_load(store) == XXFC_OK) {
        if (!path && directory) migrate_legacy(store, directory);
        xx_rt_free(directory);
        xx_rt_free(path);
        return store;
    }
    xx_rt_free(directory);
    xx_rt_free(path);
    if (loaded) *loaded = 0;
    /* Unreadable storage: keep working with defaults, but never overwrite it. */
    if (store) xx_settings_destroy(store);
    return xx_settings_create_memory();
}

void gui_settings_close(xx_settings *store)
{
    if (!store) return;
    snapshot_forget(store);
    xx_settings_destroy(store);
}

int gui_settings_load(xx_settings *store, gui_options_values *values, gui_options_page_id *page)
{
    gui_backend_optimization_options optimization;
    settings_load load;
    settings_now now;
    size_t i;
    int databases_ok = 1;
    if (!store || !values || !page || !specs_ready()) return 0;
    memset(&load, 0, sizeof(load));
    load.page = page;
    load.modes = gui_backend_get_scan_modes();
    load.have_optimization = gui_backend_get_optimization_options(&load.optimization);
    optimization = load.optimization;
    for (i = 0; i < spec_count; ++i) {
        char *text = stored_canonical(store, &specs[i], 0, values);
        if (text) apply(&specs[i], text, values, &load);
        xx_rt_free(text);
    }
    if (!values->file.save_last_directory) memset(values->file.last_directory, 0, sizeof(values->file.last_directory));
    if (!values->file.save_recent_files) memset(values->file.recent_files, 0, sizeof(values->file.recent_files));
    /* Native widgets offer their platform style. Qt styles are not applied. */
    if (strcmp(values->appearance.style, "System")) strcpy(values->appearance.style, "System");
    {
        size_t size;
        const char *qss = value_text(xx_settings_get(store, "View/Qss"), &size);
        if (qss && size < sizeof(values->appearance.qss)) {
            memcpy(values->appearance.qss, qss, size);
            values->appearance.qss[size] = 0;
        }
    }
    gui_backend_set_scan_modes(load.modes);
    values->scan.modes = gui_backend_get_scan_modes();
    /* The backend validates and masks what this CPU cannot use. */
    if (load.have_optimization && !gui_backend_set_optimization_options(&load.optimization)) (void)gui_backend_set_optimization_options(&optimization);
    {
        const char *paths[GUI_DB_COUNT];
        for (i = 0; i < GUI_DB_COUNT; ++i) paths[i] = load.paths[i] ? load.paths[i] : "";
        /* Explicit default values for both folders mean restore the default
         * folders. An empty store, or malformed entries alone, keep the current
         * folders. */
        if ((load.stored_paths || load.explicit_paths == GUI_DB_COUNT) && !gui_backend_db_paths_current(paths[GUI_DB_MAIN], paths[GUI_DB_CUSTOM]))
            databases_ok = gui_backend_set_db_paths(paths[GUI_DB_MAIN], paths[GUI_DB_CUSTOM]);
        for (i = 0; i < GUI_DB_COUNT; ++i) xx_rt_free(load.paths[i]);
    }
    {
        /* The folders in effect, as the Options window shows them (GUI_DB_* order). */
        const char *current[GUI_DB_COUNT] = {gui_backend_main_db_path(), gui_backend_custom_db_path()};
        char *fields[GUI_DB_COUNT] = {values->databases.main_path, values->databases.custom_path};
        for (i = 0; i < GUI_DB_COUNT; ++i) {
            const char *path = gui_backend_db_path_is_default((int)i) ? "" : current[i];
            size_t size = strlen(path);
            if (size >= GUI_PREFERENCE_PATH_MAX) continue;
            memcpy(fields[i], path, size);
            memset(fields[i] + size, 0, GUI_PREFERENCE_PATH_MAX - size);
        }
    }
    {
        gui_backend_scan_preferences scan, previous;
        if (gui_backend_get_scan_preferences(&previous)) {
            scan.recursive = values->scan.recursive; scan.profiling = values->scan.profiling;
            scan.sort = values->scan.sort; scan.use_cache = values->scan.use_cache;
            memcpy(scan.engine, values->scan.engine, sizeof(scan.engine));
            if (!gui_backend_set_scan_preferences(&scan)) {
                values->scan.recursive = previous.recursive; values->scan.profiling = previous.profiling;
                values->scan.sort = previous.sort; values->scan.use_cache = previous.use_cache;
                memcpy(values->scan.engine, previous.engine, sizeof(values->scan.engine));
            }
        }
    }
    values->fonts.fonts[XXWIDGETS_FONT_HEX_VIEWS] = values->hex.font;
    values->fonts.fonts[XXWIDGETS_FONT_DISASM_VIEWS] = values->disasm.font;
    now_current(&now, *page);
    snapshot_set(store, describe_all(values, &now));
    return databases_ok;
}

int gui_settings_save(xx_settings *store, const gui_options_values *values, gui_options_page_id page)
{
    settings_snapshot *snapshot;
    settings_now now;
    size_t i;
    int ok = 1;
    xxfc_status_t status;
    if (!store || !values || !specs_ready()) return 0;
    now_current(&now, page);
    if (!now.have_optimization) ok = 0;
    snapshot = snapshot_find(store);
    for (i = 0; i < spec_count; ++i) {
        const setting_spec *spec = &specs[i];
        char *text, *stored;
        int same = 0;
        if (needs_optimization(spec) && !now.have_optimization) continue;
        text = describe(spec, values, &now);
        if (!text) { ok = 0; continue; }
        /* Without a load there is nothing to compare with: write everything. */
        if (snapshot) {
            if (snapshot->texts[i] && !strcmp(snapshot->texts[i], text)) { xx_rt_free(text); continue; }
            stored = stored_canonical(store, spec, 0, values);
            same = stored && !strcmp(stored, text);
            xx_rt_free(stored);
        }
        if (!same && !write_setting(store, spec, text)) { ok = 0; xx_rt_free(text); continue; }
        /* The snapshot follows the store, not the disk: a failed save keeps its
         * changes for the next one, so a later change back must be written too. */
        if (snapshot) { xx_rt_free(snapshot->texts[i]); snapshot->texts[i] = text; }
        else xx_rt_free(text);
    }
    status = xx_settings_save(store);
    return ok && status == XXFC_OK;
}
