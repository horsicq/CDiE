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

#include "xpyc.h"
#include "../core/utils.h"

typedef struct {
    cd_u16 nMagic;
    const char *pszVersion;
} PycMagicRecord;

/* Magic-number -> interpreter release, verbatim from XPYC::g_records (sorted by
 * magic). Binary search there; a linear scan is used here and yields the same
 * result because the magic values are unique.                               */
static const PycMagicRecord g_records[] = {
    {2012, "1.5.2"},  {3000, "3000"},   {3111, "3.0a4"},  {3131, "3.0b1"},  {3141, "3.1a1"},
    {3151, "3.1a1"},  {3160, "3.2a1"},  {3170, "3.2a2"},  {3180, "3.2a3"},  {3190, "3.3a1"},
    {3200, "3.3a1"},  {3210, "3.3a1"},  {3220, "3.3a2"},  {3230, "3.3a4"},  {3250, "3.4a1"},
    {3260, "3.4a1"},  {3270, "3.4a1"},  {3280, "3.4a1"},  {3290, "3.4a4"},  {3300, "3.4a4"},
    {3310, "3.4rc2"}, {3320, "3.5a1"},  {3330, "3.5b1"},  {3340, "3.5b2"},  {3350, "3.5b3"},
    {3351, "3.5.2"},  {3360, "3.6a0"},  {3361, "3.6a1"},  {3370, "3.6a2"},  {3371, "3.6a2"},
    {3372, "3.6a2"},  {3373, "3.6b1"},  {3375, "3.6b1"},  {3376, "3.6b1"},  {3377, "3.6b1"},
    {3378, "3.6b2"},  {3379, "3.6rc1"}, {3390, "3.7a1"},  {3391, "3.7a2"},  {3392, "3.7a4"},
    {3393, "3.7b1"},  {3394, "3.7b5"},  {3400, "3.8a1"},  {3401, "3.8a1"},  {3410, "3.8a1"},
    {3411, "3.8b2"},  {3412, "3.8b2"},  {3413, "3.8b4"},  {3420, "3.9a0"},  {3421, "3.9a0"},
    {3422, "3.9a0"},  {3423, "3.9a2"},  {3424, "3.9a2"},  {3425, "3.9a2"},  {3430, "3.10a1"},
    {3431, "3.10a1"}, {3432, "3.10a2"}, {3433, "3.10a2"}, {3434, "3.10a6"}, {3435, "3.10a7"},
    {3436, "3.10b1"}, {3437, "3.10b1"}, {3438, "3.10b1"}, {3439, "3.10b1"}, {3450, "3.11a1"},
    {3451, "3.11a1"}, {3452, "3.11a1"}, {3453, "3.11a1"}, {3454, "3.11a1"}, {3455, "3.11a1"},
    {3456, "3.11a1"}, {3457, "3.11a1"}, {3458, "3.11a1"}, {3459, "3.11a1"}, {3460, "3.11a1"},
    {3461, "3.11a1"}, {3462, "3.11a2"}, {3463, "3.11a3"}, {3464, "3.11a3"}, {3465, "3.11a3"},
    {3466, "3.11a4"}, {3467, "3.11a4"}, {3468, "3.11a4"}, {3469, "3.11a4"}, {3470, "3.11a4"},
    {3471, "3.11a4"}, {3472, "3.11a4"}, {3473, "3.11a4"}, {3474, "3.11a4"}, {3475, "3.11a5"},
    {3476, "3.11a5"}, {3477, "3.11a5"}, {3478, "3.11a5"}, {3479, "3.11a5"}, {3480, "3.11a5"},
    {3481, "3.11a5"}, {3482, "3.11a5"}, {3483, "3.11a5"}, {3484, "3.11a5"}, {3485, "3.11a5"},
    {3486, "3.11a6"}, {3487, "3.11a6"}, {3488, "3.11a6"}, {3489, "3.11a6"}, {3490, "3.11a6"},
    {3491, "3.11a6"}, {3492, "3.11a7"}, {3493, "3.11a7"}, {3494, "3.11a7"}, {3495, "3.11b4"},
    {3500, "3.12a1"}, {3501, "3.12a1"}, {3502, "3.12a1"}, {3503, "3.12a1"}, {3504, "3.12a1"},
    {3505, "3.12a1"}, {3506, "3.12a1"}, {3507, "3.12a1"}, {3508, "3.12a1"}, {3509, "3.12a1"},
    {3510, "3.12a2"}, {3511, "3.12a2"}, {3512, "3.12a2"}, {3513, "3.12a4"}, {3514, "3.12a4"},
    {3515, "3.12a5"}, {3516, "3.12a5"}, {3517, "3.12a5"}, {3518, "3.12a6"}, {3519, "3.12a6"},
    {3520, "3.12a6"}, {3521, "3.12a7"}, {3522, "3.12a7"}, {3523, "3.12a7"}, {3524, "3.12a7"},
    {3525, "3.12b1"}, {3526, "3.12b1"}, {3527, "3.12b1"}, {3528, "3.12b1"}, {3529, "3.12b1"},
    {3530, "3.12b1"}, {3531, "3.12b1"}, {3550, "3.13a1"}, {3551, "3.13a1"}, {3552, "3.13a1"},
    {3553, "3.13a1"}, {3554, "3.13a1"}, {3555, "3.13a1"}, {3556, "3.13a1"}, {3557, "3.13a1"},
    {3558, "3.13a1"}, {3559, "3.13a1"}, {3560, "3.13a1"}, {3561, "3.13a1"}, {3562, "3.13a1"},
    {3563, "3.13a1"}, {3564, "3.13a1"}, {3565, "3.13a1"}, {3566, "3.13a1"}, {3567, "3.13a1"},
    {3568, "3.13a1"}, {3569, "3.13a5"}, {3570, "3.13a6"}, {3571, "3.13b1"}, {3600, "3.14 will start with"},
    {5042, "1.6"},    {5082, "2.0.1"},  {6020, "2.1.2"},  {6071, "2.2"},    {6201, "2.3a0"},
    {6202, "2.3a0"},  {6204, "2.4a0"},  {6205, "2.4a3"},  {6206, "2.4b1"},  {6207, "2.5a0"},
    {6208, "2.5a0"},  {6209, "2.5a0"},  {6210, "2.5b3"},  {6211, "2.5b3"},  {6212, "2.5c1"},
    {6213, "2.5c2"},  {6215, "2.6a0"},  {6216, "2.6a1"},  {6217, "2.7a0"},  {6218, "2.7a0"},
    {6219, "2.7a0"},  {6220, "2.7a0"},  {6221, "2.7a0"}};

