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

/* utils_entry.c - program startup.
 *
 * With CDIE_NO_CRT the executable has no C runtime at all: the linker entry
 * point is x_entry_point below, which builds argv from GetCommandLineA and
 * calls x_main. The only import left is KERNEL32.
 *
 * The compiler still emits calls to memset/memcpy for structure
 * initialisation and large copies no matter what the source says, so those
 * two symbols are provided here and forwarded to the x_ implementations.
 *
 * Without CDIE_NO_CRT a normal main() forwards to x_main, so the same source
 * builds against a hosted runtime.
 */

#include "../global.h"

#include <xxfclib/rt/xx_rt.h>

#if defined(CDIE_NO_CRT) && defined(_WIN32)

#include <windows.h>

/* ------------------------------------------------------------------------ */
/*  Compiler support routines                                                */
/* ------------------------------------------------------------------------ */

#if defined(_MSC_VER)
/* Keep the compiler from replacing an explicit call with the intrinsic. */
#pragma function(memset, memcpy, memmove)

/* MSVC emits a reference to this marker from every object file that uses
 * floating point. Normally libcmt supplies it; here it is just a symbol
 * that has to exist.                                                      */
int _fltused = 0x9875;
#endif

/* The compiler emits calls to these for aggregate initialisation and large
 * copies regardless of what the source says, so they have to exist even
 * though nothing in cdie calls them by name.
 *
 * Optimisation is off for this block on purpose. #pragma function stops the
 * compiler substituting the intrinsic for an explicit call, but it does NOT
 * stop loop-idiom recognition: at /O2 a plain byte loop that fills memory is
 * itself rewritten into "call memset". Inside the definition of memset that
 * is infinite recursion, which shows up as a stack overflow on the very
 * first call. Turning the optimiser off for these three keeps the loops as
 * loops.                                                                   */
#if defined(_MSC_VER)
#pragma optimize("", off)
#endif

void *memset(void *pDestination, int nValue, size_t nSize)
{
    unsigned char *pDst = (unsigned char *)pDestination;
    unsigned char nByte = (unsigned char)nValue;

    while (nSize--) {
        *pDst++ = nByte;
    }

    return pDestination;
}

void *memcpy(void *pDestination, const void *pSource, size_t nSize)
{
    unsigned char *pDst = (unsigned char *)pDestination;
    const unsigned char *pSrc = (const unsigned char *)pSource;

    while (nSize--) {
        *pDst++ = *pSrc++;
    }

    return pDestination;
}

void *memmove(void *pDestination, const void *pSource, size_t nSize)
{
    unsigned char *pDst = (unsigned char *)pDestination;
    const unsigned char *pSrc = (const unsigned char *)pSource;

    if (pDst == pSrc) {
        return pDestination;
    }

    if ((pDst < pSrc) || (pDst >= pSrc + nSize)) {
        while (nSize--) {
            *pDst++ = *pSrc++;
        }

        return pDestination;
    }

    pDst += nSize;
    pSrc += nSize;

    while (nSize--) {
        *--pDst = *--pSrc;
    }

    return pDestination;
}

#if defined(_MSC_VER)
#pragma optimize("", on)
#endif

/* ------------------------------------------------------------------------ */
/*  Command line -> argv                                                     */
/* ------------------------------------------------------------------------ */

#define X_MAX_ARGS 1024

/* Applies the documented Microsoft argv quoting rules, on the *wide* command
 * line so that non-ANSI arguments (a Cyrillic file path, say) survive. The
 * split arguments are written back into the same wide buffer.
 *   - arguments are separated by spaces or tabs
 *   - a double quote toggles "inside quotes"
 *   - 2n backslashes before a quote produce n backslashes and toggle
 *   - 2n+1 backslashes before a quote produce n backslashes and a literal "
 *   - backslashes not followed by a quote are literal
 */
