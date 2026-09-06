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

/* inflate.c - raw DEFLATE decoder (RFC 1951).
 *
 * A canonical-Huffman decoder in the style of Mark Adler's public-domain
 * "puff": a code table is a sorted count of code lengths plus the symbols in
 * canonical order, and decode walks the bit stream one bit at a time. It is
 * not fast, but the only thing that runs through it in this project is an
 * APK's AndroidManifest.xml (a few kilobytes), so clarity wins.
 */

#include "inflate.h"

/* Bit reader over the source buffer. */
typedef struct {
    const unsigned char *pData;
    size_t nSize;
    size_t nByte;   /* next byte to read           */
    int nBitBuf;    /* bits already pulled in       */
    int nBitCount;  /* how many bits are in nBitBuf  */
    int bError;
    size_t nMaxSize; /* hard output ceiling, 0 = none */
    int bLimit;      /* set once that ceiling is hit  */
} BitState;

/* A canonical Huffman table: pnCount[len] is how many codes have that length,
 * pnSymbol lists the symbols in order of increasing code.                   */
typedef struct {
    short *pnCount;
    short *pnSymbol;
} Huffman;

#define INFL_MAXBITS 15
#define INFL_MAXLCODES 286
#define INFL_MAXDCODES 30
#define INFL_MAXCODES (INFL_MAXLCODES + INFL_MAXDCODES)
#define INFL_FIXLCODES 288

/* Ceiling on the pre-allocation driven by the caller's declared uncompressed
 * size; anything larger is grown on demand instead. */
#define INFL_MAX_RESERVE (1024 * 1024)

static int infl_bits(BitState *pState, int nNeed)
{
    int nValue = pState->nBitBuf;

    while (pState->nBitCount < nNeed) {
        if (pState->nByte >= pState->nSize) {
            pState->bError = 1;

            return 0;
        }

        nValue |= (int)(pState->pData[pState->nByte++]) << pState->nBitCount;
        pState->nBitCount += 8;
    }

    pState->nBitBuf = (int)((unsigned int)nValue >> nNeed);
    pState->nBitCount -= nNeed;

    return nValue & ((1 << nNeed) - 1);
}

/* Decode one symbol using the given table. Returns the symbol, or -1 on a bad
 * code. Walks bit by bit, tracking the first code of each length.           */
static int infl_decode(BitState *pState, const Huffman *pHuffman)
{
    int nCode = 0;
    int nFirst = 0;
    int nIndex = 0;
    int nLen = 1;

    for (nLen = 1; nLen <= INFL_MAXBITS; nLen++) {
        nCode |= infl_bits(pState, 1);

        if (pState->bError) {
            return -1;
        }

        {
            int nCount = pHuffman->pnCount[nLen];

            if (nCode - nCount < nFirst) {
                return pHuffman->pnSymbol[nIndex + (nCode - nFirst)];
            }

            nIndex += nCount;
            nFirst += nCount;
            nFirst <<= 1;
            nCode <<= 1;
        }
    }

    return -1;
}

/* Build the count/symbol table from a list of code lengths. Returns 0 for a
 * complete code (and for the single-symbol degenerate case), which is all the
 * caller accepts. */
static int infl_construct(Huffman *pHuffman, const short *pnLengths, int nSymbols)
{
    int nLen = 0;
    int nLeft = 0;
    short nOffsets[INFL_MAXBITS + 1];
    int nSymbol = 0;

    for (nLen = 0; nLen <= INFL_MAXBITS; nLen++) {
        pHuffman->pnCount[nLen] = 0;
    }

    for (nSymbol = 0; nSymbol < nSymbols; nSymbol++) {
        pHuffman->pnCount[pnLengths[nSymbol]]++;
    }

    if (pHuffman->pnCount[0] == nSymbols) {
        return 0;
    }

    nLeft = 1;

    for (nLen = 1; nLen <= INFL_MAXBITS; nLen++) {
        nLeft <<= 1;
        nLeft -= pHuffman->pnCount[nLen];

        if (nLeft < 0) {
            return nLeft;
        }
    }

    nOffsets[1] = 0;

    for (nLen = 1; nLen < INFL_MAXBITS; nLen++) {
        nOffsets[nLen + 1] = (short)(nOffsets[nLen] + pHuffman->pnCount[nLen]);
    }

    for (nSymbol = 0; nSymbol < nSymbols; nSymbol++) {
        if (pnLengths[nSymbol] != 0) {
            pHuffman->pnSymbol[nOffsets[pnLengths[nSymbol]]++] = (short)nSymbol;
        }
    }

    return nLeft;
}

/* The length and distance base/extra tables from the RFC. */
static const short g_nLenBase[29] = {3,   4,   5,   6,   7,   8,   9,   10,  11,  13,  15,  17,  19,  23, 27,
                                     31,  35,  43,  51,  59,  67,  83,  99,  115, 131, 163, 195, 227, 258};
static const short g_nLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const short g_nDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,   25,
                                      33,   49,   65,   97,   129,  193,   257,   385,   513,  769,
                                      1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
