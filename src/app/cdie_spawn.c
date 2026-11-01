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

/* cdie_spawn.c - see cdie_spawn.h. */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "cdie_spawn.h"
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
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

extern char **environ;
#endif

#if defined(_WIN32)
#define CDIE_SPAWN_UPDATER_NAME "die-update.exe"
#else
#define CDIE_SPAWN_UPDATER_NAME "die-update"
#endif

#if defined(_WIN32)

/* ------------------------------------------------------------- Windows --- */

/* An environment variable as UTF-8, or NULL when unset or empty. The wide call
 * keeps a path outside the ANSI code page intact (see cdie_paths.c). */
static char *spawn_getenv(const char *pName)
{
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
}

/* "The system cannot find the file specified (error 2)". */
static void spawn_error(char *pError, size_t nErrorSize, DWORD nCode)
{
    WCHAR sText[256];
    DWORD nLength;
    char *pText = NULL;

    if (!pError || !nErrorSize) return;
    nLength = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, nCode, 0, sText,
                             (DWORD)(sizeof(sText) / sizeof(sText[0])), NULL);
    while (nLength && (sText[nLength - 1] == L'\r' || sText[nLength - 1] == L'\n' || sText[nLength - 1] == L' ' ||
                       sText[nLength - 1] == L'.')) {
        --nLength;
    }
    if (nLength) {
        sText[nLength] = 0;
        pText = xx_rt_utf16_to_utf8(sText);
    }
    if (pText) xx_rt_snprintf(pError, nErrorSize, "%s (error %lu)", pText, (unsigned long)nCode);
    else xx_rt_snprintf(pError, nErrorSize, "error %lu", (unsigned long)nCode);
    xx_rt_free(pText);
}

/* One argument the way the Microsoft argv rules (x_build_argv_w in
 * core/utils_entry.c, and the C runtime's) read it back. It is quoted when it
 * is empty or holds a blank or a quote. Inside the quotes backslashes are
 * literal, except before a quote, where they are doubled and the quote is
 * escaped, and at the end, where they are doubled so the closing quote stays
 * a closing quote. UTF-8 bytes go through unchanged: none of these characters
 * occurs inside a multibyte sequence. @p pOut has room for 2 * length + 2
 * bytes. Returns the number of bytes written. */
static size_t spawn_quote(char *pOut, const char *pArg)
{
    const char *p;
    size_t n = 0;
    int bQuote = (pArg[0] == 0);

    for (p = pArg; *p && !bQuote; ++p) {
        bQuote = (*p == ' ') || (*p == '\t') || (*p == '\n') || (*p == '\v') || (*p == '"');
    }
    if (!bQuote) {
        for (p = pArg; *p; ++p) pOut[n++] = *p;
        return n;
    }

    pOut[n++] = '"';
    for (p = pArg;; ++p) {
        size_t nBackslashes = 0;
        size_t nWrite;

        while (*p == '\\') {
            ++nBackslashes;
            ++p;
        }
        if (*p == 0) nWrite = 2 * nBackslashes;
        else if (*p == '"') nWrite = 2 * nBackslashes + 1;
        else nWrite = nBackslashes;
        while (nWrite--) pOut[n++] = '\\';
        if (*p == 0) break;
        pOut[n++] = *p;
    }
    pOut[n++] = '"';
    return n;
}

/* The arguments joined into a command line, UTF-8. */
static char *spawn_command_line(const char *const ppArgv[])
{
    size_t nSize = 1;
    size_t n = 0;
    size_t i;
    char *pLine;

    for (i = 0; ppArgv[i]; ++i) nSize += 2 * xx_rt_strlen(ppArgv[i]) + 3;
    pLine = (char *)xx_rt_malloc(nSize);
    if (!pLine) return NULL;
    for (i = 0; ppArgv[i]; ++i) {
        if (i) pLine[n++] = ' ';
        n += spawn_quote(pLine + n, ppArgv[i]);
    }
    pLine[n] = 0;
    return pLine;
}

/* Ctrl+C reaches every process on the console. The child stops cleanly on it
 * (die-update finishes or rolls back the folder swap); this process waits for
 * that instead of exiting first. */
static BOOL WINAPI spawn_on_control(DWORD nEvent)
{
    return (nEvent == CTRL_C_EVENT) || (nEvent == CTRL_BREAK_EVENT);
}

/* Windows 7 console handles are pseudo handles (the low two bits set) which a
 * PROC_THREAD_ATTRIBUTE_HANDLE_LIST rejects; the console hands them to the
 * child without inheritance anyway. */
