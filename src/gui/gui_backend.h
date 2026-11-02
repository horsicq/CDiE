/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* gui_backend.h - the engine side of the native desktop GUIs.
 *
 * The scan engine headers (<windows.h>-hostile: xpe.h and friends define their
 * own PE structures) never meet <windows.h>. gui_backend.c includes the engine;
 * main_gui.c includes <windows.h>. This header is the only thing they share, so
 * it exposes plain C types alone - no engine structs, no Win32 types leak.
 * The one xxfclib type, xx_pd_struct, is plain data shared with xxwidgets. */

#ifndef CDIE_GUI_BACKEND_H
#define CDIE_GUI_BACKEND_H

#include <stddef.h>
#include <stdint.h>
#include <xxfclib/data/xx_pd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The database slots, as Detect It Easy has them: the main database, always
 * used, and the optional custom one. The extra database folder of earlier
 * versions is never loaded. */
enum { GUI_DB_MAIN = 0, GUI_DB_CUSTOM = 1, GUI_DB_COUNT = 2 };

/* File-type choices Qt's XFormats exposes but xxfclib's compact scanner enum
 * does not.  They intentionally live outside XFileType; scans map them to
 * Binary, matching diec --filetype for these generic views. */
enum {
    GUI_FILE_TYPE_TEXT = 1001,
    GUI_FILE_TYPE_UNICODE,
    GUI_FILE_TYPE_7ZIP
};

/* Resolves db/db_custom next to the executable, then in the macOS bundle's
 * Resources directory. Loads both once and sorts them, regardless of the
 * toggle, because the "Databases" toggle gates whether the custom database
 * contributes at scan time - so changing it never needs a reload. Returns 1
 * when the main database yielded at least one signature, else 0. */
int gui_backend_init(void);

/* The resolved main / custom database paths (UTF-8), for the Options dialog
 * and the "not found" message. Owned by the backend; valid until
 * gui_backend_shutdown or the next gui_backend_set_db_paths; do not free. */
const char *gui_backend_main_db_path(void);
const char *gui_backend_custom_db_path(void);

/* Replaces the two database paths (UTF-8; a NULL or empty entry re-resolves
 * that slot's default, as gui_backend_init does) and reloads both. Returns 1
 * when the main database loaded at least one signature, else 0. Failure leaves
 * the previous paths and loaded database intact. */
int gui_backend_set_db_paths(const char *pMain, const char *pCustom);

/* 1 when applying the paths would change nothing, so they need no reload: an
 * empty entry matches a slot on its default folder, any other entry the same
 * folder chosen explicitly. An empty entry matches a default slot even when
 * the default lookup would now find another folder (see
 * gui_backend_db_path_moved). */
int gui_backend_db_paths_current(const char *pMain, const char *pCustom);

/* 1 when database nIndex (a GUI_DB_* slot) was loaded from the default lookup
 * (gui_backend_init, or a NULL / empty entry given to gui_backend_set_db_paths)
 * rather than from a chosen folder. It stays 1 when the lookup would now
 * answer differently, e.g. after an update created the per-user copy. */
int gui_backend_db_path_is_default(int nIndex);

/* 1 when database nIndex is on its default folder but the default lookup now
 * finds another one (an update wrote the per-user copy since it was loaded);
 * gui_backend_set_db_paths with an empty entry for that slot switches to it. */
int gui_backend_db_path_moved(int nIndex);

/* Changes whenever the loaded databases are replaced (gui_backend_init or a
 * successful gui_backend_set_db_paths), so a frontend can tell that its tags
 * and results belong to the previous databases. */
uint64_t gui_backend_database_generation(void);

/* Total number of signatures loaded across both databases. */
int gui_backend_signature_count(void);

/* Returns the unique tags used by loaded signatures, sorted and joined with
 * semicolons. The returned UTF-8 string is allocated by the engine and must be
 * released with gui_backend_free. An initialized database with no tags yields
 * an allocated empty string. */
