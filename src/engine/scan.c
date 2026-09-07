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

/* scan.c - scan orchestration: pick the file type, run the _init scripts and
 * then every applicable detection script in database order.               */

#include "cdie.h"
#include "../core/cd_fs.h"
#include "../format/xft.h"


void scan_options_init(ScanOptions *pOptions)
{
    x_memset(pOptions, 0, sizeof(*pOptions));
    pOptions->bShowType = 1;
    pOptions->bShowVersion = 1;
    pOptions->bShowInfo = 1;
    pOptions->bUseCustomDatabase = 1;
    pOptions->bUseExtraDatabase = 1;
    pOptions->bSort = 1;
}

void scan_options_free(ScanOptions *pOptions)
{
    cd_free(pOptions->pMainDatabasePath);
    cd_free(pOptions->pExtraDatabasePath);
    cd_free(pOptions->pCustomDatabasePath);
    x_memset(pOptions, 0, sizeof(*pOptions));
}

void scan_result_free(ScanResult *pResult)
{
    int i = 0;

    for (i = 0; i < pResult->nCount; i++) {
        cd_free(pResult->pRecords[i].pType);
        cd_free(pResult->pRecords[i].pName);
        cd_free(pResult->pRecords[i].pVersion);
        cd_free(pResult->pRecords[i].pInfo);
    }

    cd_free(pResult->pRecords);

    for (i = 0; i < pResult->nErrorCount; i++) {
        cd_free(pResult->ppErrors[i]);
    }

    cd_free(pResult->ppErrors);
    cd_free(pResult->pFileName);
    x_memset(pResult, 0, sizeof(*pResult));
}

static void result_add_error(ScanResult *pResult, const char *pText)
{
    pResult->ppErrors = (char **)cd_realloc(pResult->ppErrors, (size_t)(pResult->nErrorCount + 1) * sizeof(char *));
    pResult->ppErrors[pResult->nErrorCount++] = cd_strdup(pText);
}

/* Returns the first path segment of the signature name, uppercased. */
static void signature_prefix(const char *pName, char *pBuf, size_t nBufSize)
{
    size_t i = 0;

    for (i = 0; (i + 1 < nBufSize) && pName[i] && (pName[i] != '.'); i++) {
        char nChar = pName[i];

        if ((nChar >= 'a') && (nChar <= 'z')) {
            nChar = (char)(nChar - 'a' + 'A');
        }

        pBuf[i] = nChar;
    }

    pBuf[i] = 0;
}

static int should_execute(DBSignature *pRecord, XFileType fileType, int bIsCliAssembly, ScanOptions *pOptions)
{
    char sPrefix[64];

    /* CLI-assembly (PE/DOTNET) scripts run only when the file is a .NET PE;
     * the primary file type is still PE, so they cannot go through the plain
     * xft_check. Everything else matches the picked type as usual. */
    if (pRecord->fileType == XFT_CLI_ASSEMBLY) {
        if (!bIsCliAssembly) {
            return 0;
        }
    } else if (!xft_check(pRecord->fileType, fileType)) {
        return 0;
    }

    signature_prefix(pRecord->pName, sPrefix, sizeof(sPrefix));

    if ((!pOptions->bDeepScan) && ((x_strcmp(sPrefix, "DS") == 0) || (x_strcmp(sPrefix, "EP") == 0))) {
        return 0;
    }

    if ((!pOptions->bHeuristicScan) && (x_strcmp(sPrefix, "HEUR") == 0)) {
        return 0;
    }

    if (x_strcmp(pRecord->pName, "_init") == 0) {
        return 0;
    }

    if (pRecord->databaseType == DB_MAIN) {
        return 1;
    }

    if (pOptions->bUseCustomDatabase && (pRecord->databaseType == DB_CUSTOM)) {
        return 1;
    }

    if (pOptions->bUseExtraDatabase && (pRecord->databaseType == DB_EXTRA)) {
        return 1;
    }

    return 0;
}

static int sort_records(const void *pLeft, const void *pRight)
{
    const ScanRecord *pA = (const ScanRecord *)pLeft;
    const ScanRecord *pB = (const ScanRecord *)pRight;

    if (pA->nPrio != pB->nPrio) {
        return (pA->nPrio < pB->nPrio) ? -1 : 1;
    }

    return 0;
}

