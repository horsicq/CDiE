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

/* db.c - loads the Detect It Easy signature database from a directory tree.
 *
 * The layout mirrors the original engine: the database root holds shared
 * include scripts (file type "unknown") and one subdirectory per format.
 * Only regular files with the ".sg" extension or no extension at all are
 * treated as signatures, and subdirectories below a format directory are
 * ignored - exactly like XScanEngine::_loadDatabaseFromPath.              */

#include "cdie.h"
#include "../core/cd_fs.h"
#include "../format/xft.h"

static const struct {
    const char *pDirectory;
    XFileType fileType;
} g_directories[] = {
    {"", XFT_UNKNOWN},        {"Binary", XFT_BINARY},   {"COM", XFT_COM},         {"Archive", XFT_ARCHIVE}, {"ZIP", XFT_ZIP},
    {"JAR", XFT_JAR},         {"APK", XFT_APK},         {"IPA", XFT_IPA},         {"NPM", XFT_NPM},         {"MACHOFAT", XFT_MACHOFAT},
    {"DEX", XFT_DEX},         {"MSDOS", XFT_MSDOS},     {"LE", XFT_LE},           {"LX", XFT_LX},           {"NE", XFT_NE},
    {"PE", XFT_PE},           {"ELF", XFT_ELF},         {"MACH", XFT_MACHO},      {"DOS16M", XFT_DOS16M},   {"DOS4G", XFT_DOS4G},
    {"Amiga", XFT_AMIGAHUNK}, {"AtariST", XFT_ATARIST}, {"JavaClass", XFT_JAVACLASS}, {"PYC", XFT_PYC},     {"PDF", XFT_PDF},
    {"CFBF", XFT_CFBF},       {"Image", XFT_IMAGE},     {"JPEG", XFT_JPEG},       {"PNG", XFT_PNG},         {"RAR", XFT_RAR},
    {"ISO9660", XFT_ISO9660},
    /* .NET-only scripts live under PE/DOTNET and run against a CLI assembly
     * (a .NET PE), bound to the DOTNET object. Matches the reference loading
     * PE/DOTNET with FT_CLI_ASSEMBLY. A forward slash resolves on both
     * platforms.                                                            */
    {"PE/DOTNET", XFT_CLI_ASSEMBLY},
    {NULL, XFT_UNKNOWN}};

static int is_signature_file(const char *pPath)
{
    char *pSuffix = NULL;
    int bResult = 0;

    if (!cd_path_is_file(pPath)) {
        return 0;
    }

    pSuffix = cd_path_suffix(pPath);
    bResult = ((pSuffix[0] == 0) || (cd_stricmp_ascii(pSuffix, "sg") == 0)) ? 1 : 0;
    cd_free(pSuffix);

    return bResult;
}

static void db_add(DBase *pDb, const char *pName, const char *pPath, char *pText, XFileType fileType, DBKind kind)
{
    DBSignature *pRecord = NULL;

    if (pDb->nCount + 1 > pDb->nCapacity) {
        pDb->nCapacity = pDb->nCapacity ? (pDb->nCapacity * 2) : 256;
        pDb->pRecords = (DBSignature *)cd_realloc(pDb->pRecords, (size_t)pDb->nCapacity * sizeof(DBSignature));
    }

    pRecord = &pDb->pRecords[pDb->nCount++];
    pRecord->pName = cd_strdup(pName);
    pRecord->pFilePath = cd_strdup(pPath);
    pRecord->pText = pText;
    pRecord->fileType = fileType;
    pRecord->databaseType = kind;
}

static void db_load_directory(DBase *pDb, const char *pPath, XFileType fileType, DBKind kind)
{
    CDVec vec;
    size_t i = 0;

    cdvec_init(&vec);

    if (!cd_list_dir(pPath, &vec)) {
        cdvec_free(&vec);

        return;
    }

    for (i = 0; i < vec.nSize; i++) {
        CDEntry *pEntry = (CDEntry *)vec.ppData[i];

        if (is_signature_file(pEntry->pPath)) {
            cd_i64 nSize = 0;
            char *pText = cd_read_file(pEntry->pPath, &nSize);

            if (pText) {
                db_add(pDb, pEntry->pName, pEntry->pPath, pText, fileType, kind);
            }
        }

        cd_entry_free(pEntry);
    }

    cdvec_free(&vec);
}

static int compare_signatures(const void *pLeft, const void *pRight);