char *gui_backend_used_tags(void);

/* Replaces the scan-time tag filters. Both arguments are semicolon-separated
 * UTF-8 tag lists; NULL and empty strings mean no filter. The backend copies
 * the strings, so callers may release or reuse their buffers after return.
 * Allocation failure leaves both previous filters unchanged. */
int gui_backend_set_tag_filters(const char *pIncludeTags, const char *pExcludeTags);

/* "cdie <version>" (UTF-8) for the window title. Static storage; do not free. */
const char *gui_backend_app_title(void);

/* Sets the scan-flag toggles (non-zero = on). */
void gui_backend_set_flags(int bDeep, int bHeuristic, int bVerbose, int bAggressive, int bHideUnknown, int bFormat);

/* Selects whether the custom database contributes (the main database is
 * always used). */
void gui_backend_set_databases(int bUseCustom);

enum {
    GUI_SCAN_ALL_TYPES = 1u << 0,
    GUI_SCAN_OVERLAYS = 1u << 1,
    GUI_SCAN_RESOURCES = 1u << 2,
    GUI_SCAN_ARCHIVES = 1u << 3,
    GUI_SCAN_MODES_MASK = 15u,
    GUI_SCAN_MODES_DEFAULT = GUI_SCAN_OVERLAYS
};

/* Expansion modes apply to both the tree and its report. Overlay scanning is
 * enabled initially, preserving the interactive frontend's existing default.
 * Invalid bits leave the current settings unchanged. Call between scans. */
unsigned int gui_backend_get_scan_modes(void);
int gui_backend_set_scan_modes(unsigned int modes);

/* Only compiled scanner implementations are accepted. Automatic currently
 * resolves to Detect It Easy; PEiD and YARA are not built into this frontend. */
typedef struct gui_backend_scan_preferences {
    int recursive, profiling, sort, use_cache;
    char engine[16]; /* "auto" or "die" */
} gui_backend_scan_preferences;
size_t gui_backend_engine_count(void);
const char *gui_backend_engine_id(size_t index);
const char *gui_backend_engine_name(size_t index);
int gui_backend_get_scan_preferences(gui_backend_scan_preferences *preferences);
int gui_backend_set_scan_preferences(const gui_backend_scan_preferences *preferences);
/* Validate and stage all database folders before changing either databases or
 * preferences. Empty folders select executable-local defaults. Failure leaves
 * the complete previous configuration intact. Call between scans. */
int gui_backend_apply_configuration(const gui_backend_scan_preferences *preferences,
    const char *main_path, const char *custom_path,
    char *error, size_t error_capacity);
/* Why the last gui_backend_apply_configuration failed (OK after a success), so
 * a frontend can say it in its own language; error above is English. slot (may
 * be NULL) receives the database (a GUI_DB_* slot) for MISSING and LOAD, else
 * -1. */
typedef enum gui_backend_configuration_failure {
    GUI_BACKEND_CONFIGURATION_OK = 0,
    GUI_BACKEND_CONFIGURATION_OPTIONS, /* invalid scan options or an unavailable engine */
    GUI_BACKEND_CONFIGURATION_MISSING, /* a chosen database folder does not exist */
    GUI_BACKEND_CONFIGURATION_LOAD     /* the main database yields no signatures */
} gui_backend_configuration_failure;
gui_backend_configuration_failure gui_backend_configuration_failed(int *slot);
/* The optional one-entry result cache verifies the complete input with a
 * bounded BLAKE2sp digest, path, type selector, options and database generation.
 * It never caches failures/cancellation. Counters are useful for diagnostics. */
void gui_backend_clear_cache(void);
void gui_backend_cache_statistics(uint64_t *hits, uint64_t *misses);

