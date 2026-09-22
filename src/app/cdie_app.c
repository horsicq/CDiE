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

/* cdie_app.c - see cdie_app.h. */

#include "cdie_app.h"

#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/rt/xx_rt.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

int cdie_path_exists(const char *pPath)
{
    return xx_fs_exists(pPath) ? 1 : 0;
}

/* xxfclib publishes xx_str_dup, but it allocates from the xx_mem_* pool while
 * everything else cdie touches comes from xx_rt_malloc. Twelve lines to keep
 * one free function across the whole program. */
char *cdie_strdup(const char *pText)
{
    size_t nLength;
    char *pResult;

    if (!pText) {
        return NULL;
    }
    nLength = xx_rt_strlen(pText);
    pResult = (char *)xx_rt_malloc(nLength + 1U);
    if (!pResult) {
        return NULL;
    }
    xx_rt_memcpy(pResult, pText, nLength);
    pResult[nLength] = '\0';
    return pResult;
}

/* xx_fs_path_dir would do this, but it returns an xx_mem_alloc block and the
 * caller frees with xx_rt_free. Same five lines, right allocator. */
static char *cdie_dir_of(const char *pPath)
{
    const char *pLast = NULL;
    const char *p = pPath;
    char *pResult;
    size_t nLength;

    for (; *p; p++) {
        if ((*p == '/') || (*p == '\\')) {
            pLast = p;
        }
    }
    if (!pLast) {
        return cdie_strdup("");
    }
    nLength = (size_t)(pLast - pPath);
    pResult = (char *)xx_rt_malloc(nLength + 1U);
    if (!pResult) {
        return NULL;
    }
    xx_rt_memcpy(pResult, pPath, nLength);
    pResult[nLength] = '\0';
    return pResult;
}

char *cdie_app_dir(void)
{
#if defined(_WIN32)
    char sPath[MAX_PATH * 2];
    DWORD nLen = GetModuleFileNameA(NULL, sPath, (DWORD)sizeof(sPath));

    if ((nLen == 0) || (nLen >= sizeof(sPath))) {
        return cdie_strdup(".");
    }

    sPath[nLen] = 0;

    return cdie_dir_of(sPath);
#else
    char sPath[4096];
    ssize_t nLen = readlink("/proc/self/exe", sPath, sizeof(sPath) - 1);

    if (nLen <= 0) {
        return cdie_strdup(".");
    }

    sPath[nLen] = 0;

    return cdie_dir_of(sPath);
#endif
}

char *cdie_path_native(const char *pPath)
{
    char *pResult = cdie_strdup(pPath);

#if defined(_WIN32)
    char *p = pResult;

    for (; p && *p; p++) {
        if (*p == '/') {
            *p = '\\';
        }
    }
#endif

    return pResult;
}

/* Plain name order, ignoring whether the entry is a directory. xx_fs_list_dir
 * hands entries back with directories first; re-sorting here is what keeps a
 * subdirectory's contents printed at the point its own name falls, which is
 * the order cdie has always produced. */
static int cdie_entry_name_cmp(const void *pLeft, const void *pRight)
{
    const xx_fs_entry_t *pA = *(const xx_fs_entry_t *const *)pLeft;
    const xx_fs_entry_t *pB = *(const xx_fs_entry_t *const *)pRight;

    return xx_rt_strcmp(pA->name, pB->name);
}

void cdie_find_files(const char *pPath, xx_list_t *pList, int bRecursive)
{
    xx_list_t entries;
    size_t nCount = 0;
    size_t i = 0;

    if (!pPath || !pList) {
        return;
    }

    if (xx_fs_is_file(pPath)) {
        char *pCopy = cdie_strdup(pPath);

        if (pCopy) {
            xx_list_append(pList, &pCopy);
        }
        return;
    }

    if (!xx_fs_is_dir(pPath)) {
        return;
    }

    if (!xx_list_init(&entries, sizeof(xx_fs_entry_t *), NULL)) {
        return;
    }
    if (!xx_fs_list_dir(pPath, &entries)) {
        nCount = xx_list_count(&entries);
        for (i = 0; i < nCount; i++) {
            xx_fs_entry_free(*(xx_fs_entry_t **)xx_list_at(&entries, i));
        }
        xx_list_cleanup(&entries);
        return;
    }

    nCount = xx_list_count(&entries);

    if (nCount > 1) {
        xx_rt_qsort(xx_list_at(&entries, 0), nCount, sizeof(xx_fs_entry_t *),
                    cdie_entry_name_cmp);
    }

    for (i = 0; i < nCount; i++) {
        xx_fs_entry_t *pEntry = *(xx_fs_entry_t **)xx_list_at(&entries, i);

        if (!pEntry) {
            continue;
        }

        if (pEntry->type == XX_FS_ENTRY_FILE) {
            char *pCopy = cdie_strdup(pEntry->path);

            if (pCopy) {
                xx_list_append(pList, &pCopy);
            }
        } else if (bRecursive) {
            cdie_find_files(pEntry->path, pList, bRecursive);
        }

        xx_fs_entry_free(pEntry);
    }

    xx_list_cleanup(&entries);
}
