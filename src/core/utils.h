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

/* utils.h - the single point of contact with the C standard library.
 *
 * Every standard function used by cdie is reachable only through an `x_`
 * prefixed stub declared here and implemented in utils.c. No other
 * translation unit includes <string.h>, <stdlib.h>, <stdio.h> or <math.h>,
 * so swapping the runtime (a freestanding target, an instrumented
 * allocator, a different I/O backend) is a change to one file.
 *
 * Three things cannot be wrapped in a function because they are macros that
 * must expand in the caller's frame; they are exposed as macros instead:
 * the varargs helpers (X_VA_*), the non-local jump helpers (X_SETJMP,
 * X_LONGJMP) and X_OFFSETOF.
 *
 * Rule for new code: never call a standard function directly, always call
 * the x_ stub. The only exception is cd_fs.c, which additionally talks to
 * the platform directory APIs (<windows.h> / <dirent.h>).
 */

#ifndef X_UTILS_H
#define X_UTILS_H

/* Both are compiler-provided, not library functions: <stddef.h> only defines
 * types and macros, and va_list cannot be wrapped.                         */
#include <stddef.h> /* size_t, NULL, offsetof */
#include <stdarg.h> /* va_list                */

/* ------------------------------------------------------------- macros --- */

#define X_VA_LIST va_list
#define X_VA_START(ap, last) va_start(ap, last)
#define X_VA_END(ap) va_end(ap)
#define X_VA_COPY(dst, src) va_copy(dst, src)

/* C99 spells neither "never returns" nor "checks like printf", so both are
 * expressed through the GNU attributes where the compiler understands them
 * and expand to nothing everywhere else. Nothing depends on them.         */
#if defined(__GNUC__)
#define X_NORETURN __attribute__((noreturn))
#define X_PRINTF_LIKE(nFormat, nFirst) __attribute__((format(printf, nFormat, nFirst)))
#else
#define X_NORETURN
#define X_PRINTF_LIKE(nFormat, nFirst)
#endif

#define X_OFFSETOF(type, member) offsetof(type, member)

/* setjmp/longjmp are deliberately absent: they live in the C runtime, and a
 * CRT-free build cannot use them. The parser unwinds with an error flag.   */

/* fseek() origins, so callers do not need <stdio.h>. */
#define X_SEEK_SET 0
#define X_SEEK_CUR 1
#define X_SEEK_END 2

/* ------------------------------------------------------------ strings --- */

size_t x_strlen(const char *pString);
int x_strcmp(const char *pLeft, const char *pRight);
int x_strncmp(const char *pLeft, const char *pRight, size_t nSize);
char *x_strncpy(char *pDestination, const char *pSource, size_t nSize);
char *x_strchr(const char *pString, int nChar);
char *x_strrchr(const char *pString, int nChar);
char *x_strstr(const char *pHaystack, const char *pNeedle);

/* ------------------------------------------------------------- memory --- */

void *x_memcpy(void *pDestination, const void *pSource, size_t nSize);
void *x_memmove(void *pDestination, const void *pSource, size_t nSize);
void *x_memset(void *pDestination, int nValue, size_t nSize);
int x_memcmp(const void *pLeft, const void *pRight, size_t nSize);

void *x_malloc(size_t nSize);
void *x_calloc(size_t nCount, size_t nSize);
void *x_realloc(void *pPtr, size_t nSize);
void x_free(void *pPtr);

/* ------------------------------------------------------------ process --- */

X_NORETURN void x_exit(int nCode);
char *x_getenv(const char *pName);
void x_qsort(void *pBase, size_t nCount, size_t nSize, int (*fnCompare)(const void *, const void *));

/* ---------------------------------------------------------- conversion --- */

/* The end pointer stays const: unlike the C library these never hand back a
 * writable view of the input, so the qualifier is carried through.        */
double x_strtod(const char *pString, const char **ppEnd);
long x_strtol(const char *pString, const char **ppEnd, int nBase);
unsigned long long x_strtoull(const char *pString, const char **ppEnd, int nBase);

int x_rand(void);
#define X_RAND_MAX 32767

/* ---------------------------------------------------------------- I/O --- */

/* Streams are opaque handles so that <stdio.h> stays inside utils.c. */
void *x_stdout(void);
void *x_stderr(void);

