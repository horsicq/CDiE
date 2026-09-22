/* Copyright (c) 2019-2026 hors<horsicq@gmail.com>
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

/* die.c - the die_library C API, implemented over the cdie engine.
 *
 * Each entry point mirrors DIE_lib in die_library: build scan options from the
 * flags exactly as XScanEngine::setScanFlags does, load the given directory as
 * the (single, main) database, scan, and format the result the same way
 * ScanItemModel::toString() does — which is exactly what the cdie formatters
 * already reproduce byte for byte. Returned strings are freed with
 * DIE_FreeMemory.
 */

#include "die.h"

#include <xxfclib/die_engine/die_engine.h>
#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/rt/xx_rt.h>

#include "../src/app/cdie_app.h"

/* die_library's flag bits (a superset of the documented ones). */
#define SF_DEEPSCAN 0x00000001
#define SF_HEURISTICSCAN 0x00000002
#define SF_ALLTYPESSCAN 0x00000004
#define SF_RECURSIVESCAN 0x00000008
#define SF_VERBOSE 0x00000010
#define SF_AGGRESSIVESCAN 0x00000020
#define SF_RESULTASXML 0x00010000
#define SF_RESULTASJSON 0x00020000
#define SF_RESULTASTSV 0x00040000
#define SF_RESULTASCSV 0x00080000
#define SF_SORT 0x02000000
#define SF_HIDEUNKNOWN 0x04000000
#define SF_FORMATRESULT 0x10000000

/* The database loaded by DIE_LoadDatabase for the *Ex scan variants. */
static DBase g_db;
static int g_bDbLoaded = 0;

/* ------------------------------------------------------------- helpers  */

/* Fills options from the flags, matching XScanEngine::getDefaultOptions +
 * setScanFlags. Note that result sorting is gated on SF_SORT, so the default
 * (no SF_SORT) leaves results in signature-priority order, exactly as
 * die_library does. */
static void die_options_from_flags(ScanOptions *pOptions, unsigned int nFlags)
{
    scan_options_init(pOptions);

    /* getDefaultOptions sets only show type/version/info; it does not enable
     * the extra/custom databases or the default sort that the console does. */
    pOptions->bUseExtraDatabase = 0;
    pOptions->bUseCustomDatabase = 0;
    pOptions->bSort = (nFlags & SF_SORT) ? 1 : 0;

    pOptions->bDeepScan = (nFlags & SF_DEEPSCAN) ? 1 : 0;
    pOptions->bHeuristicScan = (nFlags & SF_HEURISTICSCAN) ? 1 : 0;
    pOptions->bAllTypesScan = (nFlags & SF_ALLTYPESSCAN) ? 1 : 0;
    pOptions->bRecursiveScan = (nFlags & SF_RECURSIVESCAN) ? 1 : 0;
    pOptions->bVerbose = (nFlags & SF_VERBOSE) ? 1 : 0;
    pOptions->bAggressiveScan = (nFlags & SF_AGGRESSIVESCAN) ? 1 : 0;
    pOptions->bHideUnknown = (nFlags & SF_HIDEUNKNOWN) ? 1 : 0;
    pOptions->bFormatResult = (nFlags & SF_FORMATRESULT) ? 1 : 0;
    pOptions->bResultAsJSON = (nFlags & SF_RESULTASJSON) ? 1 : 0;
    pOptions->bResultAsXML = (nFlags & SF_RESULTASXML) ? 1 : 0;
    pOptions->bResultAsCSV = (nFlags & SF_RESULTASCSV) ? 1 : 0;
    pOptions->bResultAsTSV = (nFlags & SF_RESULTASTSV) ? 1 : 0;
}

/* Formats a result the same way the console (and ScanItemModel) does. */
static char *die_format(ScanResult *pResult, ScanOptions *pOptions)
{
    if (pOptions->bResultAsJSON) {
        return die_engine_format_json(pResult, pOptions);
    }

    if (pOptions->bResultAsXML) {
        return die_engine_format_xml(pResult, pOptions);
    }

    if (pOptions->bResultAsCSV) {
        return die_engine_format_csv(pResult, pOptions, ';');
    }

    if (pOptions->bResultAsTSV) {
        return die_engine_format_csv(pResult, pOptions, '\t');
    }

    return die_engine_format_text(pResult, pOptions);
}

/* Resolves a database path, replacing a leading "$data" token with the
 * running executable's directory, as XOptions::convertPathName does. The
 * caller frees the result. */