typedef struct gui_backend_optimization_options {
    size_t buffer_size;
    size_t file_buffer_size;
    int use_sse2;
    int use_avx2;
    int use_sse42;
    int use_aes;
    int use_sha;
    int use_vpclmulqdq;
    int use_neon;
    int use_arm_crc32;
    int use_arm_crypto;
    int use_arm_dotprod;
    int use_arm_pmull;
} gui_backend_optimization_options;

typedef struct gui_backend_optimization_capabilities {
    int sse2;
    int avx2;
    int sse42;
    int aes;
    int sha;
    int vpclmulqdq;
    int neon;
    int arm_crc32;
    int arm_crypto;
    int arm_dotprod;
    int arm_pmull;
} gui_backend_optimization_capabilities;

/* Process-wide settings, independent of database initialization. Sizes are in
 * bytes; zero restores the library default. Acceleration fields must be 0 or 1 and
 * unsupported acceleration is normalized to off. Invalid arguments return 0
 * without changing any settings. Getters return the effective settings; AVX2
 * support includes CPU and operating-system support. Changes affect next scans. */
int gui_backend_get_optimization_options(gui_backend_optimization_options *pOptions);
int gui_backend_get_optimization_capabilities(gui_backend_optimization_capabilities *pCapabilities);
int gui_backend_set_optimization_options(const gui_backend_optimization_options *pOptions);

/* Emits Automatic (id 0), then the preferred concrete type and the other
 * detected concrete interpretations, including Binary. IDs are backend scan
 * selectors rather than combo-box indexes. Labels are static UTF-8 strings.
 * No callback runs on an invalid path, read failure, or before initialization. */
typedef void (*gui_backend_file_type_fn)(int typeId, const char *pName, void *pUserData);
int gui_backend_file_types(const char *pUtf8Path, gui_backend_file_type_fn pTypeFn, void *pUserData);

/* Scans one file given a UTF-8 path. On success returns 1 and hands back two
 * heap strings (UTF-8): the formatted result text and the detected file-type
 * name; free each with gui_backend_free. On failure returns 0 and sets both
 * out-pointers to NULL. */
int gui_backend_scan(const char *pUtf8Path, char **ppResultText, char **ppTypeName);

/* Receives visible, sorted detection records without parsing formatted text.
 * Field strings are borrowed only for the duration of the callback. The type
 * includes the translated label and a heuristic marker when appropriate.
 * Output strings follow gui_backend_scan's ownership rules. */
typedef void (*gui_backend_result_fn)(const char *pType, const char *pName,
    const char *pVersion, const char *pInfo, void *pUserData);
int gui_backend_scan_results(const char *pUtf8Path, gui_backend_result_fn pResultFn,
    void *pUserData, char **ppResultText, char **ppTypeName);

/* Scans the current file as the selected file-type ID from file_types().
 * ID 0 is automatic; explicit IDs run only that interpretation's scan pass.
 * The automatic APIs above do not retain a previous explicit selection. */
int gui_backend_scan_results_type(const char *pUtf8Path, int nTypeId,
    gui_backend_result_fn pResultFn, void *pUserData,
    char **ppResultText, char **ppTypeName);

/* Compact result tree with explicit parent indexes. SIZE_MAX denotes a root;
 * category selects the scan-layer color pair; an empty category denotes a group.
 * Return zero from the callback to abort on an allocation/display failure.
 * progress (may be NULL) receives the engine's levels; stopping it cancels the
 * scan, which then fails with progress->is_stop set. */
typedef struct gui_backend_detection {
    const char *name;      /* what was found, e.g. "UPX"; never NULL */
    const char *signature; /* database path of the script that found it; NULL for Unknown */
} gui_backend_detection;
/* detection is NULL on group rows (the file type, Overlay, Archive member...)
 * and is borrowed for the call. */
typedef int (*gui_backend_tree_fn)(size_t parent, const char *text, const char *category,
                                   const gui_backend_detection *detection, void *user);