static const short g_nDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int infl_codes(BitState *pState, const Huffman *pLen, const Huffman *pDist, size_t nExpectedSize, CDBuf *pOut)
{
    for (;;) {
        int nSymbol = infl_decode(pState, pLen);

        if (nSymbol < 0) {
            return 0;
        }

        if (nSymbol == 256) {
            return 1;
        }

        if (nSymbol < 256) {
            if ((nExpectedSize != 0) && (pOut->nSize >= nExpectedSize)) {
                return 0;
            }

            if ((pState->nMaxSize != 0) && (pOut->nSize >= pState->nMaxSize)) {
                pState->bLimit = 1;

                return 1;
            }

            cdbuf_append_ch(pOut, (char)(unsigned char)nSymbol);
        } else {
            int nLength = 0;
            int nDistSymbol = 0;
            int nDistance = 0;
            int i = 0;

            nSymbol -= 257;

            if (nSymbol >= 29) {
                return 0;
            }

            nLength = g_nLenBase[nSymbol] + infl_bits(pState, g_nLenExtra[nSymbol]);

            nDistSymbol = infl_decode(pState, pDist);

            if ((nDistSymbol < 0) || (nDistSymbol >= 30)) {
                return 0;
            }

            nDistance = g_nDistBase[nDistSymbol] + infl_bits(pState, g_nDistExtra[nDistSymbol]);

            if (pState->bError) {
                return 0;
            }

            if ((size_t)nDistance > pOut->nSize) {
                return 0;
            }

            if ((nExpectedSize != 0) && (pOut->nSize + (size_t)nLength > nExpectedSize)) {
                return 0;
            }

            for (i = 0; i < nLength; i++) {
                if ((pState->nMaxSize != 0) && (pOut->nSize >= pState->nMaxSize)) {
                    pState->bLimit = 1;

                    return 1;
                }

                cdbuf_append_ch(pOut, pOut->pData[pOut->nSize - (size_t)nDistance]);
            }
        }
    }
}

static int infl_stored(BitState *pState, size_t nExpectedSize, CDBuf *pOut)
{
    unsigned int nLen = 0;
    unsigned int nComplement = 0;
    size_t nCopy = 0;

    /* Stored blocks are byte aligned: drop the partial bit buffer. */
    pState->nBitBuf = 0;
    pState->nBitCount = 0;

    if (pState->nByte + 4 > pState->nSize) {
        return 0;
    }

    nLen = (unsigned int)pState->pData[pState->nByte] | ((unsigned int)pState->pData[pState->nByte + 1] << 8);

    /* The next two bytes are the one's complement of the length (RFC 1951
     * 3.2.4); zlib abandons the stream when they disagree. */
    nComplement = (unsigned int)pState->pData[pState->nByte + 2] | ((unsigned int)pState->pData[pState->nByte + 3] << 8);
    pState->nByte += 4;

    if (nLen != ((~nComplement) & 0xFFFF)) {
        return 0;
    }

    if (pState->nByte + nLen > pState->nSize) {
        return 0;
    }

    if ((nExpectedSize != 0) && (pOut->nSize + nLen > nExpectedSize)) {
        return 0;
    }

    nCopy = nLen;

    if ((pState->nMaxSize != 0) && ((pOut->nSize + nLen) > pState->nMaxSize)) {
        nCopy = (pOut->nSize < pState->nMaxSize) ? (pState->nMaxSize - pOut->nSize) : 0;
        pState->bLimit = 1;
    }

    cdbuf_append(pOut, pState->pData + pState->nByte, nCopy);
    pState->nByte += nLen;

    return 1;
}

static int infl_fixed(BitState *pState, size_t nExpectedSize, CDBuf *pOut)
{
    short nLengths[INFL_FIXLCODES];
    short nLenCount[INFL_MAXBITS + 1];
    short nLenSymbol[INFL_FIXLCODES];
    short nDistCount[INFL_MAXBITS + 1];
    short nDistSymbol[INFL_MAXDCODES];
    Huffman lenCode;
    Huffman distCode;
    int nSymbol = 0;

    for (nSymbol = 0; nSymbol < 144; nSymbol++) nLengths[nSymbol] = 8;
    for (; nSymbol < 256; nSymbol++) nLengths[nSymbol] = 9;
    for (; nSymbol < 280; nSymbol++) nLengths[nSymbol] = 7;
    for (; nSymbol < INFL_FIXLCODES; nSymbol++) nLengths[nSymbol] = 8;

    lenCode.pnCount = nLenCount;
    lenCode.pnSymbol = nLenSymbol;
    infl_construct(&lenCode, nLengths, INFL_FIXLCODES);

    for (nSymbol = 0; nSymbol < INFL_MAXDCODES; nSymbol++) nLengths[nSymbol] = 5;

    distCode.pnCount = nDistCount;
    distCode.pnSymbol = nDistSymbol;
    infl_construct(&distCode, nLengths, INFL_MAXDCODES);

    return infl_codes(pState, &lenCode, &distCode, nExpectedSize, pOut);
}

