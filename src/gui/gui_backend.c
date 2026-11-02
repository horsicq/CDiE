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
/* localtime_r in strict C99. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

/* gui_backend.c - drives the scan engine for the native GUIs. It is the mirror
 * of main_console.c's scan path: same option defaults, same db resolution, same
 * die_engine_scan_file + xx_scan_format_text, so the GUI prints exactly what `cdie` does.
 *
 * This translation unit deliberately does NOT include <windows.h>: it speaks to
 * the engine only. main_gui.c is the half that speaks Win32. */

#include "gui_backend.h"
#include "gui_result_tree.h"

#include "../global.h"
#include <xxfclib/die_engine/die_engine.h>
#include <xxfclib/die_engine/die_engine_debug.h>
#include <xxfclib/js/xx_js.h>
#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/formats/executable/elf/xx_elf.h>
#include <xxfclib/formats/executable/macho/xx_macho.h>
#include <xxfclib/formats/executable/pe/xx_pe.h>
#include <xxfclib/formats/executable/pe/xx_pe_inspect.h>
#include <xxfclib/formats/executable/msdos/xx_msdos.h>
#include <xxfclib/formats/executable/ne/xx_ne.h>
#include <xxfclib/formats/executable/le/xx_le.h>
#include <xxfclib/formats/executable/lx/xx_lx.h>
#include <xxfclib/formats/executable/dex/xx_dex.h>
#include <xxfclib/formats/executable/amigahunk/xx_amigahunk.h>
#include <xxfclib/formats/archive/zip/xx_zip.h>
#include <xxfclib/formats/executable/jar/xx_jar.h>
#include <xxfclib/formats/installer/apk/xx_apk.h>
#include <xxfclib/formats/installer/ipa/xx_ipa.h>
#include <xxfclib/formats/archive/rar/xx_rar.h>
#include <xxfclib/data/xx_data.h>
/* xx_format.h aliases fileType for format code; the scan result's member is fileType. */
#undef fileType
#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/io/xx_io.h>
#include <xxfclib/strings/xx_string.h>
#include <xxfclib/global/xx_global.h>
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/algo/blake2/xx_blake2.h>
#include <xxfclib/threads/xx_threads.h>

#include "../app/cdie_app.h"
#include "../app/cdie_paths.h"

#include <time.h>


static xx_scan_options g_options;
static DBase g_db;
static int g_bInited = 0;
static int g_bMainLoaded = 0;
/* Per GUI_DB_* slot: the loaded path came from the default lookup rather
 * than from the caller. Kept, not re-derived, because the lookup's answer
 * changes once an update creates the per-user copy. */
static int g_bDbDefault[GUI_DB_COUNT] = {1, 1};
/* Each slot's default folder name, for the lookup (cdie_paths.h). */
static const char *const g_dbNames[GUI_DB_COUNT] = {"db", "db_custom"};
/* xxwidgets' XXWIDGETS_SCAN_DATABASE_CUSTOM (1u << 1); the engine half does
 * not include xxwidgets. Its deprecated EXTRA bit (1u << 0) is ignored. */
#define GUI_SCAN_DATABASE_CUSTOM (1u << 1)
static char *g_pIncludeTags = NULL;
static char *g_pExcludeTags = NULL;
static char *g_info_path = NULL;
static gui_backend_scan_preferences g_scan_preferences = {0, 0, 1, 0, "auto"};
static uint64_t g_options_generation = 1, g_database_generation = 1;
static xx_mutex_t *g_cache_lock;
static struct {
    char *path;
    uint8_t digest[XX_BLAKE2SP_DIGEST_SIZE];
    int64_t size;
    int type_id, valid;
    uint64_t options_generation, database_generation, hits, misses;
    xx_scan_result result;
} g_cache;

void gui_backend_clear_cache(void)
{
    if (g_cache_lock) xx_mutex_lock(g_cache_lock);
    xx_rt_free(g_cache.path);
    g_cache.path = NULL;
    xx_scan_result_free(&g_cache.result);
    g_cache.valid = 0;
    if (g_cache_lock) xx_mutex_unlock(g_cache_lock);
}

static void options_changed(void)
{
    ++g_options_generation;
    gui_backend_clear_cache();
}

void gui_backend_cache_statistics(uint64_t *hits, uint64_t *misses)
{
    if (g_cache_lock) xx_mutex_lock(g_cache_lock);
    if (hits) *hits = g_cache.hits;
    if (misses) *misses = g_cache.misses;
    if (g_cache_lock) xx_mutex_unlock(g_cache_lock);
}

size_t gui_backend_engine_count(void) { return 2; }
const char *gui_backend_engine_id(size_t index) { return index == 0 ? "auto" : index == 1 ? "die" : NULL; }
const char *gui_backend_engine_name(size_t index) { return index == 0 ? "Automatic" : index == 1 ? "Detect It Easy" : NULL; }

static int valid_scan_preferences(const gui_backend_scan_preferences *p)
{
    return p && (p->recursive == 0 || p->recursive == 1) && (p->profiling == 0 || p->profiling == 1) &&
        (p->sort == 0 || p->sort == 1) && (p->use_cache == 0 || p->use_cache == 1) &&
        xx_rt_memchr(p->engine, 0, sizeof(p->engine)) &&
        (!xx_rt_strcmp(p->engine, "auto") || !xx_rt_strcmp(p->engine, "die"));
}

int gui_backend_get_scan_preferences(gui_backend_scan_preferences *preferences)
{
    if (!preferences) return 0;
    *preferences = g_scan_preferences;
    return 1;
}

int gui_backend_set_scan_preferences(const gui_backend_scan_preferences *preferences)
{
    if (!valid_scan_preferences(preferences)) return 0;
    if (g_scan_preferences.recursive != preferences->recursive || g_scan_preferences.profiling != preferences->profiling ||
        g_scan_preferences.sort != preferences->sort || g_scan_preferences.use_cache != preferences->use_cache ||
        xx_rt_strcmp(g_scan_preferences.engine, preferences->engine)) options_changed();
    g_scan_preferences = *preferences;
    g_options.recursive_scan = preferences->recursive != 0;
    g_options.profiling = preferences->profiling != 0;
    g_options.sort = preferences->sort != 0;
    return 1;
}

/* Mirrors main_console.c's resolve_database_path with no explicit path (see
 * cdie_paths.h): the per-user copy an update wrote, "<exe-dir>/<name>", the
 * bundle's Resources on macOS, else the bare relative name. */
static char *gui_resolve_db(const char *pDefaultName)
{
    char *pFound = cdie_find_database_dir(pDefaultName);

    return pFound ? pFound : cdie_strdup(pDefaultName);
}

int gui_backend_init(void)
{
    if (g_bInited) {
        return g_bMainLoaded;
    }

    xx_scan_options_init(&g_options);
    xx_rt_memset(&g_db, 0, sizeof(g_db));

    /* GUI defaults follow the Detect It Easy GUI: deep + heuristic + verbose on.
     * xx_scan_options_init has already set show type/version/info, use custom
     * and sort - the settings that make the output match the console. */
    g_options.deep_scan = 1;
    g_options.heuristic_scan = 1;
    g_options.verbose = 1;
    g_options.overlay_scan = 1;
    g_scan_preferences = (gui_backend_scan_preferences){0, 0, 1, 0, "auto"};
    g_options.recursive_scan = false;
    g_cache_lock = xx_mutex_create();
    g_cache.hits = g_cache.misses = 0;
    ++g_database_generation;

    g_options.main_database_path = gui_resolve_db(g_dbNames[GUI_DB_MAIN]);
    g_options.custom_database_path = gui_resolve_db(g_dbNames[GUI_DB_CUSTOM]);
    g_bDbDefault[GUI_DB_MAIN] = g_bDbDefault[GUI_DB_CUSTOM] = 1;

    /* Load both once. The "Databases" toggle gates whether the custom one runs
     * at scan time (via use_custom_database), so a toggle needs no reload. A
     * missing db_custom directory simply loads nothing; the extra database
     * folder of earlier versions is never loaded. */
    g_bMainLoaded = db_load(&g_db, g_options.main_database_path, DB_MAIN);
    db_load(&g_db, g_options.custom_database_path, DB_CUSTOM);
    db_sort(&g_db);

    g_bInited = 1;

    return g_bMainLoaded;
}

const char *gui_backend_main_db_path(void)
{
    return g_options.main_database_path ? g_options.main_database_path : "";
}

const char *gui_backend_custom_db_path(void)
{
    return g_options.custom_database_path ? g_options.custom_database_path : "";
}

int gui_backend_set_db_paths(const char *pMain, const char *pCustom)
{
    char *pNewMain;
    char *pNewCustom;
    DBase newDb;
    /* Decided now: the arguments may be the backend's own strings, freed below. */
    int bDefaultMain = (pMain == NULL) || (pMain[0] == 0);
    int bDefaultCustom = (pCustom == NULL) || (pCustom[0] == 0);

    if (!g_bInited) {
        return 0;
    }

    pNewMain = !bDefaultMain ? cdie_strdup(pMain) : gui_resolve_db(g_dbNames[GUI_DB_MAIN]);
    pNewCustom = !bDefaultCustom ? cdie_strdup(pCustom) : gui_resolve_db(g_dbNames[GUI_DB_CUSTOM]);
    xx_rt_memset(&newDb, 0, sizeof(newDb));

    if (!pNewMain || !pNewCustom || !db_load(&newDb, pNewMain, DB_MAIN)) {
        db_free(&newDb);
        xx_rt_free(pNewMain);
        xx_rt_free(pNewCustom);
        return 0;
    }

    db_load(&newDb, pNewCustom, DB_CUSTOM);
    db_sort(&newDb);
    db_free(&g_db);
    xx_rt_free(g_options.main_database_path);
    xx_rt_free(g_options.custom_database_path);
    g_db = newDb;
    g_options.main_database_path = pNewMain;
    g_options.custom_database_path = pNewCustom;
    g_bDbDefault[GUI_DB_MAIN] = bDefaultMain;
    g_bDbDefault[GUI_DB_CUSTOM] = bDefaultCustom;
    g_bMainLoaded = 1;
    ++g_database_generation;
    gui_backend_clear_cache();
    return 1;
}