/* What DIE shows around the result tree: the format parser's view of the
 * scanned file (empty strings when it has none) and the engine's errors and
 * warnings, one per line ("" when there are none; free with gui_backend_free). */
typedef struct gui_backend_scan_info {
    char endianness[16], mode[16], architecture[32], type[48];
    char *log;
} gui_backend_scan_info;
/* info may be NULL. */
int gui_backend_scan_tree(const char *path, int type_id, gui_backend_tree_fn callback,
    void *user, char **report, char **type_name, gui_backend_scan_info *info, xx_pd_struct *progress);
/* Detections of one file for a directory scan, with these flags and optional
 * databases (xxwidgets scan option bits; only XXWIDGETS_SCAN_DATABASE_CUSTOM
 * counts) instead of the current ones. Safe on a worker thread while nothing
 * changes the databases. */
typedef void (*gui_backend_detection_fn)(const char *type, const char *name, const char *version, const char *info, void *user);
int gui_backend_scan_detections(const char *path, unsigned int flags, unsigned int databases,
    gui_backend_detection_fn callback, void *user);
/* Loaded signatures, in database order, as "Folder/name.sg" for display and
 * by the database path a result row names. */
int gui_backend_signature_entry(int index, const char **display, const char **path);

/* Source of the loaded signature a result row names (its database path),
 * NULL when no loaded signature has that path or it is compiled bytecode.
 * Free with gui_backend_free. */
char *gui_backend_signature_source(const char *signature);
/* Runs source, an edited copy of that signature, on path the way a scan of
 * type_id runs it, with the loaded _init and include helpers but no other
 * signature. *text receives what it detected followed by its _log output and
 * errors (free with gui_backend_free). Returns 0 when the file cannot be
 * scanned; *text then says why when it can. */
int gui_backend_run_signature(const char *path, int type_id, const char *signature, const char *source, char **text);
/* The bundled description of a detection name (info/html/<name>.html beside
 * the databases), as plain text; NULL when there is none. Free with
 * gui_backend_free. */
char *gui_backend_info_text(const char *name);
/* Directory containing html/<name>.html (or <name>.html). "$data" resolves
 * beside the executable. NULL/empty restores the bundled lookup locations. */
int gui_backend_set_info_path(const char *path);

/* What the Advanced view shows for one file and type (DIE_LOOK.md 1.3-1.5 and
 * 2.5): the type page, the header values and the engine row. Pages are in the
 * order of xxwidgets_format_page, so a frontend can pass them on unchanged. */
