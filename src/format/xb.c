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

#include "xb.h"
#include "../core/cd_fs.h"


/* Display names, matching XBinary::fileTypeIdToString so that the printed
 * format line and the --showdatabase listing look exactly like diec.      */
static const char *g_pFileTypeNames[XFT_COUNT] = {
    "Unknown",  "Binary",  "COM",      "MSDOS",     "NE",         "LE",        "LX",         "PE",
    "PE32",     "PE64",    "ELF",      "ELF32",     "ELF64",      "Mach-O",    "Mach-O32",   "Mach-O64",
    "ZIP",      "JAR",     "APK",      "IPA",       "DEX",        "NPM",       "Mach-O FAT", "Archive",
    "PDF",      "CFBF",    "Image",    "JPEG",      "PNG",        "RAR",       "ISO 9660",   "Amiga Hunk",
    "Atari ST", "Java Class", "Python Bytecode",    "DOS/16M",    "DOS/4G",     ".NET"};

const char *xft_to_string(XFileType type)
{
    /* One unsigned comparison covers both ends: XFileType has no negative
     * enumerator, so a compiler that gives it an unsigned representation makes
     * "type >= 0" vacuous, and a negative value on a compiler that keeps it
     * signed wraps to a value above XFT_COUNT here.                          */
    if ((cd_u32)type < (cd_u32)XFT_COUNT) {
        return g_pFileTypeNames[type];
    }

    return "Unknown";
}

/* ------------------------------------------------------------------ file  */

int xb_open(XBFile *pFile, const char *pFileName)
{
    cd_i64 nSize = 0;
    char *pData = cd_read_file(pFileName, &nSize);

    x_memset(pFile, 0, sizeof(*pFile));

    if (pData == NULL) {
        return 0;
    }

    pFile->pData = (unsigned char *)pData;
    pFile->nSize = nSize;
    pFile->pFileName = cd_strdup(pFileName);

    return 1;
}

void xb_close(XBFile *pFile)
{
    cd_free(pFile->pData);
    cd_free(pFile->pFileName);
    x_memset(pFile, 0, sizeof(*pFile));
}

/* Overflow-safe range check. Signature scripts can pass arbitrary numbers as
 * offsets (a few database rules genuinely do), so the arithmetic must never
 * wrap around.                                                             */
static int xb_check(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    if ((nOffset < 0) || (nSize < 0) || (nOffset > pFile->nSize)) {
        return 0;
    }

    return (nSize <= pFile->nSize - nOffset) ? 1 : 0;
}

/* Clamps [nOffset, nOffset + *pnSize) to the file. Returns 0 when the range
 * lies completely outside.                                                 */
static int xb_clamp(XBFile *pFile, cd_i64 nOffset, cd_i64 *pnSize)
{
    if ((nOffset < 0) || (nOffset >= pFile->nSize)) {
        return 0;
    }

    if ((*pnSize < 0) || (*pnSize > pFile->nSize - nOffset)) {
        *pnSize = pFile->nSize - nOffset;
    }

    return 1;
}

cd_u8 xb_u8(XBFile *pFile, cd_i64 nOffset)
{
    if (!xb_check(pFile, nOffset, 1)) {
        return 0;
    }

    return pFile->pData[nOffset];
}

cd_i8 xb_i8(XBFile *pFile, cd_i64 nOffset)
{
    return (cd_i8)xb_u8(pFile, nOffset);
}

cd_u16 xb_u16(XBFile *pFile, cd_i64 nOffset, int bBigEndian)
{
    if (!xb_check(pFile, nOffset, 2)) {
        return 0;
    }

    if (bBigEndian) {
        return (cd_u16)((pFile->pData[nOffset] << 8) | pFile->pData[nOffset + 1]);
    }

    return (cd_u16)(pFile->pData[nOffset] | (pFile->pData[nOffset + 1] << 8));
}

cd_i16 xb_i16(XBFile *pFile, cd_i64 nOffset, int bBigEndian)
{
    return (cd_i16)xb_u16(pFile, nOffset, bBigEndian);
}

cd_u32 xb_u24(XBFile *pFile, cd_i64 nOffset, int bBigEndian)
{
    if (!xb_check(pFile, nOffset, 3)) {
        return 0;
    }

    if (bBigEndian) {
        return ((cd_u32)pFile->pData[nOffset] << 16) | ((cd_u32)pFile->pData[nOffset + 1] << 8) | (cd_u32)pFile->pData[nOffset + 2];
    }

    return (cd_u32)pFile->pData[nOffset] | ((cd_u32)pFile->pData[nOffset + 1] << 8) | ((cd_u32)pFile->pData[nOffset + 2] << 16);
}

cd_u32 xb_u32(XBFile *pFile, cd_i64 nOffset, int bBigEndian)
{
    if (!xb_check(pFile, nOffset, 4)) {
        return 0;
    }

    if (bBigEndian) {
        return ((cd_u32)pFile->pData[nOffset] << 24) | ((cd_u32)pFile->pData[nOffset + 1] << 16) | ((cd_u32)pFile->pData[nOffset + 2] << 8) |
               (cd_u32)pFile->pData[nOffset + 3];
    }

    return (cd_u32)pFile->pData[nOffset] | ((cd_u32)pFile->pData[nOffset + 1] << 8) | ((cd_u32)pFile->pData[nOffset + 2] << 16) |
           ((cd_u32)pFile->pData[nOffset + 3] << 24);
}

cd_i32 xb_i32(XBFile *pFile, cd_i64 nOffset, int bBigEndian)
{
    return (cd_i32)xb_u32(pFile, nOffset, bBigEndian);
}

cd_u64 xb_u64(XBFile *pFile, cd_i64 nOffset, int bBigEndian)
{
    if (!xb_check(pFile, nOffset, 8)) {
        return 0;
    }

    if (bBigEndian) {
        return ((cd_u64)xb_u32(pFile, nOffset, 1) << 32) | (cd_u64)xb_u32(pFile, nOffset + 4, 1);
    }

    return (cd_u64)xb_u32(pFile, nOffset, 0) | ((cd_u64)xb_u32(pFile, nOffset + 4, 0) << 32);
}

cd_i64 xb_i64(XBFile *pFile, cd_i64 nOffset, int bBigEndian)
{
    return (cd_i64)xb_u64(pFile, nOffset, bBigEndian);
}

