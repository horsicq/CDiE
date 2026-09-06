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

#include "xjpeg.h"

#define JPEG_SIGNATURE_SIZE     2
#define JPEG_SEGMENT_HEADER_SIZE 4
#define JPEG_DRI_SEGMENT_SIZE   6
#define JPEG_EXIF_DATA_OFFSET   10
#define JPEG_MAX_COMMENT_SIZE   100

#define JFIF_ID_OFFSET            6
#define JFIF_VERSION_MAJOR_OFFSET 0x0B
#define JFIF_VERSION_MINOR_OFFSET 0x0C

#define JPEG_MARKER_PREFIX       0xFF
#define JPEG_MARKER_STUFFED_ZERO 0x00
#define JPEG_MARKER_RST0         0xD0
#define JPEG_MARKER_RST7         0xD7
#define JPEG_MARKER_SOI          0xD8
#define JPEG_MARKER_EOI          0xD9
#define JPEG_MARKER_SOS          0xDA
#define JPEG_MARKER_DQT          0xDB
#define JPEG_MARKER_DRI          0xDD
#define JPEG_MARKER_APP1         0xE1
#define JPEG_MARKER_COM          0xFE

static int is_restart_marker(cd_u8 nId)
{
    return ((nId >= JPEG_MARKER_RST0) && (nId <= JPEG_MARKER_RST7)) ? 1 : 0;
}

static int is_marker_without_length(cd_u8 nId)
{
    return ((nId == JPEG_MARKER_SOI) || (nId == JPEG_MARKER_EOI) || is_restart_marker(nId)) ? 1 : 0;
}

static void chunk_add(XJpeg *pJpeg, const XJpegChunk *pChunk, int *pnCapacity)
{
    if (pJpeg->nChunkCount + 1 > *pnCapacity) {
        *pnCapacity = (*pnCapacity) ? ((*pnCapacity) * 2) : 32;
        pJpeg->pChunks = (XJpegChunk *)cd_realloc(pJpeg->pChunks, (size_t)(*pnCapacity) * sizeof(XJpegChunk));
    }

    pJpeg->pChunks[pJpeg->nChunkCount++] = *pChunk;
}

/* Reads one segment header. Returns 0 when the offset is not a marker. */
static int read_chunk(XJpeg *pJpeg, cd_i64 nOffset, XJpegChunk *pChunk)
{
    x_memset(pChunk, 0, sizeof(*pChunk));

    if ((nOffset < 0) || (nOffset + 2 > pJpeg->pFile->nSize)) {
        return 0;
    }

    if (xb_u8(pJpeg->pFile, nOffset) != JPEG_MARKER_PREFIX) {
        return 0;
    }

    pChunk->nId = xb_u8(pJpeg->pFile, nOffset + 1);
    pChunk->nDataOffset = nOffset;

    if (is_marker_without_length(pChunk->nId)) {
        pChunk->nDataSize = JPEG_SIGNATURE_SIZE;
    } else if (pChunk->nId == JPEG_MARKER_DRI) {
        pChunk->nDataSize = JPEG_DRI_SEGMENT_SIZE;
    } else if ((pChunk->nId != JPEG_MARKER_STUFFED_ZERO) && (pChunk->nId != JPEG_MARKER_PREFIX)) {
        cd_u16 nLength = 0;

        if (nOffset + JPEG_SEGMENT_HEADER_SIZE > pJpeg->pFile->nSize) {
            return 0;
        }

        nLength = xb_u16(pJpeg->pFile, nOffset + JPEG_SIGNATURE_SIZE, 1);

        /* A declared length below 2 does not even cover its own length field;
         * the reference rejects the chunk instead of producing a short one. */
        if (nLength < 2) {
            return 0;
        }

        pChunk->nDataSize = JPEG_SIGNATURE_SIZE + nLength;
    } else {
        return 0;
    }

    if (pChunk->nDataSize > pJpeg->pFile->nSize - nOffset) {
        return 0;
    }

    return 1;
}

