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

/* xdex.c - Dalvik executable reader, ported from XDEX.
 *
 * Only the script-visible surface: the version string, the CRC-32 hash of the
 * map-item type sequence (the "[........]" detail), and the string and
 * type-descriptor lists the ProGuard/obfuscator signatures scan.
 */

#include "xdex.h"
#include "../core/utils.h"

typedef struct {
    cd_u16 nType;
    cd_u32 nCount;
    cd_u32 nOffset;
} DexMapItem;

static cd_u32 dex_u32(XBFile *pFile, cd_i64 nOffset, int bBE)
{
    return xb_u32(pFile, nOffset, bBE);
}

/* ULEB128, returning the value and advancing *pnOffset. */
static cd_u32 dex_uleb128(XBFile *pFile, cd_i64 *pnOffset)
{
    cd_u32 nValue = 0;
    int nShift = 0;
    int i = 0;

    for (i = 0; i < 5; i++) {
        cd_u8 nByte = xb_u8(pFile, *pnOffset);

        (*pnOffset)++;
        nValue |= ((cd_u32)(nByte & 0x7F)) << nShift;
        nShift += 7;

        if ((nByte & 0x80) == 0) {
            break;
        }
    }

    return nValue;
}

/* A string_data_item: ULEB128 length (UTF-16 units, ignored) then MUTF-8
 * bytes up to the terminating NUL. Class descriptors are ASCII, which is all
 * the signatures compare, so the raw bytes are returned as-is. */
static char *dex_read_string_data(XBFile *pFile, cd_i64 nDataOffset)
{
    cd_i64 nOffset = nDataOffset;
    cd_i64 nStart = 0;
    cd_i64 nEnd = 0;
    cd_i64 nAvail = 0;
    cd_i64 nMax = 0;
    cd_u32 nUnits = 0;

    if ((nDataOffset <= 0) || (nDataOffset >= pFile->nSize)) {
        return cd_strdup("");
    }

    nUnits = dex_uleb128(pFile, &nOffset); /* character count, UTF-16 units */
    nStart = nOffset;
    nAvail = pFile->nSize - nStart;

    if (nAvail <= 0) {
        return cd_strdup("");
    }

    /* Each UTF-16 unit is at most 3 MUTF-8 bytes; +1 for the terminator. The
     * bound keeps a crafted string_id from copying the rest of the file. */
    nMax = (cd_i64)nUnits * 3 + 1;

    if (nMax > nAvail) {
        nMax = nAvail;
    }

    nEnd = nStart;

    while (((nEnd - nStart) < nMax) && (pFile->pData[nEnd] != 0)) {
        nEnd++;
    }

    return cd_strndup((const char *)pFile->pData + nStart, (size_t)(nEnd - nStart));
}

/* CRC-32 (poly 0xEDB88320, init 0xFFFFFFFF, final xor) over the little-endian
 * u16 type of each map item, in order — XDEX::getMapItemsHash. */
static cd_u32 dex_map_hash(const DexMapItem *pItems, size_t nCount)
{
    static cd_u32 pTable[256];
    static int bInit = 0;
    cd_u32 nCrc = 0xFFFFFFFFu;
    size_t i = 0;

    if (!bInit) {
        int j = 0;

        for (i = 0; i < 256; i++) {
            cd_u32 nValue = (cd_u32)i;

            for (j = 0; j < 8; j++) {
                nValue = (nValue & 1) ? ((nValue >> 1) ^ 0xEDB88320u) : (nValue >> 1);
            }

            pTable[i] = nValue;
        }

        bInit = 1;
    }

    for (i = 0; i < nCount; i++) {
        cd_u8 nLo = (cd_u8)(pItems[i].nType & 0xFF);
        cd_u8 nHi = (cd_u8)((pItems[i].nType >> 8) & 0xFF);

        nCrc = (nCrc >> 8) ^ pTable[(nCrc ^ nLo) & 0xFF];
        nCrc = (nCrc >> 8) ^ pTable[(nCrc ^ nHi) & 0xFF];
    }

    return nCrc ^ 0xFFFFFFFFu;
}