static gui_backend_configuration_failure g_configuration_failure;
static int g_configuration_slot = -1;

static int gui_configuration_failed(gui_backend_configuration_failure failure, int nSlot)
{
    g_configuration_failure = failure;
    g_configuration_slot = nSlot;
    return 0;
}

gui_backend_configuration_failure gui_backend_configuration_failed(int *slot)
{
    if (slot) *slot = g_configuration_slot;
    return g_configuration_failure;
}

int gui_backend_apply_configuration(const gui_backend_scan_preferences *preferences,
    const char *main_path, const char *custom_path,
    char *error, size_t error_capacity)
{
    const char *paths[GUI_DB_COUNT] = {main_path, custom_path};
    static const char *const names[GUI_DB_COUNT] = {"main", "custom"};
    if (error && error_capacity) error[0] = 0;
    (void)gui_configuration_failed(GUI_BACKEND_CONFIGURATION_OK, -1);
    if (!g_bInited || !valid_scan_preferences(preferences)) {
        if (error && error_capacity) xx_rt_snprintf(error, error_capacity, "Invalid scan options or unavailable scan engine.");
        return gui_configuration_failed(GUI_BACKEND_CONFIGURATION_OPTIONS, -1);
    }
    const char *current[GUI_DB_COUNT] = {gui_backend_main_db_path(), gui_backend_custom_db_path()};
    for (size_t i = 0; i < GUI_DB_COUNT; ++i) if (paths[i] && paths[i][0] && xx_rt_strcmp(paths[i], current[i]) &&
        !xx_fs_is_dir(paths[i]) && !xx_fs_is_file(paths[i])) {
        if (error && error_capacity) xx_rt_snprintf(error, error_capacity, "The %s database folder does not exist: %s", names[i], paths[i]);
        return gui_configuration_failed(GUI_BACKEND_CONFIGURATION_MISSING, (int)i);
    }
    if (!gui_backend_db_paths_current(main_path, custom_path) &&
        !gui_backend_set_db_paths(main_path, custom_path)) {
        if (error && error_capacity) xx_rt_snprintf(error, error_capacity, "Cannot load signatures from %s; the main database must contain signatures.",
            main_path && main_path[0] ? main_path : "the default main database folder");
        return gui_configuration_failed(GUI_BACKEND_CONFIGURATION_LOAD, GUI_DB_MAIN);
    }
    if (!gui_backend_set_scan_preferences(preferences)) return gui_configuration_failed(GUI_BACKEND_CONFIGURATION_OPTIONS, -1);
    return 1;
}

static const char *gui_loaded_db_path(int nIndex)
{
    const char *pPath = nIndex == GUI_DB_MAIN ? g_options.main_database_path : g_options.custom_database_path;
    return pPath ? pPath : "";
}

/* An empty request matches a default slot, a path matches the same path
 * chosen explicitly: either way, applying it would change nothing. */
static int gui_slot_current(int nIndex, const char *pPath)
{
    if ((pPath == NULL) || (pPath[0] == 0)) return g_bDbDefault[nIndex];
    return !g_bDbDefault[nIndex] && !xx_rt_strcmp(pPath, gui_loaded_db_path(nIndex));
}

int gui_backend_db_path_is_default(int nIndex)
{
    return g_bInited && nIndex >= 0 && nIndex < GUI_DB_COUNT && g_bDbDefault[nIndex];
}

int gui_backend_db_path_moved(int nIndex)
{
    char *pResolved;
    int bMoved;
    if (!gui_backend_db_path_is_default(nIndex)) return 0;
    pResolved = gui_resolve_db(g_dbNames[nIndex]);
    if (!pResolved) return 0;
    bMoved = xx_rt_strcmp(pResolved, gui_loaded_db_path(nIndex)) != 0;
    xx_rt_free(pResolved);
    return bMoved;
}

uint64_t gui_backend_database_generation(void)
{
    return g_database_generation;
}

int gui_backend_db_paths_current(const char *pMain, const char *pCustom)
{
    return g_bInited && gui_slot_current(GUI_DB_MAIN, pMain) && gui_slot_current(GUI_DB_CUSTOM, pCustom);
}

int gui_backend_signature_count(void)
{
    return g_db.nCount;
}

char *gui_backend_used_tags(void)
{
    return db_get_used_tags(g_bInited ? &g_db : NULL);
}

int gui_backend_set_tag_filters(const char *pIncludeTags, const char *pExcludeTags)
{
    char *pNewInclude = NULL;
    char *pNewExclude = NULL;

    if (!g_bInited) {
        return 0;
    }

    if ((pIncludeTags != NULL) && (pIncludeTags[0] != 0)) {
        pNewInclude = cdie_strdup(pIncludeTags);
        if (pNewInclude == NULL) {
            return 0;
        }
    }

    if ((pExcludeTags != NULL) && (pExcludeTags[0] != 0)) {
        pNewExclude = cdie_strdup(pExcludeTags);
        if (pNewExclude == NULL) {
            xx_rt_free(pNewInclude);
            return 0;
        }
    }

    int changed = xx_rt_strcmp(g_pIncludeTags ? g_pIncludeTags : "", pNewInclude ? pNewInclude : "") ||
        xx_rt_strcmp(g_pExcludeTags ? g_pExcludeTags : "", pNewExclude ? pNewExclude : "");
    xx_rt_free(g_pIncludeTags);
    xx_rt_free(g_pExcludeTags);
    g_pIncludeTags = pNewInclude;
    g_pExcludeTags = pNewExclude;
    g_options.include_tags = g_pIncludeTags;
    g_options.exclude_tags = g_pExcludeTags;

    if (changed) options_changed();

    return 1;
}

const char *gui_backend_app_title(void)
{
    return X_APPLICATIONDISPLAYNAME " " X_APPLICATIONVERSION;
}

void gui_backend_set_flags(int bDeep, int bHeuristic, int bVerbose, int bAggressive, int bHideUnknown, int bFormat)
{
    if (g_options.deep_scan != (bDeep != 0) || g_options.heuristic_scan != (bHeuristic != 0) ||
        g_options.verbose != (bVerbose != 0) || g_options.aggressive_scan != (bAggressive != 0) ||
        g_options.hide_unknown != (bHideUnknown != 0) || g_options.format_result != (bFormat != 0)) options_changed();
    g_options.deep_scan = bDeep ? 1 : 0;
    g_options.heuristic_scan = bHeuristic ? 1 : 0;
    g_options.verbose = bVerbose ? 1 : 0;
    g_options.aggressive_scan = bAggressive ? 1 : 0;
    g_options.hide_unknown = bHideUnknown ? 1 : 0;
    g_options.format_result = bFormat ? 1 : 0;
}

void gui_backend_set_databases(int bUseCustom)
{
    if (g_options.use_custom_database != (bUseCustom != 0)) options_changed();
    g_options.use_custom_database = bUseCustom ? 1 : 0;
}

int gui_backend_get_optimization_options(gui_backend_optimization_options *pOptions)
{
    if (!pOptions) return 0;
    pOptions->buffer_size = xx_get_buffer_size();
    pOptions->file_buffer_size = xx_get_file_buffer_size();
    pOptions->use_sse2 = xx_is_sse2_enabled() ? 1 : 0;
    pOptions->use_avx2 = xx_is_avx2_enabled() ? 1 : 0;
    pOptions->use_sse42 = xx_is_sse42_enabled() ? 1 : 0;
    pOptions->use_aes = xx_is_aes_enabled() ? 1 : 0;
    pOptions->use_sha = xx_is_sha_enabled() ? 1 : 0;
    pOptions->use_vpclmulqdq = xx_is_vpclmulqdq_enabled() ? 1 : 0;
    pOptions->use_neon = xx_is_neon_enabled() ? 1 : 0;
    pOptions->use_arm_crc32 = xx_is_arm_crc32_enabled() ? 1 : 0;
    pOptions->use_arm_crypto = xx_is_arm_crypto_enabled() ? 1 : 0;
    pOptions->use_arm_dotprod = xx_is_arm_dotprod_enabled() ? 1 : 0;
    pOptions->use_arm_pmull = xx_is_arm_pmull_enabled() ? 1 : 0;
    return 1;
}

int gui_backend_get_optimization_capabilities(gui_backend_optimization_capabilities *pCapabilities)
{
    if (!pCapabilities) return 0;
    pCapabilities->sse2 = xx_has_sse2() ? 1 : 0;
    pCapabilities->avx2 = xx_has_avx2() ? 1 : 0;
    pCapabilities->sse42 = xx_has_sse42() ? 1 : 0;
    pCapabilities->aes = xx_has_aes() ? 1 : 0;
    pCapabilities->sha = xx_has_sha() ? 1 : 0;
    pCapabilities->vpclmulqdq = xx_has_vpclmulqdq() ? 1 : 0;
    pCapabilities->neon = xx_has_neon() ? 1 : 0;
    pCapabilities->arm_crc32 = xx_has_arm_crc32() ? 1 : 0;
    pCapabilities->arm_crypto = xx_has_arm_crypto() ? 1 : 0;
    pCapabilities->arm_dotprod = xx_has_arm_dotprod() ? 1 : 0;
    pCapabilities->arm_pmull = xx_has_arm_pmull() ? 1 : 0;
    return 1;
}

