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
#include <xxfclib/data/xx_pd.h>
#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/global/xx_global.h>

#include "../src/app/cdie_app.h"

/* die_library's flag bits (a superset of the documented ones). */
#define SF_DEEPSCAN 0x00000001
#define SF_HEURISTICSCAN 0x00000002
#define SF_ALLTYPESSCAN 0x00000004
#define SF_RECURSIVESCAN 0x00000008
#define SF_VERBOSE 0x00000010
#define SF_AGGRESSIVESCAN 0x00000020
#define SF_OVERLAYSCAN 0x00000040
#define SF_RESOURCESSCAN 0x00000080
#define SF_ARCHIVESSCAN 0x00000100
#define SF_FIRSTWRAPPERONLY 0x00000200
#define SF_RESULTASXML 0x00010000
#define SF_RESULTASJSON 0x00020000
#define SF_RESULTASTSV 0x00040000
#define SF_RESULTASCSV 0x00080000
#define SF_USECACHE 0x01000000
#define SF_SORT 0x02000000
#define SF_HIDEUNKNOWN 0x04000000
#define SF_FORMATRESULT 0x10000000

#if defined(_WIN32)
typedef DIE_VB_CALLBACK die_scan_callback_fn;
#else
typedef void *die_scan_callback_fn;
#endif

/* The database loaded by DIE_LoadDatabase for the *Ex scan variants. */
static DBase g_db;
static int g_bDbLoaded = 0;

/* ------------------------------------------------------------- helpers  */

/* Fills options from the flags, matching XScanEngine::getDefaultOptions +
 * setScanFlags. Note that result sorting is gated on SF_SORT, so the default
 * (no SF_SORT) leaves results in signature-priority order, exactly as
 * die_library does. */
static void die_options_from_flags(xx_scan_options *pOptions, unsigned int nFlags)
{
    xx_scan_options_init(pOptions);

    /* getDefaultOptions sets only show type/version/info; it does not enable
     * the custom database or the default sort that the console does. */
    pOptions->use_custom_database = 0;
    pOptions->sort = (nFlags & SF_SORT) ? 1 : 0;

    pOptions->deep_scan = (nFlags & SF_DEEPSCAN) ? 1 : 0;
    pOptions->heuristic_scan = (nFlags & SF_HEURISTICSCAN) ? 1 : 0;
    pOptions->all_types_scan = (nFlags & SF_ALLTYPESSCAN) ? 1 : 0;
    pOptions->recursive_scan = (nFlags & SF_RECURSIVESCAN) ? 1 : 0;
    pOptions->overlay_scan = (nFlags & SF_OVERLAYSCAN) ? 1 : 0;
    pOptions->resources_scan = (nFlags & SF_RESOURCESSCAN) ? 1 : 0;
    pOptions->archives_scan = (nFlags & SF_ARCHIVESSCAN) ? 1 : 0;
    pOptions->first_wrapper_only = (nFlags & SF_FIRSTWRAPPERONLY) ? 1 : 0;
    pOptions->verbose = (nFlags & SF_VERBOSE) ? 1 : 0;
    pOptions->aggressive_scan = (nFlags & SF_AGGRESSIVESCAN) ? 1 : 0;
    pOptions->hide_unknown = (nFlags & SF_HIDEUNKNOWN) ? 1 : 0;
    pOptions->format_result = (nFlags & SF_FORMATRESULT) ? 1 : 0;
    pOptions->result_as_json = (nFlags & SF_RESULTASJSON) ? 1 : 0;
    pOptions->result_as_xml = (nFlags & SF_RESULTASXML) ? 1 : 0;
    pOptions->result_as_csv = (nFlags & SF_RESULTASCSV) ? 1 : 0;
    /* XScanEngine::setScanFlags currently omits SF_RESULTASTSV. Keep that
     * observable die_library behaviour: the public TSV bit produces the
     * ordinary text result. */
    pOptions->result_as_tsv = 0;
}

/* cdie's console XML deliberately uses a schema-stable <file-part type="...">
 * element. ScanItemModel instead uses the displayed part name as the element
 * name. Adapt only the library result so diec keeps its own public schema. */