static const char *magic_to_version(cd_u16 nMagicValue)
{
    size_t nCount = sizeof(g_records) / sizeof(g_records[0]);
    size_t i;

    for (i = 0; i < nCount; i++) {
        if (g_records[i].nMagic == nMagicValue) {
            return g_records[i].pszVersion;
        }
    }

    return "";
}

int xpyc_is_known_magic(cd_u16 nMagic)
{
    size_t nCount = sizeof(g_records) / sizeof(g_records[0]);
    size_t i;

    for (i = 0; i < nCount; i++) {
        if (g_records[i].nMagic == nMagic) {
            return 1;
        }
    }

    return 0;
}

/* ---------------------------------------------------- marshal const reader */

/* Marshal type codes (masked with 0x7F; 0x80 is the FLAG_REF bit). */
#define PYC_T_NONE 'N'
#define PYC_T_FALSE 'F'
#define PYC_T_TRUE 'T'
#define PYC_T_INT 'i'
#define PYC_T_INT64 'I'
#define PYC_T_FLOAT 'f'
#define PYC_T_STRING 's'
#define PYC_T_INTERNED 't'
#define PYC_T_UNICODE 'u'
#define PYC_T_ASCII 'a'
#define PYC_T_ASCII_INTERNED 'A'
#define PYC_T_SHORT_ASCII 'z'
#define PYC_T_SHORT_ASCII_INTERNED 'Z'
#define PYC_T_TUPLE '('
#define PYC_T_SMALL_TUPLE ')'
#define PYC_T_CODE 'c'

static void parse_version_numbers(const char *pVersion, int *pnMajor, int *pnMinor)
{
    int nMajor = 0;
    int nMinor = 0;
    const char *p = pVersion;

    while ((*p >= '0') && (*p <= '9')) {
        nMajor = nMajor * 10 + (*p - '0');
        p++;
    }

    if (*p == '.') {
        p++;

        while ((*p >= '0') && (*p <= '9')) {
            nMinor = nMinor * 10 + (*p - '0');
            p++;
        }
    }

    *pnMajor = nMajor;
    *pnMinor = nMinor;
}

