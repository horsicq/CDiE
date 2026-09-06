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

#include "cd_common.h"

/* The largest representable allocation request; <limits.h> / <stdint.h> are
 * outside the wrapped set, so SIZE_MAX is spelled out. */
#define CD_SIZE_MAX ((size_t)-1)

/* The engine cannot continue without the memory it asked for, so every path
 * that runs out - a failed allocation or a size that does not fit a size_t -
 * ends here. */
X_NORETURN static void cd_out_of_memory(void)
{
    x_fprintf(x_stderr(), "cdie: out of memory\n");
    x_exit(3);
}

/* See the policy note in cd_common.h. The reserve is what makes the retry
 * below worth attempting: releasing it buys the wind-down enough room to
 * hand out real pointers instead of NULLs.
 *
 * 64 KiB is measured, not guessed, and the measurement is the reason the
 * number has to stay small. The reserve is taken out of the same headroom it
 * exists to cover, so every byte of it is a byte the scan no longer has, and
 * an oversized reserve turns scans that would have finished into scans that
 * report a failure. Under an imposed ceiling, scanning /usr/bin/gzip through
 * DIE_ScanFileA: at 4.6 MB and above the reserve below returns the same
 * result as a build with no policy at all, and a 1 MiB reserve instead
 * returns an empty result everywhere from 4.4 MB to 5.5 MB - a band exactly
 * as wide as the excess. This size covers the allocations between the first
 * failure and the end of the scan while costing the scan nothing it needed. */
#define CD_OOM_RESERVE 0x10000

static void *g_pOomReserve = NULL;
static int g_bOomSoft = 0;
static int g_bOomRaised = 0;

int cd_alloc_begin_soft_oom(void)
{
    if (g_bOomSoft) {
        return 1;
    }

    /* x_malloc, not cd_malloc: failing to take the reserve must not be the
     * first thing that ends the process. */
    g_pOomReserve = x_malloc(CD_OOM_RESERVE);

    if (g_pOomReserve == NULL) {
        return 0;
    }

    g_bOomSoft = 1;
    g_bOomRaised = 0;

    return 1;
}

void cd_alloc_end_soft_oom(void)
{
    x_free(g_pOomReserve);
    g_pOomReserve = NULL;
    g_bOomSoft = 0;
    g_bOomRaised = 0;
}

int cd_alloc_oom(void)
{
    return g_bOomRaised;
}

/* Raises the sticky flag and releases the reserve, so that the caller's
 * retry has somewhere to come from. Returns 1 when a retry is worth making;
 * under the console policy, or once the reserve is spent, it returns 0 and
 * the caller ends the process as before.                                    */
static int cd_out_of_memory_retry(void)
{
    if (!g_bOomSoft) {
        return 0;
    }

    g_bOomRaised = 1;

    if (g_pOomReserve == NULL) {
        return 0;
    }

    x_free(g_pOomReserve);
    g_pOomReserve = NULL;

    return 1;
}

/* The non-aborting variants. These are the way out for a caller that can
 * report a failure instead of needing the memory: they raise the sticky flag
 * and return NULL, under either policy, and never end the process. A caller
 * that cannot cope with NULL must keep using cd_malloc.                    */
void *cd_try_malloc(size_t nSize)
{
    void *pResult = x_malloc(nSize ? nSize : 1);

    if (pResult == NULL) {
        g_bOomRaised = 1;
    }

    return pResult;
}

void *cd_try_calloc(size_t nCount, size_t nSize)
{
    void *pResult = x_calloc(nCount ? nCount : 1, nSize ? nSize : 1);

    if (pResult == NULL) {
        g_bOomRaised = 1;
    }

    return pResult;
}

void *cd_try_realloc(void *pPtr, size_t nSize)
{
    void *pResult = x_realloc(pPtr, nSize ? nSize : 1);

    if (pResult == NULL) {
        g_bOomRaised = 1;
    }

    return pResult;
}

void *cd_malloc(size_t nSize)
{
    size_t nRequest = nSize ? nSize : 1;
    void *pResult = x_malloc(nRequest);

    if ((pResult == NULL) && cd_out_of_memory_retry()) {
        pResult = x_malloc(nRequest);
    }

    if (pResult == NULL) {
        cd_out_of_memory();
    }

    return pResult;
}