static char *die_compat_xml(const char *pText)
{
    static const char sOpen[] = "<file-part type=\"";
    static const char sClose[] = "</file-part>";
    static const char sUnknown[] = "type=\"unknown\"";
    char aNames[64][64];
    int nDepth = 0;
    const char *p = pText ? pText : "";
    xx_buf_t output;

    xx_buf_init(&output);

    while (*p != 0) {
        if (xx_rt_strncmp(p, sOpen, sizeof(sOpen) - 1) == 0) {
            const char *pName = p + sizeof(sOpen) - 1;
            const char *pQuote = pName;
            const char *pEnd = NULL;
            size_t nName = 0;

            while ((*pQuote != 0) && (*pQuote != '\"')) {
                pQuote++;
            }

            pEnd = pQuote;
            while ((*pEnd != 0) && (*pEnd != '>')) {
                pEnd++;
            }

            nName = (size_t)(pQuote - pName);
            xx_buf_append_char(&output, '<');
            xx_buf_append(&output, pName, nName);
            xx_buf_append(&output, pQuote + ((*pQuote == '\"') ? 1 : 0), (size_t)(pEnd - pQuote - ((*pQuote == '\"') ? 1 : 0) + ((*pEnd == '>') ? 1 : 0)));

            if ((nDepth < (int)(sizeof(aNames) / sizeof(aNames[0]))) && (nName + 1 < sizeof(aNames[0])) && (pEnd > p) && (pEnd[-1] != '/')) {
                xx_rt_memcpy(aNames[nDepth], pName, nName);
                aNames[nDepth][nName] = 0;
                nDepth++;
            }

            p = pEnd + ((*pEnd == '>') ? 1 : 0);
        } else if (xx_rt_strncmp(p, sClose, sizeof(sClose) - 1) == 0) {
            xx_buf_append_str(&output, "</");

            if (nDepth > 0) {
                nDepth--;
                xx_buf_append_str(&output, aNames[nDepth]);
            } else {
                xx_buf_append_str(&output, "file-part");
            }

            xx_buf_append_char(&output, '>');
            p += sizeof(sClose) - 1;
        } else if (xx_rt_strncmp(p, sUnknown, sizeof(sUnknown) - 1) == 0) {
            xx_buf_append_str(&output, "type=\"Unknown\"");
            p += sizeof(sUnknown) - 1;
        } else {
            xx_buf_append_char(&output, *p++);
        }
    }

    /* QXmlStreamWriter does not append a newline after the root element. */
    if ((output.size > 0) && (output.data[output.size - 1] == '\n')) {
        output.size--;
        output.data[output.size] = 0;
    }

    return xx_buf_detach(&output, NULL);
}

static char *die_compat_csv(const char *pText)
{
    const char *p = pText ? pText : "";
    int bLineStart = 1;
    xx_buf_t output;

    xx_buf_init(&output);

    while (*p != 0) {
        if (bLineStart && (xx_rt_strncmp(p, "unknown;", 8) == 0)) {
            xx_buf_append_str(&output, "Unknown;");
            p += 8;
            bLineStart = 0;
        } else {
            char nChar = *p++;
            xx_buf_append_char(&output, nChar);
            bLineStart = (nChar == '\n');
        }
    }

    return xx_buf_detach(&output, NULL);
}

/* Formats a result the same way the console (and ScanItemModel) does. */
static char *die_format(xx_scan_result *pResult, xx_scan_options *pOptions)
{
    /* ScanItemModel::toString tests the formats in this exact order. */
    if (pOptions->result_as_csv) {
        char *pRaw = xx_scan_format_csv(pResult, pOptions, ';');
        char *pResultText = die_compat_csv(pRaw);

        xx_rt_free(pRaw);
        return pResultText;
    }

    if (pOptions->result_as_json) {
        return xx_scan_format_json(pResult, pOptions);
    }

    if (pOptions->result_as_tsv) {
        return xx_scan_format_csv(pResult, pOptions, '\t');
    }

    if (pOptions->result_as_xml) {
        char *pRaw = xx_scan_format_xml(pResult, pOptions);
        char *pResultText = die_compat_xml(pRaw);

        xx_rt_free(pRaw);
        return pResultText;
    }

    return xx_scan_format_text(pResult, pOptions);
}