static int x_build_argv_w(WCHAR *pCommandLine, WCHAR **ppArgv, int nMaxArgs)
{
    WCHAR *pRead = pCommandLine;
    WCHAR *pWrite = pCommandLine;
    int nArgc = 0;

    for (;;) {
        int bInQuotes = 0;

        while ((*pRead == L' ') || (*pRead == L'\t')) {
            pRead++;
        }

        if (*pRead == 0) {
            break;
        }

        if (nArgc >= nMaxArgs - 1) {
            break;
        }

        ppArgv[nArgc++] = pWrite;

        for (;;) {
            int nBackslashes = 0;

            if (*pRead == 0) {
                break;
            }

            if ((!bInQuotes) && ((*pRead == L' ') || (*pRead == L'\t'))) {
                pRead++;
                break;
            }

            while (*pRead == L'\\') {
                nBackslashes++;
                pRead++;
            }

            if (*pRead == L'"') {
                int i = 0;

                for (i = 0; i < nBackslashes / 2; i++) {
                    *pWrite++ = L'\\';
                }

                if (nBackslashes % 2) {
                    *pWrite++ = L'"';
                    pRead++;
                } else {
                    bInQuotes = !bInQuotes;
                    pRead++;
                }

                continue;
            }

            {
                int i = 0;

                for (i = 0; i < nBackslashes; i++) {
                    *pWrite++ = L'\\';
                }
            }

            if (*pRead == 0) {
                break;
            }

            *pWrite++ = *pRead++;
        }

        *pWrite++ = 0;
    }

    ppArgv[nArgc] = NULL;

    return nArgc;
}

static size_t x_wcslen(const WCHAR *pString)
{
    size_t n = 0;

    while (pString[n] != 0) {
        n++;
    }

    return n;
}

/* ------------------------------------------------------------------------ */
/*  Entry point                                                              */
/* ------------------------------------------------------------------------ */

static void x_startup(void)
{
    WCHAR *pCommandLine = GetCommandLineW();
    WCHAR **ppWideArgv = NULL;
    WCHAR *pCopy = NULL;
    char **ppArgv = NULL;
    size_t nLen = 0;
    int nArgc = 0;
    int nResult = 0;
    int i = 0;

    if (pCommandLine == NULL) {
        ExitProcess(0);
    }

    /* Everything is on the heap: stack probes come from the CRT, so no frame
     * in a CRT-free build may exceed a page. The command line is parsed as
     * UTF-16 and each argument is converted to UTF-8, which is the encoding
     * the rest of the program (and the file APIs) expect.                   */
    nLen = x_wcslen(pCommandLine);
    pCopy = (WCHAR *)xx_rt_malloc((nLen + 2) * sizeof(WCHAR));
    ppWideArgv = (WCHAR **)xx_rt_malloc(X_MAX_ARGS * sizeof(WCHAR *));
    ppArgv = (char **)xx_rt_malloc(X_MAX_ARGS * sizeof(char *));

    if ((pCopy == NULL) || (ppWideArgv == NULL) || (ppArgv == NULL)) {
        ExitProcess(3);
    }

    xx_rt_memcpy(pCopy, pCommandLine, (nLen + 1) * sizeof(WCHAR));

    nArgc = x_build_argv_w(pCopy, ppWideArgv, X_MAX_ARGS);

    for (i = 0; i < nArgc; i++) {
        ppArgv[i] = xx_rt_utf16_to_utf8(ppWideArgv[i]);

        if (ppArgv[i] == NULL) {
            ppArgv[i] = (char *)xx_rt_malloc(1);

            if (ppArgv[i] != NULL) {
                ppArgv[i][0] = 0;
            }
        }
    }

    ppArgv[nArgc] = NULL;

    nResult = x_main(nArgc, ppArgv);

    for (i = 0; i < nArgc; i++) {
        xx_rt_free(ppArgv[i]);
    }

    xx_rt_free(ppArgv);
    xx_rt_free(ppWideArgv);
    xx_rt_free(pCopy);

    ExitProcess((UINT)nResult);
}

/* The linker entry point. The loader jumps here with a 16-byte aligned
 * stack, whereas compiled code assumes the 8-byte offset that a CALL leaves
 * behind; doing the work in x_startup means the CALL below restores the
 * relationship the x64 ABI expects before any aligned SSE spill happens.   */
void __cdecl x_entry_point(void)
{
    x_startup();
}

#else /* hosted build */

int main(int nArgc, char *ppArgv[])
{
    return x_main(nArgc, ppArgv);
}

#endif
