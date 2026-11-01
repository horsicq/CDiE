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

/* cdie_updater.c - see cdie_updater.h. */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "cdie_updater.h"

#include "../global.h"
#include "../app/cdie_app.h"
#include "../app/cdie_paths.h"

#include <xxgithub/xxgithub.h>
#include <xxnetlib/xxnetlib.h>

#include <xxfclib/algo/md5/xx_md5.h>
#include <xxfclib/formats/archive/zip/xx_zip.h>
#include <xxfclib/formats/xx_format.h>
#include <xxfclib/fs/xx_fs.h>
#include <xxfclib/io/xx_io.h>
#include <xxfclib/list/xx_list.h>
#include <xxfclib/rt/xx_rt.h>
#include <xxfclib/scan/xx_scan.h>
#include <xxfclib/settings/xx_settings.h>
#include <xxfclib/strings/xx_string.h>
#include <xxfclib/threads/xx_threads.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define UPD_STAGE_PREFIX ".die-update-"
#define UPD_LOCK_NAME ".die-update.lock"

/* ---------------------------------------------------------------- strings */

static char *upd_dup(const char *pText)
{
    return cdie_strdup(pText ? pText : "");
}

static void upd_replace(char **ppField, const char *pValue)
{
    char *pCopy = pValue ? cdie_strdup(pValue) : NULL;

    xx_rt_free(*ppField);
    *ppField = pCopy;
}

static int upd_empty(const char *pText)
{
    return !pText || !pText[0];
}

/* Paths are shown to the user and written to the stamp; on Windows give them
 * the separator the system writes. */
static char *upd_native(char *pPath)
{
#if defined(_WIN32)
    char *p;

    for (p = pPath; p && *p; ++p) {
        if (*p == '/') *p = '\\';
    }
#endif
    return pPath;
}

static char *upd_join(const char *pLeft, const char *pRight)
{
    char *pJoined = xx_fs_path_join(pLeft, pRight);
    char *pResult = cdie_strdup(pJoined);

    xx_str_free(pJoined);
    return upd_native(pResult);
}

static int upd_is_separator(char c)
{
    return c == '/' || c == '\\';
}

/* The parent folder, "" when there is none. A trailing separator is ignored. */
static char *upd_dir_of(const char *pPath)
{
    size_t nLength = xx_rt_strlen(pPath);
    char *pResult;

    while (nLength > 1 && upd_is_separator(pPath[nLength - 1])) --nLength;
    while (nLength && !upd_is_separator(pPath[nLength - 1])) --nLength;
    if (!nLength) return upd_dup("");
    /* Keep the separator of a root ("/" or "C:\"). */
    if (nLength > 1 && !(nLength == 3 && pPath[1] == ':')) --nLength;
    pResult = (char *)xx_rt_malloc(nLength + 1);
    if (!pResult) return NULL;
    xx_rt_memcpy(pResult, pPath, nLength);
    pResult[nLength] = 0;
    return pResult;
}

static int upd_is_absolute(const char *pPath)
{
    if (upd_empty(pPath)) return 0;
    if (upd_is_separator(pPath[0])) return 1;
#if defined(_WIN32)
    if (((pPath[0] >= 'A' && pPath[0] <= 'Z') || (pPath[0] >= 'a' && pPath[0] <= 'z')) && pPath[1] == ':' && upd_is_separator(pPath[2])) return 1;
#endif
    return 0;
}

/* Relative paths are anchored at the current directory, once, so messages and
 * the stamp name one place even if the working directory changes later. */
static char *upd_absolute(const char *pPath)
{
    char *pCurrent;
    char *pResult;

    if (upd_is_absolute(pPath)) return upd_native(upd_dup(pPath));
    pCurrent = xx_fs_current_dir();
    if (!pCurrent) return upd_native(upd_dup(pPath));
    if (!xx_rt_strcmp(pPath, ".")) {
        pResult = upd_native(cdie_strdup(pCurrent));
    } else {
        pResult = upd_join(pCurrent, (pPath[0] == '.' && upd_is_separator(pPath[1])) ? pPath + 2 : pPath);
    }
    xx_str_free(pCurrent);
    return pResult;
}

/* "$data" at the start of a stored path means the executable's folder, as in
 * Detect It Easy's settings. */
static char *upd_expand_path(const char *pValue)
{
    if (!xx_rt_strncmp(pValue, "$data", 5) && (!pValue[5] || upd_is_separator(pValue[5]))) {
        char *pAppDir = cdie_app_dir();
        char *pResult = upd_join(pAppDir ? pAppDir : ".", pValue[5] ? pValue + 6 : "");
        xx_rt_free(pAppDir);
        return pResult;
    }
    return upd_dup(pValue);
}

/* The path in a form two spellings of one folder share: absolute, '/'
 * separators, "." and ".." resolved where the system can, no trailing
 * separator, and on Windows and macOS (case-insensitive by default) lower-case
 * ASCII. For comparisons only. */
static char *upd_canonical(const char *pPath)
{
    char *pAbsolute = upd_absolute(pPath);
    char *pResult = NULL;
    size_t nLength;
    char *p;

    if (!pAbsolute) return NULL;
#if defined(_WIN32)
    {
        void *pWide = xx_rt_utf8_to_utf16(pAbsolute);
        DWORD nSize = pWide ? GetFullPathNameW((LPCWSTR)pWide, 0, NULL, NULL) : 0;
        WCHAR *pFull = nSize ? (WCHAR *)xx_rt_malloc((size_t)nSize * sizeof(WCHAR)) : NULL;

        if (pFull && GetFullPathNameW((LPCWSTR)pWide, nSize, pFull, NULL) && pFull[0]) pResult = xx_rt_utf16_to_utf8(pFull);
        xx_rt_free(pFull);
        xx_rt_free(pWide);
    }
#else
    {
        char *pReal = realpath(pAbsolute, NULL);

        if (pReal) {
            pResult = cdie_strdup(pReal);
            free(pReal);
        }
    }
#endif
    if (!pResult) {
        pResult = pAbsolute;
    } else {
        xx_rt_free(pAbsolute);
    }
    for (p = pResult; *p; ++p) {
        if (*p == '\\') *p = '/';
#if defined(_WIN32) || defined(__APPLE__)
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
#endif
    }
    nLength = xx_rt_strlen(pResult);
    while (nLength > 1 && pResult[nLength - 1] == '/' && !(nLength == 3 && pResult[1] == ':')) pResult[--nLength] = 0;
    return pResult;
}

/* Nonzero when canonical pInner is canonical pOuter or lies below it. */
static int upd_within(const char *pInner, const char *pOuter)
{
    size_t nOuter = xx_rt_strlen(pOuter);

    if (!pInner || !pOuter || xx_rt_strncmp(pInner, pOuter, nOuter)) return 0;
    return !pInner[nOuter] || pInner[nOuter] == '/' || (nOuter && pOuter[nOuter - 1] == '/');
}

/* An environment variable that is set to something. */
static int upd_env_set(const char *pName)
{
#if defined(_WIN32)
    void *pWide = xx_rt_utf8_to_utf16(pName);
    DWORD nSize = pWide ? GetEnvironmentVariableW((LPCWSTR)pWide, NULL, 0) : 0;

    xx_rt_free(pWide);
    return nSize > 1;
#else
    const char *pValue = getenv(pName);
    return pValue && pValue[0];
#endif
}

/* The URL without "user:password@": addresses are printed and written to the
 * stamp, and credentials have no business in either. */
static char *upd_redact_url(const char *pUrl)
{
    const char *pScheme = pUrl ? xx_rt_strstr(pUrl, "://") : NULL;
    const char *pAuthority;
    const char *pEnd;
    const char *pAt = NULL;
    const char *p;
    char *pResult;
    size_t nHead;

    if (!pScheme) return upd_dup(pUrl);
    pAuthority = pScheme + 3;
    for (pEnd = pAuthority; *pEnd && *pEnd != '/' && *pEnd != '?' && *pEnd != '#'; ++pEnd) {}
    for (p = pAuthority; p < pEnd; ++p) {
        if (*p == '@') pAt = p;
    }
    if (!pAt) return upd_dup(pUrl);
    nHead = (size_t)(pAuthority - pUrl);
    pResult = (char *)xx_rt_malloc(nHead + xx_rt_strlen(pAt + 1) + 1);
    if (!pResult) return NULL;
    xx_rt_memcpy(pResult, pUrl, nHead);
    xx_rt_memcpy(pResult + nHead, pAt + 1, xx_rt_strlen(pAt + 1) + 1);
    return pResult;
}

/* Text from the server (release names, error pages) ends up on terminals and
 * in the GUI's status bar: control characters (C0, DEL and UTF-8-encoded C1)
 * become '?', so none of it can drive a terminal. */
static char *upd_sanitize(char *pText)
{
    unsigned char *p;

    for (p = (unsigned char *)pText; p && *p; ++p) {
        if (*p < 0x20 || *p == 0x7F) {
            *p = '?';
        } else if (*p == 0xC2 && p[1] >= 0x80 && p[1] <= 0x9F) {
            p[0] = '?';
            p[1] = '?';
            ++p;
        }
    }
    return pText;
}

/* Bounds for what one update may bring: db.zip is under 2 MB today. */
#define UPD_MAX_ARCHIVE_SIZE (256ll * 1024 * 1024)
#define UPD_MAX_MEMBER_SIZE (64ull * 1024 * 1024)
#define UPD_MAX_EXTRACTED_SIZE (1024ull * 1024 * 1024)
#define UPD_MAX_MEMBERS 200000

static void upd_message(cdie_update_info *pInfo, const char *pFormat, ...)
{
    XX_RT_VA_LIST args;

    XX_RT_VA_START(args, pFormat);
    xx_rt_vsnprintf(pInfo->message, sizeof(pInfo->message), pFormat, args);
    XX_RT_VA_END(args);
    upd_sanitize(pInfo->message);
}