int gui_backend_set_optimization_options(const gui_backend_optimization_options *pOptions)
{
    gui_backend_optimization_options options;
    if (!pOptions) return 0;
    options = *pOptions;
    if ((options.use_sse2 != 0 && options.use_sse2 != 1) ||
        (options.use_avx2 != 0 && options.use_avx2 != 1) ||
        (options.use_sse42 != 0 && options.use_sse42 != 1) ||
        (options.use_aes != 0 && options.use_aes != 1) ||
        (options.use_sha != 0 && options.use_sha != 1) ||
        (options.use_vpclmulqdq != 0 && options.use_vpclmulqdq != 1) ||
        (options.use_neon != 0 && options.use_neon != 1) ||
        (options.use_arm_crc32 != 0 && options.use_arm_crc32 != 1) ||
        (options.use_arm_crypto != 0 && options.use_arm_crypto != 1) ||
        (options.use_arm_dotprod != 0 && options.use_arm_dotprod != 1) ||
        (options.use_arm_pmull != 0 && options.use_arm_pmull != 1)) return 0;
    xx_set_buffer_size(options.buffer_size);
    xx_set_file_buffer_size(options.file_buffer_size);
    xx_set_sse2_enabled(options.use_sse2 != 0);
    xx_set_avx2_enabled(options.use_avx2 != 0);
    xx_set_sse42_enabled(options.use_sse42 != 0);
    xx_set_aes_enabled(options.use_aes != 0);
    xx_set_sha_enabled(options.use_sha != 0);
    xx_set_vpclmulqdq_enabled(options.use_vpclmulqdq != 0);
    xx_set_neon_enabled(options.use_neon != 0);
    xx_set_arm_crc32_enabled(options.use_arm_crc32 != 0);
    xx_set_arm_crypto_enabled(options.use_arm_crypto != 0);
    xx_set_arm_dotprod_enabled(options.use_arm_dotprod != 0);
    xx_set_arm_pmull_enabled(options.use_arm_pmull != 0);
    options_changed();
    return 1;
}

typedef struct gui_file_types_context {
    gui_backend_file_type_fn pTypeFn;
    void *pUserData;
    int bStarted;
} gui_file_types_context;

/* Broad database categories do not identify separate concrete format readers. */
static int gui_concrete_file_type(XFileType type)
{
    return type != XFT_UNKNOWN && type != XFT_PE && type != XFT_ELF &&
        type != XFT_MACHO && type != XFT_ARCHIVE && type != XFT_IMAGE &&
        type != XFT_CLI_ASSEMBLY;
}

static void gui_emit_file_type(XFileType type, void *pUserData)
{
    gui_file_types_context *pContext = (gui_file_types_context *)pUserData;
    if (!pContext->bStarted) {
        pContext->pTypeFn(0, "Automatic", pContext->pUserData);
        pContext->bStarted = 1;
    }
    if (gui_concrete_file_type(type))
        pContext->pTypeFn((int)type, xft_to_string(type), pContext->pUserData);
}

int gui_backend_file_types(const char *pUtf8Path, gui_backend_file_type_fn pTypeFn, void *pUserData)
{
    gui_file_types_context context;
    if (!g_bInited) return 0;
    context.pTypeFn = pTypeFn;
    context.pUserData = pUserData;
    context.bStarted = 0;
    return die_engine_detect_file_types(pUtf8Path, pTypeFn ? gui_emit_file_type : NULL, &context);
}

int gui_backend_scan(const char *pUtf8Path, char **ppResultText, char **ppTypeName)
{
    return gui_backend_scan_results(pUtf8Path, NULL, NULL, ppResultText, ppTypeName);
}

unsigned int gui_backend_get_scan_modes(void)
{
    return (g_options.all_types_scan ? GUI_SCAN_ALL_TYPES : 0u) |
        (g_options.overlay_scan ? GUI_SCAN_OVERLAYS : 0u) |
        (g_options.resources_scan ? GUI_SCAN_RESOURCES : 0u) |
        (g_options.archives_scan ? GUI_SCAN_ARCHIVES : 0u);
}

int gui_backend_set_scan_modes(unsigned int modes)
{
    if (!g_bInited || (modes & ~GUI_SCAN_MODES_MASK)) return 0;
    if (gui_backend_get_scan_modes() != modes) options_changed();
    g_options.all_types_scan = (modes & GUI_SCAN_ALL_TYPES) != 0;
    g_options.overlay_scan = (modes & GUI_SCAN_OVERLAYS) != 0;
    g_options.resources_scan = (modes & GUI_SCAN_RESOURCES) != 0;
    g_options.archives_scan = (modes & GUI_SCAN_ARCHIVES) != 0;
    return 1;
}

int gui_backend_scan_results(const char *pUtf8Path, gui_backend_result_fn pResultFn,
    void *pUserData, char **ppResultText, char **ppTypeName)
{
    return gui_backend_scan_results_type(pUtf8Path, 0, pResultFn, pUserData, ppResultText, ppTypeName);
}

/* Bits per address for DIE's Mode field; 0 when the architecture says nothing. */
static int arch_bits(xx_arch_t arch)
{
    switch (arch) {
    case XX_ARCH_X86_64: case XX_ARCH_ARM64: case XX_ARCH_MIPS64: case XX_ARCH_PPC64: case XX_ARCH_RISCV64: case XX_ARCH_SPARC64: return 64;
    case XX_ARCH_X86: case XX_ARCH_ARM: case XX_ARCH_MIPS: case XX_ARCH_PPC: case XX_ARCH_RISCV: case XX_ARCH_SPARC:
    case XX_ARCH_M68K: case XX_ARCH_SH: case XX_ARCH_ARC: case XX_ARCH_HPPA: return 32;
    case XX_ARCH_X86_16: case XX_ARCH_Z80: return 16;
    case XX_ARCH_AVR: return 8;
    default: return 0;
    }
}

/* The names DIE gives what the parser found. */
static const char *arch_name(xx_arch_t arch)
{
    switch (arch) {
    case XX_ARCH_X86_16: return "8086";
    case XX_ARCH_X86: return "I386";
    case XX_ARCH_X86_64: return "AMD64";
    case XX_ARCH_ARM: return "ARM";
    case XX_ARCH_ARM64: return "ARM64";
    case XX_ARCH_MIPS: return "MIPS";
    case XX_ARCH_MIPS64: return "MIPS64";
    case XX_ARCH_PPC: return "PPC";
    case XX_ARCH_PPC64: return "PPC64";
    case XX_ARCH_RISCV: return "RISCV";
    case XX_ARCH_RISCV64: return "RISCV64";
    case XX_ARCH_SPARC: return "SPARC";
    case XX_ARCH_SPARC64: return "SPARC64";
    case XX_ARCH_M68K: return "68K";
    case XX_ARCH_UNKNOWN: return "";
    default: return xx_arch_to_string(arch);
    }
}

static const char *type_name(xx_format_type_t type)
{
    switch (type) {
    case XX_TYPE_GUI_APPLICATION: return "GUI";
    case XX_TYPE_CONSOLE_APPLICATION: return "Console";
    case XX_TYPE_LIBRARY: return "DLL";
    default: return xx_type_to_string(type);
    }
}

static void copy_field(char *field, size_t capacity, const char *text)
{
    xx_rt_snprintf(field, capacity, "%s", text && xx_rt_strcmp(text, "Unknown") ? text : "");
}

/* The format parser's view of the main input, as DIE shows it above the
 * tree, for the executable families the engine's readers cover. */
static void describe_input(const char *path, const xx_scan_result *result, gui_backend_scan_info *info)
{
    xx_pe pe;
    xx_elf elf;
    xx_macho macho;
    Abstractformat *format = NULL;
    xx_io_device *device;
    xx_endian_t endian;
    xx_arch_t arch;
    int bits;
    (void)result;
    if (!(device = xx_io_file_open(path, "rb"))) return;
    xx_rt_memset(&pe, 0, sizeof(pe)); xx_rt_memset(&elf, 0, sizeof(elf)); xx_rt_memset(&macho, 0, sizeof(macho));
    xx_pe_init(&pe, device, 0);
    xx_elf_init(&elf, device, 0);
    xx_macho_init(&macho, device, 0);
    {
        /* The first reader that accepts the file. */
        Abstractformat *candidates[3] = {&pe.format, &elf.format, &macho.format};
        for (int i = 0; i < 3 && !format; ++i)
            if (candidates[i]->check_is_valid && candidates[i]->check_is_valid(candidates[i], NULL)) format = candidates[i];
    }
    if (format && (!format->handle_base_info || format->handle_base_info(format, NULL))) {
        endian = format->get_endian ? format->get_endian(format) : format->endian;
        arch = format->get_arch ? format->get_arch(format) : format->arch;
        copy_field(info->endianness, sizeof(info->endianness), endian == XX_ENDIAN_LITTLE ? "LE" : endian == XX_ENDIAN_BIG ? "BE" : "");
        bits = arch_bits(arch);
        if (bits) xx_rt_snprintf(info->mode, sizeof(info->mode), "%d-bit", bits);
        copy_field(info->architecture, sizeof(info->architecture), arch_name(arch));
        copy_field(info->type, sizeof(info->type), type_name(format->get_type ? format->get_type(format) : format->format_type));
    }
    xx_pe_destroy(&pe); xx_elf_destroy(&elf); xx_macho_destroy(&macho);
    xx_io_close(device);
}