/* Reads a marshal string at *pnOffset. On a string type it advances *pnOffset
 * past it and returns an owned UTF-8 copy; otherwise returns NULL and leaves
 * *pnOffset unchanged. Mirrors XPYC::_readMarshalString.                     */
static char *marshal_read_string(XBFile *pFile, cd_i64 *pnOffset)
{
    cd_i64 nOffset = *pnOffset;
    cd_i64 nSize = pFile->nSize;
    cd_u8 nActual = 0;
    cd_i32 nLength = 0;
    CDBuf buf;
    cd_i32 i = 0;

    if ((nOffset + 1) > nSize) {
        return NULL;
    }

    nActual = (cd_u8)(xb_u8(pFile, nOffset) & 0x7F);
    nOffset += 1;

    if ((nActual == PYC_T_SHORT_ASCII) || (nActual == PYC_T_SHORT_ASCII_INTERNED)) {
        if ((nOffset + 1) > nSize) {
            return NULL;
        }

        nLength = (cd_i32)xb_u8(pFile, nOffset);
        nOffset += 1;
    } else if ((nActual == PYC_T_STRING) || (nActual == PYC_T_INTERNED) || (nActual == PYC_T_UNICODE) || (nActual == PYC_T_ASCII) || (nActual == PYC_T_ASCII_INTERNED)) {
        if ((nOffset + 4) > nSize) {
            return NULL;
        }

        nLength = (cd_i32)xb_u32(pFile, nOffset, 0);
        nOffset += 4;
    } else {
        return NULL;
    }

    if ((nLength < 0) || (nLength > 1024 * 1024) || ((nOffset + nLength) > nSize)) {
        return NULL;
    }

    cdbuf_init(&buf);

    if ((nActual == PYC_T_UNICODE) || (nActual == PYC_T_STRING)) {
        /* Already UTF-8 bytes (QString::fromUtf8). */
        cdbuf_append(&buf, pFile->pData + nOffset, (size_t)nLength);
    } else {
        /* Latin-1 fixed length -> UTF-8 (read_ansiString). */
        for (i = 0; i < nLength; i++) {
            cd_u8 nByte = xb_u8(pFile, nOffset + i);

            if (nByte < 0x80) {
                cdbuf_append_ch(&buf, (char)nByte);
            } else {
                cdbuf_append_ch(&buf, (char)(0xC0 | (nByte >> 6)));
                cdbuf_append_ch(&buf, (char)(0x80 | (nByte & 0x3F)));
            }
        }
    }

    nOffset += nLength;
    *pnOffset = nOffset;

    return cdbuf_detach(&buf, NULL);
}

/* Advances past one marshal object at *pnOffset. Top-level string values are
 * appended to pConsts. Returns 1 if the object was consumed, 0 if parsing must
 * stop. XPYC::getCodeObject cannot advance past a code object, so the set of
 * reachable string constants ends at the first one: treating TYPE_CODE (and
 * any unhandled type) as a stop reproduces exactly which constants
 * isConstPresent can observe. Nested tuples of simple values are skipped so
 * constants after them stay reachable, matching the reference.              */