/* Stable insertion sort: std::sort in the reference engine behaves like a
 * stable sort for the small result lists produced by a single scan.        */
static void stable_sort_records(ScanRecord *pRecords, int nCount)
{
    int i = 0;
    int j = 0;

    for (i = 1; i < nCount; i++) {
        ScanRecord key = pRecords[i];

        j = i - 1;

        while ((j >= 0) && (sort_records(&pRecords[j], &key) > 0)) {
            pRecords[j + 1] = pRecords[j];
            j--;
        }

        pRecords[j + 1] = key;
    }
}

/* DOS MZ files can carry an archive after the image.  DiE reports the ZIP
 * overlay even when the executable itself is packed (for example PKLITE).
 * The PE parser already exposes overlays; retain the corresponding DOS case
 * here so the C console produces the same record. */
static void add_msdos_zip_overlay(DieEngine *pEngine)
{
    const unsigned char *pData = pEngine->file.pData;
    cd_i64 nSize = pEngine->file.nSize;
    cd_i64 nOverlay = -1;
    cd_u16 nLastPage = 0;
    cd_u16 nPages = 0;
    ScanResult *pResult = pEngine->pResult;
    ScanRecord *pRecord = NULL;

    if ((pEngine->fileType != XFT_MSDOS) || (nSize < 6) || (pData[0] != 'M') || (pData[1] != 'Z')) {
        return;
    }

    nLastPage = (cd_u16)pData[2] | ((cd_u16)pData[3] << 8);
    nPages = (cd_u16)pData[4] | ((cd_u16)pData[5] << 8);
    nOverlay = (cd_i64)nPages * 512;
    if (nLastPage) {
        nOverlay -= 512 - nLastPage;
    }

    if ((nOverlay < 0) || ((nOverlay + 4) > nSize) || (pData[nOverlay] != 'P') || (pData[nOverlay + 1] != 'K') ||
        (pData[nOverlay + 2] != 3) || (pData[nOverlay + 3] != 4)) {
        return;
    }

    if (pResult->nCount + 1 > pResult->nCapacity) {
        pResult->nCapacity = pResult->nCapacity ? (pResult->nCapacity * 2) : 16;
        pResult->pRecords = (ScanRecord *)cd_realloc(pResult->pRecords, (size_t)pResult->nCapacity * sizeof(ScanRecord));
    }

    pRecord = &pResult->pRecords[pResult->nCount++];
    x_memset(pRecord, 0, sizeof(*pRecord));
    pRecord->pType = cd_strdup("Overlay");
    pRecord->pName = cd_strdup("ZIP archive");
    pRecord->pVersion = cd_strdup("");
    pRecord->pInfo = cd_strdup("");
    pRecord->nPrio = cdie_type_to_prio("Overlay");
}

/* XCOM::isValid, whole: a COM image is loaded at 0x100 and has to fit in the
 * 64 KiB segment, so anything no larger than this can be one.              */
#define XCOM_MAX_SIZE (0x10000 - 0x100)

/* XBinary::getDeviceFileSuffix(getDevice()).toUpper() == "COM". */
static int has_com_suffix(const char *pFileName)
{
    char *pSuffix = NULL;
    int bResult = 0;

    if (pFileName == NULL) {
        return 0;
    }

    pSuffix = cd_path_suffix(pFileName);
    bResult = (cd_stricmp_ascii(pSuffix, "COM") == 0) ? 1 : 0;
    cd_free(pSuffix);

    return bResult;
}