static int spawn_is_console_pseudo_handle(HANDLE hHandle)
{
    return (((ULONG_PTR)hHandle & 3) == 3) && (GetFileType(hHandle) == FILE_TYPE_CHAR);
}

int cdie_spawn_wait(const char *pPath, const char *const ppArgv[], int *pExitCode, char *pError, size_t nErrorSize)
{
    static const DWORD aStdIds[3] = {STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
    HANDLE hSelf = GetCurrentProcess();
    HANDLE aStd[3] = {NULL, NULL, NULL};
    HANDLE aOwned[3] = {NULL, NULL, NULL};
    HANDLE aInherit[3];
    DWORD nInherit = 0;
    STARTUPINFOEXW si;
    PROCESS_INFORMATION pi;
    LPPROC_THREAD_ATTRIBUTE_LIST pAttributes = NULL;
    SIZE_T nAttributesSize = 0;
    int bAttributesLive = 0;
    DWORD nFlags = 0;
    DWORD nCode = 0;
    void *pWidePath = NULL;
    void *pWideLine = NULL;
    char *pLine;
    int bStarted = 0;
    int i;

    if (pError && nErrorSize) pError[0] = 0;
    if (!pPath || !ppArgv || !ppArgv[0] || !pExitCode) {
        spawn_error(pError, nErrorSize, ERROR_INVALID_PARAMETER);
        return 0;
    }

    pLine = spawn_command_line(ppArgv);
    pWidePath = xx_rt_utf8_to_utf16(pPath);
    pWideLine = pLine ? xx_rt_utf8_to_utf16(pLine) : NULL;
    xx_rt_free(pLine);
    if (!pWidePath || !pWideLine) {
        spawn_error(pError, nErrorSize, ERROR_NOT_ENOUGH_MEMORY);
        xx_rt_free(pWideLine);
        xx_rt_free(pWidePath);
        return 0;
    }

    /* The child writes to this process's streams. CreateProcessW hands over
     * inheritable handles only, so each standard handle is duplicated as one,
     * and a handle list keeps every other inheritable handle of this process
     * out of the child. A missing stream stays missing. */
    for (i = 0; i < 3; ++i) {
        HANDLE hHandle = GetStdHandle(aStdIds[i]);

        if (!hHandle || hHandle == INVALID_HANDLE_VALUE) continue;
        if (DuplicateHandle(hSelf, hHandle, hSelf, &aOwned[i], 0, TRUE, DUPLICATE_SAME_ACCESS)) {
            aStd[i] = aOwned[i];
            if (!spawn_is_console_pseudo_handle(aOwned[i])) aInherit[nInherit++] = aOwned[i];
        } else {
            aOwned[i] = NULL;
            aStd[i] = hHandle;
        }
    }

    xx_rt_memset(&si, 0, sizeof(si));
    xx_rt_memset(&pi, 0, sizeof(pi));
    si.StartupInfo.cb = sizeof(si.StartupInfo);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = aStd[0];
    si.StartupInfo.hStdOutput = aStd[1];
    si.StartupInfo.hStdError = aStd[2];
    if (nInherit) {
        /* Without the list (it cannot fail on a supported system) the child
         * would still get the streams, plus whatever else is inheritable. */
        InitializeProcThreadAttributeList(NULL, 1, 0, &nAttributesSize);
        pAttributes = (LPPROC_THREAD_ATTRIBUTE_LIST)xx_rt_malloc(nAttributesSize);
        if (pAttributes && InitializeProcThreadAttributeList(pAttributes, 1, 0, &nAttributesSize)) {
            bAttributesLive = 1;
            if (UpdateProcThreadAttribute(pAttributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, aInherit,
                                          (SIZE_T)nInherit * sizeof(HANDLE), NULL, NULL)) {
                si.StartupInfo.cb = sizeof(si);
                si.lpAttributeList = pAttributes;
                nFlags |= EXTENDED_STARTUPINFO_PRESENT;
            }
        }
    }

    /* A diec started without a console (DETACHED_PROCESS, a service, a GUI
     * parent with redirected streams) would otherwise give die-update a new
     * visible console window; the child still writes to the inherited
     * streams. */
    if (GetConsoleCP() == 0) nFlags |= CREATE_NO_WINDOW;

    SetConsoleCtrlHandler(spawn_on_control, TRUE);
    if (CreateProcessW((LPCWSTR)pWidePath, (LPWSTR)pWideLine, NULL, NULL, nInherit ? TRUE : FALSE, nFlags, NULL, NULL,
                       &si.StartupInfo, &pi)) {
        /* The child has its own copies now. */
        for (i = 0; i < 3; ++i) {
            if (aOwned[i]) CloseHandle(aOwned[i]);
            aOwned[i] = NULL;
        }
        CloseHandle(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
        *pExitCode = GetExitCodeProcess(pi.hProcess, &nCode) ? (int)nCode : -1;
        CloseHandle(pi.hProcess);
        bStarted = 1;
    } else {
        spawn_error(pError, nErrorSize, GetLastError());
    }
    SetConsoleCtrlHandler(spawn_on_control, FALSE);

    for (i = 0; i < 3; ++i) {
        if (aOwned[i]) CloseHandle(aOwned[i]);
    }
    if (bAttributesLive) DeleteProcThreadAttributeList(pAttributes);
    xx_rt_free(pAttributes);
    xx_rt_free(pWideLine);
    xx_rt_free(pWidePath);
    return bStarted;
}

#else

/* --------------------------------------------------------------- POSIX --- */

static char *spawn_getenv(const char *pName)
{
    const char *pValue = getenv(pName);

    return (pValue && pValue[0]) ? cdie_strdup(pValue) : NULL;
}

int cdie_spawn_wait(const char *pPath, const char *const ppArgv[], int *pExitCode, char *pError, size_t nErrorSize)
{
    struct sigaction ignore;
    struct sigaction defaultAction;
    struct sigaction oldInterrupt;
    struct sigaction oldQuit;
    struct sigaction oldChild;
    posix_spawnattr_t attributes;
    sigset_t defaults;
    pid_t pid = 0;
    int nStatus = 0;
    int nError;

    if (pError && nErrorSize) pError[0] = 0;
    if (!pPath || !ppArgv || !ppArgv[0] || !pExitCode) {
        if (pError && nErrorSize) xx_rt_snprintf(pError, nErrorSize, "%s", strerror(EINVAL));
        return 0;
    }

    /* As system() does: the terminal sends SIGINT and SIGQUIT to the whole
     * foreground process group, so this process ignores them until the child
     * has acted on them, while the child starts with the default actions
     * (unless they were ignored already). SIGCHLD must not be ignored, or the
     * child is reaped before waitpid can read its status. */
    memset(&ignore, 0, sizeof(ignore));
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    memset(&defaultAction, 0, sizeof(defaultAction));
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    sigaction(SIGINT, &ignore, &oldInterrupt);
    sigaction(SIGQUIT, &ignore, &oldQuit);
    sigaction(SIGCHLD, &defaultAction, &oldChild);
    sigemptyset(&defaults);
    if (oldInterrupt.sa_handler != SIG_IGN) sigaddset(&defaults, SIGINT);
    if (oldQuit.sa_handler != SIG_IGN) sigaddset(&defaults, SIGQUIT);

    nError = posix_spawnattr_init(&attributes);
    if (!nError) {
        nError = posix_spawnattr_setsigdefault(&attributes, &defaults);
        if (!nError) nError = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGDEF);
        if (!nError) nError = posix_spawn(&pid, pPath, NULL, &attributes, (char *const *)ppArgv, environ);
        posix_spawnattr_destroy(&attributes);
    }
    if (!nError) {
        while (waitpid(pid, &nStatus, 0) < 0) {
            if (errno != EINTR) {
                nError = errno;
                break;
            }
        }
    }

    sigaction(SIGCHLD, &oldChild, NULL);
    sigaction(SIGQUIT, &oldQuit, NULL);
    sigaction(SIGINT, &oldInterrupt, NULL);

    if (nError) {
        if (pError && nErrorSize) xx_rt_snprintf(pError, nErrorSize, "%s", strerror(nError));
        return 0;
    }
    if (WIFEXITED(nStatus)) *pExitCode = WEXITSTATUS(nStatus);
    else if (WIFSIGNALED(nStatus)) *pExitCode = 128 + WTERMSIG(nStatus);
    else *pExitCode = -1;
    return 1;
}

#endif

char *cdie_spawn_updater_path(void)
{
    char *pPath = spawn_getenv("CDIE_UPDATER");
    char *pAppDir;
    char *pJoined;

    if (pPath) {
        return pPath;
    }
    /* cdie_app_dir falls back to "." itself; NULL means out of memory. */
    pAppDir = cdie_app_dir();
    if (!pAppDir) {
        return NULL;
    }
    pJoined = xx_fs_path_join(pAppDir, CDIE_SPAWN_UPDATER_NAME);
    xx_rt_free(pAppDir);
    pPath = pJoined ? cdie_path_native(pJoined) : NULL;
    xx_str_free(pJoined);
    return pPath;
}