static int infl_dynamic(BitState *pState, size_t nExpectedSize, CDBuf *pOut)
{
    static const short g_nOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

    short nLengths[INFL_MAXCODES];
    short nLenCount[INFL_MAXBITS + 1];
    short nLenSymbol[INFL_MAXLCODES];
    short nDistCount[INFL_MAXBITS + 1];
    short nDistSymbol[INFL_MAXDCODES];
    Huffman lenCode;
    Huffman distCode;
    int nLenCodes = 0;
    int nDistCodes = 0;
    int nCodeCodes = 0;
    int nIndex = 0;
    int nLeft = 0;

    nLenCodes = infl_bits(pState, 5) + 257;
    nDistCodes = infl_bits(pState, 5) + 1;
    nCodeCodes = infl_bits(pState, 4) + 4;

    if (pState->bError || (nLenCodes > INFL_MAXLCODES) || (nDistCodes > INFL_MAXDCODES)) {
        return 0;
    }

    for (nIndex = 0; nIndex < nCodeCodes; nIndex++) {
        nLengths[g_nOrder[nIndex]] = (short)infl_bits(pState, 3);
    }

    for (; nIndex < 19; nIndex++) {
        nLengths[g_nOrder[nIndex]] = 0;
    }

    lenCode.pnCount = nLenCount;
    lenCode.pnSymbol = nLenSymbol;

    if (infl_construct(&lenCode, nLengths, 19) != 0) {
        return 0;
    }

    nIndex = 0;

    while (nIndex < nLenCodes + nDistCodes) {
        int nSymbol = infl_decode(pState, &lenCode);

        if (nSymbol < 0) {
            return 0;
        }

        if (nSymbol < 16) {
            nLengths[nIndex++] = (short)nSymbol;
        } else {
            int nLen = 0;
            int nRepeat = 0;

            if (nSymbol == 16) {
                if (nIndex == 0) {
                    return 0;
                }

                nLen = nLengths[nIndex - 1];
                nRepeat = 3 + infl_bits(pState, 2);
            } else if (nSymbol == 17) {
                nRepeat = 3 + infl_bits(pState, 3);
            } else {
                nRepeat = 11 + infl_bits(pState, 7);
            }

            if (nIndex + nRepeat > nLenCodes + nDistCodes) {
                return 0;
            }

            while (nRepeat-- > 0) {
                nLengths[nIndex++] = (short)nLen;
            }
        }
    }

    if (pState->bError) {
        return 0;
    }

    /* A missing end-of-block code makes the stream unterminable. */
    if (nLengths[256] == 0) {
        return 0;
    }

    lenCode.pnCount = nLenCount;
    lenCode.pnSymbol = nLenSymbol;

    nLeft = infl_construct(&lenCode, nLengths, nLenCodes);

    /* An under-subscribed code is only legal for the single length-1 code
     * case; zlib rejects every other incomplete set. */
    if ((nLeft != 0) && ((nLeft < 0) || (nLenCodes != nLenCount[0] + nLenCount[1]))) {
        return 0;
    }

    distCode.pnCount = nDistCount;
    distCode.pnSymbol = nDistSymbol;

    nLeft = infl_construct(&distCode, nLengths + nLenCodes, nDistCodes);

    if ((nLeft != 0) && ((nLeft < 0) || (nDistCodes != nDistCount[0] + nDistCount[1]))) {
        return 0;
    }

    return infl_codes(pState, &lenCode, &distCode, nExpectedSize, pOut);
}

int inflate_raw(const unsigned char *pSource, size_t nSourceSize, size_t nExpectedSize, size_t nMaxSize, CDBuf *pOut)
{
    BitState state;
    int bFinal = 0;
    size_t nReserve = 0;

    state.pData = pSource;
    state.nSize = nSourceSize;
    state.nByte = 0;
    state.nBitBuf = 0;
    state.nBitCount = 0;
    state.bError = 0;
    state.nMaxSize = nMaxSize;
    state.bLimit = 0;

    /* The declared size comes straight out of the file, so it only steers the
     * pre-allocation up to a sane point; past that the buffer just grows. */
    nReserve = nExpectedSize;

    if (nReserve > INFL_MAX_RESERVE) {
        nReserve = INFL_MAX_RESERVE;
    }

    if (nReserve != 0) {
        cdbuf_reserve(pOut, nReserve + 1);
    }

    do {
        int nType = 0;

        bFinal = infl_bits(&state, 1);
        nType = infl_bits(&state, 2);

        if (state.bError) {
            return 0;
        }

        if (nType == 0) {
            if (!infl_stored(&state, nExpectedSize, pOut)) {
                return 0;
            }
        } else if (nType == 1) {
            if (!infl_fixed(&state, nExpectedSize, pOut)) {
                return 0;
            }
        } else if (nType == 2) {
            if (!infl_dynamic(&state, nExpectedSize, pOut)) {
                return 0;
            }
        } else {
            return 0;
        }

        if (state.bLimit) {
            return 1;
        }
    } while (!bFinal);

    return 1;
}