void *cd_calloc(size_t nCount, size_t nSize)
{
    size_t nRequestCount = nCount ? nCount : 1;
    size_t nRequestSize = nSize ? nSize : 1;
    void *pResult = x_calloc(nRequestCount, nRequestSize);

    if ((pResult == NULL) && cd_out_of_memory_retry()) {
        pResult = x_calloc(nRequestCount, nRequestSize);
    }

    if (pResult == NULL) {
        cd_out_of_memory();
    }

    return pResult;
}

void *cd_realloc(void *pPtr, size_t nSize)
{
    size_t nRequest = nSize ? nSize : 1;
    void *pResult = x_realloc(pPtr, nRequest);

    if ((pResult == NULL) && cd_out_of_memory_retry()) {
        pResult = x_realloc(pPtr, nRequest);
    }

    if (pResult == NULL) {
        cd_out_of_memory();
    }

    return pResult;
}

void cd_free(void *pPtr)
{
    x_free(pPtr);
}

char *cd_strdup(const char *pString)
{
    if (pString == NULL) {
        return NULL;
    }

    return cd_strndup(pString, x_strlen(pString));
}

char *cd_strndup(const char *pString, size_t nSize)
{
    char *pResult = (char *)cd_malloc(nSize + 1);

    if (nSize) {
        x_memcpy(pResult, pString, nSize);
    }

    pResult[nSize] = 0;

    return pResult;
}

/* ---------------------------------------------------------------- buffer  */

void cdbuf_init(CDBuf *pBuf)
{
    pBuf->pData = NULL;
    pBuf->nSize = 0;
    pBuf->nCapacity = 0;
}

void cdbuf_free(CDBuf *pBuf)
{
    cd_free(pBuf->pData);
    cdbuf_init(pBuf);
}

void cdbuf_reserve(CDBuf *pBuf, size_t nCapacity)
{
    size_t nNeeded = 0;

    /* The buffer always keeps room for the terminator. */
    if (nCapacity == CD_SIZE_MAX) {
        cd_out_of_memory();
    }

    nNeeded = nCapacity + 1;

    if (nNeeded > pBuf->nCapacity) {
        size_t nNew = pBuf->nCapacity ? pBuf->nCapacity : 32;

        while (nNew < nNeeded) {
            /* Doubling past half of the address space wraps to 0 and the
             * loop never ends, so take the exact size instead. */
            if (nNew > (CD_SIZE_MAX / 2)) {
                nNew = nNeeded;
                break;
            }

            nNew *= 2;
        }

        pBuf->pData = (char *)cd_realloc(pBuf->pData, nNew);
        pBuf->nCapacity = nNew;
    }
}

void cdbuf_clear(CDBuf *pBuf)
{
    pBuf->nSize = 0;

    if (pBuf->pData) {
        pBuf->pData[0] = 0;
    }
}

void cdbuf_append(CDBuf *pBuf, const void *pData, size_t nSize)
{
    if (nSize == 0) {
        return;
    }

    if (nSize > (CD_SIZE_MAX - pBuf->nSize)) {
        cd_out_of_memory();
    }

    cdbuf_reserve(pBuf, pBuf->nSize + nSize);
    x_memcpy(pBuf->pData + pBuf->nSize, pData, nSize);
    pBuf->nSize += nSize;
    pBuf->pData[pBuf->nSize] = 0;
}

void cdbuf_append_str(CDBuf *pBuf, const char *pString)
{
    if (pString) {
        cdbuf_append(pBuf, pString, x_strlen(pString));
    }
}

void cdbuf_append_ch(CDBuf *pBuf, char nChar)
{
    cdbuf_reserve(pBuf, pBuf->nSize + 1);
    pBuf->pData[pBuf->nSize++] = nChar;
    pBuf->pData[pBuf->nSize] = 0;
}

X_PRINTF_LIKE(2, 3) void cdbuf_appendf(CDBuf *pBuf, const char *pFormat, ...)
{
    char sStack[512];
    X_VA_LIST args;
    int nCount;

    X_VA_START(args, pFormat);
    nCount = x_vsnprintf(sStack, sizeof(sStack), pFormat, args);
    X_VA_END(args);

    if (nCount < 0) {
        return;
    }

    if ((size_t)nCount < sizeof(sStack)) {
        cdbuf_append(pBuf, sStack, (size_t)nCount);
    } else {
        char *pHeap = (char *)cd_malloc((size_t)nCount + 1);

        X_VA_START(args, pFormat);
        x_vsnprintf(pHeap, (size_t)nCount + 1, pFormat, args);
        X_VA_END(args);

        cdbuf_append(pBuf, pHeap, (size_t)nCount);
        cd_free(pHeap);
    }
}