static XFileType pick_file_type(XBFile *pFile, XFTSet *pSet)
{
    if (xft_contains(pSet, XFT_PE32)) return XFT_PE32;
    if (xft_contains(pSet, XFT_PE64)) return XFT_PE64;
    if (xft_contains(pSet, XFT_ELF32)) return XFT_ELF32;
    if (xft_contains(pSet, XFT_ELF64)) return XFT_ELF64;
    if (xft_contains(pSet, XFT_MACHO32)) return XFT_MACHO32;
    if (xft_contains(pSet, XFT_MACHO64)) return XFT_MACHO64;
    if (xft_contains(pSet, XFT_LX)) return XFT_LX;
    if (xft_contains(pSet, XFT_LE)) return XFT_LE;
    if (xft_contains(pSet, XFT_NE)) return XFT_NE;
    if (xft_contains(pSet, XFT_DOS16M)) return XFT_DOS16M;
    if (xft_contains(pSet, XFT_DOS4G)) return XFT_DOS4G;
    if (xft_contains(pSet, XFT_MSDOS)) return XFT_MSDOS;
    if (xft_contains(pSet, XFT_APK)) return XFT_APK;
    if (xft_contains(pSet, XFT_IPA)) return XFT_IPA;
    if (xft_contains(pSet, XFT_JAR)) return XFT_JAR;
    if (xft_contains(pSet, XFT_ZIP)) return XFT_ZIP;
    if (xft_contains(pSet, XFT_DEX)) return XFT_DEX;
    if (xft_contains(pSet, XFT_NPM)) return XFT_NPM;
    if (xft_contains(pSet, XFT_MACHOFAT)) return XFT_MACHOFAT;
    if (xft_contains(pSet, XFT_AMIGAHUNK)) return XFT_AMIGAHUNK;
    if (xft_contains(pSet, XFT_PDF)) return XFT_PDF;
    if (xft_contains(pSet, XFT_CFBF)) return XFT_CFBF;
    if (xft_contains(pSet, XFT_RAR)) return XFT_RAR;
    if (xft_contains(pSet, XFT_ISO9660)) return XFT_ISO9660;
    if (xft_contains(pSet, XFT_JPEG)) return XFT_JPEG;
    if (xft_contains(pSet, XFT_PNG)) return XFT_PNG;
    if (xft_contains(pSet, XFT_JAVACLASS)) return XFT_JAVACLASS;
    if (xft_contains(pSet, XFT_PYC)) return XFT_PYC;

    /* g_arrPrefFileTypeOrder puts FT_COM ahead of FT_TEXT/FT_DATA/FT_BINARY,
     * and XBinary::getFileTypes inserts FT_COM only when nothing else was
     * recognised (the set still holds just FT_BINARY) or the file is plain
     * text, the size leaves room for the 0x100-byte PSP, and the file suffix
     * uppercases to "COM". Reaching the fallback below is this port's form of
     * that first condition. A file without the suffix still meets the COM
     * scripts: scan_engine_run's binary arm runs them the way the reference's
     * last scanProcess branch does.                                        */
    if ((pFile->nSize <= XCOM_MAX_SIZE) && has_com_suffix(pFile->pFileName)) {
        return XFT_COM;
    }

    return XFT_BINARY;
}

static void run_script(DieEngine *pEngine, DBSignature *pRecord, int bCallDetect)
{
    JSCtx *pCtx = pEngine->pJs;
    JSVal global;
    JSVal detect;
    JSVal args[3];
    JSVal callResult;
    cd_i64 nProfileStart = -1;

    /* Setting CDIE_TRACE traces the script order, which is the quickest way
     * to find the culprit when a rule misbehaves on an unusual input.      */
    if (x_getenv("CDIE_TRACE")) {
        x_fprintf(x_stderr(), "[cdie] %s\n", pRecord->pName);
        x_fflush(x_stderr());
    }

    /* DiE_Script::_executeSignature announces the script, times it and
     * reports "<name>: [n ms]" afterwards. Only detection scripts go through
     * it; the _init scripts are evaluated elsewhere and are not measured.  */
    if (bCallDetect) {
        cdie_profile_text(pEngine, pRecord->pName);
        nProfileStart = cdie_profile_start(pEngine);
    }

    js_clear_error(pCtx);

    if (!js_eval_nested(pCtx, pRecord->pText, pRecord->pName)) {
        char sBuf[1024];

        x_snprintf(sBuf, sizeof(sBuf), "%s: %s", pRecord->pName, js_error(pCtx));
        result_add_error(pEngine->pResult, sBuf);
        js_clear_error(pCtx);
        cdie_profile_end(pEngine, nProfileStart, "%s:", pRecord->pName);

        return;
    }

    if (!bCallDetect) {
        return;
    }

    global = js_global(pCtx);
    detect = js_get(pCtx, global, "detect");

    if (!js_is_callable(detect)) {
        js_release(pCtx, detect);
        js_release(pCtx, global);
        cdie_profile_end(pEngine, nProfileStart, "%s:", pRecord->pName);

        return;
    }

    args[0] = js_bool(pEngine->pOptions->bShowType);
    args[1] = js_bool(pEngine->pOptions->bShowVersion);
    args[2] = js_bool(pEngine->pOptions->bShowInfo);

    callResult = js_call(pCtx, detect, global, 3, args);

    if (js_has_exception(pCtx)) {
        char sBuf[1024];

        x_snprintf(sBuf, sizeof(sBuf), "%s: %s", pRecord->pName, js_error(pCtx));
        result_add_error(pEngine->pResult, sBuf);
        js_clear_error(pCtx);
    }

    js_release(pCtx, callResult);
    js_release(pCtx, detect);
    js_release(pCtx, global);

    cdie_profile_end(pEngine, nProfileStart, "%s:", pRecord->pName);
}

