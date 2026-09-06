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

/* cdie.h - signature database, scan options and the scan engine. */

#ifndef CDIE_H
#define CDIE_H

#include "../core/cd_common.h"
#include "../format/xb.h"
#include "../format/xpe.h"
#include "../format/xjpeg.h"
#include "../format/xpng.h"
#include "../format/xapk.h"
#include "../format/xpdf.h"
#include "../format/xelf.h"
#include "../format/xdex.h"
#include "../format/xmach.h"
#include "../format/xpyc.h"
#include "../format/xzip.h"
#include "../js/js.h"

typedef enum { DB_MAIN = 0, DB_EXTRA, DB_CUSTOM } DBKind;

typedef struct {
    char *pName;     /* file name including the extension, e.g. "_PE.0.sg" */
    char *pFilePath; /* absolute path                                      */
    char *pText;     /* script source                                      */
    XFileType fileType;
    DBKind databaseType;
} DBSignature;

typedef struct {
    DBSignature *pRecords;
    int nCount;
    int nCapacity;
} DBase;

int db_load(DBase *pDb, const char *pPath, DBKind kind);
void db_sort(DBase *pDb);
void db_free(DBase *pDb);
int db_count_for_type(DBase *pDb, XFileType fileType);

/* --------------------------------------------------------------- options  */

typedef struct {
    int bDeepScan;
    int bHeuristicScan;
    int bAggressiveScan;
    int bRecursiveScan;
    int bVerbose;
    int bAllTypesScan; /* die_library flag mapping only; the console -a option
                        * was removed as it is not implemented in the engine */
    int bFormatResult;
    int bHideUnknown;
    int bShowType;
    int bShowVersion;
    int bShowInfo;
    int bUseCustomDatabase;
    int bUseExtraDatabase;
    int bResultAsJSON;
    int bResultAsXML;
    int bResultAsCSV;
    int bResultAsTSV;
    int bResultAsPlainText;
    int bShowMessages;
    int bProfiling;
    int bSort;
    char *pMainDatabasePath;
    char *pExtraDatabasePath;
    char *pCustomDatabasePath;
} ScanOptions;

void scan_options_init(ScanOptions *pOptions);
void scan_options_free(ScanOptions *pOptions);

/* --------------------------------------------------------------- results  */

typedef struct {
    char *pType;
    char *pName;
    char *pVersion;
    char *pInfo;
    int nPrio;
    int bIsHeuristic;
    int bIsAHeuristic;
    int bIsUnknown;
} ScanRecord;

typedef struct {
    ScanRecord *pRecords;
    int nCount;
    int nCapacity;
    char **ppErrors;
    int nErrorCount;
    XFileType fileType;
    char *pFileName;
    cd_i64 nFileSize;
} ScanResult;

void scan_result_free(ScanResult *pResult);

/* ---------------------------------------------------------------- engine  */

typedef struct {
    char *pType;
    char *pName;
} BLRecord;

/* One outstanding startTiming() handle: the reference keeps the same map of
 * handle to running timer. */
typedef struct {
    cd_i64 nHandle;
    cd_i64 nStart;
} TimingRecord;

typedef struct {
    DBase *pDb;
    ScanOptions *pOptions;
    ScanResult *pResult;

    XBFile file;
    XPE pe;
    int bHasPE;
    XJpeg jpeg;
    int bHasJpeg;
    XPNG png;
    int bHasPng;
    XAPK apk;
    int bHasApk;
    XPDF pdf;
    int bHasPdf;
    XELF elf;
    int bHasElf;
    XDEX dex;
    int bHasDex;
    XMACH mach;
    int bHasMach;
    XPyc pyc;
    int bHasPyc;
    XZip zip;
    int bHasZip;
    XFileType fileType;
    int bIsCliAssembly; /* a .NET PE — bind DOTNET, run PE/DOTNET scripts */
    XBMemoryMap *pMap;

    JSCtx *pJs;

    BLRecord *pBlackList;
    int nBlackListCount;

    int bStop;
    char *pCurrentScript;

    /* Outstanding startTiming() handles, for the -l trace. */
    TimingRecord *pTimings;
    int nTimingCount;
    int nTimingCapacity;
    cd_i64 nNextTimingHandle;

    /* Cached header / entry point / overlay signature strings. */
    char *pHeaderSignature;
    char *pEntryPointSignature;
    char *pOverlaySignature;
} DieEngine;

int cdie_scan_file(const char *pFileName, DBase *pDb, ScanOptions *pOptions, ScanResult *pResult);

/* Scans an in-memory buffer (a private copy is taken). Backs DIE_ScanMemory. */
int cdie_scan_memory(const void *pData, cd_i64 nSize, DBase *pDb, ScanOptions *pOptions, ScanResult *pResult);

/* Installs the DIE script API on a JavaScript context. */
void cdie_install_api(DieEngine *pEngine);

/* --------------------------------------------------------- profiling (-l)  */

/* The reference emits its profiling trace through warningMessage, which the
 * console prints to stdout as "[WARNING] <text>"; a measured step adds
 * " [<n> ms]". These three reproduce that shape (api.c).
 *
 * cdie_profile_start returns the stamp cdie_profile_end needs, or -1 when
 * profiling is off - which is also the signal to skip building the text. */
cd_i64 cdie_profile_start(DieEngine *pEngine);
void cdie_profile_text(DieEngine *pEngine, const char *pText);
X_PRINTF_LIKE(3, 4) void cdie_profile_end(DieEngine *pEngine, cd_i64 nStart, const char *pFormat, ...);
/* Releases the outstanding startTiming() handles. */
void cdie_profile_free(DieEngine *pEngine);

/* Output helpers (result.c). */
char *cdie_format_text(ScanResult *pResult, ScanOptions *pOptions);
char *cdie_format_json(ScanResult *pResult, ScanOptions *pOptions);
char *cdie_format_xml(ScanResult *pResult, ScanOptions *pOptions);
char *cdie_format_csv(ScanResult *pResult, ScanOptions *pOptions, char nSeparator);

int cdie_type_to_prio(const char *pType);
char *cdie_translate_type(const char *pType);

#endif /* CDIE_H */