/* Resolves a database path, replacing a leading "$data" token with the
 * running executable's directory, as XOptions::convertPathName does. The
 * caller frees the result. */
static char *die_resolve_database(const char *pDatabase)
{
    const char *pPath = pDatabase;

    /* XScanEngine::loadDatabase treats NULL/empty as "$data/db". */
    if ((pPath == NULL) || (pPath[0] == 0)) {
        pPath = "$data/db";
    }

    /* Only the "$data" prefix is handled (the form the samples use). */
    if ((pPath[0] == '$') && (xx_rt_strncmp(pPath, "$data", 5) == 0)) {
        char *pAppDir = cdie_app_dir();
        char *pResult = NULL;

        if (pAppDir != NULL) {
            xx_buf_t buf;
            char *pTar = NULL;

            xx_buf_init(&buf);
            xx_buf_append_str(&buf, pAppDir);
            xx_buf_append_str(&buf, pPath + 5); /* the part after "$data" */
            pResult = xx_buf_detach(&buf, NULL);
            xx_rt_free(pAppDir);

            if (xx_fs_is_dir(pResult) || xx_fs_is_file(pResult)) {
                return pResult;
            }

            xx_buf_init(&buf);
            xx_buf_append_str(&buf, pResult);
            xx_buf_append_str(&buf, ".tar");
            pTar = xx_buf_detach(&buf, NULL);

            if (xx_fs_is_file(pTar)) {
                xx_rt_free(pResult);
                return pTar;
            }

            xx_rt_free(pTar);
            xx_rt_free(pResult);
        }
    }

    return cdie_strdup(pPath);
}

/* Copies a UTF-8 string into a fresh buffer the caller frees with
 * DIE_FreeMemory. */
static char *die_dup_result(const char *pString)
{
    return cdie_strdup(pString ? pString : "");
}

#if defined(_WIN32) && defined(DIE_BUILD_SHARED)
/* die.dll embeds the static xxfclib, whose per-thread slots are process-wide
 * TlsAlloc indices: give them back when the DLL is unloaded with FreeLibrary
 * (lpvReserved NULL), not at process exit. Declared without <windows.h>,
 * which the engine headers do not tolerate; the types match
 * BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID), and the CRT entry calls it. */
int __stdcall DllMain(void *hinstDLL, unsigned long fdwReason, void *lpvReserved);

int __stdcall DllMain(void *hinstDLL, unsigned long fdwReason, void *lpvReserved)
{
    (void)hinstDLL;

    if ((fdwReason == 0UL /* DLL_PROCESS_DETACH */) && (lpvReserved == NULL)) {
        xx_global_release_thread_slots();
    }

    return 1;
}
#endif

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

#if defined(_WIN32)
typedef struct die_callback_context {
    DBase *pDb;
    xx_scan_options *pOptions;
    DIE_VB_CALLBACK pfnCallback;
    XFileType preferredType;
    XFileType currentType;
    XFileType aDetectedTypes[XFT_COUNT];
    int nDetectedTypes;
    int nPass;
    int bHasPreferredType;
    int bIsCliAssembly;
    int bInSignatureLevel;
    uint64_t nLastIndex;
} die_callback_context;

static void die_callback_detect_type(XFileType fileType, void *pUserData)
{
    die_callback_context *pContext = (die_callback_context *)pUserData;

    if (!pContext->bHasPreferredType) {
        pContext->preferredType = fileType;
        pContext->bHasPreferredType = 1;
    }

    if (fileType == XFT_CLI_ASSEMBLY) {
        pContext->bIsCliAssembly = 1;
    } else if (!((fileType == XFT_BINARY) && (pContext->preferredType != XFT_BINARY) && (pContext->preferredType != XFT_COM)) &&
               (pContext->nDetectedTypes < (int)(sizeof(pContext->aDetectedTypes) / sizeof(pContext->aDetectedTypes[0])))) {
        /* Type detection always includes the Binary fallback. XScanEngine's
         * all-types pass does not run it for a recognized formatted file. */
        pContext->aDetectedTypes[pContext->nDetectedTypes++] = fileType;
    }
}