float xb_f32(XBFile *pFile, cd_i64 nOffset, int bBigEndian)
{
    cd_u32 nValue = xb_u32(pFile, nOffset, bBigEndian);
    float fResult = 0;

    x_memcpy(&fResult, &nValue, 4);

    return fResult;
}

double xb_f64(XBFile *pFile, cd_i64 nOffset, int bBigEndian)
{
    cd_u64 nValue = xb_u64(pFile, nOffset, bBigEndian);
    double fResult = 0;

    x_memcpy(&fResult, &nValue, 8);

    return fResult;
}

float xb_f16(XBFile *pFile, cd_i64 nOffset, int bBigEndian)
{
    /* Transcribed from XBinary::read_float16: expand a 16-bit half (1 sign,
     * 5 exponent, 10 fraction) into a 32-bit float bit pattern. */
    cd_u16 nHalf = xb_u16(pFile, nOffset, bBigEndian);
    cd_u32 nSign = (cd_u32)(nHalf >> 15);
    cd_u32 nExponent = (cd_u32)((nHalf >> 10) & 0x1F);
    cd_u32 nFraction = (cd_u32)(nHalf & 0x3FF);
    cd_u32 nValue = 0;
    float fResult = 0;

    if (nExponent == 0) {
        if (nFraction == 0) {
            nValue = (nSign << 31);
        } else {
            nExponent = 127 - 14;

            while ((nFraction & (1 << 10)) == 0) {
                nExponent--;
                nFraction <<= 1;
            }

            nFraction &= 0x3FF;
            nValue = (nSign << 31) | (nExponent << 23) | (nFraction << 13);
        }
    } else if (nExponent == 0x1F) {
        nValue = (nSign << 31) | ((cd_u32)0xFF << 23) | (nFraction << 13);
    } else {
        nValue = (nSign << 31) | ((nExponent + (127 - 15)) << 23) | (nFraction << 13);
    }

    x_memcpy(&fResult, &nValue, 4);

    return fResult;
}

char *xb_ansi_string(XBFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize)
{
    CDBuf buf;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    if (nMaxSize <= 0) {
        nMaxSize = 0x10000;
    }

    for (i = 0; i < nMaxSize; i++) {
        cd_u8 nChar = 0;

        if (!xb_check(pFile, nOffset + i, 1)) {
            break;
        }

        nChar = pFile->pData[nOffset + i];

        if (nChar == 0) {
            break;
        }

        cdbuf_append_ch(&buf, (char)nChar);
    }

    return cdbuf_detach(&buf, NULL);
}

static void append_utf8(CDBuf *pBuf, unsigned int nCode)
{
    if (nCode < 0x80) {
        cdbuf_append_ch(pBuf, (char)nCode);
    } else if (nCode < 0x800) {
        cdbuf_append_ch(pBuf, (char)(0xC0 | (nCode >> 6)));
        cdbuf_append_ch(pBuf, (char)(0x80 | (nCode & 0x3F)));
    } else {
        cdbuf_append_ch(pBuf, (char)(0xE0 | (nCode >> 12)));
        cdbuf_append_ch(pBuf, (char)(0x80 | ((nCode >> 6) & 0x3F)));
        cdbuf_append_ch(pBuf, (char)(0x80 | (nCode & 0x3F)));
    }
}

char *xb_unicode_string_n(XBFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize, int bBigEndian, cd_i64 *pnUnits)
{
    CDBuf buf;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    if (nMaxSize <= 0) {
        nMaxSize = 0x10000;
    }

    for (i = 0; i < nMaxSize; i++) {
        cd_u16 nChar = 0;

        if (!xb_check(pFile, nOffset + i * 2, 2)) {
            break;
        }

        nChar = xb_u16(pFile, nOffset + i * 2, bBigEndian);

        if (nChar == 0) {
            break;
        }

        append_utf8(&buf, nChar);
    }

    if (pnUnits != NULL) {
        *pnUnits = i;
    }

    return cdbuf_detach(&buf, NULL);
}

char *xb_unicode_string(XBFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize, int bBigEndian)
{
    return xb_unicode_string_n(pFile, nOffset, nMaxSize, bBigEndian, NULL);
}

char *xb_utf8_string(XBFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize)
{
    return xb_ansi_string(pFile, nOffset, nMaxSize);
}

char *xb_signature_hex(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    static const char *pDigits = "0123456789ABCDEF";
    CDBuf buf;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    if (!xb_clamp(pFile, nOffset, &nSize)) {
        return cdbuf_detach(&buf, NULL);
    }

    for (i = 0; i < nSize; i++) {
        unsigned char nByte = pFile->pData[nOffset + i];

        cdbuf_append_ch(&buf, pDigits[nByte >> 4]);
        cdbuf_append_ch(&buf, pDigits[nByte & 0x0F]);
    }

    return cdbuf_detach(&buf, NULL);
}

char *xb_ucsd_string(XBFile *pFile, cd_i64 nOffset)
{
    CDBuf buf;
    cd_i64 nSize = 0;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    nSize = (cd_i64)xb_u8(pFile, nOffset);

    if (nSize > 0x10000) {
        nSize = 0x10000;
    }

    /* read_uint8 yields 0 past EOF, so the payload is always nSize characters
     * long; every embedded 0x00 (real or out of range) becomes a space. */
    for (i = 0; i < nSize; i++) {
        cd_u8 nByte = xb_u8(pFile, nOffset + 1 + i);

        if (nByte == 0) {
            nByte = 0x20;
        }

        cdbuf_append_ch(&buf, (char)nByte);
    }

    return cdbuf_detach(&buf, NULL);
}