/* One detection pass over an already-populated XBFile for a single file type.
 * The file is borrowed, not closed, so the FT_COM arm below can run the pass
 * twice; the caller sorts the merged record list. bAddUnknown mirrors the
 * reference's _processDetect flag: when it is 0 an empty pass stays empty
 * instead of gaining the "Unknown" record.                                 */
static int scan_run_pass(XBFile *pOpenedFile, XFileType fileType, int bIsCliAssembly, DBase *pDb, ScanOptions *pOptions, ScanResult *pResult,
                         int bAddUnknown)
{
    /* Heap, not stack: DieEngine embeds every parser state by value and is
     * over 3 KB on its own. A frame that size walks past the guard page, and
     * the probe helper MSVC emits to prevent that (__chkstk) is a CRT symbol
     * the CDIE_NO_CRT link has no source for. cd_calloc never returns NULL
     * and zeroes, so it replaces the memset too. */
    DieEngine *pEngine = (DieEngine *)cd_calloc(1, sizeof(DieEngine));
    XBMemoryMap binaryMap;
    int i = 0;
    int nGlobalInit = -1;
    int nTypeInit = -1;

    pEngine->file = *pOpenedFile;

    pEngine->fileType = fileType;
    /* A .NET PE is typed both as PE and CLI assembly; the primary type stays
     * PE, but the CLI-assembly flag drives the DOTNET object and the
     * PE/DOTNET scripts. */
    pEngine->bIsCliAssembly = bIsCliAssembly;
    pEngine->pDb = pDb;
    pEngine->pOptions = pOptions;
    pEngine->pResult = pResult;

    if ((pEngine->fileType == XFT_PE32) || (pEngine->fileType == XFT_PE64)) {
        pEngine->bHasPE = xpe_parse(&pEngine->pe, &pEngine->file);
    } else if (pEngine->fileType == XFT_JPEG) {
        pEngine->bHasJpeg = xjpeg_parse(&pEngine->jpeg, &pEngine->file);
    } else if (pEngine->fileType == XFT_PNG) {
        pEngine->bHasPng = xpng_parse(&pEngine->file, &pEngine->png);
    } else if (pEngine->fileType == XFT_APK) {
        pEngine->bHasApk = xapk_parse(&pEngine->file, &pEngine->apk);
    } else if (pEngine->fileType == XFT_PDF) {
        pEngine->bHasPdf = xpdf_parse(&pEngine->file, &pEngine->pdf);
    } else if ((pEngine->fileType == XFT_ELF) || (pEngine->fileType == XFT_ELF32) || (pEngine->fileType == XFT_ELF64)) {
        pEngine->bHasElf = xelf_parse(&pEngine->file, &pEngine->elf);
    } else if (pEngine->fileType == XFT_DEX) {
        pEngine->bHasDex = xdex_parse(&pEngine->file, &pEngine->dex);
    } else if ((pEngine->fileType == XFT_MACHO) || (pEngine->fileType == XFT_MACHO32) || (pEngine->fileType == XFT_MACHO64)) {
        pEngine->bHasMach = xmach_parse(&pEngine->file, &pEngine->mach);
    } else if (pEngine->fileType == XFT_PYC) {
        pEngine->bHasPyc = xpyc_parse(&pEngine->file, &pEngine->pyc);
    }

    /* Every ZIP-family container (also an APK, which additionally parses its
     * AndroidManifest above) gets its central-directory record list and
     * MANIFEST.MF read for the archive-record and manifest predicates. */
    if ((pEngine->fileType == XFT_APK) || (pEngine->fileType == XFT_JAR) || (pEngine->fileType == XFT_ZIP) ||
        (pEngine->fileType == XFT_NPM) || (pEngine->fileType == XFT_IPA)) {
        pEngine->bHasZip = xzip_parse(&pEngine->file, &pEngine->zip);
    }

    if (pEngine->bHasPE) {
        pEngine->pMap = &pEngine->pe.map;
    } else {
        xbmap_init(&binaryMap);
        binaryMap.nBinarySize = pEngine->file.nSize;
        binaryMap.fileType = pEngine->fileType;
        binaryMap.nBits = 32;
        xbmap_add(&binaryMap, 0, pEngine->file.nSize, 0, (cd_u64)pEngine->file.nSize, XPART_DATA, "Data");
        pEngine->pMap = &binaryMap;
    }

    pResult->fileType = pEngine->fileType;

    pEngine->pJs = js_new();
    js_set_user(pEngine->pJs, pEngine);
    cdie_install_api(pEngine);

    /* Locate the global, per-format and (for a .NET PE) DOTNET _init scripts. */
    {
        int nCliInit = -1;

        for (i = 0; i < pDb->nCount; i++) {
            if (x_strcmp(pDb->pRecords[i].pName, "_init") != 0) {
                continue;
            }

            if (pDb->pRecords[i].fileType == XFT_UNKNOWN) {
                nGlobalInit = i;
            }

            if (pDb->pRecords[i].fileType == XFT_CLI_ASSEMBLY) {
                nCliInit = i;
            } else if (xft_check(pDb->pRecords[i].fileType, pEngine->fileType)) {
                nTypeInit = i;
            }
        }

        if (nGlobalInit >= 0) {
            run_script(pEngine, &pDb->pRecords[nGlobalInit], 0);
        }

        if (nTypeInit >= 0) {
            run_script(pEngine, &pDb->pRecords[nTypeInit], 0);
        }

        /* The DOTNET _init runs after the PE one, so it can build on it. */
        if (pEngine->bIsCliAssembly && (nCliInit >= 0)) {
            run_script(pEngine, &pDb->pRecords[nCliInit], 0);
        }
    }

    /* cd_alloc_oom() is always false unless the soft out-of-memory policy is
     * armed, which only the shared library does; there it ends the scan at
     * the next script boundary instead of ending the process. */
    for (i = 0; (i < pDb->nCount) && (!pEngine->bStop) && (!cd_alloc_oom()); i++) {
        if (should_execute(&pDb->pRecords[i], pEngine->fileType, pEngine->bIsCliAssembly, pOptions)) {
            run_script(pEngine, &pDb->pRecords[i], 1);
        }
    }

    if (bAddUnknown && (pResult->nCount == 0)) {
        ScanRecord *pRecord = NULL;

        pResult->pRecords = (ScanRecord *)cd_calloc(1, sizeof(ScanRecord));
        pResult->nCapacity = 1;
        pRecord = &pResult->pRecords[0];
        pRecord->pType = cd_strdup("Unknown");
        pRecord->pName = cd_strdup("Unknown");
        pRecord->pVersion = cd_strdup("");
        pRecord->pInfo = cd_strdup("");
        pRecord->nPrio = cdie_type_to_prio("Unknown");
        pRecord->bIsUnknown = 1;
        pResult->nCount = 1;
    }

    add_msdos_zip_overlay(pEngine);

    js_free(pEngine->pJs);

    for (i = 0; i < pEngine->nBlackListCount; i++) {
        cd_free(pEngine->pBlackList[i].pType);
        cd_free(pEngine->pBlackList[i].pName);
    }

    cd_free(pEngine->pBlackList);
    cdie_profile_free(pEngine);

    if (pEngine->bHasPE) {
        xpe_free(&pEngine->pe);
    } else {
        xbmap_free(&binaryMap);
    }

    if (pEngine->bHasJpeg) {
        xjpeg_free(&pEngine->jpeg);
    }

    if (pEngine->bHasApk) {
        xapk_free(&pEngine->apk);
    }

    if (pEngine->bHasPdf) {
        xpdf_free(&pEngine->pdf);
    }

    if (pEngine->bHasElf) {
        xelf_free(&pEngine->elf);
    }

    if (pEngine->bHasDex) {
        xdex_free(&pEngine->dex);
    }

    if (pEngine->bHasMach) {
        xmach_free(&pEngine->mach);
    }

    if (pEngine->bHasPyc) {
        xpyc_free(&pEngine->pyc);
    }

    if (pEngine->bHasZip) {
        xzip_free(&pEngine->zip);
    }

    cd_free(pEngine);

    return 1;
}