typedef enum gui_backend_facts_page {
    GUI_FACTS_PAGE_BINARY = 0, GUI_FACTS_PAGE_COM, GUI_FACTS_PAGE_ARCHIVE, GUI_FACTS_PAGE_MSDOS,
    GUI_FACTS_PAGE_NE, GUI_FACTS_PAGE_LE, GUI_FACTS_PAGE_DEX, GUI_FACTS_PAGE_PE, GUI_FACTS_PAGE_ELF,
    GUI_FACTS_PAGE_MACH, GUI_FACTS_PAGE_COUNT
} gui_backend_facts_page;
enum {
    GUI_FACTS_SEG_OFF = 1u << 0,    /* MSDOS: show base and entry as "hhhh:llll" halves of 32 bits */
    GUI_FACTS_EXPORTS = 1u << 1,    /* PE data directories present, as Qt DIE tests them */
    GUI_FACTS_IMPORTS = 1u << 2,
    GUI_FACTS_RESOURCES = 1u << 3,
    GUI_FACTS_DOTNET = 1u << 4,     /* a COM descriptor; .NET stays disabled until it has a viewer */
    GUI_FACTS_TLS = 1u << 5,
    GUI_FACTS_OVERLAY = 1u << 6,    /* PE and MSDOS: overlay_offset/overlay_size are valid */
    GUI_FACTS_MANIFEST = 1u << 7,   /* PE: manifest holds the RT_MANIFEST text */
    GUI_FACTS_VERSION = 1u << 8,    /* PE: version holds the RT_VERSION strings */
    GUI_FACTS_SECTIONS = 1u << 9,   /* PE, ELF, Mach-O: sections is not zero */
    GUI_FACTS_PROGRAMS = 1u << 10,  /* ELF */
    GUI_FACTS_COMMANDS = 1u << 11,  /* Mach-O */
    GUI_FACTS_SEGMENTS = 1u << 12,
    GUI_FACTS_LIBRARIES = 1u << 13,
    GUI_FACTS_ENTRY_OFFSET = 1u << 14, /* entry_point_offset is a file offset */
    GUI_FACTS_NET_ID = 1u << 15     /* PE: net_id is valid; Qt titles the time stamp box ".NET ID"
                                     * and shows net_id as 8 hex digits instead of the date */
};
typedef struct gui_backend_facts {
    int type_id;             /* the file_types() ID described: the preferred type when 0 was asked */
    char type_name[32];      /* its name, e.g. "PE64", "MSDOS", "Binary" */
    int page;                /* gui_backend_facts_page */
    char main_label[64];     /* the page button: "PE", "ELF", "Mach-O", "MSDOS", "NE", "LE" or "LX",
                              * "DEX", "Binary", or the archive format, e.g. "ZIP(2.0)"; "" for COM */
    unsigned int flags;      /* GUI_FACTS_* */
    uint64_t file_size;
    /* Header values; Qt pads them to bits / 4 hex digits and shows them bold
     * when not zero. */
    uint64_t base_address, entry_point;
    unsigned int base_bits, entry_bits; /* 8, 16, 32 or 64 */
    int64_t entry_point_offset;         /* -1 unless GUI_FACTS_ENTRY_OFFSET */
    /* The engine row, as gui_backend_scan_info has it ("" when unknown). */
    char endianness[16], mode[16], architecture[32], type[48];
    /* File Information structures for the page's buttons and arrows (static
     * strings, NULL when the page has no such item): the main button, Sections,
     * TLS, and the ELF and Mach-O arrows. */
    const char *header_selector, *sections_selector, *tls_selector, *programs_selector;
    const char *commands_selector, *segments_selector, *libraries_selector;
    /* Counts: PE NumberOfSections, ELF section headers, Mach-O sections. */
    uint32_t sections, programs, commands, segments, libraries;
    /* PE */
    uint32_t time_date_stamp, size_of_image;
    char time_date_stamp_text[32];      /* "yyyy-MM-dd hh:mm:ss" in local time, as Qt DIE */
    int64_t overlay_offset, overlay_size; /* -1 and 0 without an overlay */
    char *manifest, *version;           /* heap UTF-8 or NULL; "Key: Value" lines for version */
    /* XPE::getNetId: with a COM descriptor, a TimeDateStamp with bit 31 set
     * (a deterministic .NET build's hash), else 0. */
    uint32_t net_id;
} gui_backend_facts;
/* Fills facts for path as type_id (a gui_backend_file_types ID; 0 resolves to
 * the preferred concrete type, as a type-0 scan does, skipping one whose own
 * format reader rejects the file, as Qt DIE's list does). Reads headers only and
 * needs no database, so it may run before gui_backend_init. Returns 1 on
 * success; 0 for an unreadable file or a type the file does not have, with
 * facts zeroed. Release with gui_backend_facts_free, also after a failure. */
int gui_backend_file_facts(const char *path, int type_id, gui_backend_facts *facts);
void gui_backend_facts_free(gui_backend_facts *facts);

/* Frees a string handed back by gui_backend_scan (uses the engine allocator,
 * which on the CRT-free build is not the Win32 heap the frontend uses). */
void gui_backend_free(void *pPtr);

/* Releases the databases and options. */
void gui_backend_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* CDIE_GUI_BACKEND_H */