X_PRINTF_LIKE(1, 2) int x_printf(const char *pFormat, ...);
X_PRINTF_LIKE(2, 3) int x_fprintf(void *pStream, const char *pFormat, ...);
X_PRINTF_LIKE(3, 4) int x_snprintf(char *pBuffer, size_t nSize, const char *pFormat, ...);
X_PRINTF_LIKE(3, 0) int x_vsnprintf(char *pBuffer, size_t nSize, const char *pFormat, X_VA_LIST args);
int x_fflush(void *pStream);

void *x_fopen(const char *pFileName, const char *pMode);
int x_fclose(void *pFile);

#if defined(_WIN32)
/* UTF-8 <-> UTF-16 at the Win32 boundary. Only defined on Windows; every
 * path in the program is UTF-8, but the file and directory APIs are the wide
 * ones so that non-ANSI paths open. The result is a heap-allocated WCHAR* /
 * char* (freed with x_free); the header keeps them opaque to avoid pulling
 * in <windows.h>. */
void *x_utf8_to_utf16(const char *pUtf8);
char *x_utf16_to_utf8(const void *pUtf16);
#endif
size_t x_fread(void *pBuffer, size_t nSize, size_t nCount, void *pFile);
/* File offsets are 64 bit on every target: `long` is 32 bit on 64-bit
 * Windows, which truncates the size of any file at or above 2 GiB. */
int x_fseek(void *pFile, long long nOffset, int nOrigin);
long long x_ftell(void *pFile);
void x_rewind(void *pFile);

/* ----------------------------------------------------------------- math -- */

double x_floor(double nValue);
double x_ceil(double nValue);
double x_fabs(double nValue);
double x_sqrt(double nValue);
double x_log(double nValue);
double x_log10(double nValue);
double x_exp(double nValue);
double x_sin(double nValue);
double x_cos(double nValue);
double x_tan(double nValue);
double x_pow(double nBase, double nExponent);
double x_fmod(double nValue, double nDivisor);

/* Quiet NaN and positive infinity, so that <math.h> macros stay internal. */
double x_nan(void);
double x_inf(void);
int x_isnan(double nValue);
int x_isinf(double nValue);
/* The sign bit, which is not the same question as nValue < 0: -0.0 prints
 * with its sign and compares equal to zero.                                */
int x_signbit(double nValue);

/* ------------------------------------------- double <-> decimal string --- */

/* ECMAScript Number::toString: the shortest decimal that reads back as the
 * same double, with the exponent rules of the specification. Returns the
 * number of characters written.                                            */
int x_dtoa_shortest(double nValue, char *pBuffer, size_t nBufferSize);

/* Number.prototype.toFixed: nDigits digits after the decimal point. */
int x_dtoa_fixed(double nValue, int nDigits, char *pBuffer, size_t nBufferSize);

/* Number.prototype.toPrecision: nDigits significant digits. */
int x_dtoa_precision(double nValue, int nDigits, char *pBuffer, size_t nBufferSize);

/* printf's %e, %f and %g of the magnitude of nValue - never a sign, the
 * caller owns that - under the C99 rules rather than the ECMAScript ones the
 * three entry points above follow. nConversion is one of 'e', 'E', 'f', 'F',
 * 'g' or 'G', nPrecision is the conversion precision or -1 for the default,
 * and bAlt is the # flag. Exists for the hand-written formatter in utils.c,
 * whose platform has no printf to fall back on. Returns the number of
 * characters the conversion produces, snprintf-style.                       */
int x_dtoa_cformat(double nValue, char nConversion, int nPrecision, int bAlt, char *pBuffer, size_t nBufferSize);

/* ---------------------------------------------------------------- clock --- */

/* Milliseconds counted from an unspecified fixed point, non-decreasing for
 * the lifetime of the process. Only differences of two readings mean
 * anything; the profiling trace and the archive-scan budget are the callers.  */
long long x_clock_ms(void);

/* ---------------------------------------------------------- entry point --- */

/* The program entry point. On Windows the real entry is provided by
 * utils_entry.c, which builds argv from GetCommandLineA and calls this; the
 * C runtime is not involved. Elsewhere main() forwards to it.              */
int x_main(int nArgc, char *ppArgv[]);

#endif /* X_UTILS_H */