static void upd_copy_text(char *pTarget, size_t nSize, const char *pText)
{
    xx_rt_snprintf(pTarget, nSize, "%s", pText ? pText : "");
}

static int upd_is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int upd_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* The first 32 hex digits of an .md5 file ("<hex>", "<hex>  name" or
 * "<hex> *name"), lower-cased. */
static int upd_parse_md5(const char *pText, size_t nSize, char sMd5[33])
{
    size_t i = 0;
    size_t j;

    while (i < nSize && upd_is_space(pText[i])) ++i;
    if (nSize - i < 32) return 0;
    for (j = 0; j < 32; ++j) {
        char c = pText[i + j];
        if (!upd_is_hex(c)) return 0;
        sMd5[j] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
    }
    sMd5[32] = 0;
    return i + 32 == nSize || upd_is_space(pText[i + 32]);
}

/* -------------------------------------------------------------- settings */

static char *upd_settings_string(const xx_settings *pStore, const char *pKey)
{
    const xx_settings_value *pValue = xx_settings_get(pStore, pKey);
    char *pResult;

    if (!pValue || pValue->type != XX_SETTINGS_VALUE_STRING || !pValue->data.buffer.data || !pValue->data.buffer.size) return NULL;
    pResult = (char *)xx_rt_malloc(pValue->data.buffer.size + 1);
    if (!pResult) return NULL;
    xx_rt_memcpy(pResult, pValue->data.buffer.data, pValue->data.buffer.size);
    pResult[pValue->data.buffer.size] = 0;
    return pResult;
}

/* xx_settings rejects the whole file for one line it cannot parse. die.ini is
 * also written by Detect It Easy and edited by hand, so when that happens the
 * two keys this program needs are read by a lenient scan instead: QSettings
 * sections, '\' or '/' key separators, optional quotes, no escapes. */
static void upd_lenient_ini(const char *pPath, char **ppMain, char **ppUrl)
{
    int64_t nSize = 0;
    char *pData = xx_fs_read_file(pPath, &nSize);
    char sSection[128] = "";
    char *pLine;

    if (!pData) return;
    pLine = pData;
    while (pLine && *pLine) {
        char *pEnd = pLine;
        char *pNext;
        char *pEquals;

        while (*pEnd && *pEnd != '\n') ++pEnd;
        pNext = *pEnd ? pEnd + 1 : NULL;
        *pEnd = 0;
        if (pEnd > pLine && pEnd[-1] == '\r') pEnd[-1] = 0;
        while (upd_is_space(*pLine)) ++pLine;

        if (*pLine == '[') {
            char *pClose = xx_rt_strchr(pLine, ']');
            if (pClose) {
                *pClose = 0;
                upd_copy_text(sSection, sizeof(sSection), xx_rt_strcmp(pLine + 1, "General") ? pLine + 1 : "");
            }
        } else if (*pLine && *pLine != ';' && *pLine != '#' && (pEquals = xx_rt_strchr(pLine, '=')) != NULL) {
            char sKey[256];
            char *pValue = pEquals + 1;
            char *p;
            size_t nValue;

            *pEquals = 0;
            for (p = pEquals; p > pLine && upd_is_space(p[-1]); --p) p[-1] = 0;
            xx_rt_snprintf(sKey, sizeof(sKey), "%s%s%s", sSection, sSection[0] ? "/" : "", pLine);
            for (p = sKey; *p; ++p) if (*p == '\\') *p = '/';
            while (upd_is_space(*pValue)) ++pValue;
            nValue = xx_rt_strlen(pValue);
            while (nValue && upd_is_space(pValue[nValue - 1])) pValue[--nValue] = 0;
            if (nValue >= 2 && pValue[0] == '"' && pValue[nValue - 1] == '"') {
                pValue[nValue - 1] = 0;
                ++pValue;
            }
            if (*pValue) {
                if (!xx_rt_strcmp(sKey, CDIE_UPDATE_KEY_MAIN_PATH) && !*ppMain) *ppMain = upd_dup(pValue);
                else if (!xx_rt_strcmp(sKey, CDIE_UPDATE_KEY_URL) && !*ppUrl) *ppUrl = upd_dup(pValue);
            }
        }
        pLine = pNext;
    }
    xx_str_free(pData);
}

void cdie_update_config_init(cdie_update_config *pConfig)
{
    if (!pConfig) return;
    xx_rt_memset(pConfig, 0, sizeof(*pConfig));
    pConfig->user_fallback = 1;
}

void cdie_update_config_free(cdie_update_config *pConfig)
{
    if (!pConfig) return;
    xx_rt_free(pConfig->main_path);
    xx_rt_free(pConfig->url);
    xx_rt_free(pConfig->api_base);
    xx_rt_free(pConfig->settings_location);
    xx_rt_memset(pConfig, 0, sizeof(*pConfig));
}

cdie_update_status cdie_update_config_load_settings(cdie_update_config *pConfig, const char *pSettingsFile, char *pError, size_t nErrorSize)
{
    xx_settings *pStore;
    char *pMain = NULL;
    char *pUrl = NULL;
    int bIni;

    if (pError && nErrorSize) pError[0] = 0;
    if (!pConfig) return CDIE_UPDATE_ERROR_ARGUMENT;

    if (!upd_empty(pSettingsFile)) {
        /* xx_settings treats a missing file as an empty store; a file named
         * on purpose that is not there is a mistake worth reporting. */
        if (!xx_fs_is_file(pSettingsFile)) {
            if (pError && nErrorSize) xx_rt_snprintf(pError, nErrorSize, "Settings file not found: %s", pSettingsFile);
            return CDIE_UPDATE_ERROR_ARGUMENT;
        }
        pStore = xx_settings_create_ini(pSettingsFile);
    } else {
        /* Detect It Easy's own store: die.ini beside a portable executable,
         * otherwise the native NTInfo/die settings (the registry on Windows,
         * biz.ntinfo.die on macOS), the same store the GUI uses. */
        char *pAppDir = cdie_app_dir();
        char *pAbsolute = pAppDir ? upd_absolute(pAppDir) : NULL;

        pStore = pAbsolute ? xx_settings_create_application(pAbsolute, X_SETTINGS_ORGANIZATION, X_SETTINGS_APPLICATION, XX_SETTINGS_MODE_AUTO) : NULL;
        xx_rt_free(pAbsolute);
        xx_rt_free(pAppDir);
    }
    if (!pStore) {
        if (pError && nErrorSize) xx_rt_snprintf(pError, nErrorSize, "Cannot open the settings store");
        return CDIE_UPDATE_ERROR_ARGUMENT;
    }

    upd_replace(&pConfig->settings_location, xx_settings_get_location(pStore));
    upd_native(pConfig->settings_location);
    bIni = xx_settings_get_format(pStore) == XX_SETTINGS_FORMAT_INI;

    /* The Extra/Path key stored by Detect It Easy or an earlier cdie is
     * left alone: not read, and the store is never written. */
    if (xx_settings_load(pStore) == XXFC_OK) {
        pMain = upd_settings_string(pStore, CDIE_UPDATE_KEY_MAIN_PATH);
        pUrl = upd_settings_string(pStore, CDIE_UPDATE_KEY_URL);
    } else if (bIni && xx_settings_get_location(pStore)) {
        upd_lenient_ini(xx_settings_get_location(pStore), &pMain, &pUrl);
    } else {
        if (pError && nErrorSize) {
            xx_rt_snprintf(pError, nErrorSize, "Cannot read the settings in %s",
                           pConfig->settings_location ? pConfig->settings_location : "the native store");
        }
        xx_settings_destroy(pStore);
        return CDIE_UPDATE_ERROR_ARGUMENT;
    }
    xx_settings_destroy(pStore);

    /* Detect It Easy stores its defaults on every exit. "$data/db" is the
     * folder beside the program, which is what the default lookup finds
     * anyway -- but taken as a chosen folder it would switch off the per-user
     * fallback of an installed copy, and diec, which reads no settings, would
     * keep scanning the per-user copy. So the stored defaults count as unset. */
    if (!cdie_is_portable()) {
        if (pMain && (!xx_rt_strcmp(pMain, "$data/db") || !xx_rt_strcmp(pMain, "$data\\db"))) pMain[0] = 0;
    }
    if (upd_empty(pConfig->main_path) && !upd_empty(pMain)) {
        xx_rt_free(pConfig->main_path);
        pConfig->main_path = upd_expand_path(pMain);
    }
    if (upd_empty(pConfig->url) && !upd_empty(pUrl)) upd_replace(&pConfig->url, pUrl);
    xx_rt_free(pMain);
    xx_rt_free(pUrl);
    return CDIE_UPDATE_OK;
}

/* ------------------------------------------------------------------ info */

void cdie_update_info_init(cdie_update_info *pInfo)
{
    if (!pInfo) return;
    xx_rt_memset(pInfo, 0, sizeof(*pInfo));
    pInfo->remote_size = -1;
}

void cdie_update_info_free(cdie_update_info *pInfo)
{
    if (!pInfo) return;
    xx_rt_free(pInfo->source_url);
    xx_rt_free(pInfo->archive_url);
    xx_rt_free(pInfo->archive_name);
    xx_rt_free(pInfo->release_name);
    xx_rt_free(pInfo->release_tag);
    xx_rt_free(pInfo->main_path);
    xx_rt_free(pInfo->installed_source);
    cdie_update_info_init(pInfo);
}

char *cdie_update_display_url(const char *pUrl)
{
    return upd_sanitize(upd_redact_url(pUrl));
}

const char *cdie_update_status_name(cdie_update_status status)
{
    switch (status) {
        case CDIE_UPDATE_OK: return "ok";
        case CDIE_UPDATE_AVAILABLE: return "update-available";
        case CDIE_UPDATE_ERROR_NETWORK: return "network-error";
        case CDIE_UPDATE_ERROR_DATABASE: return "database-error";
        case CDIE_UPDATE_ERROR_ARGUMENT: return "invalid-argument";
        case CDIE_UPDATE_ERROR_VERIFY: return "verification-failed";
        case CDIE_UPDATE_ERROR_CANCELLED: return "cancelled";
        case CDIE_UPDATE_ERROR_BUSY: return "busy";
    }
    return "unknown";
}