int xdex_parse(XBFile *pFile, XDEX *pDex)
{
    int bBE = 0;
    cd_u32 nEndianTag = 0;
    cd_u32 nStringIdsSize = 0;
    cd_u32 nStringIdsOff = 0;
    cd_u32 nTypeIdsSize = 0;
    cd_u32 nTypeIdsOff = 0;
    cd_u32 nMapOff = 0;
    DexMapItem *pMapItems = NULL;
    size_t nMapCount = 0;
    cd_u32 i = 0;

    x_memset(pDex, 0, sizeof(XDEX));
    cdvec_init(&pDex->vecStrings);
    cdvec_init(&pDex->vecItemStrings);

    if ((pFile == NULL) || (pFile->nSize < 0x70)) {
        return 0;
    }

    /* magic "dex\n" */
    if ((xb_u8(pFile, 0) != 0x64) || (xb_u8(pFile, 1) != 0x65) || (xb_u8(pFile, 2) != 0x78) || (xb_u8(pFile, 3) != 0x0A)) {
        return 0;
    }

    /* version: three ASCII digits at offset 4, NUL-terminated. */
    {
        int k = 0;

        for (k = 0; k < 3; k++) {
            cd_u8 c = xb_u8(pFile, 4 + k);

            pDex->sVersion[k] = (char)((c == 0) ? '0' : c);
        }

        pDex->sVersion[3] = 0;
    }

    /* endian_tag at 0x28: 0x12345678 = LE, 0x78563412 = BE. */
    nEndianTag = xb_u32(pFile, 0x28, 0);

    if (nEndianTag == 0x78563412u) {
        bBE = 1;
    }

    pDex->bBigEndian = bBE;

    nStringIdsSize = xb_u32(pFile, 0x38, bBE);
    nStringIdsOff = xb_u32(pFile, 0x3C, bBE);
    nTypeIdsSize = xb_u32(pFile, 0x40, bBE);
    nTypeIdsOff = xb_u32(pFile, 0x44, bBE);
    nMapOff = xb_u32(pFile, 0x34, bBE);

    /* Map list -> items, for the hash. */
    if ((nMapOff != 0) && ((cd_i64)nMapOff + 4 <= pFile->nSize)) {
        cd_u32 nDeclared = xb_u32(pFile, nMapOff, bBE);
        cd_i64 nOffset = (cd_i64)nMapOff + 4;
        cd_i64 nAvail = pFile->nSize - nOffset;
        cd_u32 nItems = nDeclared;

        if ((cd_i64)nItems > nAvail / 12) {
            nItems = (cd_u32)(nAvail / 12);
        }

        if (nItems > 0x10000) {
            nItems = 0x10000;
        }

        if (nItems > 0) {
            pMapItems = (DexMapItem *)cd_malloc((size_t)nItems * sizeof(DexMapItem));

            for (i = 0; i < nItems; i++) {
                pMapItems[i].nType = xb_u16(pFile, nOffset, bBE);
                pMapItems[i].nCount = xb_u32(pFile, nOffset + 4, bBE);
                pMapItems[i].nOffset = xb_u32(pFile, nOffset + 8, bBE);
                nOffset += 12;
            }

            nMapCount = nItems;
        }
    }

    pDex->nMapHash = dex_map_hash(pMapItems, nMapCount);

    /* String pool. */
    for (i = 0; i < nStringIdsSize; i++) {
        cd_i64 nIdOffset = (cd_i64)nStringIdsOff + (cd_i64)i * 4;
        cd_u32 nDataOff = 0;

        if (nIdOffset + 4 > pFile->nSize) {
            break;
        }

        nDataOff = dex_u32(pFile, nIdOffset, bBE);
        cdvec_push(&pDex->vecStrings, dex_read_string_data(pFile, (cd_i64)nDataOff));
    }

    /* Type descriptors: index into the string pool. */
    for (i = 0; i < nTypeIdsSize; i++) {
        cd_i64 nIdOffset = (cd_i64)nTypeIdsOff + (cd_i64)i * 4;
        cd_u32 nStringIndex = 0;

        if (nIdOffset + 4 > pFile->nSize) {
            break;
        }

        nStringIndex = dex_u32(pFile, nIdOffset, bBE);

        if (((cd_i32)nStringIndex >= 0) && (nStringIndex < pDex->vecStrings.nSize)) {
            cdvec_push(&pDex->vecItemStrings, cd_strdup((const char *)pDex->vecStrings.ppData[nStringIndex]));
        }
    }

    cd_free(pMapItems);

    pDex->bValid = 1;

    return 1;
}

void xdex_free(XDEX *pDex)
{
    size_t i = 0;

    for (i = 0; i < pDex->vecStrings.nSize; i++) {
        cd_free(pDex->vecStrings.ppData[i]);
    }

    for (i = 0; i < pDex->vecItemStrings.nSize; i++) {
        cd_free(pDex->vecItemStrings.ppData[i]);
    }

    cdvec_free(&pDex->vecStrings);
    cdvec_free(&pDex->vecItemStrings);
    pDex->bValid = 0;
}

char *xdex_map_hash_hex(XDEX *pDex)
{
    CDBuf buf;

    cdbuf_init(&buf);
    cdbuf_appendf(&buf, "%08x", (unsigned)pDex->nMapHash);

    return cdbuf_detach(&buf, NULL);
}

static int dex_list_contains(CDVec *pList, const char *pString)
{
    size_t i = 0;

    for (i = 0; i < pList->nSize; i++) {
        if (x_strcmp((const char *)pList->ppData[i], pString) == 0) {
            return 1;
        }
    }

    return 0;
}

int xdex_string_present(XDEX *pDex, const char *pString)
{
    return dex_list_contains(&pDex->vecStrings, pString);
}

int xdex_item_string_present(XDEX *pDex, const char *pString)
{
    return dex_list_contains(&pDex->vecItemStrings, pString);
}

static const char *android_version_from_api(int nApi)
{
    /* XBinary::getAndroidVersionFromApi, limited to the API levels the DEX
     * version table can produce. */
    switch (nApi) {
        case 14: return "4.0.1-4.0.2";
        case 24: return "7.0";
        case 26: return "8.0";
        case 28: return "9.0";
        case 29: return "10.0";
        default: return "Unknown";
    }
}

const char *xdex_android_version(XDEX *pDex)
{
    /* XDEX::getOsVersion: map the DEX version to an API level, then to the
     * release; an unlisted version returns itself (035 was the first valid,
     * 036 was skipped). */
    static const struct {
        const char *pszVersion;
        int nApi;
    } g_versions[] = {{"035", 14}, {"037", 24}, {"038", 26}, {"039", 28}, {"040", 29}};
    size_t i = 0;

    for (i = 0; i < sizeof(g_versions) / sizeof(g_versions[0]); i++) {
        if (x_strcmp(pDex->sVersion, g_versions[i].pszVersion) == 0) {
            return android_version_from_api(g_versions[i].nApi);
        }
    }

    return pDex->sVersion;
}