static void parse_chunks(XJpeg *pJpeg)
{
    cd_i64 nOffset = 0;
    int nCapacity = 0;
    int bComplete = 0;

    for (;;) {
        XJpegChunk chunk;

        if (nOffset == -1) {
            break;
        }

        if (!read_chunk(pJpeg, nOffset, &chunk)) {
            break;
        }

        chunk_add(pJpeg, &chunk, &nCapacity);

        nOffset = chunk.nDataOffset + chunk.nDataSize;

        if (chunk.nId == JPEG_MARKER_SOS) {
            cd_i64 nDataOffset = nOffset;
            cd_i64 nEntropyEnd = pJpeg->pFile->nSize;
            cd_i64 nNextMarker = -1;

            /* Entropy-coded data may contain stuffed zeroes, runs of 0xFF fill
             * bytes and restart markers; none of those end the scan. */
            while (nOffset < pJpeg->pFile->nSize) {
                cd_i64 nPrefixOffset = xb_find_u8(pJpeg->pFile, nOffset, -1, JPEG_MARKER_PREFIX);
                cd_i64 nIdOffset = 0;
                cd_u8 nId = 0;

                if ((nPrefixOffset < 0) || (nPrefixOffset >= pJpeg->pFile->nSize - 1)) {
                    break;
                }

                nIdOffset = nPrefixOffset + 1;

                while ((nIdOffset < pJpeg->pFile->nSize) && (xb_u8(pJpeg->pFile, nIdOffset) == JPEG_MARKER_PREFIX)) {
                    nIdOffset++;
                }

                if (nIdOffset >= pJpeg->pFile->nSize) {
                    break;
                }

                nId = xb_u8(pJpeg->pFile, nIdOffset);

                if ((nId == JPEG_MARKER_STUFFED_ZERO) || is_restart_marker(nId)) {
                    nOffset = nIdOffset + 1;

                    continue;
                }

                nEntropyEnd = nPrefixOffset;
                nNextMarker = nIdOffset - 1;

                break;
            }

            if (nEntropyEnd > nDataOffset) {
                XJpegChunk entropy;

                x_memset(&entropy, 0, sizeof(entropy));
                entropy.bEntropyCodedData = 1;
                entropy.nDataOffset = nDataOffset;
                entropy.nDataSize = nEntropyEnd - nDataOffset;
                chunk_add(pJpeg, &entropy, &nCapacity);
            }

            if (nNextMarker < 0) {
                break;
            }

            nOffset = nNextMarker;
        }

        if (chunk.nId == JPEG_MARKER_EOI) {
            bComplete = 1;

            break;
        }

        if (pJpeg->nChunkCount > 100000) {
            break;
        }
    }

    /* A walk that never reached EOI describes nothing the reference would
     * report: XJpeg::getChunks drops the whole list in that case. */
    if (!bComplete) {
        pJpeg->nChunkCount = 0;
    }
}

int xjpeg_is_chunk_present(XJpeg *pJpeg, cd_u8 nId)
{
    int i = 0;

    for (i = 0; i < pJpeg->nChunkCount; i++) {
        if ((!pJpeg->pChunks[i].bEntropyCodedData) && (pJpeg->pChunks[i].nId == nId)) {
            return 1;
        }
    }

    return 0;
}

static void parse_version(XJpeg *pJpeg)
{
    char *pIdent = xb_ansi_string(pJpeg->pFile, JFIF_ID_OFFSET, 5);

    if (x_strcmp(pIdent, "JFIF") == 0) {
        char sBuf[32];

        x_snprintf(sBuf, sizeof(sBuf), "%u.%u", (unsigned)xb_u8(pJpeg->pFile, JFIF_VERSION_MAJOR_OFFSET), (unsigned)xb_u8(pJpeg->pFile, JFIF_VERSION_MINOR_OFFSET));
        pJpeg->pVersion = cd_strdup(sBuf);
    } else {
        pJpeg->pVersion = cd_strdup("");
    }

    cd_free(pIdent);
}

static void parse_comment(XJpeg *pJpeg)
{
    CDBuf buf;
    int i = 0;
    char *pText = NULL;
    char *pClean = NULL;
    size_t nSize = 0;
    size_t j = 0;

    cdbuf_init(&buf);

    for (i = 0; i < pJpeg->nChunkCount; i++) {
        if (pJpeg->pChunks[i].bEntropyCodedData || (pJpeg->pChunks[i].nId != JPEG_MARKER_COM)) {
            continue;
        }

        /* XJpeg::getComment skips a segment that cannot hold a payload or that
         * does not fit in the file, and never reads past the declared length. */
        if ((pJpeg->pChunks[i].nDataSize <= JPEG_SEGMENT_HEADER_SIZE) || (pJpeg->pChunks[i].nDataOffset < 0) ||
            (pJpeg->pChunks[i].nDataOffset > pJpeg->pFile->nSize - pJpeg->pChunks[i].nDataSize)) {
            continue;
        }

        {
            char *pPart = xb_ansi_string(pJpeg->pFile, pJpeg->pChunks[i].nDataOffset + JPEG_SEGMENT_HEADER_SIZE,
                                         pJpeg->pChunks[i].nDataSize - JPEG_SEGMENT_HEADER_SIZE);

            cdbuf_append_str(&buf, pPart);
            cd_free(pPart);
        }
    }

    pText = cdbuf_detach(&buf, &nSize);

    if (nSize > JPEG_MAX_COMMENT_SIZE) {
        pText[JPEG_MAX_COMMENT_SIZE] = 0;
        nSize = JPEG_MAX_COMMENT_SIZE;
    }

    /* The reference strips CR and LF from the comment. */
    pClean = (char *)cd_malloc(nSize + 1);

    for (i = 0, j = 0; j < nSize; j++) {
        if ((pText[j] != '\r') && (pText[j] != '\n')) {
            pClean[i++] = pText[j];
        }
    }

    pClean[i] = 0;
    cd_free(pText);
    pJpeg->pComment = pClean;
}

