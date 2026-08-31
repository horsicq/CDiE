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

/* gui_entry.c - startup for the GUI, the WINDOWS-subsystem analogue of
 * core/utils_entry.c.
 *
 * With CDIE_NO_CRT the executable has no C runtime: the linker entry point is
 * x_gui_entry_point below, which just runs the window loop and calls
 * ExitProcess. The only non-USER/GDI import left is KERNEL32.
 *
 * The engine object files that make up the GUI (they exclude core/utils_entry.c)
 * still cause the compiler to emit memset/memcpy/memmove and reference _fltused,
 * so those four symbols are supplied here - the same routines utils_entry.c
 * gives the console.
 *
 * Without CDIE_NO_CRT a normal WinMain forwards to the same gui_run(), so the
 * source builds against a hosted runtime too (the path used on 32-bit, where
 * the CRT is needed for the compiler's 64-bit arithmetic helpers). */

int gui_run(void);

#if defined(CDIE_NO_CRT) && defined(_WIN32)

#include <windows.h>

/* ------------------------------------------------------------------------ */
/*  Compiler support routines (identical to core/utils_entry.c)              */
/* ------------------------------------------------------------------------ */

#if defined(_MSC_VER)
/* Keep the compiler from replacing an explicit call with the intrinsic. */
#pragma function(memset, memcpy, memmove)

/* MSVC references this marker from every object that uses floating point. */
int _fltused = 0x9875;
#endif

/* Optimisation is off for this block on purpose: at /O2 a plain byte loop that
 * fills memory is itself rewritten into "call memset", which inside memset is
 * infinite recursion. Turning the optimiser off keeps the loops as loops. */
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
/*  Entry point                                                              */
/* ------------------------------------------------------------------------ */

static void gui_startup(void)
{
    int nResult = gui_run();

    ExitProcess((UINT)nResult);
}

/* The linker entry point. The loader jumps here with a 16-byte aligned stack,
 * whereas compiled code assumes the 8-byte offset that a CALL leaves behind;
 * doing the work in gui_startup means the CALL below restores the relationship
 * the x64 ABI expects before any aligned SSE spill happens - the same reason
 * core/utils_entry.c splits x_entry_point from x_startup. */
void __cdecl x_gui_entry_point(void)
{
    gui_startup();
}

#else /* hosted build: a normal WinMain forwards to gui_run */

#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;

    return gui_run();
}

#endif