static char *die_resolve_database(const char *pDatabase)
{
    const char *pMarker = NULL;

    if (pDatabase == NULL) {
        return cdie_strdup("");
    }

    pMarker = pDatabase;

    /* Only the "$data" prefix is handled (the form the samples use). */
    if ((pMarker[0] == '$') && (xx_rt_strncmp(pMarker, "$data", 5) == 0)) {
        char *pAppDir = cdie_app_dir();
        char *pResult = NULL;

        if (pAppDir != NULL) {
            xx_buf_t buf;

            xx_buf_init(&buf);
            xx_buf_append_str(&buf, pAppDir);
            xx_buf_append_str(&buf, pDatabase + 5); /* the part after "$data" */
            pResult = xx_buf_detach(&buf, NULL);
            xx_rt_free(pAppDir);

            return pResult;
        }
    }

    return cdie_strdup(pDatabase);
}

/* Copies a UTF-8 string into a fresh buffer the caller frees with
 * DIE_FreeMemory. */
static char *die_dup_result(const char *pString)
{
    return cdie_strdup(pString ? pString : "");
}

/* --- wide <-> UTF-8 ----------------------------------------------------- */

#if defined(_WIN32)
/* wchar_t is UTF-16 on Windows; reuse the runtime's converters. */
static char *die_wide_to_utf8(const wchar_t *pWide)
{
    char *pResult = xx_rt_utf16_to_utf8((const void *)pWide);

    return pResult ? pResult : cdie_strdup("");
}

static wchar_t *die_utf8_to_wide(const char *pUtf8)
{
    if (pUtf8 == NULL) {
        pUtf8 = "";
    }

    return (wchar_t *)xx_rt_utf8_to_utf16(pUtf8);
}
#else
/* wchar_t is UTF-32 on the platforms this build targets otherwise. */
static char *die_wide_to_utf8(const wchar_t *pWide)
{
    xx_buf_t buf;
    size_t i = 0;

    /* NULL is a supported argument (it is how the A side asks for the default
     * database), so it must map to the empty string rather than fault, the
     * way XBinary::_fromWCharArray guards its own pointer. */
    if (pWide == NULL) {
        return cdie_strdup("");
    }

    xx_buf_init(&buf);

    for (i = 0; pWide[i] != 0; i++) {
        unsigned long nCp = (unsigned long)pWide[i];

        if (nCp < 0x80) {
            xx_buf_append_char(&buf, (char)nCp);
        } else if (nCp < 0x800) {
            xx_buf_append_char(&buf, (char)(0xC0 | (nCp >> 6)));
            xx_buf_append_char(&buf, (char)(0x80 | (nCp & 0x3F)));
        } else if (nCp < 0x10000) {
            xx_buf_append_char(&buf, (char)(0xE0 | (nCp >> 12)));
            xx_buf_append_char(&buf, (char)(0x80 | ((nCp >> 6) & 0x3F)));
            xx_buf_append_char(&buf, (char)(0x80 | (nCp & 0x3F)));
        } else {
            xx_buf_append_char(&buf, (char)(0xF0 | (nCp >> 18)));
            xx_buf_append_char(&buf, (char)(0x80 | ((nCp >> 12) & 0x3F)));
            xx_buf_append_char(&buf, (char)(0x80 | ((nCp >> 6) & 0x3F)));
            xx_buf_append_char(&buf, (char)(0x80 | (nCp & 0x3F)));
        }
    }

    return xx_buf_detach(&buf, NULL);
}

/* Decodes UTF-8 with the same replacement behaviour QString::fromUtf8 applies:
 * an ill-formed lead byte, a truncated sequence or a missing continuation byte
 * each produce U+FFFD and advance the input by exactly one byte. Without that
 * a stray 0xFF lifted out of a scanned binary would swallow the three bytes
 * after it, so the W exports and the A exports disagreed on the same file. */
