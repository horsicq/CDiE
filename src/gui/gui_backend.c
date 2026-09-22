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

/* gui_backend.c - drives the scan engine for the WinAPI GUI. It is the mirror
 * of main_console.c's scan path: same option defaults, same db resolution, same
 * die_engine_scan_file + die_engine_format_text, so the GUI prints exactly what `cdie` does.
 *
 * This translation unit deliberately does NOT include <windows.h>: it speaks to
 * the engine only. main_gui.c is the half that speaks Win32. */

#include "gui_backend.h"

#include "../global.h"
#include <xxfclib/die_engine/die_engine.h>
#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/rt/xx_rt.h>

#include "../app/cdie_app.h"


static ScanOptions g_options;
static DBase g_db;
static int g_bInited = 0;
static int g_bMainLoaded = 0;

/* Mirrors main_console.c's resolve_database_path with no explicit path: use
 * "<exe-dir>/<name>" when it exists, else the bare relative name. */
static char *gui_resolve_db(const char *pDefaultName)
{
    char *pAppDir = cdie_app_dir();
    char *pCandidate = xx_fs_path_join(pAppDir, pDefaultName);

    xx_rt_free(pAppDir);

    if (xx_fs_is_dir(pCandidate)) {
        return pCandidate;
    }

    xx_rt_free(pCandidate);

    return cdie_strdup(pDefaultName);
}

int gui_backend_init(void)
{
    if (g_bInited) {
        return g_bMainLoaded;
    }

    scan_options_init(&g_options);
    xx_rt_memset(&g_db, 0, sizeof(g_db));

    /* GUI defaults follow the Detect It Easy GUI: deep + heuristic + verbose on.
     * scan_options_init has already set show type/version/info, use extra/custom
     * and sort - the settings that make the output match the console. */
    g_options.bDeepScan = 1;
    g_options.bHeuristicScan = 1;
    g_options.bVerbose = 1;

    g_options.pMainDatabasePath = gui_resolve_db("db");
    g_options.pExtraDatabasePath = gui_resolve_db("db_extra");
    g_options.pCustomDatabasePath = gui_resolve_db("db_custom");

    /* Load all three once. The "Databases" toggles gate which of them run at
     * scan time (via bUseExtraDatabase / bUseCustomDatabase), so a toggle needs
     * no reload. A missing db_extra / db_custom directory simply loads nothing. */
    g_bMainLoaded = db_load(&g_db, g_options.pMainDatabasePath, DB_MAIN);
    db_load(&g_db, g_options.pExtraDatabasePath, DB_EXTRA);
    db_load(&g_db, g_options.pCustomDatabasePath, DB_CUSTOM);
    db_sort(&g_db);

    g_bInited = 1;

    return g_bMainLoaded;
}

const char *gui_backend_main_db_path(void)
{
    return g_options.pMainDatabasePath ? g_options.pMainDatabasePath : "";
}

const char *gui_backend_extra_db_path(void)
{
    return g_options.pExtraDatabasePath ? g_options.pExtraDatabasePath : "";
}

const char *gui_backend_custom_db_path(void)
{
    return g_options.pCustomDatabasePath ? g_options.pCustomDatabasePath : "";
}

int gui_backend_set_db_paths(const char *pMain, const char *pExtra, const char *pCustom)
{
    if (!g_bInited) {
        return 0;
    }

    xx_rt_free(g_options.pMainDatabasePath);
    xx_rt_free(g_options.pExtraDatabasePath);
    xx_rt_free(g_options.pCustomDatabasePath);

    g_options.pMainDatabasePath = ((pMain != NULL) && (pMain[0] != 0)) ? cdie_strdup(pMain) : gui_resolve_db("db");
    g_options.pExtraDatabasePath = ((pExtra != NULL) && (pExtra[0] != 0)) ? cdie_strdup(pExtra) : gui_resolve_db("db_extra");
    g_options.pCustomDatabasePath = ((pCustom != NULL) && (pCustom[0] != 0)) ? cdie_strdup(pCustom) : gui_resolve_db("db_custom");

    db_free(&g_db);
    xx_rt_memset(&g_db, 0, sizeof(g_db));

    g_bMainLoaded = db_load(&g_db, g_options.pMainDatabasePath, DB_MAIN);
    db_load(&g_db, g_options.pExtraDatabasePath, DB_EXTRA);
    db_load(&g_db, g_options.pCustomDatabasePath, DB_CUSTOM);
    db_sort(&g_db);

    return g_bMainLoaded;
}

int gui_backend_signature_count(void)
{
    return g_db.nCount;
}

const char *gui_backend_app_title(void)
{
    return X_APPLICATIONDISPLAYNAME " " X_APPLICATIONVERSION;
}

void gui_backend_set_flags(int bDeep, int bHeuristic, int bVerbose, int bAggressive, int bHideUnknown, int bFormat)
{
    g_options.bDeepScan = bDeep ? 1 : 0;
    g_options.bHeuristicScan = bHeuristic ? 1 : 0;
    g_options.bVerbose = bVerbose ? 1 : 0;
    g_options.bAggressiveScan = bAggressive ? 1 : 0;
    g_options.bHideUnknown = bHideUnknown ? 1 : 0;
    g_options.bFormatResult = bFormat ? 1 : 0;
}

void gui_backend_set_databases(int bUseExtra, int bUseCustom)
{
    g_options.bUseExtraDatabase = bUseExtra ? 1 : 0;
    g_options.bUseCustomDatabase = bUseCustom ? 1 : 0;
}

int gui_backend_scan(const char *pUtf8Path, char **ppResultText, char **ppTypeName)
{
    ScanResult result;

    if (ppResultText != NULL) {
        *ppResultText = NULL;
    }

    if (ppTypeName != NULL) {
        *ppTypeName = NULL;
    }

    if ((!g_bInited) || (pUtf8Path == NULL) || (pUtf8Path[0] == 0)) {
        return 0;
    }

    if (!die_engine_scan_file(pUtf8Path, &g_db, &g_options, &result)) {
        return 0;
    }

    if (ppResultText != NULL) {
        *ppResultText = die_engine_format_text(&result, &g_options); /* text mode, as the console default */
    }

    if (ppTypeName != NULL) {
        *ppTypeName = cdie_strdup(xft_to_string(result.fileType));
    }

    scan_result_free(&result);

    return 1;
}

void gui_backend_free(void *pPtr)
{
    xx_rt_free(pPtr);
}

void gui_backend_shutdown(void)
{
    if (g_bInited) {
        db_free(&g_db);
        scan_options_free(&g_options);
        xx_rt_memset(&g_db, 0, sizeof(g_db));
        g_bInited = 0;
        g_bMainLoaded = 0;
    }
}