static int die_callback_prefix_is(const char *pName, const char *pPrefix)
{
    size_t i = 0;

    if ((pName == NULL) || (pPrefix == NULL)) {
        return 0;
    }

    for (i = 0; pPrefix[i] != 0; i++) {
        char nName = pName[i];

        if ((nName >= 'a') && (nName <= 'z')) {
            nName = (char)(nName - 'a' + 'A');
        }

        if (nName != pPrefix[i]) {
            return 0;
        }
    }

    return (pName[i] == '.') || (pName[i] == 0);
}

static int die_callback_record_runs(const die_callback_context *pContext, const DBSignature *pRecord)
{
    if ((pRecord == NULL) || (pRecord->pName == NULL) || !pContext->bHasPreferredType) {
        return 0;
    }

    if (pRecord->fileType == XFT_CLI_ASSEMBLY) {
        if (!pContext->bIsCliAssembly) {
            return 0;
        }
    } else if (!xft_check(pRecord->fileType, pContext->currentType)) {
        return 0;
    }

    if ((!pContext->pOptions->deep_scan) && (die_callback_prefix_is(pRecord->pName, "DS") || die_callback_prefix_is(pRecord->pName, "EP"))) {
        return 0;
    }

    if ((!pContext->pOptions->heuristic_scan) && die_callback_prefix_is(pRecord->pName, "HEUR")) {
        return 0;
    }

    if (xx_rt_strcmp(pRecord->pName, "_init") == 0) {
        return 0;
    }

    return pRecord->databaseType == DB_MAIN;
}

/* Bridge xxfclib's synchronous signature progress to die_library's VB
 * callback. Returning true asks the engine to stop. */
static bool die_callback_observer(const xx_pd_struct *pProgress, void *pUserData)
{
    die_callback_context *pContext = (die_callback_context *)pUserData;
    int i = 0;

    for (i = 0; i < XX_PD_LEVELS; i++) {
        const xx_pd_record *pProgressRecord = &pProgress->records[i];

        if (pProgressRecord->is_busy && (xx_rt_strcmp(pProgressRecord->status, "DIE signatures") == 0)) {
            uint64_t nIndex = pProgressRecord->current;

            if (!pContext->bInSignatureLevel) {
                pContext->bInSignatureLevel = 1;
                pContext->currentType = pContext->preferredType;

                /* xx_scan_device runs applicable parent formats before the
                 * preferred format. detect_file_types reports preferred first,
                 * then the detected parents, so reverse that list per pass. */
                if (pContext->pOptions->all_types_scan && (pContext->nPass < pContext->nDetectedTypes)) {
                    pContext->currentType = pContext->aDetectedTypes[pContext->nDetectedTypes - 1 - pContext->nPass];
                }

                pContext->nPass++;
            }

            if ((nIndex == pContext->nLastIndex) || (nIndex >= (uint64_t)pContext->pDb->nCount)) {
                return false;
            }

            pContext->nLastIndex = nIndex;

            if (die_callback_record_runs(pContext, &pContext->pDb->pRecords[nIndex])) {
                wchar_t *pName = die_utf8_to_wide(pContext->pDb->pRecords[nIndex].pName);
                int nContinue = pContext->pfnCallback(pName, (int)nIndex, pContext->pDb->nCount);

                xx_rt_free(pName);
                return nContinue == 0;
            }

            return false;
        }
    }

    pContext->bInSignatureLevel = 0;
    pContext->nLastIndex = UINT64_MAX;

    return false;
}
#endif

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
                             DBase *pPreloaded, die_scan_callback_fn pfnCallback)
{
    xx_scan_options options;
    xx_scan_result result;
    DBase localDb;
    DBase *pDb = pPreloaded;
    char *pFormatted = NULL;
    char *pResultString = NULL;
    int bOk = 0;

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
#if defined(_WIN32)
        if (pfnCallback != NULL) {
            xx_pd_struct progress = xx_pd_init();
            die_callback_context callbackContext;
            xx_pd_observer previousObserver;

            xx_rt_memset(&callbackContext, 0, sizeof(callbackContext));
            callbackContext.pDb = pDb;
            callbackContext.pOptions = &options;
            callbackContext.pfnCallback = pfnCallback;
            callbackContext.nLastIndex = UINT64_MAX;
            die_engine_detect_file_types(pFileName, die_callback_detect_type, &callbackContext);
            previousObserver = xx_pd_set_observer(&progress, die_callback_observer, &callbackContext);
            bOk = die_engine_scan_file_type_pd(pFileName, pDb, &options, XFT_UNKNOWN, &result, &progress);
            xx_pd_set_observer(previousObserver.progress, previousObserver.callback, previousObserver.user_data);
        } else
#endif
        {
            bOk = die_engine_scan_file(pFileName, pDb, &options, &result);
        }
    } else {
        bOk = die_engine_scan_memory(pMemory, (int64_t)nMemorySize, pDb, &options, &result);
    }

    if (bOk) {
        pFormatted = die_format(&result, &options);
        pResultString = die_dup_result(pFormatted);
        xx_rt_free(pFormatted);
        xx_scan_result_free(&result);
    } else {
        pResultString = cdie_strdup("");
    }

    if (pDb == &localDb) {
        db_free(&localDb);
    }

    xx_scan_options_free(&options);

    return pResultString;
}