static wchar_t *die_utf8_to_wide(const char *pUtf8)
{
    size_t nLen = 0;
    wchar_t *pResult = NULL;
    size_t nOut = 0;
    size_t i = 0;

    if (pUtf8 == NULL) {
        pUtf8 = "";
    }

    nLen = xx_rt_strlen(pUtf8);
    pResult = (wchar_t *)xx_rt_malloc((nLen + 1) * sizeof(wchar_t));

    if (pResult == NULL) {
        return NULL;
    }

    while (i < nLen) {
        unsigned char c = (unsigned char)pUtf8[i];
        unsigned long nCp = 0;
        int nExtra = 0;
        int bOk = 1;
        int k = 0;

        if (c < 0x80) {
            nCp = c;
            nExtra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            nCp = c & 0x1F;
            nExtra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            nCp = c & 0x0F;
            nExtra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            nCp = c & 0x07;
            nExtra = 3;
        } else {
            /* 0x80-0xBF is a stray continuation byte, 0xF8-0xFF is not a
             * lead byte at all. */
            bOk = 0;
        }

        if (bOk) {
            for (k = 0; k < nExtra; k++) {
                unsigned char nNext = (i + 1 + (size_t)k < nLen) ? (unsigned char)pUtf8[i + 1 + (size_t)k] : 0;

                if ((nNext & 0xC0) != 0x80) {
                    bOk = 0;
                    break;
                }

                nCp = (nCp << 6) | (nNext & 0x3F);
            }
        }

        if (bOk) {
            i += (size_t)(1 + nExtra);
        } else {
            /* One U+FFFD for the whole maximal subpart -- the lead byte plus
             * whatever continuation bytes did validate -- which is what Qt
             * emits, rather than one per byte. k is 0 for a bad lead byte. */
            nCp = 0xFFFD;
            i += (size_t)(1 + k);
        }

        pResult[nOut++] = (wchar_t)nCp;
    }

    pResult[nOut] = 0;

    return pResult;
}
#endif

/* Turns a UTF-8 result into a freshly allocated wide string. */
static wchar_t *die_dup_result_w(const char *pUtf8)
{
    wchar_t *pResult = die_utf8_to_wide(pUtf8 ? pUtf8 : "");

    return pResult;
}

/* --- the actual scans --------------------------------------------------- */

/* Loads pDatabasePath as the single main database, scans pFileName (bFileMode)
 * or a memory buffer, formats, and returns a UTF-8 string (empty on failure).
 *
 * bFileMode is explicit rather than derived from pFileName != NULL: a NULL
 * name must fail like an unopenable file (empty string, as die_lib.cpp does
 * with a null QString) instead of falling through to a zero-byte memory scan,
 * which would report a fabricated "Binary / Unknown" for a file that was
 * never opened. */
static char *die_scan_common(int bFileMode, const char *pFileName, const void *pMemory, int nMemorySize, unsigned int nFlags, const char *pDatabasePath,
                             DBase *pPreloaded)
{
    ScanOptions options;
    ScanResult result;
    DBase localDb;
    DBase *pDb = pPreloaded;
    char *pFormatted = NULL;
    char *pResultString = NULL;
    int bOk = 0;

    if (bFileMode && (pFileName == NULL)) {
        return cdie_strdup("");
    }

    die_options_from_flags(&options, nFlags);

    if (pDb == NULL) {
        char *pResolved = die_resolve_database(pDatabasePath);

        xx_rt_memset(&localDb, 0, sizeof(localDb));
        db_load(&localDb, pResolved, DB_MAIN);
        db_sort(&localDb);
        xx_rt_free(pResolved);
        pDb = &localDb;
    }

    if (bFileMode) {
        bOk = die_engine_scan_file(pFileName, pDb, &options, &result);
    } else {
        bOk = die_engine_scan_memory(pMemory, (int64_t)nMemorySize, pDb, &options, &result);
    }

    if (bOk) {
        pFormatted = die_format(&result, &options);
        pResultString = die_dup_result(pFormatted);
        xx_rt_free(pFormatted);
        scan_result_free(&result);
    } else {
        pResultString = cdie_strdup("");
    }

    if (pDb == &localDb) {
        db_free(&localDb);
    }

    scan_options_free(&options);

    return pResultString;
}

/* ------------------------------------------------------------- exports  */

DIE_API char *DIE_ScanFileA(char *pszFileName, unsigned int nFlags, char *pszDatabase)
{
    return die_scan_common(1, pszFileName, NULL, 0, nFlags, pszDatabase, NULL);
}

DIE_API wchar_t *DIE_ScanFileW(wchar_t *pwszFileName, unsigned int nFlags, wchar_t *pwszDatabase)
{
    char *pFileName = die_wide_to_utf8(pwszFileName);
    char *pDatabase = die_wide_to_utf8(pwszDatabase);
    char *pResult = die_scan_common(1, pFileName, NULL, 0, nFlags, pDatabase, NULL);
    wchar_t *pWide = die_dup_result_w(pResult);

    xx_rt_free(pFileName);
    xx_rt_free(pDatabase);
    xx_rt_free(pResult);

    return pWide;
}

DIE_API char *DIE_ScanMemoryA(char *pMemory, int nMemorySize, unsigned int nFlags, char *pszDatabase)
{
    return die_scan_common(0, NULL, pMemory, nMemorySize, nFlags, pszDatabase, NULL);
}