/* ----------------------------------------------------------------- stamp */

/* db/die-update.ini, one "Key=Value" per line. Written by this file only, so
 * a plain reader is enough; unknown lines are ignored. */
static void upd_read_stamp(cdie_update_info *pInfo)
{
    char *pPath = upd_join(pInfo->main_path, CDIE_UPDATE_STAMP_NAME);
    int64_t nSize = 0;
    char *pData = pPath ? xx_fs_read_file(pPath, &nSize) : NULL;
    char *pLine = pData;

    xx_rt_free(pPath);
    while (pLine && *pLine) {
        char *pEnd = pLine;
        char *pNext;
        char *pValue;

        while (*pEnd && *pEnd != '\n') ++pEnd;
        pNext = *pEnd ? pEnd + 1 : NULL;
        *pEnd = 0;
        if (pEnd > pLine && pEnd[-1] == '\r') pEnd[-1] = 0;
        pValue = xx_rt_strchr(pLine, '=');
        if (pValue) {
            *pValue++ = 0;
            if (!xx_rt_strcmp(pLine, "Md5")) {
                if (!upd_parse_md5(pValue, xx_rt_strlen(pValue), pInfo->installed_md5)) pInfo->installed_md5[0] = 0;
            } else if (!xx_rt_strcmp(pLine, "Asset")) {
                upd_copy_text(pInfo->installed_asset, sizeof(pInfo->installed_asset), pValue);
            } else if (!xx_rt_strcmp(pLine, "Installed")) {
                upd_copy_text(pInfo->installed_at, sizeof(pInfo->installed_at), pValue);
            } else if (!xx_rt_strcmp(pLine, "Source")) {
                upd_replace(&pInfo->installed_source, pValue);
            }
        }
        pLine = pNext;
    }
    xx_str_free(pData);
}

static int upd_write_file(const char *pPath, const char *pText)
{
    xx_io_device *pDevice = xx_io_file_open(pPath, "wb");
    size_t nLength = xx_rt_strlen(pText);
    int bOk;

    if (!pDevice) return 0;
    bOk = xx_io_write(pDevice, pText, nLength) == (ssize_t)nLength;
    if (xx_io_close(pDevice) != 0) bOk = 0;
    return bOk;
}

/* Written beside the stamp and renamed over it, so a reader never sees half
 * a record. Addresses are recorded without credentials. */
static int upd_write_stamp(const char *pDatabase, const cdie_update_info *pInfo, const char *pMd5, const char *pTime)
{
    char *pPath = upd_join(pDatabase, CDIE_UPDATE_STAMP_NAME);
    char *pTemporary = upd_join(pDatabase, CDIE_UPDATE_STAMP_NAME ".tmp");
    char *pSource = upd_redact_url(pInfo->source_url ? pInfo->source_url : "");
    char *pArchive = upd_redact_url(pInfo->archive_url ? pInfo->archive_url : "");
    xx_buf_t text;
    int bOk;

    xx_buf_init(&text);
    xx_buf_append_str(&text, "; Written by die-update. Identifies the archive this database came from.\n[Update]\n");
    xx_buf_appendf(&text, "Source=%s\n", pSource ? pSource : "");
    xx_buf_appendf(&text, "Archive=%s\n", pArchive ? pArchive : "");
    if (pInfo->release_tag) xx_buf_appendf(&text, "Release=%s\n", pInfo->release_tag);
    xx_buf_appendf(&text, "Md5=%s\n", pMd5);
    if (pInfo->remote_asset[0]) xx_buf_appendf(&text, "Asset=%s\n", pInfo->remote_asset);
    xx_buf_appendf(&text, "Installed=%s\n", pTime);
    bOk = pPath && pTemporary && text.data && upd_write_file(pTemporary, text.data) && xx_io_file_replace_a(pTemporary, pPath, true);
    if (!bOk && pTemporary) (void)xx_io_file_remove_a(pTemporary);
    xx_buf_free(&text);
    xx_rt_free(pSource);
    xx_rt_free(pArchive);
    xx_rt_free(pTemporary);
    xx_rt_free(pPath);
    return bOk;
}

/* ---------------------------------------------------------------- GitHub */

static void upd_github_init(xx_github_t *pGh, const cdie_update_config *pConfig, xx_pd_struct *pPd)
{
    xx_github_init(pGh);
    xx_str_free(pGh->user_agent);
    pGh->user_agent = xx_str_dup("die-update/" X_APPLICATIONVERSION " xxgithub/" XXGITHUB_VERSION_STRING);
    if (pConfig->no_proxy) pGh->proxy_mode = XX_NET_PROXY_NONE;
    if (pConfig->timeout_ms) pGh->timeout_ms = pConfig->timeout_ms;
    pGh->pd = pPd;
}

static cdie_update_status upd_github_failure(cdie_update_info *pInfo, const xx_github_t *pGh, xx_github_status_t status, const char *pWhat)
{
    const char *pHint = "";

    if (status == XX_GITHUB_ERROR_CANCELLED) {
        upd_message(pInfo, "Cancelled");
        return CDIE_UPDATE_ERROR_CANCELLED;
    }
#if !defined(_WIN32)
    if (pGh->net_status == XX_NET_ERROR_UNSUPPORTED) pHint = " (HTTPS needs libcurl: install the libcurl package)";
#endif
    upd_message(pInfo, "%s: %s%s", pWhat, pGh->error_message[0] ? pGh->error_message : xx_github_status_string(status), pHint);
    if (status == XX_GITHUB_ERROR_IO) return CDIE_UPDATE_ERROR_DATABASE;
    if (status == XX_GITHUB_ERROR_ARGUMENT) return CDIE_UPDATE_ERROR_ARGUMENT;
    return CDIE_UPDATE_ERROR_NETWORK;
}

/* The MD5 published next to the archive. Returns OK with sMd5 filled, OK with
 * sMd5 empty when there is none, or an error. A release asset that is listed
 * as the checksum (@p bListed) must hold one; a guessed "<url>.md5" that
 * answers with something else (a web server's catch-all page) is ignored. */
static cdie_update_status upd_fetch_md5(xx_github_t *pGh, const char *pUrl, int bListed, cdie_update_info *pInfo, char sMd5[33])
{
    xx_buf_t content;
    xx_github_status_t status;

    sMd5[0] = 0;
    xx_buf_init(&content);
    status = xx_github_get_web_file(pGh, pUrl, &content, NULL);
    if (status == XX_GITHUB_OK) {
        int bOk = content.data && upd_parse_md5(content.data, content.size, sMd5);
        xx_buf_free(&content);
        if (!bOk) {
            sMd5[0] = 0;
            if (!bListed) return CDIE_UPDATE_OK;
            upd_message(pInfo, "%s does not hold an MD5 checksum", pUrl);
            return CDIE_UPDATE_ERROR_VERIFY;
        }
        return CDIE_UPDATE_OK;
    }
    xx_buf_free(&content);
    /* No checksum published: 404, or the 403 some storage services return
     * for a missing object. */
    if (status == XX_GITHUB_NOT_FOUND || status == XX_GITHUB_ERROR_AUTH) return CDIE_UPDATE_OK;
    return upd_github_failure(pInfo, pGh, status, "Cannot read the checksum");
}

static const char *upd_file_name(const char *pPath)
{
    const char *pName = pPath;
    const char *p;

    for (p = pPath; *p; ++p) {
        if (upd_is_separator(*p)) pName = p + 1;
    }
    return pName;
}

/* Whether a URL the release parser accepted really names a release. Off
 * github.com a plain "https://host/dir/db.zip" has the owner/repository shape
 * too, so there the path must carry the releases component. */
static int upd_is_release_source(const xx_github_release_url_t *pUrl, const char *pText)
{
    char sNeedle[256];

    if (!xx_rt_strcmp(pUrl->web_base, "https://github.com") || pUrl->tag) return 1;
    xx_rt_snprintf(sNeedle, sizeof(sNeedle), "/%s/%s/releases", pUrl->owner, pUrl->repo);
    return xx_rt_strstr(pText, sNeedle) != NULL;
}