static char *join_errors(const xx_scan_result *result)
{
    xx_buf_t log;
    int i;
    xx_buf_init(&log);
    for (i = 0; i < result->nErrorCount; ++i)
        if (result->ppErrors && result->ppErrors[i] && result->ppErrors[i][0]) xx_buf_appendf(&log, "%s\n", result->ppErrors[i]);
    if (!log.size) xx_buf_append_str(&log, "");
    return xx_buf_detach(&log, NULL);
}

static int input_identity(const char *path, uint8_t digest[XX_BLAKE2SP_DIGEST_SIZE], int64_t *size, xx_pd_struct *progress)
{
    xx_io_device *device = xx_io_file_open(path, "rb");
    int ok = 0;
    if (!device) return 0;
    *size = xx_io_total_size(device);
    if (*size >= 0) ok = xx_blake2sp_calc_device(device, 0, *size, digest, progress);
    xx_io_close(device);
    return ok;
}

static int copy_scan_result(xx_scan_result *copy, const xx_scan_result *source)
{
    xx_rt_memset(copy, 0, sizeof(*copy));
    copy->sort = source->sort;
    if (!xx_scan_append_result(NULL, copy, source, 0, NULL)) goto failed;
    if (source->pFileName && !(copy->pFileName = cdie_strdup(source->pFileName))) goto failed;
    return 1;
failed:
    xx_scan_result_free(copy);
    return 0;
}

static int cached_result(const char *path, int type_id, const uint8_t digest[XX_BLAKE2SP_DIGEST_SIZE], int64_t size,
                         xx_scan_result *result)
{
    int found = 0;
    if (!g_cache_lock) return 0;
    xx_mutex_lock(g_cache_lock);
    if (g_cache.valid && g_cache.type_id == type_id && g_cache.size == size &&
        g_cache.options_generation == g_options_generation && g_cache.database_generation == g_database_generation &&
        !xx_rt_strcmp(path, g_cache.path) && !xx_rt_memcmp(digest, g_cache.digest, XX_BLAKE2SP_DIGEST_SIZE))
        found = copy_scan_result(result, &g_cache.result);
    if (found) ++g_cache.hits; else ++g_cache.misses;
    xx_mutex_unlock(g_cache_lock);
    return found;
}

static void save_cached_result(const char *path, int type_id, const uint8_t digest[XX_BLAKE2SP_DIGEST_SIZE], int64_t size,
                              const xx_scan_result *result)
{
    char *copy_path;
    xx_scan_result copy;
    if (!g_cache_lock || !(copy_path = cdie_strdup(path))) return;
    if (!copy_scan_result(&copy, result)) { xx_rt_free(copy_path); return; }
    xx_mutex_lock(g_cache_lock);
    gui_backend_clear_cache();
    g_cache.path = copy_path;
    g_cache.result = copy;
    g_cache.type_id = type_id;
    g_cache.size = size;
    xx_rt_memcpy(g_cache.digest, digest, XX_BLAKE2SP_DIGEST_SIZE);
    g_cache.options_generation = g_options_generation;
    g_cache.database_generation = g_database_generation;
    g_cache.valid = 1;
    xx_mutex_unlock(g_cache_lock);
}

static int scan_configured(const char *path, int type_id, xx_scan_options *options, xx_scan_result *result, xx_pd_struct *progress)
{
    int ok;
    xx_scan_options effective = *options;
    long long started = xx_rt_clock_ms();
    /* The legacy engine strips recursive_scan and its low-level callbacks
     * reject it. Expose the supported native equivalent explicitly in Options:
     * recursive requests both resource and overlay child scans. */
    if (effective.recursive_scan) effective.overlay_scan = effective.resources_scan = true;
    effective.recursive_scan = false;
    /* The low-level per-script profiler writes to stdout. A GUI scan instead
     * publishes aggregate measured work in its Log, also for expanded scans. */
    effective.profiling = false;
    ok = die_engine_scan_file_type_pd(path, &g_db, &effective, (XFileType)type_id, result, progress);
    if (ok && options->profiling) {
        char line[192];
        xx_rt_snprintf(line, sizeof(line), "[Profiling] Detect It Easy: %d detection records, %lld ms total scan time.",
            result->nCount, xx_rt_clock_ms() - started);
        ok = xx_scan_result_add_error(result, line, progress);
    }
    return ok;
}

static int scan_results_impl(const char *pUtf8Path, int nTypeId,
    gui_backend_result_fn pResultFn, void *pUserData, char **ppResultText, char **ppTypeName, gui_backend_tree_fn tree_fn,
    gui_backend_scan_info *pInfo, xx_pd_struct *pProgress)
{
    xx_scan_result result;
    char *pText = NULL;
    char *pTypeName = NULL;
    int i;
    uint8_t digest[XX_BLAKE2SP_DIGEST_SIZE];
    int64_t input_size = 0;
    int identity_valid = 0, hit = 0;

    if (ppResultText != NULL) {
        *ppResultText = NULL;
    }

    if (ppTypeName != NULL) {
        *ppTypeName = NULL;
    }

    if ((!g_bInited) || (pUtf8Path == NULL) || (pUtf8Path[0] == 0) ||
        nTypeId < 0 || nTypeId >= XFT_COUNT ||
        (nTypeId != 0 && !gui_concrete_file_type((XFileType)nTypeId))) {
        return 0;
    }

    xx_scan_options options = g_options;
    xx_rt_memset(&result, 0, sizeof(result));
    if (g_scan_preferences.use_cache) {
        identity_valid = input_identity(pUtf8Path, digest, &input_size, pProgress);
        if (xx_pd_is_stopped(pProgress)) return 0;
        if (identity_valid) hit = cached_result(pUtf8Path, nTypeId, digest, input_size, &result);
    }
    if (!hit && !scan_configured(pUtf8Path, nTypeId, &options, &result, pProgress)) {
        xx_scan_result_free(&result);
        return 0;
    }
    if (!hit && identity_valid && !xx_pd_is_stopped(pProgress)) {
        uint8_t after[XX_BLAKE2SP_DIGEST_SIZE];
        int64_t after_size = 0;
        if (input_identity(pUtf8Path, after, &after_size, pProgress) && input_size == after_size &&
            !xx_rt_memcmp(digest, after, sizeof(digest))) save_cached_result(pUtf8Path, nTypeId, digest, input_size, &result);
    }
    if (xx_pd_is_stopped(pProgress)) { xx_scan_result_free(&result); return 0; }

    if (ppResultText != NULL) {
        pText = xx_scan_format_text(&result, &g_options);
    }

    if (ppTypeName != NULL) {
        pTypeName = cdie_strdup(xft_to_string(result.fileType));
    }
    if ((ppResultText && !pText) || (ppTypeName && !pTypeName)) {
        xx_rt_free(pText);
        xx_rt_free(pTypeName);
        xx_scan_result_free(&result);
        return 0;
    }

    if (tree_fn && !gui_result_tree_emit(&result, g_options.hide_unknown, tree_fn, pUserData))
        goto scan_allocation_failed;
    if (pInfo) {
        describe_input(pUtf8Path, &result, pInfo);
        pInfo->log = join_errors(&result);
        if (!pInfo->log) goto scan_allocation_failed;
    }

    for (i = 0; pResultFn && i < result.nCount; ++i) {
        xx_scan_record *pRecord = &result.pRecords[i];
        char *pType;
        char *pDisplayType;
        const char *pMarker;
        size_t nMarkerLength;
        size_t nLength;
        if (pRecord->is_unknown && g_options.hide_unknown) continue;
        pType = xx_scan_translate_type(pRecord->type ? pRecord->type : "");
        pMarker = pRecord->is_heuristic ? "(Heur) " : pRecord->is_aggressive_heuristic ? "(A-Heur) " : "";
        if (!pType) goto scan_allocation_failed;
        nMarkerLength = xx_rt_strlen(pMarker);
        nLength = nMarkerLength + xx_rt_strlen(pType) + 1;
        pDisplayType = (char *)xx_rt_malloc(nLength);
        if (!pDisplayType) { xx_rt_free(pType); goto scan_allocation_failed; }
        xx_rt_memcpy(pDisplayType, pMarker, nMarkerLength);
        xx_rt_memcpy(pDisplayType + nMarkerLength, pType, nLength - nMarkerLength);
        pResultFn(pDisplayType, pRecord->name ? pRecord->name : "",
            pRecord->version ? pRecord->version : "", pRecord->info ? pRecord->info : "", pUserData);
        xx_rt_free(pDisplayType);
        xx_rt_free(pType);
    }

    if (ppResultText) *ppResultText = pText;
    if (ppTypeName) *ppTypeName = pTypeName;
    xx_scan_result_free(&result);
    return 1;

scan_allocation_failed:
    xx_rt_free(pText);
    xx_rt_free(pTypeName);
    xx_scan_result_free(&result);
    return 0;
}

static const DBSignature *find_signature(const char *signature)
{
    int i;
    if (!g_bInited || !signature || !signature[0]) return NULL;
    for (i = 0; i < g_db.nCount; ++i) {
        const DBSignature *record = &g_db.pRecords[i];
        const char *path = record->pFilePath && record->pFilePath[0] ? record->pFilePath : record->pName;
        if (path && !xx_rt_strcmp(path, signature)) return record;
    }
    return NULL;
}