static int marshal_read_object(XBFile *pFile, cd_i64 *pnOffset, CDVec *pConsts, int bTopLevel, int nDepth)
{
    cd_i64 nOffset = *pnOffset;
    cd_i64 nSize = pFile->nSize;
    cd_u8 nActual = 0;

    /* Nested tuples recurse; cap the depth so a crafted file of repeating
     * one-element small tuples cannot overflow the stack. Real modules nest
     * constants only a handful of levels, so the cap never fires on them and
     * the output stays identical (a stop matches getCodeObject's semantics). */
    if (nDepth > 200) {
        return 0;
    }

    if ((nOffset + 1) > nSize) {
        return 0;
    }

    nActual = (cd_u8)(xb_u8(pFile, nOffset) & 0x7F);

    if ((nActual == PYC_T_NONE) || (nActual == PYC_T_TRUE) || (nActual == PYC_T_FALSE)) {
        *pnOffset = nOffset + 1;

        return 1;
    }

    if (nActual == PYC_T_INT) {
        if ((nOffset + 1 + 4) > nSize) {
            return 0;
        }

        *pnOffset = nOffset + 1 + 4;

        return 1;
    }

    if ((nActual == PYC_T_INT64) || (nActual == PYC_T_FLOAT)) {
        if ((nOffset + 1 + 8) > nSize) {
            return 0;
        }

        *pnOffset = nOffset + 1 + 8;

        return 1;
    }

    if ((nActual == PYC_T_STRING) || (nActual == PYC_T_INTERNED) || (nActual == PYC_T_UNICODE) || (nActual == PYC_T_ASCII) || (nActual == PYC_T_ASCII_INTERNED) ||
        (nActual == PYC_T_SHORT_ASCII) || (nActual == PYC_T_SHORT_ASCII_INTERNED)) {
        char *pStr = marshal_read_string(pFile, &nOffset);

        if (pStr == NULL) {
            return 0;
        }

        if (bTopLevel && (pConsts != NULL)) {
            cdvec_push(pConsts, pStr);
        } else {
            cd_free(pStr);
        }

        *pnOffset = nOffset;

        return 1;
    }

    if ((nActual == PYC_T_TUPLE) || (nActual == PYC_T_SMALL_TUPLE)) {
        cd_i32 nCount = 0;
        cd_i32 i = 0;

        nOffset += 1;

        if (nActual == PYC_T_SMALL_TUPLE) {
            if ((nOffset + 1) > nSize) {
                return 0;
            }

            nCount = (cd_i32)xb_u8(pFile, nOffset);
            nOffset += 1;
        } else {
            if ((nOffset + 4) > nSize) {
                return 0;
            }

            nCount = (cd_i32)xb_u32(pFile, nOffset, 0);
            nOffset += 4;
        }

        if ((nCount < 0) || (nCount > 10000)) {
            return 0;
        }

        for (i = 0; i < nCount; i++) {
            /* Nested elements are not top-level constants. */
            if (!marshal_read_object(pFile, &nOffset, pConsts, 0, nDepth + 1)) {
                return 0;
            }
        }

        *pnOffset = nOffset;

        return 1;
    }

    /* TYPE_CODE, TYPE_REF and anything else: getCodeObject stops here. */
    return 0;
}

/* Fills pPyc->vecConsts with the module code object's top-level string
 * constants, following XPYC::getCodeObject as far as the constants tuple. */