static void parse_dqt_md5(XJpeg *pJpeg)
{
    CDBuf buf;
    int i = 0;
    XBFile temp;
    char *pHash = NULL;
    size_t k = 0;
    unsigned char nEmpty = 0;

    cdbuf_init(&buf);

    for (i = 0; i < pJpeg->nChunkCount; i++) {
        cd_i64 nOffset = 0;
        cd_i64 nSize = 0;

        if (pJpeg->pChunks[i].bEntropyCodedData || (pJpeg->pChunks[i].nId != JPEG_MARKER_DQT)) {
            continue;
        }

        nOffset = pJpeg->pChunks[i].nDataOffset + JPEG_SEGMENT_HEADER_SIZE;
        nSize = pJpeg->pChunks[i].nDataSize - JPEG_SEGMENT_HEADER_SIZE;

        if ((nSize > 0) && (nOffset >= 0) && (nOffset < pJpeg->pFile->nSize)) {
            if (nSize > pJpeg->pFile->nSize - nOffset) {
                nSize = pJpeg->pFile->nSize - nOffset;
            }

            cdbuf_append(&buf, pJpeg->pFile->pData + nOffset, (size_t)nSize);
        }
    }

    /* xb_md5 works on an XBFile, so the collected payload is wrapped in one.
     * An empty collection still needs a non-NULL pData; nSize is 0 then, so
     * nEmpty is never read.                                                  */
    x_memset(&temp, 0, sizeof(temp));
    temp.pData = buf.pData ? (unsigned char *)buf.pData : &nEmpty;
    temp.nSize = (cd_i64)buf.nSize;

    pHash = xb_md5(&temp, 0, temp.nSize);

    for (k = 0; pHash[k]; k++) {
        if ((pHash[k] >= 'A') && (pHash[k] <= 'F')) {
            pHash[k] = (char)(pHash[k] - 'A' + 'a');
        }
    }

    pJpeg->pDqtMD5 = pHash;
    cdbuf_free(&buf);
}

static void parse_exif_region(XJpeg *pJpeg)
{
    int i = 0;

    for (i = 0; i < pJpeg->nChunkCount; i++) {
        if (pJpeg->pChunks[i].bEntropyCodedData || (pJpeg->pChunks[i].nId != JPEG_MARKER_APP1)) {
            continue;
        }

        if (pJpeg->pChunks[i].nDataSize > JPEG_EXIF_DATA_OFFSET) {
            char *pIdent = xb_ansi_string(pJpeg->pFile, pJpeg->pChunks[i].nDataOffset + JPEG_SEGMENT_HEADER_SIZE, 16);

            if (x_strcmp(pIdent, "Exif") == 0) {
                pJpeg->nExifOffset = pJpeg->pChunks[i].nDataOffset + JPEG_EXIF_DATA_OFFSET;
                pJpeg->nExifSize = pJpeg->pChunks[i].nDataSize - JPEG_EXIF_DATA_OFFSET;
            }

            cd_free(pIdent);
        }

        break; /* only the first APP1 segment is inspected */
    }
}

/* ------------------------------------------------------------ TIFF / EXIF */

static int tiff_base_type_size(cd_u16 nType)
{
    switch (nType) {
        case 1: return 1;  /* BYTE      */
        case 2: return 1;  /* ASCII     */
        case 3: return 2;  /* SHORT     */
        case 4: return 4;  /* LONG      */
        case 5: return 8;  /* RATIONAL  */
        case 6: return 1;  /* SBYTE     */
        case 7: return 1;  /* UNDEFINED */
        case 8: return 2;  /* SSHORT    */
        case 9: return 4;  /* SLONG     */
        case 10: return 8; /* SRATIONAL */
        case 11: return 4; /* FLOAT     */
        case 12: return 8; /* DOUBLE    */
        default: return 0;
    }
}

/* Reads the ASCII value of one IFD tag inside the EXIF block. Offsets in the
 * block are relative to the start of the TIFF header.                       */
