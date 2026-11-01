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

/* cdie_paths.c - see cdie_paths.h. */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "cdie_paths.h"
#include "cdie_app.h"

#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/strings/xx_string.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* xx_fs_path_join allocates with xx_mem; every string cdie hands out is an
 * xx_rt block (see cdie_app.h, "ONE ALLOCATOR"). */
static char *cdie_paths_join(const char *pLeft, const char *pRight)
{
    char *pJoined = xx_fs_path_join(pLeft, pRight);
    char *pResult = cdie_strdup(pJoined);

    xx_str_free(pJoined);
    return pResult;
}

/* An environment variable as UTF-8, or NULL when unset or empty. */
static char *cdie_paths_getenv(const char *pName)
{
#if defined(_WIN32)
    /* GetEnvironmentVariableW rather than xx_rt_getenv: the value is a path,
     * and the ANSI call would mangle anything outside the code page. */
    void *pWideName = xx_rt_utf8_to_utf16(pName);
    WCHAR *pValue;
    DWORD nCapacity;
    DWORD nLength;
    char *pResult;

    if (!pWideName) return NULL;
    nCapacity = GetEnvironmentVariableW((LPCWSTR)pWideName, NULL, 0);
    if (nCapacity <= 1) {
        xx_rt_free(pWideName);
        return NULL;
    }
    pValue = (WCHAR *)xx_rt_malloc((size_t)nCapacity * sizeof(WCHAR));
    if (!pValue) {
        xx_rt_free(pWideName);
        return NULL;
    }
    nLength = GetEnvironmentVariableW((LPCWSTR)pWideName, pValue, nCapacity);
    xx_rt_free(pWideName);
    if (!nLength || nLength >= nCapacity) {
        xx_rt_free(pValue);
        return NULL;
    }
    pValue[nLength] = 0;
    pResult = xx_rt_utf16_to_utf8(pValue);
    xx_rt_free(pValue);
    return pResult;
#else
    const char *pValue = getenv(pName);

    return (pValue && pValue[0]) ? cdie_strdup(pValue) : NULL;
#endif
}

int cdie_is_portable(void)
{
    char *pAppDir = cdie_app_dir();
    int bPortable = 0;

    if (pAppDir) {
        char *pUpper = cdie_paths_join(pAppDir, "PORTABLE");
        char *pLower = cdie_paths_join(pAppDir, "portable");

        bPortable = (pUpper && xx_fs_is_file(pUpper)) || (pLower && xx_fs_is_file(pLower));
        xx_rt_free(pUpper);
        xx_rt_free(pLower);
        xx_rt_free(pAppDir);
    }
    return bPortable;
}

#if !defined(_WIN32)
/* Root must not adopt a folder another account controls: with "sudo -E" or a
 * preserved HOME, the user data folder would belong to the invoking user, and
 * renames and deletions there could be redirected by them. */
static int cdie_paths_owned_by_root(const char *pPath)
{
    struct stat info;

    return pPath && stat(pPath, &info) == 0 && info.st_uid == 0;
}
#endif

char *cdie_user_data_dir(void)
{
    char *pOverride;
    char *pBase;
    char *pResult;

#if !defined(_WIN32)
    if (geteuid() == 0) {
        char *pHome = cdie_paths_getenv("HOME");
        int bOwned = cdie_paths_owned_by_root(pHome);

        xx_rt_free(pHome);
        if (!bOwned) return NULL;
        pBase = cdie_paths_getenv("HOME");
        if (!pBase) return NULL;
        pResult = cdie_paths_join(pBase,
#if defined(__APPLE__)
                                  "Library/Application Support/NTInfo/cdie"
#else
                                  ".local/share/NTInfo/cdie"
#endif
        );
        xx_rt_free(pBase);
        return pResult;
    }
#endif

    pOverride = cdie_paths_getenv("CDIE_USER_DATA_DIR");
    if (pOverride) {
        return pOverride;
    }

#if defined(_WIN32)
    pBase = cdie_paths_getenv("LOCALAPPDATA");
    if (!pBase) return NULL;
    pResult = cdie_paths_join(pBase, "NTInfo\\cdie");
#elif defined(__APPLE__)
    pBase = cdie_paths_getenv("HOME");
    if (!pBase) return NULL;
    pResult = cdie_paths_join(pBase, "Library/Application Support/NTInfo/cdie");
#else
    pBase = cdie_paths_getenv("XDG_DATA_HOME");
    /* The XDG specification ignores a relative XDG_DATA_HOME. */
    if (pBase && pBase[0] != '/') {
        xx_rt_free(pBase);
        pBase = NULL;
    }
    if (pBase) {
        pResult = cdie_paths_join(pBase, "NTInfo/cdie");
    } else {
        pBase = cdie_paths_getenv("HOME");
        if (!pBase) return NULL;
        pResult = cdie_paths_join(pBase, ".local/share/NTInfo/cdie");
    }
#endif
    xx_rt_free(pBase);
    return pResult;
}

char *cdie_user_database_dir(const char *pName)
{
    char *pUserDir;
    char *pCandidate;

    if (!pName || !pName[0] || cdie_is_portable()) {
        return NULL;
    }
    pUserDir = cdie_user_data_dir();
    if (!pUserDir) {
        return NULL;
    }
    pCandidate = cdie_paths_join(pUserDir, pName);
    xx_rt_free(pUserDir);
    if (pCandidate && xx_fs_is_dir(pCandidate)) {
        return pCandidate;
    }
    xx_rt_free(pCandidate);
    return NULL;
}

char *cdie_find_database_dir(const char *pName)
{
    char *pAppDir;
    char *pCandidate;

    if (!pName || !pName[0]) {
        return NULL;
    }

    pCandidate = cdie_user_database_dir(pName);
    if (pCandidate) {
        return pCandidate;
    }

    pAppDir = cdie_app_dir();
    pCandidate = cdie_paths_join(pAppDir, pName);
    if (pCandidate && xx_fs_is_dir(pCandidate)) {
        xx_rt_free(pAppDir);
        return pCandidate;
    }
    xx_rt_free(pCandidate);

#if defined(__APPLE__)
    {
        /* Finder starts a bundle independently of the working directory. */
        char *pResources = cdie_paths_join(pAppDir, "../Resources");

        pCandidate = cdie_paths_join(pResources, pName);
        xx_rt_free(pResources);
        if (pCandidate && xx_fs_is_dir(pCandidate)) {
            xx_rt_free(pAppDir);
            return pCandidate;
        }
        xx_rt_free(pCandidate);
    }
#endif
    xx_rt_free(pAppDir);

    if (xx_fs_is_dir(pName)) {
        return cdie_strdup(pName);
    }
    return NULL;
}

char *cdie_translations_dir(void)
{
    static const char *const aCandidates[] = {
        "translations",
#if defined(__APPLE__)
        "../Resources/translations",
#elif !defined(_WIN32)
        "../share/cdie/translations",
#endif
    };
    char *pAppDir = cdie_app_dir();
    size_t i;

    if (!pAppDir) {
        return NULL;
    }
    for (i = 0; i < sizeof(aCandidates) / sizeof(aCandidates[0]); ++i) {
        char *pCandidate = cdie_paths_join(pAppDir, aCandidates[i]);

        if (pCandidate && xx_fs_is_dir(pCandidate)) {
            xx_rt_free(pAppDir);
            return pCandidate;
        }
        xx_rt_free(pCandidate);
    }
    xx_rt_free(pAppDir);
    return NULL;
}
