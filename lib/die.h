/* Copyright (c) 2019-2026 hors<horsicq@gmail.com>
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

/* die.h - public C API of the Detect It Easy library.
 *
 * ABI-compatible with horsicq/die_library: the same flags, the same exported
 * function names and signatures, so a program written against die_library
 * (its samples included) compiles and links against this library unchanged.
 * The implementation is the pure-C cdie engine rather than the Qt one.
 *
 * Threading: the library is single-threaded, exactly as die_library is. The
 * database that DIE_LoadDatabase installs for the *Ex scans is one process
 * global, so a DIE_LoadDatabase* running concurrently with a DIE_Scan*Ex
 * frees the signature array the scan is walking. Serialise the calls, or give
 * each thread its own database by using the non-Ex entry points.
 */

#ifndef DIE_H
#define DIE_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Export decoration --------------------------------------------------
 *
 * Building the shared library defines DIE_BUILD_SHARED (exports). A program
 * that links the shared library sees the default (imports on Windows). A
 * program that links the static library defines DIE_STATIC (no decoration).
 */
#if defined(_WIN32) && defined(__TINYC__)
/* TinyCC rejects __declspec(dllexport) but accepts the __attribute__ form. */
#if defined(DIE_BUILD_SHARED)
#define DIE_API __attribute__((dllexport))
#elif defined(DIE_STATIC)
#define DIE_API
#else
#define DIE_API __attribute__((dllimport))
#endif
#elif defined(_WIN32)
#if defined(DIE_BUILD_SHARED)
#define DIE_API __declspec(dllexport)
#elif defined(DIE_STATIC)
#define DIE_API
#else
#define DIE_API __declspec(dllimport)
#endif
#else
#if defined(DIE_BUILD_SHARED)
#define DIE_API __attribute__((visibility("default")))
#else
#define DIE_API
#endif
#endif

/* --- Scan flags --------------------------------------------------------- */
#define DIE_DEEPSCAN 0x00000001
#define DIE_HEURISTICSCAN 0x00000002
/* Accepted for source compatibility with die_library but inert: honouring it
 * would add a second result group and change the output shape, which would
 * break the byte-for-byte parity with diec this port is built around. See
 * docs/LIMITATIONS.md. */
#define DIE_ALLTYPESSCAN 0x00000004
#define DIE_RECURSIVESCAN 0x00000008
#define DIE_VERBOSE 0x00000010
#define DIE_AGGRESSIVESCAN 0x00000020
#define DIE_RESULTASXML 0x00010000
#define DIE_RESULTASJSON 0x00020000
#define DIE_RESULTASTSV 0x00040000
#define DIE_RESULTASCSV 0x00080000

/* --- Core functions ----------------------------------------------------- */
DIE_API char *DIE_ScanFileA(char *pszFileName, unsigned int nFlags, char *pszDatabase);
DIE_API wchar_t *DIE_ScanFileW(wchar_t *pwszFileName, unsigned int nFlags, wchar_t *pwszDatabase);
DIE_API char *DIE_ScanMemoryA(char *pMemory, int nMemorySize, unsigned int nFlags, char *pszDatabase);
DIE_API wchar_t *DIE_ScanMemoryW(char *pMemory, int nMemorySize, unsigned int nFlags, wchar_t *pwszDatabase);
DIE_API int DIE_LoadDatabaseA(char *pszDatabase);
DIE_API int DIE_LoadDatabaseW(wchar_t *pwszDatabase);
DIE_API char *DIE_ScanFileExA(char *pszFileName, unsigned int nFlags);
DIE_API wchar_t *DIE_ScanFileExW(wchar_t *pwszFileName, unsigned int nFlags);
DIE_API char *DIE_ScanMemoryExA(char *pMemory, int nMemorySize, unsigned int nFlags);
DIE_API wchar_t *DIE_ScanMemoryExW(char *pMemory, int nMemorySize, unsigned int nFlags);
DIE_API void DIE_FreeMemoryA(char *pszString);
DIE_API void DIE_FreeMemoryW(wchar_t *pwszString);

/* --- Windows-specific (VB) ---------------------------------------------- */
#if defined(_WIN32)
DIE_API int __stdcall DIE_VB_ScanFile(wchar_t *pwszFileName, unsigned int nFlags, wchar_t *pwszDatabase, wchar_t *pwszBuffer, int nBufferSize);
typedef int(__stdcall *DIE_VB_CALLBACK)(wchar_t *curSigName, int curSigindex, int maxSigs);
DIE_API int __stdcall DIE_VB_ScanFileCallback(wchar_t *pwszFileName, unsigned int nFlags, wchar_t *pwszDatabase, wchar_t *pwszBuffer, int nBufferSize,
                                              DIE_VB_CALLBACK pfnCallback);
#endif

/* --- Unicode name mapping ----------------------------------------------- */
#if defined(UNICODE) || defined(_UNICODE)
#define DIE_ScanFile DIE_ScanFileW
#define DIE_ScanMemory DIE_ScanMemoryW
#define DIE_LoadDatabase DIE_LoadDatabaseW
#define DIE_ScanFileEx DIE_ScanFileExW
#define DIE_ScanMemoryEx DIE_ScanMemoryExW
#define DIE_FreeMemory DIE_FreeMemoryW
#else
#define DIE_ScanFile DIE_ScanFileA
#define DIE_ScanMemory DIE_ScanMemoryA
#define DIE_LoadDatabase DIE_LoadDatabaseA
#define DIE_ScanFileEx DIE_ScanFileExA
#define DIE_ScanMemoryEx DIE_ScanMemoryExA
#define DIE_FreeMemory DIE_FreeMemoryA
#endif

#ifdef __cplusplus
}
#endif

#endif