/* Fills archive_url, archive_name, remote_* and release_* from the source. */
static cdie_update_status upd_query_source(const cdie_update_config *pConfig, cdie_update_info *pInfo, xx_github_t *pGh)
{
    xx_github_release_url_t release_url;
    cdie_update_status result = CDIE_UPDATE_OK;

    xx_github_release_url_init(&release_url);

    if (xx_github_parse_release_url(pInfo->source_url, &release_url) && upd_is_release_source(&release_url, pInfo->source_url)) {
        xx_github_release_t release;
        xx_github_status_t status;
        const char *pArchive = release_url.asset ? release_url.asset : CDIE_UPDATE_ARCHIVE_NAME;
        char *pChecksumName = NULL;

        upd_replace(&pInfo->archive_name, pArchive);
        upd_sanitize(pInfo->archive_name);
        xx_github_set_api_base(pGh, upd_empty(pConfig->api_base) ? release_url.api_base : pConfig->api_base);
        xx_github_release_init(&release);
        if (release_url.latest) {
            status = xx_github_get_latest_release(pGh, release_url.owner, release_url.repo, false, &release);
        } else {
            status = xx_github_get_release_by_tag(pGh, release_url.owner, release_url.repo, release_url.tag, &release);
        }
        pChecksumName = cdie_strdup(pArchive);
        if (pChecksumName) {
            size_t n = xx_rt_strlen(pChecksumName);
            char *p = (char *)xx_rt_realloc(pChecksumName, n + 5);
            if (p) {
                xx_rt_memcpy(p + n, ".md5", 5);
                pChecksumName = p;
            }
        }

        if (status == XX_GITHUB_OK) {
            const xx_github_asset_t *pAsset = xx_github_release_find_asset(&release, pArchive);
            const xx_github_asset_t *pChecksum = pChecksumName ? xx_github_release_find_asset(&release, pChecksumName) : NULL;

            upd_replace(&pInfo->release_tag, release.tag);
            upd_replace(&pInfo->release_name, release.name);
            upd_sanitize(pInfo->release_tag);
            upd_sanitize(pInfo->release_name);
            if (!pAsset) {
                upd_message(pInfo, "The release %s has no %s", release.tag ? release.tag : "", pArchive);
                result = CDIE_UPDATE_ERROR_NETWORK;
            } else {
                char *pDirect = xx_github_release_download_url(&release_url, release.tag, pArchive);

                /* The browser address needs no API call and does not count
                 * against the API rate limit. */
                upd_replace(&pInfo->archive_url, pAsset->download_url ? pAsset->download_url : pDirect);
                xx_str_free(pDirect);
                pInfo->remote_size = pAsset->size;
                pInfo->remote_time = pAsset->updated_at;
                xx_rt_snprintf(pInfo->remote_asset, sizeof(pInfo->remote_asset), "%lld:%lld:%lld", (long long)pAsset->id,
                               (long long)pAsset->size, (long long)pAsset->updated_at);
                if (pChecksum) {
                    char *pChecksumUrl = pChecksum->download_url ? NULL : xx_github_release_download_url(&release_url, release.tag, pChecksumName);
                    result = upd_fetch_md5(pGh, pChecksum->download_url ? pChecksum->download_url : pChecksumUrl, 1, pInfo, pInfo->remote_md5);
                    xx_str_free(pChecksumUrl);
                }
            }
        } else if (status == XX_GITHUB_ERROR_CANCELLED) {
            result = upd_github_failure(pInfo, pGh, status, "Cannot read the release");
        } else if ((release_url.asset && status != XX_GITHUB_ERROR_NETWORK) ||
                   (!release_url.latest && (status == XX_GITHUB_ERROR_RATE_LIMIT || status == XX_GITHUB_ERROR_API ||
                                            status == XX_GITHUB_ERROR_AUTH || status == XX_GITHUB_ERROR_RESPONSE))) {
            /* The API is unavailable (most often the 60 requests an hour an
             * anonymous address gets), but the download addresses of a tagged
             * release do not need it. A download address given directly is
             * used as is even where no API answers it at all (a mirror). */
            char *pArchiveUrl = xx_github_release_download_url(&release_url, release_url.tag, pArchive);
            char *pChecksumUrl = pChecksumName ? xx_github_release_download_url(&release_url, release_url.tag, pChecksumName) : NULL;

            upd_replace(&pInfo->release_tag, release_url.tag);
            upd_replace(&pInfo->archive_url, pArchiveUrl);
            if (pChecksumUrl) result = upd_fetch_md5(pGh, pChecksumUrl, 0, pInfo, pInfo->remote_md5);
            if (!pArchiveUrl) {
                upd_message(pInfo, "Cannot build the download address");
                result = CDIE_UPDATE_ERROR_ARGUMENT;
            }
            xx_str_free(pArchiveUrl);
            xx_str_free(pChecksumUrl);
        } else {
            result = upd_github_failure(pInfo, pGh, status, status == XX_GITHUB_NOT_FOUND ? "Release not found" : "Cannot read the release");
        }
        xx_rt_free(pChecksumName);
        xx_github_release_free(&release);
    } else {
        /* Any other http(s) address names the archive itself; its checksum,
         * when published, sits beside it as "<url>.md5". */
        xx_net_url_t parsed;

        xx_net_url_init(&parsed);
        if (xx_net_url_parse(pInfo->source_url, &parsed) != XX_NET_OK || !parsed.scheme ||
            (xx_rt_strcmp(parsed.scheme, "https") && xx_rt_strcmp(parsed.scheme, "http"))) {
            upd_message(pInfo, "Not an http or https address: %s", pInfo->source_url);
            result = CDIE_UPDATE_ERROR_ARGUMENT;
        } else {
            const char *pName = upd_file_name(parsed.path ? parsed.path : "");

            upd_replace(&pInfo->archive_url, pInfo->source_url);
            upd_replace(&pInfo->archive_name, pName[0] ? pName : CDIE_UPDATE_ARCHIVE_NAME);
            upd_sanitize(pInfo->archive_name);
            if (!parsed.query && !parsed.fragment) {
                char *pChecksumUrl = xx_str_concat(pInfo->source_url, ".md5");
                if (pChecksumUrl) result = upd_fetch_md5(pGh, pChecksumUrl, 0, pInfo, pInfo->remote_md5);
                xx_str_free(pChecksumUrl);
            }
        }
        xx_net_url_free(&parsed);
    }

    xx_github_release_url_free(&release_url);
    return result;
}

/* ----------------------------------------------------------------- check */

/* A folder an update may replace: missing, empty, written by die-update, or
 * shaped like a signature database (an _init or *.sg at its top, or one of
 * the engine's file-type folders). A replaced folder is deleted, so anything
 * else -- a mistyped -D, a project's own "db" folder -- is refused. */
static int upd_looks_like_database(const char *pPath)
{
    static const char *const ppTypes[] = {"Binary", "COM", "MSDOS", "NE", "LE", "LX", "PE", "ELF", "MACH", "MACHOFAT", "Archive", "ZIP",
                                          "JAR", "APK", "IPA", "DEX", "NPM", "Amiga", "AtariST", "DOS16M", "DOS4G", "PDF", "CFBF",
                                          "Image", "JPEG", "PNG", "RAR", "ISO9660", "JavaClass", "PYC"};
    xx_list_t entries;
    size_t i;
    size_t j;
    size_t nCount;
    int bResult = 0;

    if (!xx_fs_exists(pPath)) return 1;
    if (!xx_fs_is_dir(pPath) || !xx_list_init(&entries, sizeof(xx_fs_entry_t *), NULL)) return 0;
    if (xx_fs_list_dir(pPath, &entries)) {
        nCount = xx_list_count(&entries);
        if (!nCount) bResult = 1;
        for (i = 0; i < nCount; ++i) {
            xx_fs_entry_t *pEntry = *(xx_fs_entry_t **)xx_list_at(&entries, i);
            size_t nName = pEntry ? xx_rt_strlen(pEntry->name) : 0;

            if (!pEntry) continue;
            if (pEntry->type == XX_FS_ENTRY_FILE) {
                if (!xx_rt_strcmp(pEntry->name, CDIE_UPDATE_STAMP_NAME) || !xx_rt_strcmp(pEntry->name, "_init") ||
                    (nName > 3 && !xx_rt_strcmp(pEntry->name + nName - 3, ".sg"))) {
                    bResult = 1;
                }
            } else {
                for (j = 0; j < sizeof(ppTypes) / sizeof(ppTypes[0]); ++j) {
                    if (!xx_rt_strcmp(pEntry->name, ppTypes[j])) bResult = 1;
                }
            }
        }
        for (i = 0; i < nCount; ++i) xx_fs_entry_free(*(xx_fs_entry_t **)xx_list_at(&entries, i));
    }
    xx_list_cleanup(&entries);
    return bResult;
}

/* Folders an update must never replace, whatever they hold: a filesystem
 * root, the home folder, the program's own folder or one above it, and the
 * current folder or one above it (renaming those would move the user's work
 * or the running program). */
static const char *upd_protected_target(const char *pPath)
{
    char *pTarget = upd_canonical(pPath);
    const char *pReason = NULL;
    char *pAppDir = cdie_app_dir();
    char *pApp = pAppDir ? upd_canonical(pAppDir) : NULL;
    char *pCurrent = xx_fs_current_dir();
    char *pCwd = pCurrent ? upd_canonical(pCurrent) : NULL;
#if defined(_WIN32)
    const char *pHomeVariable = "USERPROFILE";
#else
    const char *pHomeVariable = "HOME";
#endif
    char *pHomeText = NULL;
    char *pHome = NULL;

#if defined(_WIN32)
    {
        void *pName = xx_rt_utf8_to_utf16(pHomeVariable);
        DWORD nSize = pName ? GetEnvironmentVariableW((LPCWSTR)pName, NULL, 0) : 0;
        WCHAR *pValue = nSize > 1 ? (WCHAR *)xx_rt_malloc((size_t)nSize * sizeof(WCHAR)) : NULL;
        if (pValue && GetEnvironmentVariableW((LPCWSTR)pName, pValue, nSize)) pHomeText = xx_rt_utf16_to_utf8(pValue);
        xx_rt_free(pValue);
        xx_rt_free(pName);
    }
#else
    if (getenv(pHomeVariable) && getenv(pHomeVariable)[0]) pHomeText = cdie_strdup(getenv(pHomeVariable));
#endif
    pHome = pHomeText ? upd_canonical(pHomeText) : NULL;

    if (!pTarget) {
        pReason = "it cannot be resolved";
    } else if (!xx_rt_strcmp(pTarget, "/") || (xx_rt_strlen(pTarget) <= 3 && pTarget[0] && pTarget[1] == ':')) {
        pReason = "it is the root of a drive";
    } else if (pHome && !xx_rt_strcmp(pTarget, pHome)) {
        pReason = "it is the home folder";
    } else if (pApp && upd_within(pApp, pTarget)) {
        pReason = "it holds this program";
    } else if (pCwd && upd_within(pCwd, pTarget)) {
        pReason = "it holds the current folder";
    }
    xx_rt_free(pTarget);
    xx_rt_free(pAppDir);
    xx_rt_free(pApp);
    xx_str_free(pCurrent);
    xx_rt_free(pCwd);
    xx_rt_free(pHomeText);
    xx_rt_free(pHome);
    return pReason;
}

/* Nonzero when the folder is named db_extra (any case on Windows and macOS). */
static int upd_is_extra_folder(const char *pPath)
{
    char *pCanonical = upd_canonical(pPath);
    int bResult = pCanonical && !xx_rt_strcmp(upd_file_name(pCanonical), "db_extra");

    xx_rt_free(pCanonical);
    return bResult;
}

