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

/* The project compiles as strict ISO C99 (CMAKE_C_EXTENSIONS OFF), which
 * hides everything POSIX behind feature test macros: without this one,
 * readlink and the S_IS* macros have no declaration. It has to come before
 * the first include, so it leads the file.                                 */
#if !defined(_WIN32)
#if !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
/* macOS keeps a good deal of <dirent.h> and <sys/stat.h> behind this one. */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#endif

#include "cd_fs.h"

#if defined(_WIN32)
#include <windows.h>

/* GetFileAttributesW on a UTF-8 path. Returns INVALID_FILE_ATTRIBUTES for a
 * NULL or unconvertible path. The wide form is used throughout so that
 * non-ANSI paths — the whole reason this exists — resolve. */
static DWORD cd_win_attributes(const char *pPath)
{
    WCHAR *pWide = NULL;
    DWORD nAttributes = INVALID_FILE_ATTRIBUTES;

    if (pPath == NULL) {
        return INVALID_FILE_ATTRIBUTES;
    }

    pWide = (WCHAR *)x_utf8_to_utf16(pPath);

    if (pWide == NULL) {
        return INVALID_FILE_ATTRIBUTES;
    }

    nAttributes = GetFileAttributesW(pWide);
    x_free(pWide);

    return nAttributes;
}
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#endif