/* Moves every record and error of pFrom into pTo - to the front when bFront
 * is set, otherwise after what is already there - leaving pFrom empty. The
 * record strings change owner, so only the arrays are freed.               */
static void result_merge(ScanResult *pTo, ScanResult *pFrom, int bFront)
{
    int nTotal = 0;

    if (pFrom->nCount > 0) {
        int nAt = bFront ? 0 : pTo->nCount;

        nTotal = pFrom->nCount + pTo->nCount;

        if (nTotal > pTo->nCapacity) {
            pTo->nCapacity = nTotal;
            pTo->pRecords = (ScanRecord *)cd_realloc(pTo->pRecords, (size_t)nTotal * sizeof(ScanRecord));
        }

        if (bFront && (pTo->nCount > 0)) {
            x_memmove(pTo->pRecords + pFrom->nCount, pTo->pRecords, (size_t)pTo->nCount * sizeof(ScanRecord));
        }

        x_memcpy(pTo->pRecords + nAt, pFrom->pRecords, (size_t)pFrom->nCount * sizeof(ScanRecord));
        pTo->nCount = nTotal;
    }

    if (pFrom->nErrorCount > 0) {
        int nErrors = pFrom->nErrorCount + pTo->nErrorCount;
        int nAt = bFront ? 0 : pTo->nErrorCount;
        int i = 0;

        pTo->ppErrors = (char **)cd_realloc(pTo->ppErrors, (size_t)nErrors * sizeof(char *));

        if (bFront && (pTo->nErrorCount > 0)) {
            x_memmove(pTo->ppErrors + pFrom->nErrorCount, pTo->ppErrors, (size_t)pTo->nErrorCount * sizeof(char *));
        }

        for (i = 0; i < pFrom->nErrorCount; i++) {
            pTo->ppErrors[nAt + i] = pFrom->ppErrors[i];
        }

        pTo->nErrorCount = nErrors;
    }

    cd_free(pFrom->pRecords);
    cd_free(pFrom->ppErrors);
    cd_free(pFrom->pFileName);
    x_memset(pFrom, 0, sizeof(*pFrom));
}