static cdie_update_status upd_check_target(cdie_update_info *pInfo, const char *pPath, const char *pName)
{
    const char *pReason = upd_protected_target(pPath);

    if (pReason) {
        upd_message(pInfo, "Refusing to replace %s: %s", pPath, pReason);
        return CDIE_UPDATE_ERROR_ARGUMENT;
    }
    /* cdie no longer uses an extra database. An old db_extra folder (one an
     * earlier die-update installed looks like a database) may hold the only
     * copy of the user's own rules, so it is never replaced, even when -D
     * names it. */
    if (upd_is_extra_folder(pPath)) {
        upd_message(pInfo, "Refusing to replace %s: cdie no longer uses the extra database; -D names the main database folder (db)",
                    pPath);
        return CDIE_UPDATE_ERROR_ARGUMENT;
    }
    if (!upd_looks_like_database(pPath)) {
        upd_message(pInfo, "Refusing to replace %s: it does not look like a signature database (%s); name the database folder itself",
                    pPath, pName);
        return CDIE_UPDATE_ERROR_ARGUMENT;
    }
    return CDIE_UPDATE_OK;
}

static cdie_update_status upd_prepare(const cdie_update_config *pConfig, cdie_update_info *pInfo)
{
    char *pMain;
    const char *pUrl = pConfig->url;

    if (upd_empty(pUrl) || !xx_rt_strcmp(pUrl, CDIE_UPDATE_LEGACY_URL)) pUrl = CDIE_UPDATE_DEFAULT_URL;
    upd_replace(&pInfo->source_url, pUrl);

    if (!upd_empty(pConfig->main_path)) {
        pMain = upd_dup(pConfig->main_path);
        pInfo->main_is_default = 0;
    } else {
        /* Where the scanners look (cdie_paths.h); with nothing there yet, the
         * current folder. */
        pMain = cdie_find_database_dir("db");
        if (!pMain) pMain = upd_dup("db");
        pInfo->main_is_default = 1;
    }
    xx_rt_free(pInfo->main_path);
    pInfo->main_path = pMain ? upd_absolute(pMain) : NULL;
    xx_rt_free(pMain);

    /* The main database is the only target; an extra folder in the
     * configuration is ignored. */
    if (!pInfo->source_url || !pInfo->main_path) {
        upd_message(pInfo, "Out of memory");
        return CDIE_UPDATE_ERROR_ARGUMENT;
    }
    if (xx_fs_is_file(pInfo->main_path)) {
        upd_message(pInfo, "%s is a file; a TAR database cannot be updated, only a database folder", pInfo->main_path);
        return CDIE_UPDATE_ERROR_ARGUMENT;
    }
    {
        cdie_update_status status = upd_check_target(pInfo, pInfo->main_path, "db");
        if (status != CDIE_UPDATE_OK) return status;
    }

    pInfo->has_database = xx_fs_is_dir(pInfo->main_path);
    pInfo->installed_md5[0] = 0;
    pInfo->installed_asset[0] = 0;
    pInfo->installed_at[0] = 0;
    if (pInfo->has_database) upd_read_stamp(pInfo);
    return CDIE_UPDATE_OK;
}

static void upd_decide(cdie_update_info *pInfo)
{
    if (!pInfo->has_database) {
        pInfo->update_available = 1;
        upd_message(pInfo, "No database at %s", pInfo->main_path);
    } else if (pInfo->remote_md5[0] && pInfo->installed_md5[0]) {
        pInfo->update_available = xx_rt_strcmp(pInfo->remote_md5, pInfo->installed_md5) != 0;
    } else if (pInfo->remote_asset[0] && pInfo->installed_asset[0]) {
        pInfo->update_available = xx_rt_strcmp(pInfo->remote_asset, pInfo->installed_asset) != 0;
    } else {
        pInfo->update_available = 1;
        if (!pInfo->installed_md5[0] && !pInfo->installed_asset[0]) {
            upd_message(pInfo, "The installed database has no update record");
        } else {
            upd_message(pInfo, "The source does not identify its version");
        }
        return;
    }
    if (pInfo->has_database) {
        upd_message(pInfo, pInfo->update_available ? "An update is available" : "The database is up to date");
    }
}

static cdie_update_status upd_check(const cdie_update_config *pConfig, cdie_update_info *pInfo, xx_github_t *pGh)
{
    cdie_update_status status = upd_prepare(pConfig, pInfo);

    if (status != CDIE_UPDATE_OK) return status;
    status = upd_query_source(pConfig, pInfo, pGh);
    if (status != CDIE_UPDATE_OK) return status;
    upd_decide(pInfo);
    return pInfo->update_available ? CDIE_UPDATE_AVAILABLE : CDIE_UPDATE_OK;
}

cdie_update_status cdie_update_check(const cdie_update_config *pConfig, cdie_update_info *pInfo, xx_pd_struct *pPd)
{
    xx_github_t gh;
    cdie_update_status status;

    if (!pConfig || !pInfo) return CDIE_UPDATE_ERROR_ARGUMENT;
    cdie_update_info_free(pInfo);
    upd_github_init(&gh, pConfig, pPd);
    status = upd_check(pConfig, pInfo, &gh);
    xx_github_free(&gh);
    return status;
}

/* ------------------------------------------------------------------ lock */

typedef struct upd_lock_s {
#if defined(_WIN32)
    HANDLE handle;
#else
    int fd;
    char *path;
#endif
} upd_lock;

/* An exclusive lock beside the database: a second update -- the GUI's while a
 * console one runs -- reports BUSY instead of racing it. The operating system
 * drops the lock when a process dies, so a crash never leaves it stuck. */