int cd_path_exists(const char *pPath)
{
#if defined(_WIN32)
    if (pPath == NULL) {
        return 0;
    }

    return (cd_win_attributes(pPath) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
#else
    struct stat st;

    if (pPath == NULL) {
        return 0;
    }

    return (stat(pPath, &st) == 0) ? 1 : 0;
#endif
}

int cd_path_is_dir(const char *pPath)
{
#if defined(_WIN32)
    DWORD nAttributes = 0;

    if (pPath == NULL) {
        return 0;
    }

    nAttributes = cd_win_attributes(pPath);

    if (nAttributes == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }

    return (nAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
    struct stat st;

    if ((pPath == NULL) || (stat(pPath, &st) != 0)) {
        return 0;
    }

    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

int cd_path_is_file(const char *pPath)
{
#if defined(_WIN32)
    DWORD nAttributes = 0;

    if (pPath == NULL) {
        return 0;
    }

    nAttributes = cd_win_attributes(pPath);

    if (nAttributes == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }

    return (nAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : 1;
#else
    struct stat st;

    if ((pPath == NULL) || (stat(pPath, &st) != 0)) {
        return 0;
    }

    return S_ISREG(st.st_mode) ? 1 : 0;
#endif
}

char *cd_read_file(const char *pPath, cd_i64 *pnSize)
{
    void *pFile = x_fopen(pPath, "rb");
    char *pData = NULL;
    cd_i64 nSize = 0;
    size_t nRead = 0;

    if (pnSize) {
        *pnSize = 0;
    }

    if (pFile == NULL) {
        return NULL;
    }

    if (x_fseek(pFile, 0, X_SEEK_END) != 0) {
        x_fclose(pFile);
        return NULL;
    }

    nSize = (cd_i64)x_ftell(pFile);

    if (nSize < 0) {
        x_fclose(pFile);
        return NULL;
    }

    /* The whole file goes into one allocation, so the size has to fit a
     * size_t with room for the terminator. */
    if ((cd_u64)nSize >= (cd_u64)(size_t)-1) {
        x_fclose(pFile);
        return NULL;
    }

    x_rewind(pFile);

    pData = (char *)cd_malloc((size_t)nSize + 1);
    nRead = x_fread(pData, 1, (size_t)nSize, pFile);
    pData[nRead] = 0;
    x_fclose(pFile);

    if (pnSize) {
        *pnSize = (cd_i64)nRead;
    }

    return pData;
}

void cd_entry_free(CDEntry *pEntry)
{
    if (pEntry) {
        cd_free(pEntry->pName);
        cd_free(pEntry->pPath);
        cd_free(pEntry);
    }
}

static int cd_entry_cmp(const void *pLeft, const void *pRight)
{
    const CDEntry *pA = *(const CDEntry *const *)pLeft;
    const CDEntry *pB = *(const CDEntry *const *)pRight;

    return x_strcmp(pA->pName, pB->pName);
}

int cd_list_dir(const char *pPath, CDVec *pVec)
{
    size_t nStart = pVec->nSize;

#if defined(_WIN32)
    /* The wide directory API, so Unicode-named entries survive: the file name
     * from each result is converted back to UTF-8 for the rest of the code. */
    WIN32_FIND_DATAW findData;
    HANDLE hFind = INVALID_HANDLE_VALUE;
    char *pPattern = cd_path_join(pPath, "*");
    WCHAR *pWidePattern = (WCHAR *)x_utf8_to_utf16(pPattern);

    cd_free(pPattern);

    if (pWidePattern == NULL) {
        return 0;
    }

    hFind = FindFirstFileW(pWidePattern, &findData);
    x_free(pWidePattern);

    if (hFind == INVALID_HANDLE_VALUE) {
        return 0;
    }

    do {
        CDEntry *pEntry = NULL;
        char *pName = x_utf16_to_utf8(findData.cFileName);

        if (pName == NULL) {
            continue;
        }

        if ((x_strcmp(pName, ".") == 0) || (x_strcmp(pName, "..") == 0)) {
            x_free(pName);

            continue;
        }

        pEntry = (CDEntry *)cd_calloc(1, sizeof(CDEntry));
        pEntry->pName = cd_strdup(pName);
        pEntry->pPath = cd_path_join(pPath, pName);
        pEntry->type = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? CD_ENTRY_DIR : CD_ENTRY_FILE;
        pEntry->nSize = ((cd_i64)findData.nFileSizeHigh << 32) | (cd_i64)findData.nFileSizeLow;
        x_free(pName);

        cdvec_push(pVec, pEntry);
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);
#else
    DIR *pDir = opendir(pPath);
    struct dirent *pDirent = NULL;

    if (pDir == NULL) {
        return 0;
    }

    while ((pDirent = readdir(pDir)) != NULL) {
        CDEntry *pEntry = NULL;
        struct stat st;

        if ((x_strcmp(pDirent->d_name, ".") == 0) || (x_strcmp(pDirent->d_name, "..") == 0)) {
            continue;
        }

        pEntry = (CDEntry *)cd_calloc(1, sizeof(CDEntry));
        pEntry->pName = cd_strdup(pDirent->d_name);
        pEntry->pPath = cd_path_join(pPath, pDirent->d_name);
        pEntry->type = CD_ENTRY_FILE;
        pEntry->nSize = 0;

        if (stat(pEntry->pPath, &st) == 0) {
            pEntry->type = S_ISDIR(st.st_mode) ? CD_ENTRY_DIR : CD_ENTRY_FILE;
            pEntry->nSize = (cd_i64)st.st_size;
        }

        cdvec_push(pVec, pEntry);
    }

    closedir(pDir);
#endif

    if (pVec->nSize > nStart + 1) {
        x_qsort(pVec->ppData + nStart, pVec->nSize - nStart, sizeof(void *), cd_entry_cmp);
    }

    return 1;
}

static int cd_is_sep(char nChar)
{
    return ((nChar == '/') || (nChar == '\\')) ? 1 : 0;
}

char *cd_path_join(const char *pLeft, const char *pRight)
{
    CDBuf buf;
    size_t nLen = 0;

    cdbuf_init(&buf);
    cdbuf_append_str(&buf, pLeft);

    nLen = buf.nSize;

    if ((nLen > 0) && (!cd_is_sep(buf.pData[nLen - 1]))) {
        cdbuf_append_ch(&buf, '/');
    }

    cdbuf_append_str(&buf, pRight);

    return cdbuf_detach(&buf, NULL);
}

static const char *cd_last_sep(const char *pPath)
{
    const char *pResult = NULL;
    const char *p = pPath;

    for (; *p; p++) {
        if (cd_is_sep(*p)) {
            pResult = p;
        }
    }

    return pResult;
}

char *cd_path_dir(const char *pPath)
{
    const char *pSep = cd_last_sep(pPath);

    if (pSep == NULL) {
        return cd_strdup("");
    }

    return cd_strndup(pPath, (size_t)(pSep - pPath));
}

char *cd_path_file_name(const char *pPath)
{
    const char *pSep = cd_last_sep(pPath);

    if (pSep == NULL) {
        return cd_strdup(pPath);
    }

    return cd_strdup(pSep + 1);
}

char *cd_path_base_name(const char *pPath)
{
    char *pFileName = cd_path_file_name(pPath);
    char *pDot = x_strrchr(pFileName, '.');

    if (pDot && (pDot != pFileName)) {
        *pDot = 0;
    }

    return pFileName;
}

char *cd_path_suffix(const char *pPath)
{
    char *pFileName = cd_path_file_name(pPath);
    char *pDot = x_strrchr(pFileName, '.');
    char *pResult = NULL;

    if (pDot && (pDot != pFileName)) {
        pResult = cd_strdup(pDot + 1);
    } else {
        pResult = cd_strdup("");
    }

    cd_free(pFileName);

    return pResult;
}

char *cd_path_complete_suffix(const char *pPath)
{
    char *pFileName = cd_path_file_name(pPath);
    char *pDot = x_strchr(pFileName, '.');
    char *pResult = NULL;

    if (pDot && (pDot != pFileName)) {
        pResult = cd_strdup(pDot + 1);
    } else {
        pResult = cd_strdup("");
    }

    cd_free(pFileName);

    return pResult;
}

char *cd_path_native(const char *pPath)
{
    char *pResult = cd_strdup(pPath);

#if defined(_WIN32)
    char *p = pResult;

    for (; *p; p++) {
        if (*p == '/') {
            *p = '\\';
        }
    }
#endif

    return pResult;
}

char *cd_app_dir(void)
{
#if defined(_WIN32)
    char sPath[MAX_PATH * 2];
    DWORD nLen = GetModuleFileNameA(NULL, sPath, (DWORD)sizeof(sPath));

    if ((nLen == 0) || (nLen >= sizeof(sPath))) {
        return cd_strdup(".");
    }

    sPath[nLen] = 0;

    return cd_path_dir(sPath);
#else
    char sPath[4096];
    ssize_t nLen = readlink("/proc/self/exe", sPath, sizeof(sPath) - 1);

    if (nLen <= 0) {
        return cd_strdup(".");
    }

    sPath[nLen] = 0;

    return cd_path_dir(sPath);
#endif
}

void cd_find_files(const char *pPath, CDVec *pVecNames, int bRecursive)
{
    if (cd_path_is_file(pPath)) {
        cdvec_push(pVecNames, cd_strdup(pPath));
    } else if (cd_path_is_dir(pPath)) {
        CDVec vecEntries;
        size_t i = 0;

        cdvec_init(&vecEntries);
        cd_list_dir(pPath, &vecEntries);

        for (i = 0; i < vecEntries.nSize; i++) {
            CDEntry *pEntry = (CDEntry *)vecEntries.ppData[i];

            if (pEntry->type == CD_ENTRY_FILE) {
                cdvec_push(pVecNames, cd_strdup(pEntry->pPath));
            } else if (bRecursive) {
                cd_find_files(pEntry->pPath, pVecNames, bRecursive);
            }

            cd_entry_free(pEntry);
        }

        cdvec_free(&vecEntries);
    }
}