char *xb_uuid(XBFile *pFile, cd_i64 nOffset)
{
    /* XBinary::read_UUID with the default little-endian flag: the read and the
     * hex formatting swap by the same flag, so on a little-endian target the
     * two swaps cancel and the first four fields are the little-endian values;
     * the last field is the six bytes in file order. Lower case, hyphenated. */
    static const char *pDigits = "0123456789abcdef";
    CDBuf buf;
    cd_u32 nA = xb_u32(pFile, nOffset + 0, 0);
    cd_u32 nB = xb_u16(pFile, nOffset + 4, 0);
    cd_u32 nC = xb_u16(pFile, nOffset + 6, 0);
    cd_u32 nD = xb_u16(pFile, nOffset + 8, 0);
    int nShift = 0;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    for (nShift = 28; nShift >= 0; nShift -= 4) {
        cdbuf_append_ch(&buf, pDigits[(nA >> nShift) & 0xF]);
    }

    cdbuf_append_ch(&buf, '-');

    for (nShift = 12; nShift >= 0; nShift -= 4) {
        cdbuf_append_ch(&buf, pDigits[(nB >> nShift) & 0xF]);
    }

    cdbuf_append_ch(&buf, '-');

    for (nShift = 12; nShift >= 0; nShift -= 4) {
        cdbuf_append_ch(&buf, pDigits[(nC >> nShift) & 0xF]);
    }

    cdbuf_append_ch(&buf, '-');

    for (nShift = 12; nShift >= 0; nShift -= 4) {
        cdbuf_append_ch(&buf, pDigits[(nD >> nShift) & 0xF]);
    }

    cdbuf_append_ch(&buf, '-');

    for (i = 0; i < 6; i++) {
        cd_u8 nByte = xb_u8(pFile, nOffset + 10 + i);

        cdbuf_append_ch(&buf, pDigits[nByte >> 4]);
        cdbuf_append_ch(&buf, pDigits[nByte & 0x0F]);
    }

    return cdbuf_detach(&buf, NULL);
}

/* ------------------------------------------------------------- searching  */

cd_i64 xb_find_bytes(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, const unsigned char *pNeedle, cd_i64 nNeedleSize)
{
    cd_i64 i = 0;

    if (nNeedleSize <= 0) {
        return -1;
    }

    if (!xb_clamp(pFile, nOffset, &nSize)) {
        return -1;
    }

    if (nNeedleSize > nSize) {
        return -1;
    }

    for (i = 0; i <= nSize - nNeedleSize; i++) {
        if (pFile->pData[nOffset + i] == pNeedle[0]) {
            if (x_memcmp(pFile->pData + nOffset + i, pNeedle, (size_t)nNeedleSize) == 0) {
                return nOffset + i;
            }
        }
    }

    return -1;
}

cd_i64 xb_find_ansi_string(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, const char *pString)
{
    return xb_find_bytes(pFile, nOffset, nSize, (const unsigned char *)pString, (cd_i64)x_strlen(pString));
}

cd_i64 xb_find_unicode_string(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, const char *pString, int bBigEndian)
{
    size_t nLength = x_strlen(pString);
    unsigned char *pNeedle = (unsigned char *)cd_malloc(nLength * 2 + 2);
    size_t i = 0;
    cd_i64 nResult = 0;

    for (i = 0; i < nLength; i++) {
        if (bBigEndian) {
            pNeedle[i * 2] = 0;
            pNeedle[i * 2 + 1] = (unsigned char)pString[i];
        } else {
            pNeedle[i * 2] = (unsigned char)pString[i];
            pNeedle[i * 2 + 1] = 0;
        }
    }

    nResult = xb_find_bytes(pFile, nOffset, nSize, pNeedle, (cd_i64)(nLength * 2));
    cd_free(pNeedle);

    return nResult;
}

cd_i64 xb_find_u8(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u8 nValue)
{
    return xb_find_bytes(pFile, nOffset, nSize, &nValue, 1);
}

cd_i64 xb_find_u16(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u16 nValue)
{
    unsigned char sBuf[2];

    sBuf[0] = (unsigned char)(nValue & 0xFF);
    sBuf[1] = (unsigned char)(nValue >> 8);

    return xb_find_bytes(pFile, nOffset, nSize, sBuf, 2);
}

cd_i64 xb_find_u32(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u32 nValue)
{
    unsigned char sBuf[4];

    sBuf[0] = (unsigned char)(nValue & 0xFF);
    sBuf[1] = (unsigned char)((nValue >> 8) & 0xFF);
    sBuf[2] = (unsigned char)((nValue >> 16) & 0xFF);
    sBuf[3] = (unsigned char)((nValue >> 24) & 0xFF);

    return xb_find_bytes(pFile, nOffset, nSize, sBuf, 4);
}

/* ------------------------------------------------------------- checksums  */

double xb_entropy(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    cd_i64 pCount[256];
    cd_i64 i = 0;
    double nResult = 0;

    x_memset(pCount, 0, sizeof(pCount));

    if ((!xb_clamp(pFile, nOffset, &nSize)) || (nSize <= 0)) {
        return 0;
    }

    for (i = 0; i < nSize; i++) {
        pCount[pFile->pData[nOffset + i]]++;
    }

    for (i = 0; i < 256; i++) {
        if (pCount[i]) {
            double p = (double)pCount[i] / (double)nSize;

            nResult -= p * (x_log(p) / x_log(2.0));
        }
    }

    return nResult;
}

int xb_is_zero_filled(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    cd_i64 i = 0;

    if ((nSize <= 0) || (!xb_check(pFile, nOffset, nSize))) {
        return 0;
    }

    for (i = 0; i < nSize; i++) {
        if (pFile->pData[nOffset + i]) {
            return 0;
        }
    }

    return 1;
}

/* --- MD5 (RFC 1321), compact public-domain style implementation --------- */

typedef struct {
    cd_u32 nState[4];
    cd_u64 nCount;
    unsigned char sBuffer[64];
} MD5Ctx;

static cd_u32 md5_rol(cd_u32 nValue, int nShift)
{
    return (nValue << nShift) | (nValue >> (32 - nShift));
}