static cdie_update_status upd_lock_acquire(const char *pDir, upd_lock *pLock)
{
    char *pPath = upd_join(pDir, UPD_LOCK_NAME);
#if defined(_WIN32)
    void *pWide = pPath ? xx_rt_utf8_to_utf16(pPath) : NULL;
    DWORD nError;

    pLock->handle = INVALID_HANDLE_VALUE;
    xx_rt_free(pPath);
    if (!pWide) return CDIE_UPDATE_ERROR_DATABASE;
    pLock->handle = CreateFileW((LPCWSTR)pWide, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                                FILE_ATTRIBUTE_HIDDEN | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    nError = GetLastError();
    xx_rt_free(pWide);
    if (pLock->handle != INVALID_HANDLE_VALUE) return CDIE_UPDATE_OK;
    if (nError == ERROR_SHARING_VIOLATION) return CDIE_UPDATE_ERROR_BUSY;
    /* ACCESS_DENIED also reports a lock file whose deletion is pending (its
     * holder just finished): busy only when such a file is there. Otherwise
     * the folder takes new folders but not new files -- the root of a
     * system drive does that for ordinary users. */
    if (nError == ERROR_ACCESS_DENIED) {
        char *pAgain = upd_join(pDir, UPD_LOCK_NAME);
        void *pAgainWide = pAgain ? xx_rt_utf8_to_utf16(pAgain) : NULL;
        int bExists = pAgainWide && GetFileAttributesW((LPCWSTR)pAgainWide) != INVALID_FILE_ATTRIBUTES;
        xx_rt_free(pAgainWide);
        xx_rt_free(pAgain);
        return bExists ? CDIE_UPDATE_ERROR_BUSY : CDIE_UPDATE_ERROR_DATABASE;
    }
    return CDIE_UPDATE_ERROR_DATABASE;
#else
    struct flock lock;
    int nTry;

    pLock->fd = -1;
    pLock->path = pPath;
    if (!pPath) return CDIE_UPDATE_ERROR_DATABASE;
    for (nTry = 0; nTry < 10; ++nTry) {
        struct stat opened;
        struct stat named;

        /* O_NOFOLLOW: a link planted under the lock's name is not followed. */
        pLock->fd = open(pPath, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0644);
        if (pLock->fd < 0) return CDIE_UPDATE_ERROR_DATABASE;
        xx_rt_memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        if (fcntl(pLock->fd, F_SETLK, &lock) != 0) {
            int bBusy = errno == EACCES || errno == EAGAIN;
            close(pLock->fd);
            pLock->fd = -1;
            return bBusy ? CDIE_UPDATE_ERROR_BUSY : CDIE_UPDATE_ERROR_DATABASE;
        }
        /* The holder unlinks the file when it is done. A lock taken on a file
         * opened just before that protects nothing: the next updater creates
         * a new file. Only a lock on the file the name still refers to counts. */
        if (fstat(pLock->fd, &opened) == 0 && stat(pPath, &named) == 0 && opened.st_dev == named.st_dev && opened.st_ino == named.st_ino) {
            return CDIE_UPDATE_OK;
        }
        close(pLock->fd);
        pLock->fd = -1;
    }
    return CDIE_UPDATE_ERROR_BUSY;
#endif
}

static void upd_lock_release(upd_lock *pLock)
{
#if defined(_WIN32)
    if (pLock->handle != INVALID_HANDLE_VALUE) CloseHandle(pLock->handle);
    pLock->handle = INVALID_HANDLE_VALUE;
#else
    if (pLock->fd >= 0) {
        unlink(pLock->path);
        close(pLock->fd);
    }
    pLock->fd = -1;
    xx_rt_free(pLock->path);
    pLock->path = NULL;
#endif
}

/* --------------------------------------------------------------- staging */

/* A rename can fail for a moment on Windows while an indexer or a virus
 * scanner holds a handle inside the folder. */
static int upd_rename(const char *pFrom, const char *pTo)
{
    int nTry;

    for (nTry = 0; nTry < 30; ++nTry) {
        if (xx_fs_rename(pFrom, pTo)) return 1;
#if defined(_WIN32)
        xx_thread_sleep(100);
#else
        break;
#endif
    }
    return 0;
}

static char *upd_make_stage(const char *pParent)
{
    unsigned nTry;
    long long nClock = xx_rt_clock_ms();

    for (nTry = 0; nTry < 100; ++nTry) {
        char sName[64];
        char *pStage;

        xx_rt_snprintf(sName, sizeof(sName), UPD_STAGE_PREFIX "%llx-%u", (unsigned long long)nClock, nTry);
        pStage = upd_join(pParent, sName);
        if (pStage && xx_fs_make_dir(pStage)) return pStage;
        xx_rt_free(pStage);
        if (!xx_fs_is_dir(pParent)) break;
    }
    return NULL;
}

static int upd_starts_with(const char *pText, const char *pPrefix)
{
    return !xx_rt_strncmp(pText, pPrefix, xx_rt_strlen(pPrefix));
}

/* <stage>/targets.ini records, before anything is moved, where every
 * "old-<name>" folder of the stage came from: "old-db=<absolute target>". */
#define UPD_TARGETS_NAME "targets.ini"

/* The recorded origin of <stage>/<pOldName>, or NULL. */
static char *upd_stage_origin(const char *pStage, const char *pOldName)
{
    char *pPath = upd_join(pStage, UPD_TARGETS_NAME);
    int64_t nSize = 0;
    char *pData = pPath ? xx_fs_read_file(pPath, &nSize) : NULL;
    char *pLine = pData;
    char *pResult = NULL;
    size_t nName = xx_rt_strlen(pOldName);

    xx_rt_free(pPath);
    while (pLine && *pLine && !pResult) {
        char *pEnd = pLine;
        char *pNext;

        while (*pEnd && *pEnd != '\n') ++pEnd;
        pNext = *pEnd ? pEnd + 1 : NULL;
        *pEnd = 0;
        if (pEnd > pLine && pEnd[-1] == '\r') pEnd[-1] = 0;
        if (!xx_rt_strncmp(pLine, pOldName, nName) && pLine[nName] == '=' && pLine[nName + 1]) pResult = upd_dup(pLine + nName + 1);
        pLine = pNext;
    }
    xx_str_free(pData);
    return pResult;
}

/* Leftovers of an earlier run that was killed. Called with the lock held, so
 * no other update is using them. An old database still in a stage goes back
 * to its recorded place when that is empty (the run died between the two
 * renames); when the place is taken, the swap had finished and the copy is
 * the replaced one. A stage is deleted only when no old database is left in
 * it that could not be put back -- never the only copy of a database. */
static void upd_clean_stages(const char *pParent, const char *pKeep)
{
    xx_list_t entries;
    size_t i;
    size_t nCount;

    if (!xx_list_init(&entries, sizeof(xx_fs_entry_t *), NULL)) return;
    if (xx_fs_list_dir(pParent, &entries)) {
        nCount = xx_list_count(&entries);
        for (i = 0; i < nCount; ++i) {
            xx_fs_entry_t *pEntry = *(xx_fs_entry_t **)xx_list_at(&entries, i);
            xx_list_t inner;
            size_t j;
            int bKeep = 0;

            /* Compare names, not paths: the listing joins with '/', while the
             * stage path carries the native separator. */
            if (!pEntry || pEntry->type != XX_FS_ENTRY_DIR || !upd_starts_with(pEntry->name, UPD_STAGE_PREFIX) ||
                (pKeep && !xx_rt_strcmp(pEntry->name, upd_file_name(pKeep)))) {
                continue;
            }
            if (!xx_list_init(&inner, sizeof(xx_fs_entry_t *), NULL)) continue;
            if (!xx_fs_list_dir(pEntry->path, &inner)) bKeep = 1;
            for (j = 0; j < xx_list_count(&inner); ++j) {
                xx_fs_entry_t *pOld = *(xx_fs_entry_t **)xx_list_at(&inner, j);
                if (pOld && pOld->type == XX_FS_ENTRY_DIR && upd_starts_with(pOld->name, "old-")) {
                    char *pOrigin = upd_stage_origin(pEntry->path, pOld->name);
                    if (!pOrigin) {
                        bKeep = 1;
                    } else if (!xx_fs_exists(pOrigin)) {
                        char *pFrom = upd_native(cdie_strdup(pOld->path));
                        if (!pFrom || !upd_rename(pFrom, pOrigin)) bKeep = 1;
                        xx_rt_free(pFrom);
                    }
                    xx_rt_free(pOrigin);
                }
                xx_fs_entry_free(pOld);
            }
            xx_list_cleanup(&inner);
            if (!bKeep) (void)xx_fs_remove_tree(pEntry->path);
        }
        for (i = 0; i < nCount; ++i) xx_fs_entry_free(*(xx_fs_entry_t **)xx_list_at(&entries, i));
    }
    xx_list_cleanup(&entries);
}

/* ---------------------------------------------------------------- verify */

static cdie_update_status upd_verify_archive(const char *pZip, cdie_update_info *pInfo, char sMd5[33], xx_pd_struct *pPd)
{
    xx_io_device *pDevice = xx_io_file_open(pZip, "rb");
    unsigned char digest[16];
    int64_t nSize;
    char *pHex;
    int i;

    if (!pDevice) {
        upd_message(pInfo, "Cannot open the downloaded %s", pInfo->archive_name);
        return CDIE_UPDATE_ERROR_DATABASE;
    }
    nSize = xx_io_total_size(pDevice);
    if (pInfo->remote_size >= 0 && nSize != pInfo->remote_size) {
        xx_io_close(pDevice);
        upd_message(pInfo, "The download is %lld bytes, the release lists %lld", (long long)nSize, (long long)pInfo->remote_size);
        return CDIE_UPDATE_ERROR_VERIFY;
    }
    if (!xx_md5_device(pDevice, 0, -1, digest, sizeof(digest), pPd)) {
        xx_io_close(pDevice);
        if (xx_pd_is_stopped(pPd)) {
            upd_message(pInfo, "Cancelled");
            return CDIE_UPDATE_ERROR_CANCELLED;
        }
        upd_message(pInfo, "Cannot read the downloaded %s", pInfo->archive_name);
        return CDIE_UPDATE_ERROR_VERIFY;
    }
    xx_io_close(pDevice);
    pHex = xx_md5_digest_hex(digest);
    if (!pHex) {
        upd_message(pInfo, "Out of memory");
        return CDIE_UPDATE_ERROR_VERIFY;
    }
    for (i = 0; i < 32 && pHex[i]; ++i) sMd5[i] = (pHex[i] >= 'A' && pHex[i] <= 'F') ? (char)(pHex[i] - 'A' + 'a') : pHex[i];
    sMd5[32] = 0;
    xx_str_free(pHex);
    if (pInfo->remote_md5[0] && xx_rt_strcmp(pInfo->remote_md5, sMd5)) {
        upd_message(pInfo, "MD5 mismatch: expected %s, got %s", pInfo->remote_md5, sMd5);
        return CDIE_UPDATE_ERROR_VERIFY;
    }
    return CDIE_UPDATE_OK;
}

/* The name of a ZIP member as UTF-8. ZIP records carry it as a wide string. */
static char *upd_record_name(const xx_archive_record *pRecord)
{
    const char *pName = xx_archive_record_get_original_name(pRecord);
    const wchar_t *pWide;
    char *pUtf8;
    char *pResult;

    if (pName && pName[0]) return upd_dup(pName);
    pWide = xx_archive_record_get_original_name_w(pRecord);
    if (!pWide || !pWide[0]) return NULL;
    pUtf8 = xx_str_unicode_to_utf8(pWide);
    pResult = pUtf8 ? cdie_strdup(pUtf8) : NULL;
    xx_str_free(pUtf8);
    return pResult;
}

/* Extract every member whose first path component is one of ppNames into
 * pRoot. pbFound[i] is set when a member of ppNames[i] was seen. */
static cdie_update_status upd_extract(const char *pZip, const char *pRoot, const char *const *ppNames, size_t nNames, int *pbFound,
                                      cdie_update_info *pInfo, xx_pd_struct *pPd)
{
    xx_io_device *pDevice = xx_io_file_open(pZip, "rb");
    xx_zip *pArchive = pDevice ? xx_zip_create(pDevice, 0) : NULL;
    Abstractformat *pFormat = pArchive ? xx_zip_to_format(pArchive) : NULL;
    xx_archive_record_state *pState = NULL;
    xx_io_extract_scope scope;
    xx_list_t options;
    xx_meta meta;
    cdie_update_status result = CDIE_UPDATE_OK;
    int64_t nVisited = 0;
    uint64_t nExtracted = 0;
    unsigned long nMembers = 0;
    int nLevel = -1;
    int bScope = 0;

    xx_rt_memset(&scope, 0, sizeof(scope));
    xx_list_init(&options, sizeof(xx_meta), xx_meta_free_elem);
    if (!pFormat) {
        upd_message(pInfo, "%s is not a zip archive", pInfo->archive_name);
        result = CDIE_UPDATE_ERROR_VERIFY;
        goto done;
    }

    /* A fresh folder, so nothing is ever overwritten. The reader refuses
     * members that would leave it ("..", device names), which fails the whole
     * update; an absolute or drive-qualified name never starts with "db", so
     * it is skipped below before the reader sees it, as is every member of
     * another top folder (the extra or the custom database, ...). */
    xx_meta_init(&meta, XX_META_ID_OPT_UNPACK_PATH);
    if (!xx_var_set_str(&meta.var, pRoot) || !xx_list_append(&options, &meta)) {
        xx_meta_cleanup(&meta);
        upd_message(pInfo, "Out of memory");
        result = CDIE_UPDATE_ERROR_VERIFY;
        goto done;
    }
    /* The folder is private and fresh, so replacing never meets anything
     * the update did not write; and "replace" publishes with a plain rename,
     * where "no replace" needs hard links (absent on FAT, exFAT, many SMB and
     * VM shares). */
    xx_meta_init(&meta, XX_META_ID_OPT_OVERWRITE);
    xx_var_set_bool(&meta.var, true);
    if (!xx_list_append(&options, &meta)) {
        xx_meta_cleanup(&meta);
        upd_message(pInfo, "Out of memory");
        result = CDIE_UPDATE_ERROR_VERIFY;
        goto done;
    }
    xx_meta_init(&meta, XX_META_ID_OPT_MAX_MEMBER_SIZE);
    xx_var_set_u64(&meta.var, UPD_MAX_MEMBER_SIZE);
    if (!xx_list_append(&options, &meta)) {
        xx_meta_cleanup(&meta);
        upd_message(pInfo, "Out of memory");
        result = CDIE_UPDATE_ERROR_VERIFY;
        goto done;
    }

    bScope = xx_io_extract_begin(&scope, pRoot);
    pState = xx_format_create_archive_records_reading(pFormat, (const xx_list_s *)&options, pPd);
    if (!pState) {
        upd_message(pInfo, "%s is not a readable zip archive", pInfo->archive_name);
        result = CDIE_UPDATE_ERROR_VERIFY;
        goto done;
    }
    nLevel = xx_pd_enter_level(pPd, pState->total_records > 0 ? (uint64_t)pState->total_records : 0, "Extracting");

    if (pState->has_record) {
        do {
            const xx_archive_record *pRecord = xx_format_get_current_archive_record(pFormat, pState);
            char *pName = pRecord ? upd_record_name(pRecord) : NULL;
            size_t nTop = 0;
            size_t i;

            ++nVisited;
            if (pName) {
                while (pName[nTop] && !upd_is_separator(pName[nTop])) ++nTop;
                for (i = 0; i < nNames; ++i) {
                    if (xx_rt_strlen(ppNames[i]) == nTop && !xx_rt_strncmp(pName, ppNames[i], nTop)) break;
                }
                if (i < nNames) {
                    nExtracted += xx_archive_record_get_meta_u64(pRecord, XX_META_ID_UNCOMPRESSED_SIZE, 0);
                    if (++nMembers > UPD_MAX_MEMBERS || nExtracted > UPD_MAX_EXTRACTED_SIZE) {
                        upd_message(pInfo, "%s unpacks to more than a database update may hold", pInfo->archive_name);
                        result = CDIE_UPDATE_ERROR_VERIFY;
                        xx_rt_free(pName);
                        goto done;
                    }
                    if (!xx_format_unpack_current_archive_record(pFormat, pState, pPd)) {
                        if (xx_pd_is_stopped(pPd)) {
                            upd_message(pInfo, "Cancelled");
                            result = CDIE_UPDATE_ERROR_CANCELLED;
                        } else {
                            upd_message(pInfo, "Cannot extract %s from %s%s%s (error %d)", pName, pInfo->archive_name,
                                        (pPd && pPd->error_string[0]) ? ": " : "", (pPd && pPd->error_string[0]) ? pPd->error_string : "",
                                        xx_io_extract_errno());
                            result = CDIE_UPDATE_ERROR_VERIFY;
                        }
                        xx_rt_free(pName);
                        goto done;
                    }
                    pbFound[i] = 1;
                }
            }
            xx_rt_free(pName);
            if (nLevel >= 0) xx_pd_set_current(pPd, nLevel, (uint64_t)nVisited);
            if (xx_pd_is_stopped(pPd)) {
                upd_message(pInfo, "Cancelled");
                result = CDIE_UPDATE_ERROR_CANCELLED;
                goto done;
            }
        } while (xx_format_archive_record_move_to_next(pFormat, pState, pPd));
    }

    if (pArchive->directory_damaged || (pState->total_records >= 0 && nVisited != pState->total_records)) {
        upd_message(pInfo, "%s is damaged", pInfo->archive_name);
        result = CDIE_UPDATE_ERROR_VERIFY;
    }

done:
    if (nLevel >= 0) xx_pd_leave_level(pPd, nLevel);
    if (pState) xx_format_free_archive_records_reading(pFormat, pState);
    if (bScope) xx_io_extract_end(&scope);
    xx_list_cleanup(&options);
    if (pArchive) xx_zip_free(pArchive);
    if (pDevice) xx_io_close(pDevice);
    return result;
}

/* ---------------------------------------------------------------- install */

typedef struct upd_target_s {
    const char *name;   /* "db": top folder in the archive */
    const char *target; /* where it goes */
    char *parent;
    char *stage;        /* staging folder in parent */
    char *staged;       /* stage/x/<name> */
    char *old;          /* stage/old-<name> */
    int found;
    int moved_old;
    int moved_new;
} upd_target;

static void upd_target_free(upd_target *pTarget)
{
    xx_rt_free(pTarget->parent);
    xx_rt_free(pTarget->stage);
    xx_rt_free(pTarget->staged);
    xx_rt_free(pTarget->old);
}

/* Whether the default database folder belongs to a read-only installation,
 * so an update goes to the per-user folder without trying it: an AppImage
 * (its AppRun sets CDIE_READONLY_INSTALL), a package-managed /usr install
 * (built with CDIE_SYSTEM_INSTALL; even root must not rewrite files dpkg
 * owns), and a macOS application bundle (writing into a signed bundle breaks
 * its seal even when the user owns it). The per-user copy itself is not. */
static int upd_default_is_read_only(const char *pMainPath)
{
    char *pUser = cdie_user_data_dir();
    int bInUser = 0;

    if (pUser) {
        char *pUserKey = upd_canonical(pUser);
        char *pMainKey = upd_canonical(pMainPath);
        bInUser = pUserKey && pMainKey && upd_within(pMainKey, pUserKey);
        xx_rt_free(pUserKey);
        xx_rt_free(pMainKey);
        xx_rt_free(pUser);
    }
    if (bInUser) return 0;
    if (upd_env_set("CDIE_READONLY_INSTALL")) return 1;
#if defined(CDIE_SYSTEM_INSTALL) && CDIE_SYSTEM_INSTALL
    return 1;
#elif defined(__APPLE__)
    return xx_rt_strstr(pMainPath, ".app/Contents/") != NULL;
#else
    return 0;
#endif
}

/* Retarget a default database to cdie_user_data_dir() (its db only). */
static int upd_use_user_folder(cdie_update_info *pInfo)
{
    char *pUser = cdie_user_data_dir();
    char *pMain;

    if (!pUser) return 0;
    pMain = upd_join(pUser, "db");
    xx_rt_free(pUser);
    if (!pMain) return 0;
    xx_rt_free(pInfo->main_path);
    pInfo->main_path = pMain;
    pInfo->used_user_folder = 1;
    return 1;
}

/* Write <stage>/targets.ini naming where each old-<name> of the stage will
 * come from, before anything is moved (see upd_clean_stages). */
static int upd_record_targets(const char *pStage, const upd_target *pTargets, size_t nTargets)
{
    char *pPath = upd_join(pStage, UPD_TARGETS_NAME);
    xx_buf_t text;
    size_t i;
    int bOk;

    xx_buf_init(&text);
    for (i = 0; i < nTargets; ++i) {
        if (pTargets[i].found && pTargets[i].stage && !xx_rt_strcmp(pTargets[i].stage, pStage)) {
            xx_buf_appendf(&text, "old-%s=%s\n", pTargets[i].name, pTargets[i].target);
        }
    }
    bOk = pPath && text.data && upd_write_file(pPath, text.data);
    xx_buf_free(&text);
    xx_rt_free(pPath);
    return bOk;
}

#if !defined(_WIN32)
/* Archives carry whatever modes their writer recorded; the installed
 * database gets ordinary ones (directories 0755, files 0644), so neither an
 * odd archive nor the caller's umask decides who may read it. */
static void upd_normalize_modes(const char *pPath, unsigned nDepth)
{
    xx_list_t entries;
    size_t i;

    if (nDepth > 64) return;
    (void)chmod(pPath, 0755);
    if (!xx_list_init(&entries, sizeof(xx_fs_entry_t *), NULL)) return;
    if (xx_fs_list_dir(pPath, &entries)) {
        for (i = 0; i < xx_list_count(&entries); ++i) {
            xx_fs_entry_t *pEntry = *(xx_fs_entry_t **)xx_list_at(&entries, i);
            struct stat info;
            if (pEntry && lstat(pEntry->path, &info) == 0) {
                if (S_ISDIR(info.st_mode)) upd_normalize_modes(pEntry->path, nDepth + 1);
                else if (S_ISREG(info.st_mode)) (void)chmod(pEntry->path, 0644);
            }
            xx_fs_entry_free(pEntry);
        }
    }
    xx_list_cleanup(&entries);
}
#endif

/* Undo the renames of every target, last first. */
static void upd_rollback(upd_target *pTargets, size_t nTargets)
{
    size_t i = nTargets;

    while (i--) {
        upd_target *t = &pTargets[i];
        if (t->moved_new) {
            if (upd_rename(t->target, t->staged)) t->moved_new = 0;
        }
        if (t->moved_old && !t->moved_new) {
            if (upd_rename(t->old, t->target)) t->moved_old = 0;
        }
    }
}

static void upd_lock_init(upd_lock *pLock)
{
#if defined(_WIN32)
    pLock->handle = INVALID_HANDLE_VALUE;
#else
    pLock->fd = -1;
    pLock->path = NULL;
#endif
}

static cdie_update_status upd_install(const cdie_update_config *pConfig, cdie_update_info *pInfo, xx_github_t *pGh, int bForce, xx_pd_struct *pPd)
{
    /* One target: db.zip's db/ replaces the main database. Other top folders
     * of the archive (the extra database included) are never extracted, and
     * an extra database folder beside the target is left as it is.
     * A stage left by an earlier version that also replaced the extra
     * database is still put right: upd_clean_stages restores every old-*
     * folder of a stage to the place its targets.ini (upd_stage_origin)
     * records, whatever its name -- keep that generic, never narrow it to
     * old-db. */
    upd_target targets[1];
    const size_t nTargets = 1;
    upd_target *t = &targets[0];
    upd_lock lock;
    char *pZip = NULL;
    char sMd5[33] = "";
    char sTime[32] = "";
    cdie_update_status result = CDIE_UPDATE_OK;
    xx_github_status_t status;

    xx_rt_memset(targets, 0, sizeof(targets));
    upd_lock_init(&lock);

    if (pInfo->remote_size > UPD_MAX_ARCHIVE_SIZE) {
        upd_message(pInfo, "%s is %lld bytes, more than a database update may be", pInfo->archive_name ? pInfo->archive_name : "The archive",
                    (long long)pInfo->remote_size);
        return CDIE_UPDATE_ERROR_VERIFY;
    }

    /* A read-only installation is never tried in place. */
    if (pInfo->main_is_default && pConfig->user_fallback && !cdie_is_portable() && upd_default_is_read_only(pInfo->main_path)) {
        if (!upd_use_user_folder(pInfo)) {
            upd_message(pInfo, "The installed database is read-only and there is no per-user folder for an update");
            return CDIE_UPDATE_ERROR_DATABASE;
        }
    }

    t->name = "db";
    t->target = pInfo->main_path;

    /* The staging folder sits in the target's parent, so every rename stays
     * on one volume. Creating it is also the test for a writable folder. */
    for (;;) {
        t->parent = upd_dir_of(t->target);
        if (t->parent && t->parent[0]) {
            (void)xx_io_create_dirs_a(t->parent, true);
            t->stage = upd_make_stage(t->parent);
        }
        if (t->stage) break;

        upd_message(pInfo, "Cannot write to the database folder %s", (t->parent && t->parent[0]) ? t->parent : pInfo->main_path);
        xx_rt_free(t->parent);
        t->parent = NULL;

        /* A default folder that turns out read-only (an installed copy not
         * flagged as one) falls back to the user's own data folder. */
        if (pInfo->main_is_default && pConfig->user_fallback && !pInfo->used_user_folder && !cdie_is_portable() &&
            upd_use_user_folder(pInfo)) {
            t->target = pInfo->main_path;
            continue;
        }
        return CDIE_UPDATE_ERROR_DATABASE;
    }

    /* A lock in the parent folder, so a second update waits its turn; then
     * leftovers of killed runs are put right. */
    result = upd_lock_acquire(t->parent, &lock);
    if (result != CDIE_UPDATE_OK) {
        upd_message(pInfo, result == CDIE_UPDATE_ERROR_BUSY ? "Another database update is running in %s" : "Cannot lock %s", t->parent);
        goto cleanup;
    }
    upd_clean_stages(t->parent, t->stage);

    /* Download and verify. The file name is fixed: the archive's own name
     * comes from the server. */
    pZip = upd_join(t->stage, "archive.zip");
    if (!pZip) {
        upd_message(pInfo, "Out of memory");
        result = CDIE_UPDATE_ERROR_DATABASE;
        goto cleanup;
    }
    status = xx_github_download_file(pGh, pInfo->archive_url, pZip);
    if (status != XX_GITHUB_OK) {
        result = upd_github_failure(pInfo, pGh, status, "Cannot download the database");
        goto cleanup;
    }
    result = upd_verify_archive(pZip, pInfo, sMd5, pPd);
    if (result != CDIE_UPDATE_OK) goto cleanup;
    if (!pInfo->remote_md5[0] && !bForce && pInfo->installed_md5[0] && !xx_rt_strcmp(sMd5, pInfo->installed_md5) && !pInfo->used_user_folder) {
        /* A source without a published checksum: the archive only proves to
         * be the installed one once it is here. The same bytes uploaded again
         * get a new asset id; record it, or every check reports an update. */
        if (pInfo->remote_asset[0] && xx_rt_strcmp(pInfo->remote_asset, pInfo->installed_asset)) {
            if (!xx_net_time_format_iso8601(xx_net_time_now(), sTime, sizeof(sTime))) sTime[0] = 0;
            if (upd_write_stamp(pInfo->main_path, pInfo, sMd5, sTime)) {
                upd_copy_text(pInfo->installed_asset, sizeof(pInfo->installed_asset), pInfo->remote_asset);
                upd_copy_text(pInfo->installed_at, sizeof(pInfo->installed_at), sTime);
            }
        }
        pInfo->update_available = 0;
        upd_message(pInfo, "The database is up to date");
        result = CDIE_UPDATE_OK;
        goto cleanup;
    }

    /* Extract db/ into the staging folder; every other member is skipped. */
    {
        char *pRoot = upd_join(t->stage, "x");
        const char *ppName[1];
        int bFound = 0;

        ppName[0] = t->name;
        t->staged = pRoot ? upd_join(pRoot, t->name) : NULL;
        {
            char sOld[64];
            xx_rt_snprintf(sOld, sizeof(sOld), "old-%s", t->name);
            t->old = upd_join(t->stage, sOld);
        }
        if (!pRoot || !t->staged || !t->old || !xx_io_create_dirs_a(pRoot, true)) {
            xx_rt_free(pRoot);
            upd_message(pInfo, "Cannot write to %s", t->stage);
            result = CDIE_UPDATE_ERROR_DATABASE;
            goto cleanup;
        }
        result = upd_extract(pZip, pRoot, ppName, 1, &bFound, pInfo, pPd);
        xx_rt_free(pRoot);
        if (result != CDIE_UPDATE_OK) goto cleanup;
        t->found = bFound && xx_fs_is_dir(t->staged);
#if !defined(_WIN32)
        if (t->found) upd_normalize_modes(t->staged, 0);
#endif
    }
    if (!t->found) {
        upd_message(pInfo, "%s has no db folder", pInfo->archive_name);
        result = CDIE_UPDATE_ERROR_VERIFY;
        goto cleanup;
    }

    /* Prove the new database loads before anything is replaced. */
    {
        DBase test;
        int bLoaded;
        int nLevel = xx_pd_enter_level(pPd, 0, "Checking the database");

        xx_rt_memset(&test, 0, sizeof(test));
        bLoaded = db_load(&test, t->staged, DB_MAIN);
        pInfo->signatures = test.nCount;
        db_free(&test);
        if (nLevel >= 0) xx_pd_leave_level(pPd, nLevel);
        if (!bLoaded || pInfo->signatures <= 0) {
            upd_message(pInfo, "%s does not contain a usable database", pInfo->archive_name);
            result = CDIE_UPDATE_ERROR_VERIFY;
            goto cleanup;
        }
    }
    if (xx_pd_is_stopped(pPd)) {
        upd_message(pInfo, "Cancelled");
        result = CDIE_UPDATE_ERROR_CANCELLED;
        goto cleanup;
    }

    /* Record where the old folder comes from before moving it. */
    if (!upd_record_targets(t->stage, targets, nTargets)) {
        upd_message(pInfo, "Cannot write to %s", t->stage);
        result = CDIE_UPDATE_ERROR_DATABASE;
        goto cleanup;
    }

    /* Swap. From here on a stop request is ignored: the folder is either
     * replaced or restored. */
    if (xx_fs_exists(t->target)) {
        if (!upd_rename(t->target, t->old)) {
            upd_message(pInfo, "Cannot replace %s; close programs that use it and try again", t->target);
            result = CDIE_UPDATE_ERROR_DATABASE;
            goto cleanup;
        }
        t->moved_old = 1;
    }
    if (!upd_rename(t->staged, t->target)) {
        upd_rollback(targets, nTargets);
        upd_message(pInfo, "Cannot move the new database to %s", t->target);
        result = CDIE_UPDATE_ERROR_DATABASE;
        goto cleanup;
    }
    t->moved_new = 1;

    /* The stamp is written last: a run that dies before this point leaves a
     * database without a record, which the next check reports as an update,
     * never a mixed database reported as current. */
    if (!xx_net_time_format_iso8601(xx_net_time_now(), sTime, sizeof(sTime))) sTime[0] = 0;
    pInfo->installed = 1;
    pInfo->update_available = 0;
    pInfo->has_database = 1;
    upd_copy_text(pInfo->installed_md5, sizeof(pInfo->installed_md5), sMd5);
    upd_copy_text(pInfo->installed_asset, sizeof(pInfo->installed_asset), pInfo->remote_asset);
    upd_copy_text(pInfo->installed_at, sizeof(pInfo->installed_at), sTime);
    upd_replace(&pInfo->installed_source, pInfo->source_url);
    if (!upd_write_stamp(pInfo->main_path, pInfo, sMd5, sTime)) {
        pInfo->installed_md5[0] = 0;
        upd_message(pInfo, "Installed %d signatures in %s, but could not write %s; the next check will offer the update again", pInfo->signatures,
                    pInfo->main_path, CDIE_UPDATE_STAMP_NAME);
    } else if (pInfo->used_user_folder) {
        upd_message(pInfo, "Installed %d signatures in %s (the default database folder is read-only)", pInfo->signatures, pInfo->main_path);
    } else {
        upd_message(pInfo, "Installed %d signatures in %s", pInfo->signatures, pInfo->main_path);
    }
    result = CDIE_UPDATE_OK;

cleanup:
    /* A stage still holding the old folder that a rollback could not put
     * back is kept: the next run's upd_clean_stages restores it from the
     * stage's targets.ini. */
    if (t->stage && !(t->moved_old && !t->moved_new)) (void)xx_fs_remove_tree(t->stage);
    upd_lock_release(&lock);
    upd_target_free(t);
    xx_rt_free(pZip);
    return result;
}

cdie_update_status cdie_update_install(const cdie_update_config *pConfig, cdie_update_info *pInfo, int bForce, xx_pd_struct *pPd)
{
    xx_github_t gh;
    cdie_update_status status;

    if (!pConfig || !pInfo) return CDIE_UPDATE_ERROR_ARGUMENT;
    cdie_update_info_free(pInfo);
    upd_github_init(&gh, pConfig, pPd);
    status = upd_check(pConfig, pInfo, &gh);
    if (status == CDIE_UPDATE_AVAILABLE || (status == CDIE_UPDATE_OK && bForce)) {
        status = upd_install(pConfig, pInfo, &gh, bForce, pPd);
    }
    xx_github_free(&gh);
    return status;
}