/* XScanEngine's hasNonGenericCOMRecords. _COM.0.sg answers for every file
 * that reaches it, so a COM pass only counts when it produced something
 * beyond the operating-system and format lines it always emits.            */
static int com_has_non_generic(ScanResult *pResult)
{
    int i = 0;

    for (i = 0; i < pResult->nCount; i++) {
        char *pType = cdie_translate_type(pResult->pRecords[i].pType);
        int bGeneric = ((cd_stricmp_ascii(pType, "Operation system") == 0) || (cd_stricmp_ascii(pType, "Format") == 0)) ? 1 : 0;

        cd_free(pType);

        if (!bGeneric) {
            return 1;
        }
    }

    return 0;
}

/* The scan proper, over an already-populated XBFile (the struct is taken by
 * value and closed here). Both cdie_scan_file and cdie_scan_memory funnel
 * through this so the two entry points share one verified path. */
static int scan_engine_run(XBFile *pOpenedFile, DBase *pDb, ScanOptions *pOptions, ScanResult *pResult)
{
    XFTSet set;
    XFileType fileType = XFT_BINARY;
    int bIsCliAssembly = 0;
    int nResult = 0;
#if defined(DIE_BUILD_SHARED)
    /* Inside a host process an allocation failure must not take the process
     * with it, so the shared library runs the scan under the soft policy and
     * reports the failure as a failed scan. The console and the static
     * library keep the abort, and with it their exact behaviour. */
    int bSoftOom = cd_alloc_begin_soft_oom();
#endif

    xft_detect(pOpenedFile, &set);
    fileType = pick_file_type(pOpenedFile, &set);
    bIsCliAssembly = xft_contains(&set, XFT_CLI_ASSEMBLY);

    if (fileType == XFT_COM) {
        /* XScanEngine::scanProcess's FT_COM arm is two passes: a deep scan
         * runs the FT_BINARY scripts first, and the FT_COM pass adds its
         * "Unknown" record only when that produced nothing. The binary
         * records come first in the merged list, and a file that answered as
         * binary is reported as Binary rather than COM.                    */
        ScanResult binResult;
        int bIsBinary = 0;

        x_memset(&binResult, 0, sizeof(binResult));

        if (pOptions->bDeepScan) {
            scan_run_pass(pOpenedFile, XFT_BINARY, bIsCliAssembly, pDb, pOptions, &binResult, 0);
            bIsBinary = (binResult.nCount > 0);
        }

        nResult = scan_run_pass(pOpenedFile, XFT_COM, bIsCliAssembly, pDb, pOptions, pResult, !bIsBinary);

        result_merge(pResult, &binResult, 1);
        pResult->fileType = bIsBinary ? XFT_BINARY : XFT_COM;
    } else if ((fileType == XFT_BINARY) && (pOpenedFile->nSize <= XCOM_MAX_SIZE)) {
        /* XScanEngine::scanProcess's last arm - everything no format claimed
         * - offers the file to the COM scripts before the binary ones, which
         * is the only way the 248 COM signatures run on a file that is not
         * named *.com. XCOM::isValid is nothing but the size test above: a
         * COM image loads at 0x100 and cannot cross the 64 KiB segment.
         *
         * The reference copies the options and clears the verbose flag for
         * that pass, so the COM "Operation system" line stays out, and never
         * lets it add "Unknown". Its records are kept only when one of them
         * is neither an operating system nor a format, and that same answer
         * is what suppresses the binary pass's own "Unknown".              */
        ScanResult comResult;
        ScanOptions comOptions = *pOptions;
        int bIsCom = 0;

        x_memset(&comResult, 0, sizeof(comResult));
        comOptions.bVerbose = 0;

        scan_run_pass(pOpenedFile, XFT_COM, bIsCliAssembly, pDb, &comOptions, &comResult, 0);
        bIsCom = com_has_non_generic(&comResult);

        nResult = scan_run_pass(pOpenedFile, XFT_BINARY, bIsCliAssembly, pDb, pOptions, pResult, !bIsCom);

        if (bIsCom) {
            result_merge(pResult, &comResult, 0);
        } else {
            scan_result_free(&comResult);
        }
    } else {
        nResult = scan_run_pass(pOpenedFile, fileType, bIsCliAssembly, pDb, pOptions, pResult, 1);
    }

    if (pOptions->bSort) {
        stable_sort_records(pResult->pRecords, pResult->nCount);
    }

    xb_close(pOpenedFile);

#if defined(DIE_BUILD_SHARED)
    if (bSoftOom) {
        if (cd_alloc_oom()) {
            nResult = 0;
        }

        cd_alloc_end_soft_oom();
    }
#endif

    return nResult;
}