int db_load(DBase *pDb, const char *pPath, DBKind kind)
{
    int i = 0;
    int nBefore = pDb->nCount;

    if ((pPath == NULL) || (pPath[0] == 0) || (!cd_path_is_dir(pPath))) {
        return 0;
    }

    for (i = 0; g_directories[i].pDirectory; i++) {
        if (g_directories[i].pDirectory[0] == 0) {
            db_load_directory(pDb, pPath, g_directories[i].fileType, kind);
        } else {
            char *pSubPath = cd_path_join(pPath, g_directories[i].pDirectory);

            db_load_directory(pDb, pSubPath, g_directories[i].fileType, kind);
            cd_free(pSubPath);
        }
    }

    /* XScanEngine::loadDatabase sorts the records it has just read, appends
     * them and then sorts the whole accumulated list again, so an extra or
     * custom database interleaves with the main one by (file type, priority,
     * name) instead of trailing it.                                        */
    if (pDb->nCount > 1) {
        x_qsort(pDb->pRecords, (size_t)pDb->nCount, sizeof(DBSignature), compare_signatures);
    }

    return (pDb->nCount > nBefore) ? 1 : 0;
}

/* Extracts the priority digit: the segment before the extension when the
 * name contains more than one dot, otherwise "9".                          */
static const char *signature_priority(const char *pName, char *pBuf, size_t nBufSize)
{
    const char *pLast = NULL;
    const char *p = pName;
    int nDots = 0;

    for (p = pName; *p; p++) {
        if (*p == '.') {
            nDots++;
            pLast = p;
        }
    }

    if (nDots > 1) {
        /* section(".", nDots-1, nDots-1): the text between the last two dots. */
        const char *pStart = NULL;
        const char *pEnd = pLast;
        int nIndex = 0;

        pStart = pName;

        for (p = pName; p < pLast; p++) {
            if (*p == '.') {
                nIndex++;

                if (nIndex == nDots - 1) {
                    pStart = p + 1;
                    break;
                }
            }
        }

        {
            size_t nSize = (size_t)(pEnd - pStart);

            if (nSize >= nBufSize) {
                nSize = nBufSize - 1;
            }

            x_memcpy(pBuf, pStart, nSize);
            pBuf[nSize] = 0;
        }

        return pBuf;
    }

    x_snprintf(pBuf, nBufSize, "9");

    return pBuf;
}

/* Note: sort_signature_prio extracts the priority section only when BOTH
 * names hold more than one dot, so in the reference a pair involving a
 * single-dot name compares "9" against "9". Porting that literally was tried
 * and measured worse: the reference comparator is intransitive over this
 * mix, so its result depends on std::sort's partitioning, and matching the
 * rule under x_qsort moved the script order further from diec (7 differing
 * positions instead of 3 on the .NET corpus). The simple per-name form is
 * kept deliberately; see the port notes.                                   */
static int compare_signatures(const void *pLeft, const void *pRight)
{
    const DBSignature *pA = (const DBSignature *)pLeft;
    const DBSignature *pB = (const DBSignature *)pRight;
    char sPrioA[32];
    char sPrioB[32];
    int nCmp = 0;

    if (pA->fileType != pB->fileType) {
        return (pA->fileType < pB->fileType) ? -1 : 1;
    }

    signature_priority(pA->pName, sPrioA, sizeof(sPrioA));
    signature_priority(pB->pName, sPrioB, sizeof(sPrioB));

    nCmp = x_strcmp(sPrioA, sPrioB);

    if (nCmp != 0) {
        return nCmp;
    }

    return x_strcmp(pA->pName, pB->pName);
}

void db_sort(DBase *pDb)
{
    /* Sorting happens per database in db_load; kept for API symmetry. */
    (void)pDb;
}

void db_free(DBase *pDb)
{
    int i = 0;

    for (i = 0; i < pDb->nCount; i++) {
        cd_free(pDb->pRecords[i].pName);
        cd_free(pDb->pRecords[i].pFilePath);
        cd_free(pDb->pRecords[i].pText);
    }

    cd_free(pDb->pRecords);
    x_memset(pDb, 0, sizeof(*pDb));
}

int db_count_for_type(DBase *pDb, XFileType fileType)
{
    int nResult = 0;
    int i = 0;

    for (i = 0; i < pDb->nCount; i++) {
        if ((x_strcmp(pDb->pRecords[i].pName, "_init") != 0) && xft_check(pDb->pRecords[i].fileType, fileType)) {
            nResult++;
        }
    }

    return nResult;
}