static void collect_consts(XBFile *pFile, XPyc *pPyc)
{
    int nMajor = 0;
    int nMinor = 0;
    int bHashBased = 0;
    cd_i64 nSize = pFile->nSize;
    cd_i64 nCodeOffset = 4; /* magic + marker */
    cd_i64 nOffset = 0;
    cd_i32 nCount = 0;
    cd_i32 i = 0;
    cd_u8 nActual = 0;

    parse_version_numbers(pPyc->sVersion, &nMajor, &nMinor);

    if ((nMajor > 3) || ((nMajor == 3) && (nMinor >= 7))) {
        if ((nCodeOffset + 4) <= nSize) {
            bHashBased = (xb_u32(pFile, nCodeOffset, 0) & 0x00000001) ? 1 : 0;
        }

        nCodeOffset += 4;                    /* flags */
        nCodeOffset += bHashBased ? (8 + 4) : (4 + 4);
    } else {
        nCodeOffset += 8;                    /* timestamp + source size */
    }

    if (nCodeOffset >= nSize) {
        return;
    }

    /* The module object must be a code object (0x63 with the ref bit masked). */
    if ((xb_u8(pFile, nCodeOffset) & 0x7F) != PYC_T_CODE) {
        return;
    }

    nOffset = nCodeOffset + 1;

    if ((nMajor >= 3) && (nMinor >= 11)) {
        /* argcount, posonly, kwonly, stacksize, flags: five raw int32. */
        nOffset += 5 * 4;
    } else {
        /* argcount [, posonly (3.8+)] [, kwonly (3.0+)], nlocals, stacksize,
         * flags: each a marshalled int (a type byte + a 4- or 8-byte body). */
        int nInts = 1;                        /* argcount */

        if ((nMajor >= 3) && (nMinor >= 8)) {
            nInts++;                          /* posonlyargcount */
        }
        if (nMajor >= 3) {
            nInts++;                          /* kwonlyargcount */
        }
        nInts += 3;                           /* nlocals, stacksize, flags */

        for (i = 0; i < nInts; i++) {
            cd_u8 nType = 0;

            if ((nOffset + 1) > nSize) {
                return;
            }

            nType = xb_u8(pFile, nOffset);
            nOffset += 1;

            if (nType == PYC_T_INT64) {
                nOffset += 8;
            } else if (nType == PYC_T_INT) {
                nOffset += 4;
            }
            /* TYPE_NONE/TYPE_TRUE/TYPE_FALSE carry no body. */
        }
    }

    /* co_code: a string/bytes object. */
    if ((nOffset + 1) <= nSize) {
        cd_u8 nCodeType = (cd_u8)(xb_u8(pFile, nOffset) & 0x7F);

        nOffset += 1;

        if ((nCodeType == PYC_T_STRING) || (nCodeType == PYC_T_SHORT_ASCII) || (nCodeType == PYC_T_ASCII)) {
            cd_i32 nCodeLength = 0;

            if (nCodeType == PYC_T_SHORT_ASCII) {
                nCodeLength = (cd_i32)xb_u8(pFile, nOffset);
                nOffset += 1;
            } else {
                nCodeLength = (cd_i32)xb_u32(pFile, nOffset, 0);
                nOffset += 4;
            }

            if ((nCodeLength >= 0) && (nCodeLength <= 1024 * 1024) && ((nOffset + nCodeLength) <= nSize)) {
                nOffset += nCodeLength;
            }
        }
    }

    /* co_consts: a tuple whose top-level string elements feed isConstPresent. */
    if ((nOffset + 1) > nSize) {
        return;
    }

    nActual = (cd_u8)(xb_u8(pFile, nOffset) & 0x7F);
    nOffset += 1;

    if ((nActual != PYC_T_TUPLE) && (nActual != PYC_T_SMALL_TUPLE)) {
        return;
    }

    if (nActual == PYC_T_SMALL_TUPLE) {
        if ((nOffset + 1) > nSize) {
            return;
        }

        nCount = (cd_i32)xb_u8(pFile, nOffset);
        nOffset += 1;
    } else {
        if ((nOffset + 4) > nSize) {
            return;
        }

        nCount = (cd_i32)xb_u32(pFile, nOffset, 0);
        nOffset += 4;
    }

    if ((nCount < 0) || (nCount > 10000)) {
        return;
    }

    for (i = 0; i < nCount; i++) {
        if (!marshal_read_object(pFile, &nOffset, &pPyc->vecConsts, 1, 0)) {
            break;
        }
    }
}

int xpyc_parse(XBFile *pFile, XPyc *pPyc)
{
    x_memset(pPyc, 0, sizeof(*pPyc));
    pPyc->sVersion[0] = 0;
    cdvec_init(&pPyc->vecConsts);

    /* Detection already required size >= 12 and the "\r\n" marker at offset 2;
     * record the magic and map it to a release string (empty if unlisted). */
    pPyc->nMagic = xb_u16(pFile, 0, 0);
    x_strncpy(pPyc->sVersion, magic_to_version(pPyc->nMagic), sizeof(pPyc->sVersion) - 1);
    pPyc->sVersion[sizeof(pPyc->sVersion) - 1] = 0;

    collect_consts(pFile, pPyc);

    pPyc->bValid = 1;

    return 1;
}

void xpyc_free(XPyc *pPyc)
{
    size_t i = 0;

    for (i = 0; i < pPyc->vecConsts.nSize; i++) {
        cd_free(pPyc->vecConsts.ppData[i]);
    }

    cdvec_free(&pPyc->vecConsts);
    pPyc->bValid = 0;
}

int xpyc_const_present(XPyc *pPyc, const char *pValue)
{
    size_t i = 0;

    if ((pPyc == NULL) || (pValue == NULL)) {
        return 0;
    }

    for (i = 0; i < pPyc->vecConsts.nSize; i++) {
        const char *pConst = (const char *)pPyc->vecConsts.ppData[i];

        if ((pConst != NULL) && (x_strcmp(pConst, pValue) == 0)) {
            return 1;
        }
    }

    return 0;
}