int cdie_scan_file(const char *pFileName, DBase *pDb, ScanOptions *pOptions, ScanResult *pResult)
{
    XBFile file;

    x_memset(pResult, 0, sizeof(*pResult));

    if (!xb_open(&file, pFileName)) {
        return 0;
    }

    pResult->pFileName = cd_strdup(pFileName);
    pResult->nFileSize = file.nSize;

    return scan_engine_run(&file, pDb, pOptions, pResult);
}

int cdie_scan_memory(const void *pData, cd_i64 nSize, DBase *pDb, ScanOptions *pOptions, ScanResult *pResult)
{
    XBFile file;

    x_memset(pResult, 0, sizeof(*pResult));
    x_memset(&file, 0, sizeof(file));

    if (nSize < 0) {
        nSize = 0;
    }

    /* A private copy so xb_close can free it like a file-backed buffer. The
     * size is the caller's, so this is one of the allocations that has to be
     * allowed to fail rather than end the process; the NULL check below is
     * what makes cd_try_malloc the right primitive here. */
    file.pData = (unsigned char *)cd_try_malloc((size_t)(nSize > 0 ? nSize : 1));

    if (file.pData == NULL) {
        return 0;
    }

    if (nSize > 0) {
        x_memcpy(file.pData, pData, (size_t)nSize);
    }

    file.nSize = nSize;
    file.pFileName = cd_strdup("");

    pResult->pFileName = cd_strdup("");
    pResult->nFileSize = nSize;

    return scan_engine_run(&file, pDb, pOptions, pResult);
}