static void md5_transform(cd_u32 *pState, const unsigned char *pBlock)
{
    static const cd_u32 pK[64] = {
        0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
        0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
        0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
        0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu, 0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
        0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
        0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
        0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
        0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u};
    static const int pS[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                               4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    cd_u32 pM[16];
    cd_u32 a = pState[0];
    cd_u32 b = pState[1];
    cd_u32 c = pState[2];
    cd_u32 d = pState[3];
    int i = 0;

    for (i = 0; i < 16; i++) {
        pM[i] = (cd_u32)pBlock[i * 4] | ((cd_u32)pBlock[i * 4 + 1] << 8) | ((cd_u32)pBlock[i * 4 + 2] << 16) | ((cd_u32)pBlock[i * 4 + 3] << 24);
    }

    for (i = 0; i < 64; i++) {
        cd_u32 f = 0;
        int g = 0;
        cd_u32 nTemp = 0;

        if (i < 16) {
            f = (b & c) | ((~b) & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | ((~d) & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | (~d));
            g = (7 * i) % 16;
        }

        nTemp = d;
        d = c;
        c = b;
        b = b + md5_rol(a + f + pK[i] + pM[g], pS[i]);
        a = nTemp;
    }

    pState[0] += a;
    pState[1] += b;
    pState[2] += c;
    pState[3] += d;
}

char *xb_md5(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    static const char *pDigits = "0123456789ABCDEF";
    MD5Ctx ctx;
    unsigned char sDigest[16];
    char *pResult = (char *)cd_malloc(33);
    cd_i64 i = 0;
    cd_u64 nBits = 0;
    size_t nPad = 0;
    unsigned char sTail[128];
    size_t nTail = 0;

    if (!xb_clamp(pFile, nOffset, &nSize)) {
        nOffset = 0;
        nSize = 0;
    }

    ctx.nState[0] = 0x67452301u;
    ctx.nState[1] = 0xefcdab89u;
    ctx.nState[2] = 0x98badcfeu;
    ctx.nState[3] = 0x10325476u;

    for (i = 0; i + 64 <= nSize; i += 64) {
        md5_transform(ctx.nState, pFile->pData + nOffset + i);
    }

    nTail = (size_t)(nSize - i);

    if (nTail) {
        x_memcpy(sTail, pFile->pData + nOffset + i, nTail);
    }

    nBits = (cd_u64)nSize * 8;
    sTail[nTail++] = 0x80;
    nPad = ((nTail % 64) <= 56) ? (56 - (nTail % 64)) : (120 - (nTail % 64));
    x_memset(sTail + nTail, 0, nPad);
    nTail += nPad;

    for (i = 0; i < 8; i++) {
        sTail[nTail++] = (unsigned char)((nBits >> (i * 8)) & 0xFF);
    }

    for (i = 0; (size_t)i < nTail; i += 64) {
        md5_transform(ctx.nState, sTail + i);
    }

    for (i = 0; i < 4; i++) {
        sDigest[i * 4] = (unsigned char)(ctx.nState[i] & 0xFF);
        sDigest[i * 4 + 1] = (unsigned char)((ctx.nState[i] >> 8) & 0xFF);
        sDigest[i * 4 + 2] = (unsigned char)((ctx.nState[i] >> 16) & 0xFF);
        sDigest[i * 4 + 3] = (unsigned char)((ctx.nState[i] >> 24) & 0xFF);
    }

    for (i = 0; i < 16; i++) {
        pResult[i * 2] = pDigits[sDigest[i] >> 4];
        pResult[i * 2 + 1] = pDigits[sDigest[i] & 0x0F];
    }

    pResult[32] = 0;

    return pResult;
}

/* The reflected CRC-32 table for polynomial 0xEDB88320. Built at compile time:
 * a lazily filled static would be a data race when two host threads enter
 * through the shared library at the same time. */
static const cd_u32 g_crc32Table[256] = {
    0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu, 0x076DC419u, 0x706AF48Fu,
    0xE963A535u, 0x9E6495A3u, 0x0EDB8832u, 0x79DCB8A4u, 0xE0D5E91Eu, 0x97D2D988u,
    0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D07u, 0x90BF1D91u, 0x1DB71064u, 0x6AB020F2u,
    0xF3B97148u, 0x84BE41DEu, 0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u,
    0x136C9856u, 0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu, 0x14015C4Fu, 0x63066CD9u,
    0xFA0F3D63u, 0x8D080DF5u, 0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u, 0xA2677172u,
    0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu, 0x35B5A8FAu, 0x42B2986Cu,
    0xDBBBC9D6u, 0xACBCF940u, 0x32D86CE3u, 0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u,
    0x26D930ACu, 0x51DE003Au, 0xC8D75180u, 0xBFD06116u, 0x21B4F4B5u, 0x56B3C423u,
    0xCFBA9599u, 0xB8BDA50Fu, 0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u,
    0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du, 0x76DC4190u, 0x01DB7106u,
    0x98D220BCu, 0xEFD5102Au, 0x71B18589u, 0x06B6B51Fu, 0x9FBFE4A5u, 0xE8B8D433u,
    0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu, 0xE10E9818u, 0x7F6A0DBBu, 0x086D3D2Du,
    0x91646C97u, 0xE6635C01u, 0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu,
    0x6C0695EDu, 0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u, 0x65B0D9C6u, 0x12B7E950u,
    0x8BBEB8EAu, 0xFCB9887Cu, 0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u, 0xFBD44C65u,
    0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u, 0x4ADFA541u, 0x3DD895D7u,
    0xA4D1C46Du, 0xD3D6F4FBu, 0x4369E96Au, 0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u,
    0x44042D73u, 0x33031DE5u, 0xAA0A4C5Fu, 0xDD0D7CC9u, 0x5005713Cu, 0x270241AAu,
    0xBE0B1010u, 0xC90C2086u, 0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
    0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u, 0x59B33D17u, 0x2EB40D81u,
    0xB7BD5C3Bu, 0xC0BA6CADu, 0xEDB88320u, 0x9ABFB3B6u, 0x03B6E20Cu, 0x74B1D29Au,
    0xEAD54739u, 0x9DD277AFu, 0x04DB2615u, 0x73DC1683u, 0xE3630B12u, 0x94643B84u,
    0x0D6D6A3Eu, 0x7A6A5AA8u, 0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u,
    0xF00F9344u, 0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu, 0xF762575Du, 0x806567CBu,
    0x196C3671u, 0x6E6B06E7u, 0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au, 0x67DD4ACCu,
    0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u, 0xD6D6A3E8u, 0xA1D1937Eu,
    0x38D8C2C4u, 0x4FDFF252u, 0xD1BB67F1u, 0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu,
    0xD80D2BDAu, 0xAF0A1B4Cu, 0x36034AF6u, 0x41047A60u, 0xDF60EFC3u, 0xA867DF55u,
    0x316E8EEFu, 0x4669BE79u, 0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u,
    0xCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu, 0xC5BA3BBEu, 0xB2BD0B28u,
    0x2BB45A92u, 0x5CB36A04u, 0xC2D7FFA7u, 0xB5D0CF31u, 0x2CD99E8Bu, 0x5BDEAE1Du,
    0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu, 0x026D930Au, 0x9C0906A9u, 0xEB0E363Fu,
    0x72076785u, 0x05005713u, 0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u,
    0x92D28E9Bu, 0xE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u, 0x86D3D2D4u, 0xF1D4E242u,
    0x68DDB3F8u, 0x1FDA836Eu, 0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u, 0x18B74777u,
    0x88085AE6u, 0xFF0F6A70u, 0x66063BCAu, 0x11010B5Cu, 0x8F659EFFu, 0xF862AE69u,
    0x616BFFD3u, 0x166CCF45u, 0xA00AE278u, 0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u,
    0xA7672661u, 0xD06016F7u, 0x4969474Du, 0x3E6E77DBu, 0xAED16A4Au, 0xD9D65ADCu,
    0x40DF0B66u, 0x37D83BF0u, 0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
    0xBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u, 0xBAD03605u, 0xCDD70693u,
    0x54DE5729u, 0x23D967BFu, 0xB3667A2Eu, 0xC4614AB8u, 0x5D681B02u, 0x2A6F2B94u,
    0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu, 0x2D02EF8Du};

cd_u32 xb_crc32(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u32 nInit)
{
    cd_u32 nCrc = ~nInit;
    cd_i64 i = 0;

    if (!xb_clamp(pFile, nOffset, &nSize)) {
        return ~nCrc;
    }

    for (i = 0; i < nSize; i++) {
        nCrc = g_crc32Table[(nCrc ^ pFile->pData[nOffset + i]) & 0xFF] ^ (nCrc >> 8);
    }

    return ~nCrc;
}

cd_u32 xb_adler32(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize)
{
    cd_u32 a = 1;
    cd_u32 b = 0;
    cd_i64 i = 0;

    if (!xb_clamp(pFile, nOffset, &nSize)) {
        return (b << 16) | a;
    }

    for (i = 0; i < nSize; i++) {
        a = (a + pFile->pData[nOffset + i]) % 65521;
        b = (b + a) % 65521;
    }

    return (b << 16) | a;
}

cd_u16 xb_crc16(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u16 nInit)
{
    cd_u16 nCrc = nInit;
    cd_i64 i = 0;
    int j = 0;

    if (!xb_clamp(pFile, nOffset, &nSize)) {
        return nCrc;
    }

    for (i = 0; i < nSize; i++) {
        nCrc ^= pFile->pData[nOffset + i];

        for (j = 0; j < 8; j++) {
            nCrc = (nCrc & 1) ? (cd_u16)((nCrc >> 1) ^ 0xA001) : (cd_u16)(nCrc >> 1);
        }
    }

    return nCrc;
}

cd_u32 xb_string_custom_crc32(const char *pString)
{
    cd_u32 nResult = 0;
    size_t nSize = pString ? x_strlen(pString) : 0;
    size_t i = 0;
    int k = 0;

    for (i = 0; i < nSize; i++) {
        nResult ^= (unsigned char)pString[i];

        for (k = 0; k < 8; k++) {
            nResult = (nResult & 1) ? ((nResult >> 1) ^ 0x82f63b78u) : (nResult >> 1);
        }
    }

    return ~nResult;
}

/* --------------------------------------------------------------- mem map  */

void xbmap_init(XBMemoryMap *pMap)
{
    x_memset(pMap, 0, sizeof(*pMap));
}

void xbmap_free(XBMemoryMap *pMap)
{
    cd_free(pMap->pRecords);
    x_memset(pMap, 0, sizeof(*pMap));
}

void xbmap_add(XBMemoryMap *pMap, cd_i64 nOffset, cd_i64 nSize, cd_u64 nAddress, cd_u64 nVirtualSize, XFilePart filePart, const char *pName)
{
    XBRegion *pRegion = NULL;

    if (pMap->nCount + 1 > pMap->nCapacity) {
        pMap->nCapacity = pMap->nCapacity ? (pMap->nCapacity * 2) : 16;
        pMap->pRecords = (XBRegion *)cd_realloc(pMap->pRecords, (size_t)pMap->nCapacity * sizeof(XBRegion));
    }

    pRegion = &pMap->pRecords[pMap->nCount++];
    x_memset(pRegion, 0, sizeof(*pRegion));
    pRegion->nOffset = nOffset;
    pRegion->nSize = nSize;
    pRegion->nAddress = nAddress;
    pRegion->nVirtualSize = nVirtualSize;
    pRegion->filePart = filePart;

    if (pName) {
        x_strncpy(pRegion->sName, pName, sizeof(pRegion->sName) - 1);
    }
}

cd_i64 xbmap_address_to_offset(XBMemoryMap *pMap, cd_u64 nAddress)
{
    int i = 0;

    for (i = 0; i < pMap->nCount; i++) {
        XBRegion *pRegion = &pMap->pRecords[i];

        if ((pRegion->nVirtualSize == 0) || (pRegion->nOffset == -1)) {
            continue;
        }

        if ((nAddress >= pRegion->nAddress) && (nAddress < pRegion->nAddress + pRegion->nVirtualSize)) {
            cd_u64 nDelta = nAddress - pRegion->nAddress;

            if ((cd_i64)nDelta >= pRegion->nSize) {
                return -1;
            }

            return pRegion->nOffset + (cd_i64)nDelta;
        }
    }

    return -1;
}

cd_i64 xbmap_offset_to_address(XBMemoryMap *pMap, cd_i64 nOffset)
{
    int i = 0;

    for (i = 0; i < pMap->nCount; i++) {
        XBRegion *pRegion = &pMap->pRecords[i];

        if ((pRegion->nOffset == -1) || (pRegion->nSize == 0)) {
            continue;
        }

        if ((nOffset >= pRegion->nOffset) && (nOffset < pRegion->nOffset + pRegion->nSize)) {
            if (pRegion->nVirtualSize == 0) {
                return -1;
            }

            return (cd_i64)(pRegion->nAddress + (cd_u64)(nOffset - pRegion->nOffset));
        }
    }

    return -1;
}

cd_i64 xbmap_raw_size(XBMemoryMap *pMap)
{
    cd_i64 nResult = 0;
    cd_i64 nOverlayOffset = -1;
    int i = 0;

    for (i = 0; i < pMap->nCount; i++) {
        XBRegion *pRegion = &pMap->pRecords[i];

        if ((pRegion->nOffset != -1) && (pRegion->filePart != XPART_OVERLAY)) {
            cd_i64 nEnd = pRegion->nOffset + pRegion->nSize;

            if (nEnd > nResult) {
                nResult = nEnd;
            }
        }

        if (pRegion->filePart == XPART_OVERLAY) {
            nOverlayOffset = pRegion->nOffset;
        }
    }

    if ((nOverlayOffset != -1) && (nOverlayOffset < nResult)) {
        nResult = nOverlayOffset;
    }

    return nResult;
}

/* ------------------------------------------------------------- signature  */

char *xb_convert_signature(const char *pSignature)
{
    CDBuf buf;
    size_t nSize = pSignature ? x_strlen(pSignature) : 0;
    size_t i = 0;
    int bHasQuote = 0;
    int bInsideString = 0;

    cdbuf_init(&buf);

    for (i = 0; i < nSize; i++) {
        if (pSignature[i] == '\'') {
            bHasQuote = 1;
            break;
        }
    }

    for (i = 0; i < nSize; i++) {
        char nChar = pSignature[i];

        if (bHasQuote && (nChar == '\'')) {
            bInsideString = !bInsideString;
        } else if (bInsideString) {
            static const char *pDigits = "0123456789abcdef";
            unsigned char nValue = (unsigned char)nChar;

            cdbuf_append_ch(&buf, pDigits[nValue >> 4]);
            cdbuf_append_ch(&buf, pDigits[nValue & 0x0F]);
        } else if (nChar != ' ') {
            if (nChar == '?') {
                cdbuf_append_ch(&buf, '.');
            } else if ((nChar >= 'A') && (nChar <= 'Z')) {
                cdbuf_append_ch(&buf, (char)(nChar - 'A' + 'a'));
            } else {
                cdbuf_append_ch(&buf, nChar);
            }
        }
    }

    return cdbuf_detach(&buf, NULL);
}

static XSigRecord *sig_add(XSignature *pSignature)
{
    XSigRecord *pRecord = NULL;

    pSignature->pRecords = (XSigRecord *)cd_realloc(pSignature->pRecords, (size_t)(pSignature->nCount + 1) * sizeof(XSigRecord));
    pRecord = &pSignature->pRecords[pSignature->nCount++];
    x_memset(pRecord, 0, sizeof(*pRecord));

    return pRecord;
}

static int hex_val(char nChar)
{
    if ((nChar >= '0') && (nChar <= '9')) {
        return nChar - '0';
    }

    if ((nChar >= 'a') && (nChar <= 'f')) {
        return nChar - 'a' + 10;
    }

    return -1;
}

static int sig_bytes(XSignature *pSignature, const char *pText, int nStart, XSigRecord **ppOut)
{
    int nCount = 0;
    int nSize = (int)x_strlen(pText);
    int i = 0;
    CDBuf buf;

    cdbuf_init(&buf);

    for (i = nStart; i < nSize; i++) {
        char nChar = pText[i];

        if (hex_val(nChar) >= 0) {
            nCount++;
            cdbuf_append_ch(&buf, nChar);
        } else if ((nChar == '.') || (nChar == '$') || (nChar == '#') || (nChar == '*') || (nChar == '!') || (nChar == '_') || (nChar == '%') || (nChar == '+')) {
            break;
        } else {
            pSignature->bValid = 0;
            break;
        }
    }

    if (nCount) {
        XSigRecord *pRecord = sig_add(pSignature);
        int j = 0;
        int nBytes = nCount / 2;

        pRecord->type = XSIG_BYTES;
        pRecord->pData = (unsigned char *)cd_malloc((size_t)(nBytes ? nBytes : 1));

        for (j = 0; j < nBytes; j++) {
            pRecord->pData[j] = (unsigned char)((hex_val(buf.pData[j * 2]) << 4) | hex_val(buf.pData[j * 2 + 1]));
        }

        pRecord->nDataSize = nBytes;
        pRecord->nWindowSize = nBytes;

        if (ppOut) {
            *ppOut = pRecord;
        }
    }

    cdbuf_free(&buf);

    return nCount;
}

static int sig_run(const char *pText, int nStart, char nChar)
{
    int nCount = 0;
    int nSize = (int)x_strlen(pText);
    int i = 0;

    for (i = nStart; i < nSize; i++) {
        if (pText[i] == nChar) {
            nCount++;
        } else {
            break;
        }
    }

    return nCount;
}

static int sig_run2(const char *pText, int nStart, const char *pPair)
{
    int nCount = 0;
    int nSize = (int)x_strlen(pText);
    int i = 0;

    for (i = nStart; i + 1 < nSize; i += 2) {
        if ((pText[i] == pPair[0]) && (pText[i + 1] == pPair[1])) {
            nCount += 2;
        } else {
            break;
        }
    }

    return nCount;
}

int xb_signature_parse(XSignature *pSignature, const char *pConverted)
{
    int nSize = (int)x_strlen(pConverted);
    int i = 0;

    x_memset(pSignature, 0, sizeof(*pSignature));
    pSignature->bValid = 1;

    while (i < nSize) {
        char nChar = pConverted[i];
        char nChar2 = ((i + 1) < nSize) ? pConverted[i + 1] : 0;

        if (nChar == '.') {
            int nCount = sig_run(pConverted, i, '.');
            XSigRecord *pRecord = sig_add(pSignature);

            pRecord->type = XSIG_SKIP;
            pRecord->nWindowSize = nCount / 2;
            i += nCount;
        } else if (nChar == '*') {
            int nCount = sig_run(pConverted, i, '*');
            XSigRecord *pRecord = sig_add(pSignature);

            pRecord->type = XSIG_NOTNULL;
            pRecord->nWindowSize = nCount / 2;
            i += nCount;
        } else if ((nChar == '%') && (nChar2 == '%')) {
            int nCount = sig_run2(pConverted, i, "%%");
            XSigRecord *pRecord = sig_add(pSignature);

            pRecord->type = XSIG_ANSI;
            pRecord->nWindowSize = nCount / 2;
            i += nCount;
        } else if ((nChar == '%') && (nChar2 == '&')) {
            int nCount = sig_run2(pConverted, i, "%&");
            XSigRecord *pRecord = sig_add(pSignature);

            pRecord->type = XSIG_ANSINUMBER;
            pRecord->nWindowSize = nCount / 2;
            i += nCount;
        } else if ((nChar == '!') && (nChar2 == '%')) {
            int nCount = sig_run2(pConverted, i, "!%");
            XSigRecord *pRecord = sig_add(pSignature);

            pRecord->type = XSIG_NOTANSI;
            pRecord->nWindowSize = nCount / 2;
            i += nCount;
        } else if ((nChar == '_') && (nChar2 == '%')) {
            int nCount = sig_run2(pConverted, i, "_%");
            XSigRecord *pRecord = sig_add(pSignature);

            pRecord->type = XSIG_NOTANSIANDNULL;
            pRecord->nWindowSize = nCount / 2;
            i += nCount;
        } else if (nChar == '+') {
            int nCount = sig_run(pConverted, i, '+');
            XSignature temp;
            int nTemp = 0;

            x_memset(&temp, 0, sizeof(temp));
            temp.bValid = 1;
            nTemp = sig_bytes(&temp, pConverted, i + nCount, NULL);

            if (temp.nCount) {
                XSigRecord *pRecord = sig_add(pSignature);

                pRecord->type = XSIG_FINDBYTES;
                pRecord->pData = temp.pRecords[0].pData;
                pRecord->nDataSize = temp.pRecords[0].nDataSize;
                pRecord->nFindDelta = 32 * nCount;
                temp.pRecords[0].pData = NULL;
                i += nCount + nTemp;
            } else {
                i += nCount;
            }

            xb_signature_free(&temp);
        } else if (nChar == '$') {
            int nCount = sig_run(pConverted, i, '$');
            XSigRecord *pRecord = sig_add(pSignature);

            pRecord->type = XSIG_RELOFFSET;
            pRecord->nSizeOfAddr = nCount / 2;
            i += nCount;
        } else if (nChar == '#') {
            int nCount = 0;
            int nSizeOfAddress = 0;
            int bIsBaseAddress = 0;
            CDBuf base;
            XSigRecord *pRecord = NULL;
            int j = 0;

            cdbuf_init(&base);

            for (j = i; j < nSize; j++) {
                if (pConverted[j] == '#') {
                    nCount++;
                    nSizeOfAddress++;
                } else if (pConverted[j] == '[') {
                    nCount++;
                    bIsBaseAddress = 1;
                } else if (pConverted[j] == ']') {
                    nCount++;
                    bIsBaseAddress = 0;
                } else if (bIsBaseAddress) {
                    nCount++;
                    cdbuf_append_ch(&base, pConverted[j]);
                } else {
                    break;
                }
            }

            pRecord = sig_add(pSignature);
            pRecord->type = XSIG_ADDRESS;
            pRecord->nSizeOfAddr = nSizeOfAddress / 2;
            pRecord->nBaseAddress = base.pData ? (cd_u64)x_strtoull(base.pData, NULL, 16) : 0;
            cdbuf_free(&base);
            i += nCount;
        } else {
            int nBytes = sig_bytes(pSignature, pConverted, i, NULL);

            if (nBytes) {
                i += nBytes;
            } else {
                break;
            }
        }
    }

    return pSignature->bValid;
}

void xb_signature_free(XSignature *pSignature)
{
    int i = 0;

    for (i = 0; i < pSignature->nCount; i++) {
        cd_free(pSignature->pRecords[i].pData);
    }

    cd_free(pSignature->pRecords);
    x_memset(pSignature, 0, sizeof(*pSignature));
}

static int mem_is_not_null(const unsigned char *pData, cd_i64 nSize)
{
    cd_i64 i = 0;

    for (i = 0; i < nSize; i++) {
        if (pData[i] == 0) {
            return 0;
        }
    }

    return 1;
}

static int mem_is_ansi(const unsigned char *pData, cd_i64 nSize)
{
    cd_i64 i = 0;

    for (i = 0; i < nSize; i++) {
        if ((pData[i] < 0x20) || (pData[i] > 0x7E)) {
            return 0;
        }
    }

    return 1;
}

static int mem_is_not_ansi(const unsigned char *pData, cd_i64 nSize)
{
    cd_i64 i = 0;

    for (i = 0; i < nSize; i++) {
        if ((pData[i] >= 0x20) && (pData[i] <= 0x7E)) {
            return 0;
        }
    }

    return 1;
}

static int mem_is_not_ansi_and_null(const unsigned char *pData, cd_i64 nSize)
{
    cd_i64 i = 0;

    for (i = 0; i < nSize; i++) {
        if (((pData[i] >= 0x20) && (pData[i] <= 0x7E)) || (pData[i] == 0)) {
            return 0;
        }
    }

    return 1;
}

static int mem_is_ansi_number(const unsigned char *pData, cd_i64 nSize)
{
    cd_i64 i = 0;

    for (i = 0; i < nSize; i++) {
        if ((pData[i] < '0') || (pData[i] > '9')) {
            return 0;
        }
    }

    return 1;
}

int xb_signature_compare(XBFile *pFile, XBMemoryMap *pMap, XSignature *pSignature, cd_i64 nOffset)
{
    int i = 0;
    cd_i64 nFileSize = pFile->nSize;

    for (i = 0; i < pSignature->nCount; i++) {
        XSigRecord *pRecord = &pSignature->pRecords[i];

        switch (pRecord->type) {
            case XSIG_BYTES: {
                cd_i64 nNeed = pRecord->nDataSize;

                if ((nNeed <= 0) || (nOffset < 0) || (nOffset > nFileSize) || (nNeed > nFileSize - nOffset)) {
                    return 0;
                }

                if (x_memcmp(pFile->pData + nOffset, pRecord->pData, (size_t)nNeed) != 0) {
                    return 0;
                }

                nOffset += nNeed;
                break;
            }

            case XSIG_NOTNULL:
            case XSIG_ANSI:
            case XSIG_NOTANSI:
            case XSIG_NOTANSIANDNULL:
            case XSIG_ANSINUMBER: {
                cd_i64 nNeed = pRecord->nWindowSize;
                const unsigned char *pData = NULL;
                int bOk = 1;

                if ((nNeed <= 0) || (nOffset < 0) || (nOffset > nFileSize) || (nNeed > nFileSize - nOffset)) {
                    return 0;
                }

                pData = pFile->pData + nOffset;

                if (pRecord->type == XSIG_NOTNULL) {
                    bOk = mem_is_not_null(pData, nNeed);
                } else if (pRecord->type == XSIG_ANSI) {
                    bOk = mem_is_ansi(pData, nNeed);
                } else if (pRecord->type == XSIG_NOTANSI) {
                    bOk = mem_is_not_ansi(pData, nNeed);
                } else if (pRecord->type == XSIG_NOTANSIANDNULL) {
                    bOk = mem_is_not_ansi_and_null(pData, nNeed);
                } else {
                    bOk = mem_is_ansi_number(pData, nNeed);
                }

                if (!bOk) {
                    return 0;
                }

                nOffset += nNeed;
                break;
            }

            case XSIG_FINDBYTES: {
                cd_i64 nLimit = pRecord->nFindDelta + pRecord->nDataSize;
                cd_i64 nWhere = xb_find_bytes(pFile, nOffset, nLimit, pRecord->pData, pRecord->nDataSize);

                if (nWhere == -1) {
                    return 0;
                }

                nOffset = nWhere + pRecord->nDataSize;
                break;
            }

            case XSIG_SKIP: {
                cd_i64 nAdd = pRecord->nWindowSize;

                if ((nAdd < 0) || (nOffset < 0) || (nOffset > nFileSize) || (nAdd > nFileSize - nOffset)) {
                    return 0;
                }

                nOffset += nAdd;
                break;
            }

            case XSIG_RELOFFSET: {
                cd_i64 nValue = 0;

                switch (pRecord->nSizeOfAddr) {
                    case 1: nValue = 1 + xb_i8(pFile, nOffset); break;
                    case 2: nValue = 2 + xb_u16(pFile, nOffset, pMap->bBigEndian); break;
                    case 4: nValue = 4 + xb_i32(pFile, nOffset, pMap->bBigEndian); break;
                    case 8: nValue = 8 + xb_i64(pFile, nOffset, pMap->bBigEndian); break;
                    default: return 0;
                }

                if ((pMap->fileType == XFT_COM) || (pMap->fileType == XFT_MSDOS)) {
                    cd_i64 nBase = nOffset & ~0xFFFFll;
                    cd_i64 nDelta = nOffset & 0xFFFF;

                    nOffset = nBase + ((nDelta + nValue) & 0xFFFF);
                } else {
                    cd_i64 nAddress = xbmap_offset_to_address(pMap, nOffset);

                    if (nAddress == -1) {
                        return 0;
                    }

                    nOffset = xbmap_address_to_offset(pMap, (cd_u64)(nAddress + nValue));
                }

                if (nOffset == -1) {
                    return 0;
                }

                break;
            }

            case XSIG_ADDRESS: {
                cd_u64 nAddress = 0;

                switch (pRecord->nSizeOfAddr) {
                    case 1: nAddress = xb_u8(pFile, nOffset); break;
                    case 2: nAddress = xb_u16(pFile, nOffset, pMap->bBigEndian); break;
                    case 4: nAddress = xb_u32(pFile, nOffset, pMap->bBigEndian); break;
                    case 8: nAddress = xb_u64(pFile, nOffset, pMap->bBigEndian); break;
                    default: return 0;
                }

                nOffset = xbmap_address_to_offset(pMap, nAddress);

                if (nOffset == -1) {
                    return 0;
                }

                break;
            }

            /* Every XSigType the parser emits is handled above; an unknown
             * record is a no-op here, as it is in the reference's if/else
             * chain over SIGNATURE_RECORD::st.                              */
            default: break;
        }
    }

    return 1;
}

int xb_compare_signature_strings(const char *pBase, const char *pOpt)
{
    char *pConvBase = xb_convert_signature(pBase);
    char *pConvOpt = xb_convert_signature(pOpt);
    size_t nBase = x_strlen(pConvBase);
    size_t nOpt = x_strlen(pConvOpt);
    size_t nMin = (nBase < nOpt) ? nBase : nOpt;
    size_t i = 0;
    int bResult = 0;

    if (nMin && (nBase >= nOpt)) {
        bResult = 1;

        for (i = 0; i < nMin; i++) {
            if ((pConvBase[i] != '.') && (pConvOpt[i] != '.')) {
                if (pConvBase[i] != pConvOpt[i]) {
                    bResult = 0;
                    break;
                }
            }
        }
    }

    cd_free(pConvBase);
    cd_free(pConvOpt);

    return bResult;
}

int xb_compare_signature(XBFile *pFile, XBMemoryMap *pMap, const char *pSignature, cd_i64 nOffset)
{
    char *pConverted = xb_convert_signature(pSignature);
    XSignature signature;
    int bResult = 0;

    xb_signature_parse(&signature, pConverted);

    if (signature.nCount) {
        bResult = xb_signature_compare(pFile, pMap, &signature, nOffset);
    }

    xb_signature_free(&signature);
    cd_free(pConverted);

    return bResult;
}

cd_i64 xb_find_signature(XBFile *pFile, XBMemoryMap *pMap, cd_i64 nOffset, cd_i64 nSize, const char *pSignature)
{
    char *pConverted = xb_convert_signature(pSignature);
    XSignature signature;
    cd_i64 nResult = -1;
    cd_i64 i = 0;

    xb_signature_parse(&signature, pConverted);

    if (signature.nCount && xb_clamp(pFile, nOffset, &nSize)) {
        /* Fast path: signatures that start with a literal byte block. */
        if ((signature.pRecords[0].type == XSIG_BYTES) && (signature.pRecords[0].nDataSize > 0)) {
            cd_i64 nSearch = nOffset;
            cd_i64 nLimit = nOffset + nSize;

            while (nSearch < nLimit) {
                cd_i64 nFound = xb_find_bytes(pFile, nSearch, nLimit - nSearch, signature.pRecords[0].pData, signature.pRecords[0].nDataSize);

                if (nFound == -1) {
                    break;
                }

                if (xb_signature_compare(pFile, pMap, &signature, nFound)) {
                    nResult = nFound;
                    break;
                }

                nSearch = nFound + 1;
            }
        } else {
            for (i = 0; i < nSize; i++) {
                if (xb_signature_compare(pFile, pMap, &signature, nOffset + i)) {
                    nResult = nOffset + i;
                    break;
                }
            }
        }
    }

    xb_signature_free(&signature);
    cd_free(pConverted);

    return nResult;
}