char *gui_backend_signature_source(const char *signature)
{
    const DBSignature *record = find_signature(signature);
    char *source;
    if (!record || !record->pText || js_is_bytecode(record->pText, record->nSize)) return NULL;
    source = (char *)xx_rt_malloc(record->nSize + 1);
    if (!source) return NULL;
    xx_rt_memcpy(source, record->pText, record->nSize);
    source[record->nSize] = 0;
    return source;
}

static void append_log(const char *text, void *user)
{
    xx_buf_append_str((xx_buf_t *)user, text);
}

int gui_backend_run_signature(const char *path, int type_id, const char *signature, const char *source, char **text)
{
    xx_scan_options options;
    xx_scan_result result;
    die_engine_debug_config config;
    xx_buf_t log, out;
    char *detected;
    int ok, i;
    if (text) *text = NULL;
    if (!g_bInited || !path || !path[0] || !source || !text || type_id < 0 || type_id >= XFT_COUNT ||
        (type_id && !gui_concrete_file_type((XFileType)type_id)))
        return 0;
    xx_buf_init(&log);
    xx_buf_init(&out);
    xx_rt_memset(&config, 0, sizeof(config));
    config.size = sizeof(config);
    config.version = DIE_ENGINE_DEBUG_VERSION;
    config.log = append_log;
    config.log_user = &log;
    config.signature_source = source;
    config.signature_name = signature;
    /* One input, as the debugger requires: no child scans of its parts. */
    options = g_options;
    options.recursive_scan = false;
    options.all_types_scan = false;
    options.overlay_scan = false;
    options.resources_scan = false;
    options.archives_scan = false;
    ok = die_engine_scan_file_type_debug_pd(path, &g_db, &options, (XFileType)type_id, &result, NULL, &config);
    detected = ok ? xx_scan_format_text(&result, &options) : NULL;
    if (detected) { xx_buf_append_str(&out, detected); xx_rt_free(detected); }
    for (i = 0; i < result.nErrorCount; ++i)
        if (result.ppErrors && result.ppErrors[i]) xx_buf_appendf(&out, "%s\n", result.ppErrors[i]);
    if (log.size) {
        if (out.size && out.data[out.size - 1] != '\n') xx_buf_append_str(&out, "\n");
        xx_buf_append(&out, log.data, log.size);
    }
    xx_scan_result_free(&result);
    xx_buf_free(&log);
    *text = xx_buf_detach(&out, NULL);
    return ok;
}

/* <dir>/info/html/<name>.html for the directory the main database is in, the
 * executable's and, in a macOS bundle, Resources. */
static char *read_info(const char *dir, const char *file)
{
    char *info = xx_fs_path_join(dir, "info"), *html = info ? xx_fs_path_join(info, "html") : NULL;
    char *path = html ? xx_fs_path_join(html, file) : NULL, *data = NULL;
    if (path && xx_fs_is_file(path)) data = xx_fs_read_file(path, NULL);
    xx_rt_free(info); xx_rt_free(html); xx_rt_free(path);
    return data;
}

int gui_backend_set_info_path(const char *path)
{
    char *copy = path && path[0] ? cdie_strdup(path) : NULL;
    if (path && path[0] && !copy) return 0;
    xx_rt_free(g_info_path); g_info_path = copy;
    return 1;
}

static char *read_configured_info(const char *file)
{
    char *directory, *html, *path, *data = NULL;
    if (!g_info_path || !g_info_path[0]) return NULL;
    if (!xx_rt_strncmp(g_info_path, "$data", 5) && (!g_info_path[5] || xx_fs_is_separator(g_info_path[5]))) {
        char *app = cdie_app_dir();
        directory = app ? xx_fs_path_join(app, g_info_path + (g_info_path[5] ? 6 : 5)) : NULL;
        xx_rt_free(app);
    } else directory = cdie_strdup(g_info_path);
    html = directory ? xx_fs_path_join(directory, "html") : NULL;
    path = html ? xx_fs_path_join(html, file) : NULL;
    if (path && xx_fs_is_file(path)) data = xx_fs_read_file(path, NULL);
    xx_rt_free(path);
    if (!data && directory) {
        path = xx_fs_path_join(directory, file);
        if (path && xx_fs_is_file(path)) data = xx_fs_read_file(path, NULL);
        xx_rt_free(path);
    }
    xx_rt_free(html); xx_rt_free(directory);
    return data;
}

/* The descriptions are short HTML or plain text: drop tags, keep paragraph
 * breaks and decode the common entities. */