/* ------------------------------------------------------------- exports  */

DIE_API char *DIE_ScanFileA(char *pszFileName, unsigned int nFlags, char *pszDatabase)
{
    return die_scan_common(1, pszFileName, NULL, 0, nFlags, pszDatabase, NULL, NULL);
}

DIE_API wchar_t *DIE_ScanFileW(wchar_t *pwszFileName, unsigned int nFlags, wchar_t *pwszDatabase)
{
    char *pFileName = die_wide_to_utf8(pwszFileName);
    char *pDatabase = die_wide_to_utf8(pwszDatabase);
    char *pResult = die_scan_common(1, pFileName, NULL, 0, nFlags, pDatabase, NULL, NULL);
    wchar_t *pWide = die_dup_result_w(pResult);

    xx_rt_free(pFileName);
    xx_rt_free(pDatabase);
    xx_rt_free(pResult);

    return pWide;
}

DIE_API char *DIE_ScanMemoryA(char *pMemory, int nMemorySize, unsigned int nFlags, char *pszDatabase)
{
    return die_scan_common(0, NULL, pMemory, nMemorySize, nFlags, pszDatabase, NULL, NULL);
}

DIE_API wchar_t *DIE_ScanMemoryW(char *pMemory, int nMemorySize, unsigned int nFlags, wchar_t *pwszDatabase)
{
    char *pDatabase = die_wide_to_utf8(pwszDatabase);
    char *pResult = die_scan_common(0, NULL, pMemory, nMemorySize, nFlags, pDatabase, NULL, NULL);
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
    return die_scan_common(1, pszFileName, NULL, 0, nFlags, NULL, g_bDbLoaded ? &g_db : NULL, NULL);
}

DIE_API wchar_t *DIE_ScanFileExW(wchar_t *pwszFileName, unsigned int nFlags)
{
    char *pFileName = die_wide_to_utf8(pwszFileName);
    char *pResult = die_scan_common(1, pFileName, NULL, 0, nFlags, NULL, g_bDbLoaded ? &g_db : NULL, NULL);
    wchar_t *pWide = die_dup_result_w(pResult);

    xx_rt_free(pFileName);
    xx_rt_free(pResult);

    return pWide;
}

DIE_API char *DIE_ScanMemoryExA(char *pMemory, int nMemorySize, unsigned int nFlags)
{
    return die_scan_common(0, NULL, pMemory, nMemorySize, nFlags, NULL, g_bDbLoaded ? &g_db : NULL, NULL);
}

DIE_API wchar_t *DIE_ScanMemoryExW(char *pMemory, int nMemorySize, unsigned int nFlags)
{
    char *pResult = die_scan_common(0, NULL, pMemory, nMemorySize, nFlags, NULL, g_bDbLoaded ? &g_db : NULL, NULL);
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
    char *pResult = die_scan_common(1, pFileName, NULL, 0, nFlags, pDatabase, NULL, NULL);
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
    char *pFileName = die_wide_to_utf8(pwszFileName);
    char *pDatabase = die_wide_to_utf8(pwszDatabase);
    char *pResult = die_scan_common(1, pFileName, NULL, 0, nFlags, pDatabase, NULL, pfnCallback);
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
#endif