static char *tiff_find_ascii_tag(XJpeg *pJpeg, cd_u16 nWantedTag)
{
    XBFile *pFile = pJpeg->pFile;
    cd_i64 nBase = pJpeg->nExifOffset;
    cd_i64 nSize = pJpeg->nExifSize;
    int bBigEndian = 0;
    cd_i64 nTableOffset = 0;
    int nGuard = 0;

    if (nSize < 8) {
        return NULL;
    }

    if ((xb_u8(pFile, nBase) == 'M') && (xb_u8(pFile, nBase + 1) == 'M') && (xb_u8(pFile, nBase + 2) == 0) && (xb_u8(pFile, nBase + 3) == 0x2A)) {
        bBigEndian = 1;
    } else if (!((xb_u8(pFile, nBase) == 'I') && (xb_u8(pFile, nBase + 1) == 'I') && (xb_u8(pFile, nBase + 2) == 0x2A) && (xb_u8(pFile, nBase + 3) == 0))) {
        return NULL;
    }

    nTableOffset = (cd_i64)xb_u32(pFile, nBase + 4, bBigEndian);

    while ((nTableOffset > 0) && (nTableOffset < nSize) && (nGuard++ < 64)) {
        cd_u16 nCount = xb_u16(pFile, nBase + nTableOffset, bBigEndian);
        cd_i64 nCurrent = nTableOffset + 2;
        cd_u16 i = 0;
        cd_i64 nNextTable = 0;

        for (i = 0; i < nCount; i++) {
            cd_u16 nTag = xb_u16(pFile, nBase + nCurrent, bBigEndian);
            cd_u16 nType = xb_u16(pFile, nBase + nCurrent + 2, bBigEndian);
            cd_u32 nElements = xb_u32(pFile, nBase + nCurrent + 4, bBigEndian);
            cd_i64 nDataSize = (cd_i64)tiff_base_type_size(nType) * (cd_i64)nElements;
            cd_i64 nValueOffset = 0;

            if (nDataSize > 4) {
                nValueOffset = (cd_i64)xb_u32(pFile, nBase + nCurrent + 8, bBigEndian);
            } else {
                nValueOffset = nCurrent + 8;
            }

            if (nTag == nWantedTag) {
                if ((nValueOffset >= 0) && (nValueOffset < nSize) && (nDataSize > 0)) {
                    if (nDataSize > nSize - nValueOffset) {
                        nDataSize = nSize - nValueOffset;
                    }

                    return xb_ansi_string(pFile, nBase + nValueOffset, nDataSize);
                }

                return NULL;
            }

            nCurrent += 12;
        }

        nNextTable = (cd_i64)xb_u32(pFile, nBase + nCurrent, bBigEndian);

        if (nNextTable < nTableOffset + 2 + (cd_i64)nCount * 12 + 8) {
            break;
        }

        nTableOffset = nNextTable;
    }

    return NULL;
}

static void parse_exif_camera(XJpeg *pJpeg)
{
    char *pMake = NULL;
    char *pModel = NULL;

    pJpeg->pExifCameraName = cd_strdup("");

    if (pJpeg->nExifSize <= 0) {
        return;
    }

    pMake = tiff_find_ascii_tag(pJpeg, 0x10F);
    pModel = tiff_find_ascii_tag(pJpeg, 0x110);

    if ((pMake && pMake[0]) || (pModel && pModel[0])) {
        CDBuf buf;

        cdbuf_init(&buf);
        cdbuf_append_str(&buf, pMake ? pMake : "");
        cdbuf_append_ch(&buf, '(');
        cdbuf_append_str(&buf, pModel ? pModel : "");
        cdbuf_append_ch(&buf, ')');

        cd_free(pJpeg->pExifCameraName);
        pJpeg->pExifCameraName = cdbuf_detach(&buf, NULL);
    }

    cd_free(pMake);
    cd_free(pModel);
}

/* ------------------------------------------------------------------- main */

int xjpeg_parse(XJpeg *pJpeg, XBFile *pFile)
{
    x_memset(pJpeg, 0, sizeof(*pJpeg));
    pJpeg->pFile = pFile;

    if (pFile->nSize < 20) {
        return 0;
    }

    if ((xb_u8(pFile, 0) != 0xFF) || (xb_u8(pFile, 1) != 0xD8) || (xb_u8(pFile, 2) != 0xFF)) {
        return 0;
    }

    pJpeg->bValid = 1;

    parse_chunks(pJpeg);
    parse_version(pJpeg);
    parse_comment(pJpeg);
    parse_dqt_md5(pJpeg);
    parse_exif_region(pJpeg);
    parse_exif_camera(pJpeg);

    return 1;
}

void xjpeg_free(XJpeg *pJpeg)
{
    cd_free(pJpeg->pChunks);
    cd_free(pJpeg->pVersion);
    cd_free(pJpeg->pComment);
    cd_free(pJpeg->pDqtMD5);
    cd_free(pJpeg->pExifCameraName);
    x_memset(pJpeg, 0, sizeof(*pJpeg));
}