char *cdbuf_detach(CDBuf *pBuf, size_t *pnSize)
{
    char *pResult = pBuf->pData;

    if (pResult == NULL) {
        pResult = (char *)cd_malloc(1);
        pResult[0] = 0;
    }

    if (pnSize) {
        *pnSize = pBuf->nSize;
    }

    cdbuf_init(pBuf);

    return pResult;
}

/* ---------------------------------------------------------------- vector  */

void cdvec_init(CDVec *pVec)
{
    pVec->ppData = NULL;
    pVec->nSize = 0;
    pVec->nCapacity = 0;
}

void cdvec_free(CDVec *pVec)
{
    cd_free(pVec->ppData);
    cdvec_init(pVec);
}

static void cdvec_grow(CDVec *pVec, size_t nNeeded)
{
    if (nNeeded > pVec->nCapacity) {
        size_t nNew = pVec->nCapacity ? pVec->nCapacity : 8;

        while (nNew < nNeeded) {
            /* See cdbuf_reserve: doubling wraps instead of terminating. */
            if (nNew > (CD_SIZE_MAX / 2)) {
                nNew = nNeeded;
                break;
            }

            nNew *= 2;
        }

        if (nNew > (CD_SIZE_MAX / sizeof(void *))) {
            cd_out_of_memory();
        }

        pVec->ppData = (void **)cd_realloc(pVec->ppData, nNew * sizeof(void *));
        pVec->nCapacity = nNew;
    }
}

void cdvec_push(CDVec *pVec, void *pItem)
{
    cdvec_grow(pVec, pVec->nSize + 1);
    pVec->ppData[pVec->nSize++] = pItem;
}

void cdvec_insert(CDVec *pVec, size_t nIndex, void *pItem)
{
    if (nIndex > pVec->nSize) {
        nIndex = pVec->nSize;
    }

    cdvec_grow(pVec, pVec->nSize + 1);
    x_memmove(pVec->ppData + nIndex + 1, pVec->ppData + nIndex, (pVec->nSize - nIndex) * sizeof(void *));
    pVec->ppData[nIndex] = pItem;
    pVec->nSize++;
}

void cdvec_remove(CDVec *pVec, size_t nIndex)
{
    if (nIndex >= pVec->nSize) {
        return;
    }

    x_memmove(pVec->ppData + nIndex, pVec->ppData + nIndex + 1, (pVec->nSize - nIndex - 1) * sizeof(void *));
    pVec->nSize--;
}

void cdvec_clear(CDVec *pVec)
{
    pVec->nSize = 0;
}

/* ------------------------------------------------------------ misc utils  */

cd_u32 cd_hash_str(const char *pString, size_t nSize)
{
    /* FNV-1a */
    cd_u32 nHash = 2166136261u;
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        nHash ^= (cd_u8)pString[i];
        nHash *= 16777619u;
    }

    return nHash;
}

static char cd_lower_ascii(char nChar)
{
    if ((nChar >= 'A') && (nChar <= 'Z')) {
        return (char)(nChar - 'A' + 'a');
    }

    return nChar;
}

int cd_stricmp_ascii(const char *pLeft, const char *pRight)
{
    while (*pLeft && *pRight) {
        char a = cd_lower_ascii(*pLeft);
        char b = cd_lower_ascii(*pRight);

        if (a != b) {
            return (int)(unsigned char)a - (int)(unsigned char)b;
        }

        pLeft++;
        pRight++;
    }

    return (int)(unsigned char)cd_lower_ascii(*pLeft) - (int)(unsigned char)cd_lower_ascii(*pRight);
}

int cd_strnicmp_ascii(const char *pLeft, const char *pRight, size_t nSize)
{
    size_t i = 0;

    for (i = 0; i < nSize; i++) {
        char a = cd_lower_ascii(pLeft[i]);
        char b = cd_lower_ascii(pRight[i]);

        if (a != b) {
            return (int)(unsigned char)a - (int)(unsigned char)b;
        }

        if (a == 0) {
            break;
        }
    }

    return 0;
}