DIE_API wchar_t *DIE_ScanMemoryW(char *pMemory, int nMemorySize, unsigned int nFlags, wchar_t *pwszDatabase)
{
    char *pDatabase = die_wide_to_utf8(pwszDatabase);
    char *pResult = die_scan_common(0, NULL, pMemory, nMemorySize, nFlags, pDatabase, NULL);
    wchar_t *pWide = die_dup_result_w(pResult);

    xx_rt_free(pDatabase);
    xx_rt_free(pResult);

    return pWide;
}

DIE_API int DIE_LoadDatabaseA(char *pszDatabase)
{
    char *pResolved = die_resolve_database(pszDatabase);
    int nResult = 0;

    if (g_bDbLoaded) {
        db_free(&g_db);
        g_bDbLoaded = 0;
    }

    xx_rt_memset(&g_db, 0, sizeof(g_db));
    nResult = db_load(&g_db, pResolved, DB_MAIN);
    db_sort(&g_db);
    g_bDbLoaded = 1;
    xx_rt_free(pResolved);

    return nResult;
}

DIE_API int DIE_LoadDatabaseW(wchar_t *pwszDatabase)
{
    char *pDatabase = die_wide_to_utf8(pwszDatabase);
    int nResult = DIE_LoadDatabaseA(pDatabase);

    xx_rt_free(pDatabase);

    return nResult;
}

DIE_API char *DIE_ScanFileExA(char *pszFileName, unsigned int nFlags)
{
    return die_scan_common(1, pszFileName, NULL, 0, nFlags, NULL, g_bDbLoaded ? &g_db : NULL);
}

DIE_API wchar_t *DIE_ScanFileExW(wchar_t *pwszFileName, unsigned int nFlags)
{
    char *pFileName = die_wide_to_utf8(pwszFileName);
    char *pResult = die_scan_common(1, pFileName, NULL, 0, nFlags, NULL, g_bDbLoaded ? &g_db : NULL);
    wchar_t *pWide = die_dup_result_w(pResult);

    xx_rt_free(pFileName);
    xx_rt_free(pResult);

    return pWide;
}

DIE_API char *DIE_ScanMemoryExA(char *pMemory, int nMemorySize, unsigned int nFlags)
{
    return die_scan_common(0, NULL, pMemory, nMemorySize, nFlags, NULL, g_bDbLoaded ? &g_db : NULL);
}

DIE_API wchar_t *DIE_ScanMemoryExW(char *pMemory, int nMemorySize, unsigned int nFlags)
{
    char *pResult = die_scan_common(0, NULL, pMemory, nMemorySize, nFlags, NULL, g_bDbLoaded ? &g_db : NULL);
    wchar_t *pWide = die_dup_result_w(pResult);

    xx_rt_free(pResult);

    return pWide;
}

DIE_API void DIE_FreeMemoryA(char *pszString)
{
    xx_rt_free(pszString);
}

DIE_API void DIE_FreeMemoryW(wchar_t *pwszString)
{
    xx_rt_free(pwszString);
}

#if defined(_WIN32)
DIE_API int __stdcall DIE_VB_ScanFile(wchar_t *pwszFileName, unsigned int nFlags, wchar_t *pwszDatabase, wchar_t *pwszBuffer, int nBufferSize)
{
    char *pFileName = die_wide_to_utf8(pwszFileName);
    char *pDatabase = die_wide_to_utf8(pwszDatabase);
    char *pResult = die_scan_common(1, pFileName, NULL, 0, nFlags, pDatabase, NULL);
    wchar_t *pWide = die_utf8_to_wide(pResult);
    int nLen = 0;
    int nOut = 0;

    while (pWide && (pWide[nLen] != 0)) {
        nLen++;
    }

    if (nLen < nBufferSize) {
        int i = 0;

        for (i = 0; i < nLen; i++) {
            pwszBuffer[i] = pWide[i];
        }

        pwszBuffer[nLen] = 0;
        nOut = nLen;
    }

    xx_rt_free(pFileName);
    xx_rt_free(pDatabase);
    xx_rt_free(pResult);
    xx_rt_free(pWide);

    return nOut;
}

DIE_API int __stdcall DIE_VB_ScanFileCallback(wchar_t *pwszFileName, unsigned int nFlags, wchar_t *pwszDatabase, wchar_t *pwszBuffer, int nBufferSize,
                                              DIE_VB_CALLBACK pfnCallback)
{
    /* The per-signature progress callback is not surfaced by the cdie engine;
     * the scan still runs and the buffer is filled exactly as DIE_VB_ScanFile. */
    (void)pfnCallback;

    return DIE_VB_ScanFile(pwszFileName, nFlags, pwszDatabase, pwszBuffer, nBufferSize);
}
#endif
