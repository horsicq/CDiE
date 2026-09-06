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

/* cd_common.h - basic types, allocation helpers and a growable byte buffer. */

#ifndef CD_COMMON_H
#define CD_COMMON_H

/* utils.h is the only header that reaches the C standard library; it also
 * provides size_t and NULL, so nothing else here needs a system include.  */
#include "utils.h"

#if defined(_MSC_VER)
typedef signed __int8 cd_i8;
typedef unsigned __int8 cd_u8;
typedef signed __int16 cd_i16;
typedef unsigned __int16 cd_u16;
typedef signed __int32 cd_i32;
typedef unsigned __int32 cd_u32;
typedef signed __int64 cd_i64;
typedef unsigned __int64 cd_u64;
#else
#include <stdint.h>
typedef int8_t cd_i8;
typedef uint8_t cd_u8;
typedef int16_t cd_i16;
typedef uint16_t cd_u16;
typedef int32_t cd_i32;
typedef uint32_t cd_u32;
typedef int64_t cd_i64;
typedef uint64_t cd_u64;
#endif

#ifndef CD_TRUE
#define CD_TRUE 1
#define CD_FALSE 0
#endif

/* ---------------------------------------------------------------- memory  */

/* Out-of-memory policy.
 *
 * cd_malloc and friends never hand back NULL: some two hundred call sites in
 * the engine use the result without a check, and a CRT-free build has no
 * longjmp to unwind an allocation failure with. The default is therefore to
 * end the process, which is the right answer for a console tool and the
 * wrong one for libdie.so, which lives inside somebody else's process.
 *
 * cd_alloc_begin_soft_oom() takes an emergency reserve and switches the
 * allocator over for the duration of one scan. The first failure hands the
 * reserve back to the allocator, raises a sticky flag and retries, so the
 * caller still receives the memory it asked for and no pointer in the engine
 * becomes NULL. The scan then winds down at once - scan.c tests
 * cd_alloc_oom() beside its cancellation flag - and the entry point turns
 * the flag into a failed scan. A request larger than the reserve still ends
 * the process, so this narrows the window rather than closing it.
 *
 * The state is process-wide, like the rest of the library's, so one scan at
 * a time per process. A single request larger than the reserve still ends
 * the process: closing that too means giving every call site a NULL check,
 * for which cd_try_malloc below is the primitive.                           */
int cd_alloc_begin_soft_oom(void);
void cd_alloc_end_soft_oom(void);
/* Sticky within one begin/end pair: an allocation has failed. */
int cd_alloc_oom(void);

/* Return NULL on failure and raise cd_alloc_oom() instead of ending the
 * process. For the sites whose size comes from the file being scanned, where
 * the caller has somewhere to report the failure to. */
void *cd_try_malloc(size_t nSize);
void *cd_try_calloc(size_t nCount, size_t nSize);
void *cd_try_realloc(void *pPtr, size_t nSize);

void *cd_malloc(size_t nSize);
void *cd_calloc(size_t nCount, size_t nSize);
void *cd_realloc(void *pPtr, size_t nSize);
void cd_free(void *pPtr);
char *cd_strdup(const char *pString);
char *cd_strndup(const char *pString, size_t nSize);

/* ---------------------------------------------------------------- buffer  */

/* Growable byte buffer. Always keeps a terminating NUL past pData[nSize]. */
typedef struct {
    char *pData;
    size_t nSize;
    size_t nCapacity;
} CDBuf;

void cdbuf_init(CDBuf *pBuf);
void cdbuf_free(CDBuf *pBuf);
void cdbuf_reserve(CDBuf *pBuf, size_t nCapacity);
void cdbuf_clear(CDBuf *pBuf);
void cdbuf_append(CDBuf *pBuf, const void *pData, size_t nSize);
void cdbuf_append_str(CDBuf *pBuf, const char *pString);
void cdbuf_append_ch(CDBuf *pBuf, char nChar);
X_PRINTF_LIKE(2, 3) void cdbuf_appendf(CDBuf *pBuf, const char *pFormat, ...);
/* Detaches the buffer contents; the caller owns the returned pointer. */
char *cdbuf_detach(CDBuf *pBuf, size_t *pnSize);

/* ---------------------------------------------------------------- vector  */

/* Vector of pointers. */
typedef struct {
    void **ppData;
    size_t nSize;
    size_t nCapacity;
} CDVec;

void cdvec_init(CDVec *pVec);
void cdvec_free(CDVec *pVec);
void cdvec_push(CDVec *pVec, void *pItem);
void cdvec_insert(CDVec *pVec, size_t nIndex, void *pItem);
void cdvec_remove(CDVec *pVec, size_t nIndex);
void cdvec_clear(CDVec *pVec);

/* ------------------------------------------------------------ misc utils  */

cd_u32 cd_hash_str(const char *pString, size_t nSize);
int cd_stricmp_ascii(const char *pLeft, const char *pRight);
/* Case-insensitive ASCII compare of two counted strings. */
int cd_strnicmp_ascii(const char *pLeft, const char *pRight, size_t nSize);

#endif /* CD_COMMON_H */