static char *info_plain_text(const char *html)
{
    xx_buf_t out;
    const char *s = html;
    xx_buf_init(&out);
    while (*s) {
        if (*s == '<') {
            const char *end = s + 1;
            while (*end && *end != '>') ++end;
            if ((end - s >= 3 && (s[1] == 'b' || s[1] == 'B') && (s[2] == 'r' || s[2] == 'R')) ||
                (end - s >= 2 && (s[1] == 'p' || s[1] == 'P')) || (end - s >= 3 && s[1] == '/' && (s[2] == 'p' || s[2] == 'P')))
                xx_buf_append_str(&out, "\n");
            s = *end ? end + 1 : end;
        } else if (*s == '&') {
            static const struct { const char *entity, *text; } entities[] = {
                {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&nbsp;", " "}};
            size_t k;
            for (k = 0; k < sizeof(entities) / sizeof(entities[0]); ++k) {
                size_t length = xx_rt_strlen(entities[k].entity);
                if (!xx_rt_strncmp(s, entities[k].entity, length)) { xx_buf_append_str(&out, entities[k].text); s += length; break; }
            }
            if (k == sizeof(entities) / sizeof(entities[0])) { xx_buf_append_char(&out, *s); ++s; }
        } else if (*s != '\r') {
            xx_buf_append_char(&out, *s); ++s;
        } else ++s;
    }
    return xx_buf_detach(&out, NULL);
}

char *gui_backend_info_text(const char *name)
{
    char *file, *data = NULL, *text;
    const char *s;
    if (!name || !name[0]) return NULL;
    /* A name is a file name here, never a path. */
    for (s = name; *s; ++s)
        if (*s == '/' || *s == '\\' || *s == ':' || (unsigned char)*s < 32) return NULL;
    if (!xx_rt_strcmp(name, ".") || !xx_rt_strcmp(name, "..")) return NULL;
    file = (char *)xx_rt_malloc(xx_rt_strlen(name) + 6);
    if (!file) return NULL;
    xx_rt_memcpy(file, name, xx_rt_strlen(name));
    xx_rt_memcpy(file + xx_rt_strlen(name), ".html", 6);
    data = read_configured_info(file);
    if (!data && g_options.main_database_path) {
        char *parent = xx_fs_path_join(g_options.main_database_path, "..");
        if (parent) data = read_info(parent, file);
        xx_rt_free(parent);
    }
    if (!data) {
        char *app = cdie_app_dir();
        if (app) data = read_info(app, file);
#if defined(__APPLE__)
        if (!data && app) {
            char *resources = xx_fs_path_join(app, "../Resources");
            if (resources) data = read_info(resources, file);
            xx_rt_free(resources);
        }
#endif
        xx_rt_free(app);
    }
    xx_rt_free(file);
    if (!data) return NULL;
    text = info_plain_text(data);
    xx_str_free(data);
    return text;
}

int gui_backend_scan_results_type(const char *path, int type_id, gui_backend_result_fn callback,
    void *user, char **report, char **type_name)
{
    return scan_results_impl(path, type_id, callback, user, report, type_name, NULL, NULL, NULL);
}

int gui_backend_scan_tree(const char *path, int type_id, gui_backend_tree_fn callback,
    void *user, char **report, char **type_name, gui_backend_scan_info *info, xx_pd_struct *progress)
{
    if (info) xx_rt_memset(info, 0, sizeof(*info));
    if (!callback) return 0;
    return scan_results_impl(path, type_id, NULL, user, report, type_name, callback, info, progress);
}

int gui_backend_scan_detections(const char *path, unsigned int flags, unsigned int databases,
    gui_backend_detection_fn callback, void *user)
{
    xx_scan_options options;
    xx_scan_result result;
    int i;
    if (!g_bInited || !path || !path[0] || !callback) return 0;
    options = g_options;
    options.deep_scan = (flags & 1u) != 0;
    options.heuristic_scan = (flags & 2u) != 0;
    options.verbose = (flags & 4u) != 0;
    options.aggressive_scan = (flags & 8u) != 0;
    options.hide_unknown = (flags & 16u) != 0;
    options.use_custom_database = (databases & GUI_SCAN_DATABASE_CUSTOM) != 0;
    if (!scan_configured(path, XFT_UNKNOWN, &options, &result, NULL)) {
        xx_scan_result_free(&result);
        return 0;
    }
    for (i = 0; i < result.nCount; ++i) {
        const xx_scan_record *record = &result.pRecords[i];
        if (record->is_unknown && options.hide_unknown) continue;
        callback(record->type ? record->type : "", record->name ? record->name : "",
            record->version ? record->version : "", record->info ? record->info : "", user);
    }
    xx_scan_result_free(&result);
    return 1;
}

int gui_backend_signature_entry(int index, const char **display, const char **path)
{
    const DBSignature *record;
    const char *full, *base, *folder, *s;
    if (!g_bInited || index < 0 || index >= g_db.nCount) return 0;
    record = &g_db.pRecords[index];
    full = record->pFilePath && record->pFilePath[0] ? record->pFilePath : record->pName;
    if (path) *path = full;
    if (display) {
        /* The folder and file name: "PE/_Microsoft.6.sg". */
        base = folder = full;
        for (s = full; *s; ++s)
            if (*s == '/' || *s == '\\') { folder = base; base = s + 1; }
        *display = folder;
    }
    return 1;
}

/* ------------------------------------------------------------ file facts */

/* What the Advanced view shows, as Qt DIE's FormatsWidget::reload() fills it:
 * the page and its values come from the selected file type's own reader. */

#define FACTS_MAX_TYPES 64
typedef struct facts_types {
    int wanted, found, first;
    int count, list[FACTS_MAX_TYPES]; /* the concrete types, in detection order */
} facts_types;

static void facts_type(XFileType type, void *user)
{
    facts_types *types = (facts_types *)user;
    if (!gui_concrete_file_type(type)) return;
    if (!types->first) types->first = (int)type;
    if (types->count < FACTS_MAX_TYPES) types->list[types->count++] = (int)type;
    if ((int)type == types->wanted) types->found = 1;
}

static int facts_page(int type)
{
    switch (type) {
    case XFT_ZIP: case XFT_JAR: case XFT_APK: case XFT_IPA: case XFT_NPM: case XFT_RAR: case XFT_ISO9660:
        return GUI_FACTS_PAGE_ARCHIVE;
    case XFT_DEX: return GUI_FACTS_PAGE_DEX;
    case XFT_MSDOS: return GUI_FACTS_PAGE_MSDOS;
    case XFT_NE: return GUI_FACTS_PAGE_NE;
    case XFT_LE: case XFT_LX: return GUI_FACTS_PAGE_LE;
    case XFT_PE32: case XFT_PE64: return GUI_FACTS_PAGE_PE;
    case XFT_ELF32: case XFT_ELF64: return GUI_FACTS_PAGE_ELF;
    case XFT_MACHO32: case XFT_MACHO64: return GUI_FACTS_PAGE_MACH;
    case XFT_COM: return GUI_FACTS_PAGE_COM;
    default: return GUI_FACTS_PAGE_BINARY;
    }
}

/* The reader of the other types that have one in the engine's link closure
 * (xx_format_create_reader is outside it); Binary and the rest have none.
 * Every reader struct starts with its Abstractformat. */
static void facts_free_ne(void *reader) { xx_ne_free((xx_ne *)reader); }
static void facts_free_le(void *reader) { xx_le_free((xx_le *)reader); }
static void facts_free_lx(void *reader) { xx_lx_free((xx_lx *)reader); }
static void facts_free_dex(void *reader) { xx_dex_free((xx_dex *)reader); }
static void facts_free_amigahunk(void *reader) { xx_amigahunk_free((xx_amigahunk *)reader); }
static void facts_free_zip(void *reader) { xx_zip_free((xx_zip *)reader); }
static void facts_free_jar(void *reader) { xx_jar_free((xx_jar *)reader); }
static void facts_free_apk(void *reader) { xx_apk_free((xx_apk *)reader); }
static void facts_free_ipa(void *reader) { xx_ipa_free((xx_ipa *)reader); }
static void facts_free_rar(void *reader) { xx_rar_free((xx_rar *)reader); }
static Abstractformat *facts_reader(int type, xx_io_device *device, void (**release)(void *))
{
    void *reader = NULL;
    switch (type) {
    case XFT_NE: reader = xx_ne_create(device, 0); *release = facts_free_ne; break;
    case XFT_LE: reader = xx_le_create(device, 0); *release = facts_free_le; break;
    case XFT_LX: reader = xx_lx_create(device, 0); *release = facts_free_lx; break;
    case XFT_DEX: reader = xx_dex_create(device, 0); *release = facts_free_dex; break;
    case XFT_AMIGAHUNK: reader = xx_amigahunk_create(device, 0); *release = facts_free_amigahunk; break;
    case XFT_ZIP: reader = xx_zip_create(device, 0); *release = facts_free_zip; break;
    case XFT_JAR: reader = xx_jar_create(device, 0); *release = facts_free_jar; break;
    case XFT_APK: reader = xx_apk_create(device, 0); *release = facts_free_apk; break;
    case XFT_IPA: reader = xx_ipa_create(device, 0); *release = facts_free_ipa; break;
    case XFT_RAR: reader = xx_rar_create(device, 0); *release = facts_free_rar; break;
    default: break;
    }
    return (Abstractformat *)reader;
}

/* Whether the type's own reader accepts the file; types without one here
 * pass. The detector can name a type its reader rejects (an 80-byte NE or LE
 * header: Qt DIE lists only Binary and MSDOS there), and the automatic type
 * then moves on, as Qt's does. */
static int facts_type_valid(int type, xx_io_device *device)
{
    void (*release)(void *) = NULL;
    Abstractformat *format = facts_reader(type, device, &release);
    int valid;
    if (!format) return 1;
    valid = xx_format_is_valid(format, NULL) ? 1 : 0;
    release(format);
    return valid;
}

/* XBinary::getWidthModeFromSize: the address width of a file without a mode. */
static unsigned int facts_size_bits(uint64_t size)
{
    return size >= 0xFFFFFFFFu ? 64 : size >= 0xFFFFu ? 32 : size >= 0xFFu ? 16 : 8;
}

/* The engine row, filled the way describe_input fills gui_backend_scan_info. */
static void facts_engine_row(gui_backend_facts *facts, xx_endian_t endian, xx_arch_t arch, xx_format_type_t type)
{
    int bits = arch_bits(arch);
    copy_field(facts->endianness, sizeof(facts->endianness), endian == XX_ENDIAN_LITTLE ? "LE" : endian == XX_ENDIAN_BIG ? "BE" : "");
    if (bits) xx_rt_snprintf(facts->mode, sizeof(facts->mode), "%d-bit", bits);
    copy_field(facts->architecture, sizeof(facts->architecture), arch_name(arch));
    copy_field(facts->type, sizeof(facts->type), type_name(type));
}

static void facts_format_row(gui_backend_facts *facts, Abstractformat *format)
{
    facts_engine_row(facts, format->get_endian ? format->get_endian(format) : format->endian,
        format->get_arch ? format->get_arch(format) : format->arch,
        format->get_type ? format->get_type(format) : format->format_type);
}

static void facts_entry_offset(gui_backend_facts *facts, const xx_memory_map *map)
{
    int64_t offset = map ? xx_memory_map_address_to_offset(map, facts->entry_point) : -1;
    if (offset >= 0) { facts->entry_point_offset = offset; facts->flags |= GUI_FACTS_ENTRY_OFFSET; }
}

/* XBinary::valueToTimeString(DT_TYPE_POSIX): a QDateTime in local time. */
static int facts_localtime(int64_t seconds, struct tm *out)
{
    time_t value = (time_t)seconds;
    if ((int64_t)value != seconds) return 0;
#ifdef _WIN32
    return localtime_s(out, &value) == 0;
#else
    return localtime_r(&value, out) != NULL;
#endif
}

/* The stamp is unsigned 32-bit seconds, up to 2106. Where time_t is 32-bit
 * the local offset is taken 84 years earlier (same weekday and leap cycle)
 * and the date computed here (days_from_civil inverse, H. Hinnant). */
static void facts_time(char *text, size_t capacity, uint32_t stamp)
{
    struct tm local;
    int64_t value = (int64_t)stamp;
    if (!facts_localtime(value, &local)) {
        const int64_t shift = 2650838400; /* 84 years */
        int64_t shifted = value - shift, days, offset, era, doe, yoe, doy, mp, y, m, d, secs;
        if (!facts_localtime(shifted, &local)) return;
        y = local.tm_year + 1900; m = local.tm_mon + 1; d = local.tm_mday;
        y -= m <= 2; era = (y >= 0 ? y : y - 399) / 400; yoe = y - era * 400;
        doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        days = era * 146097 + doe - 719468;
        offset = days * 86400 + local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec - shifted;
        value += offset;
        days = (value >= 0 ? value : value - 86399) / 86400; secs = value - days * 86400;
        days += 719468; era = (days >= 0 ? days : days - 146096) / 146097; doe = days - era * 146097;
        yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        mp = (5 * doy + 2) / 153; d = doy - (153 * mp + 2) / 5 + 1; m = mp < 10 ? mp + 3 : mp - 9; y = yoe + era * 400 + (m <= 2);
        local.tm_year = (int)(y - 1900); local.tm_mon = (int)(m - 1); local.tm_mday = (int)d;
        local.tm_hour = (int)(secs / 3600); local.tm_min = (int)(secs / 60 % 60); local.tm_sec = (int)(secs % 60);
    }
    xx_rt_snprintf(text, capacity, "%04d-%02d-%02d %02d:%02d:%02d", local.tm_year + 1900, local.tm_mon + 1,
        local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);
}

static char *facts_strdup(const char *text)
{
    size_t size = xx_rt_strlen(text) + 1;
    char *copy = (char *)xx_rt_malloc(size);
    if (copy) xx_rt_memcpy(copy, text, size);
    return copy;
}

/* The first resource of a type decides, as XPE::isResourcePresent(id, -1). */
static int facts_resource(const xx_pe_inspection *pe, uint32_t type)
{
    int i;
    for (i = 0; i < pe->nResourceCount; ++i)
        if (pe->pResources[i].nTypeId == type) return pe->pResources[i].nSize != 0;
    return 0;
}

static int facts_pe(gui_backend_facts *facts, xx_io_device *device)
{
    xx_pe_inspection pe;
    xx_pe reader;
    int ok, i;
    xx_rt_memset(&pe, 0, sizeof(pe));
    ok = xx_pe_inspect_analyze_from_device(&pe, device, 0, NULL) && pe.bValid;
    if (ok) {
        facts->base_bits = facts->entry_bits = pe.bIs64 ? 64 : 32;
        facts->base_address = pe.map.module_address != XX_INVALID_ADDRESS ? pe.map.module_address : pe.nImageBase;
        facts->entry_point = pe.nEntryPointAddress != XX_INVALID_ADDRESS ? pe.nEntryPointAddress : facts->base_address + pe.nAddressOfEntryPoint;
        if (pe.nEntryPointOffset >= 0) { facts->entry_point_offset = pe.nEntryPointOffset; facts->flags |= GUI_FACTS_ENTRY_OFFSET; }
        facts->sections = pe.nNumberOfSections;
        facts->time_date_stamp = pe.nTimeDateStamp;
        facts_time(facts->time_date_stamp_text, sizeof(facts->time_date_stamp_text), pe.nTimeDateStamp);
        facts->size_of_image = pe.nSizeOfImage;
        /* XPE::isOptionalHeader_DataDirectoryPresent tests the RVA alone. */
        if (pe.pDirRVA[XX_PE_INSPECT_DIR_EXPORT]) facts->flags |= GUI_FACTS_EXPORTS;
        if (pe.pDirRVA[XX_PE_INSPECT_DIR_IMPORT]) facts->flags |= GUI_FACTS_IMPORTS;
        if (pe.pDirRVA[XX_PE_INSPECT_DIR_RESOURCE]) facts->flags |= GUI_FACTS_RESOURCES;
        if (pe.pDirRVA[XX_PE_INSPECT_DIR_TLS]) facts->flags |= GUI_FACTS_TLS;
        if (pe.pDirRVA[14]) facts->flags |= GUI_FACTS_DOTNET; /* IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR */
        if ((facts->flags & GUI_FACTS_DOTNET) && (pe.nTimeDateStamp & 0x80000000u)) {
            facts->net_id = pe.nTimeDateStamp;
            facts->flags |= GUI_FACTS_NET_ID;
        }
        if (pe.nNumberOfSections) facts->flags |= GUI_FACTS_SECTIONS;
        if (pe.nOverlayOffset >= 0 && pe.nOverlaySize > 0) {
            facts->overlay_offset = pe.nOverlayOffset; facts->overlay_size = pe.nOverlaySize;
            facts->flags |= GUI_FACTS_OVERLAY;
        }
        if (facts_resource(&pe, 24)) { /* RT_MANIFEST */
            facts->flags |= GUI_FACTS_MANIFEST;
            facts->manifest = facts_strdup(pe.pManifest ? pe.pManifest : "");
        }
        if (facts_resource(&pe, 16)) { /* RT_VERSION */
            xx_buf_t text;
            facts->flags |= GUI_FACTS_VERSION;
            xx_buf_init(&text);
            for (i = 0; i < pe.nVersionCount; ++i)
                xx_buf_appendf(&text, "%s: %s\n", pe.pVersionRecords[i].pKey ? pe.pVersionRecords[i].pKey : "",
                    pe.pVersionRecords[i].pValue ? pe.pVersionRecords[i].pValue : "");
            if (!text.size) xx_buf_append_str(&text, "");
            facts->version = xx_buf_detach(&text, NULL);
        }
        facts->header_selector = "IMAGE_FILE_HEADER";
        facts->sections_selector = "IMAGE_SECTION_HEADER";
        facts->tls_selector = pe.bIs64 ? "IMAGE_TLS_DIRECTORY64" : "IMAGE_TLS_DIRECTORY32";
        /* The engine row exactly as describe_input reads it. */
        xx_rt_memset(&reader, 0, sizeof(reader));
        xx_pe_init(&reader, device, 0);
        if (reader.format.check_is_valid(&reader.format, NULL) && xx_format_handle_base_info(&reader.format, NULL))
            facts_format_row(facts, &reader.format);
        xx_pe_destroy(&reader);
    }
    xx_pe_inspect_free(&pe);
    return ok;
}

static int facts_elf(gui_backend_facts *facts, xx_io_device *device)
{
    xx_elf elf;
    int ok;
    xx_rt_memset(&elf, 0, sizeof(elf));
    xx_elf_init(&elf, device, 0);
    ok = elf.format.check_is_valid(&elf.format, NULL) && xx_format_handle_base_info(&elf.format, NULL);
    if (ok) {
        const xx_memory_map *map = xx_format_get_memory_map(&elf.format, XX_MEMORY_MAP_MODE_UNKNOWN, NULL);
        int is_64 = xx_elf_is_64(&elf);
        uint64_t first_load = 0, i;
        int have_load = 0;
        facts_format_row(facts, &elf.format);
        facts->base_bits = facts->entry_bits = is_64 ? 64 : 32;
        /* XELF::getMemoryMap loads the image at module address 0 (XBinary's
         * base) and moves the lowest PT_LOAD there: the first one's p_vaddr,
         * lowered by each one's p_vaddr aligned down to its p_align
         * (S_ALIGN_DOWN64 mask, p_align <= 1 meaning 1), over the first 1000
         * program headers. Qt DIE shows base 0 and the entry relative to that
         * address. The file offset uses e_entry. */
        for (i = 0; i < xx_elf_get_number_of_program_headers(&elf) && i < 1000u; ++i) {
            const xx_elf_program_header *header = xx_elf_get_program_header(&elf, i);
            if (header && header->type == 1u) { /* PT_LOAD */
                uint64_t align = header->alignment > 1u ? header->alignment : 1u;
                uint64_t aligned = header->virtual_address & ~(align - 1u);
                if (!have_load) { first_load = header->virtual_address; have_load = 1; }
                if (aligned < first_load) first_load = aligned;
            }
        }
        facts->entry_point = xx_elf_get_entry_point(&elf);
        facts_entry_offset(facts, map);
        facts->entry_point -= first_load;
        if (!is_64) facts->entry_point &= 0xFFFFFFFFu;
        facts->programs = (uint32_t)xx_elf_get_number_of_program_headers(&elf);
        facts->sections = (uint32_t)xx_elf_get_number_of_section_headers(&elf);
        if (facts->programs) facts->flags |= GUI_FACTS_PROGRAMS;
        if (facts->sections) facts->flags |= GUI_FACTS_SECTIONS;
        facts->header_selector = is_64 ? "elf_header64" : "elf_header32";
        facts->programs_selector = is_64 ? "program_header64" : "program_header32";
        facts->sections_selector = is_64 ? "section_header64" : "section_header32";
    }
    xx_elf_destroy(&elf);
    return ok;
}

/* XMACH::getLibraryRecords: the LC_LOAD_DYLIB commands, which the reader
 * validated but does not keep. */
static uint32_t facts_macho_libraries(xx_macho *macho)
{
    int big = macho->format.endian == XX_ENDIAN_BIG;
    int64_t offset = macho->format.base_address + (macho->is_64 ? 32 : 28);
    int64_t end = offset + (int64_t)macho->commands_size;
    uint32_t i, count = 0;
    for (i = 0; i < macho->command_count && end - offset >= 8; ++i) {
        uint32_t command = xx_io_get_u32(macho->format.device, offset, big != 0);
        uint32_t size = xx_io_get_u32(macho->format.device, offset + 4, big != 0);
        if (size < 8 || (int64_t)size > end - offset) break;
        if (command == 0xCu) ++count;
        offset += size;
    }
    return count;
}

static int facts_macho(gui_backend_facts *facts, xx_io_device *device)
{
    xx_macho macho;
    int ok;
    uint32_t i;
    xx_rt_memset(&macho, 0, sizeof(macho));
    xx_macho_init(&macho, device, 0);
    ok = macho.format.check_is_valid(&macho.format, NULL) && xx_format_handle_base_info(&macho.format, NULL);
    if (ok) {
        const xx_memory_map *map = xx_format_get_memory_map(&macho.format, XX_MEMORY_MAP_MODE_UNKNOWN, NULL);
        int is_64 = xx_macho_is_64(&macho);
        facts_format_row(facts, &macho.format);
        facts->base_bits = facts->entry_bits = is_64 ? 64 : 32;
        /* XMACH::getBaseAddress: the size of a leading segment without flags
         * (__PAGEZERO), which is where __TEXT starts in an executable. */
        if (macho.segment_count && !macho.segments[0].flags) facts->base_address = macho.segments[0].virtual_size;
        facts->entry_point = map && map->entry_point_address != XX_INVALID_ADDRESS ? map->entry_point_address : 0;
        facts_entry_offset(facts, map);
        facts->commands = macho.command_count;
        facts->segments = macho.segment_count;
        /* XMACH::getSectionRecords ignores an implausible nsects. */
        for (i = 0; i < macho.segment_count; ++i)
            if (!(macho.segments[i].section_count & 0xFFFFFF00u)) facts->sections += macho.segments[i].section_count;
        facts->libraries = facts_macho_libraries(&macho);
        if (facts->commands) facts->flags |= GUI_FACTS_COMMANDS;
        if (facts->segments) facts->flags |= GUI_FACTS_SEGMENTS;
        if (facts->sections) facts->flags |= GUI_FACTS_SECTIONS;
        if (facts->libraries) facts->flags |= GUI_FACTS_LIBRARIES;
        facts->header_selector = is_64 ? "mach_header_64" : "mach_header";
        facts->commands_selector = "load_command";
        facts->segments_selector = is_64 ? "segment_command_64" : "segment_command";
        facts->sections_selector = is_64 ? "section_64" : "section";
        facts->libraries_selector = "dylib_command";
    }
    xx_macho_destroy(&macho);
    return ok;
}

static void facts_map_overlay(gui_backend_facts *facts, const xx_memory_map *map)
{
    size_t i;
    for (i = 0; map && i < map->record_count; ++i)
        if ((map->records[i].file_part & XX_FILE_PART_OVERLAY) && !map->records[i].is_virtual && map->records[i].size > 0) {
            facts->overlay_offset = map->records[i].offset; facts->overlay_size = map->records[i].size;
            facts->flags |= GUI_FACTS_OVERLAY;
            return;
        }
}

/* XMSDOS: the load module sits at 0x10000000 and the entry is
 * module + CS * 16 + IP, wrapped at 1 MiB; both shown as seg:off. */
static int facts_msdos(gui_backend_facts *facts, xx_io_device *device)
{
    xx_msdos msdos;
    int ok;
    xx_rt_memset(&msdos, 0, sizeof(msdos));
    xx_msdos_init(&msdos, device, 0);
    ok = msdos.format.check_is_valid(&msdos.format, NULL) && xx_format_handle_base_info(&msdos.format, NULL);
    if (ok) {
        uint64_t entry = (uint64_t)msdos.cs * 16u + msdos.ip;
        if (entry >= 0x100000u) entry -= 0x100000u;
        facts->flags |= GUI_FACTS_SEG_OFF;
        facts->base_bits = facts->entry_bits = 32;
        facts->base_address = 0x10000000u;
        facts->entry_point = 0x10000000u + entry;
        facts_map_overlay(facts, xx_format_get_memory_map(&msdos.format, XX_MEMORY_MAP_MODE_UNKNOWN, NULL));
        copy_field(facts->endianness, sizeof(facts->endianness), "LE");
        copy_field(facts->mode, sizeof(facts->mode), "16-bit");
        copy_field(facts->architecture, sizeof(facts->architecture), "8086");
        copy_field(facts->type, sizeof(facts->type), "EXE");
    }
    xx_msdos_destroy(&msdos);
    return ok;
}

/* NE, LE/LX, DEX, archives and Amiga hunks: the reader's memory map; COM as
 * XCOM has it. A file the reader rejects keeps the defaults, not a failure. */
static void facts_other(gui_backend_facts *facts, xx_io_device *device)
{
    void (*release)(void *) = NULL;
    Abstractformat *format = facts_reader(facts->type_id, device, &release);
    const xx_memory_map *map = NULL;
    if (format && xx_format_is_valid(format, NULL) && xx_format_handle_base_info(format, NULL)) {
        map = xx_format_get_memory_map(format, XX_MEMORY_MAP_MODE_UNKNOWN, NULL);
        if (map) facts_engine_row(facts, map->endian, map->arch, map->format_type);
        else facts_format_row(facts, format);
        if (facts->page == GUI_FACTS_PAGE_ARCHIVE) {
            /* XBinary::getFileFormatString: "ZIP(2.0)". XZip::getVersion takes
             * the first central record's version, then its minimum version,
             * then the first local header's; the others name no version. */
            unsigned int version = 0;
            if (facts->type_id == XFT_ZIP) {
                const xx_zip *zip = (const xx_zip *)format;
                if (zip->cd_offset >= 0 && xx_io_get_u32(device, zip->cd_offset, false) == 0x02014b50u) {
                    version = xx_io_get_u8(device, zip->cd_offset + 4);
                    if (!version) version = xx_io_get_u8(device, zip->cd_offset + 6);
                }
                if (!version) version = xx_io_get_u8(device, 4);
            }
            if (version)
                xx_rt_snprintf(facts->main_label, sizeof(facts->main_label), "%s(%u.%u)", facts->type_name, version / 10, version % 10);
            /* An archive is data: no byte order (Qt says Unknown) and MODE_DATA. */
            facts->endianness[0] = 0;
            copy_field(facts->mode, sizeof(facts->mode), "Data");
        } else if (map) {
            if (map->module_address != XX_INVALID_ADDRESS) facts->base_address = map->module_address;
            if (map->entry_point_address != XX_INVALID_ADDRESS) {
                facts->entry_point = map->entry_point_address;
                facts_entry_offset(facts, map);
            }
        }
    }
    switch (facts->page) {
    case GUI_FACTS_PAGE_COM: /* XCOM: loaded at 0100, MODE_16, 8086, EXE */
        facts->base_bits = facts->entry_bits = 16;
        facts->base_address = facts->entry_point = 0x100u;
        facts->entry_point_offset = 0;
        facts->flags |= GUI_FACTS_ENTRY_OFFSET;
        copy_field(facts->endianness, sizeof(facts->endianness), "LE");
        copy_field(facts->mode, sizeof(facts->mode), "16-bit");
        copy_field(facts->architecture, sizeof(facts->architecture), "8086");
        copy_field(facts->type, sizeof(facts->type), "EXE");
        break;
    case GUI_FACTS_PAGE_NE: case GUI_FACTS_PAGE_LE: facts->base_bits = facts->entry_bits = 32; break; /* MODE_16SEG / MODE_32 */
    case GUI_FACTS_PAGE_DEX: facts->base_bits = 32; facts->entry_bits = 16; break;
    default: facts->base_bits = facts_size_bits(facts->file_size); facts->entry_bits = 32; break;
    }
    if (format) release(format);
}

void gui_backend_facts_free(gui_backend_facts *facts)
{
    if (!facts) return;
    xx_rt_free(facts->manifest);
    xx_rt_free(facts->version);
    facts->manifest = facts->version = NULL;
}

int gui_backend_file_facts(const char *path, int type_id, gui_backend_facts *facts)
{
    facts_types types;
    xx_io_device *device;
    int64_t size;
    int ok = 1;
    if (!facts) return 0;
    xx_rt_memset(facts, 0, sizeof(*facts));
    facts->entry_point_offset = facts->overlay_offset = -1;
    if (!path || !*path || type_id < 0 || type_id >= XFT_COUNT || (type_id && !gui_concrete_file_type((XFileType)type_id))) return 0;
    types.wanted = type_id; types.found = 0; types.first = 0; types.count = 0;
    if (!die_engine_detect_file_types(path, facts_type, &types) || !types.first || (type_id && !types.found)) return 0;
    if (!(device = xx_io_file_open(path, "rb"))) return 0;
    size = xx_io_total_size(device);
    if (size < 0) { xx_io_close(device); return 0; }
    if (!type_id) {
        int i;
        type_id = types.first;
        for (i = 0; i < types.count; ++i)
            if (facts_type_valid(types.list[i], device)) { type_id = types.list[i]; break; }
    }
    facts->type_id = type_id;
    copy_field(facts->type_name, sizeof(facts->type_name), xft_to_string((XFileType)type_id));
    facts->page = facts_page(type_id);
    facts->file_size = (uint64_t)size;
    facts->base_bits = facts_size_bits(facts->file_size);
    facts->entry_bits = 32;
    switch (facts->page) {
    case GUI_FACTS_PAGE_PE: copy_field(facts->main_label, sizeof(facts->main_label), "PE"); ok = facts_pe(facts, device); break;
    case GUI_FACTS_PAGE_ELF: copy_field(facts->main_label, sizeof(facts->main_label), "ELF"); ok = facts_elf(facts, device); break;
    case GUI_FACTS_PAGE_MACH: copy_field(facts->main_label, sizeof(facts->main_label), "Mach-O"); ok = facts_macho(facts, device); break;
    case GUI_FACTS_PAGE_MSDOS: copy_field(facts->main_label, sizeof(facts->main_label), "MSDOS"); ok = facts_msdos(facts, device); break;
    default:
        copy_field(facts->main_label, sizeof(facts->main_label),
            facts->page == GUI_FACTS_PAGE_NE ? "NE" : facts->page == GUI_FACTS_PAGE_LE ? (type_id == XFT_LX ? "LX" : "LE") :
            facts->page == GUI_FACTS_PAGE_DEX ? "DEX" : facts->page == GUI_FACTS_PAGE_ARCHIVE ? facts->type_name :
            facts->page == GUI_FACTS_PAGE_COM ? "" : "Binary");
        facts_other(facts, device);
        break;
    }
    xx_io_close(device);
    if (!ok) {
        gui_backend_facts_free(facts);
        xx_rt_memset(facts, 0, sizeof(*facts));
        facts->entry_point_offset = facts->overlay_offset = -1;
    }
    return ok;
}

void gui_backend_free(void *pPtr)
{
    xx_rt_free(pPtr);
}

void gui_backend_shutdown(void)
{
    xx_rt_free(g_info_path); g_info_path = NULL;
    gui_backend_clear_cache();
    if (g_cache_lock) { xx_mutex_destroy(g_cache_lock); g_cache_lock = NULL; }
    if (g_bInited) {
        db_free(&g_db);
        xx_rt_free(g_pIncludeTags);
        xx_rt_free(g_pExcludeTags);
        g_pIncludeTags = NULL;
        g_pExcludeTags = NULL;
        g_options.include_tags = NULL;
        g_options.exclude_tags = NULL;
        xx_scan_options_free(&g_options);
        xx_rt_memset(&g_db, 0, sizeof(g_db));
        g_bInited = 0;
        g_bMainLoaded = 0;
    }
}
