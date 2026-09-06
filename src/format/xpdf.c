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

/* xpdf.c - a PDF object reader, ported from XPDF.
 *
 * PDF detection in the database is entirely key/value driven: the scripts ask
 * for "/Filter", "/Creator", "/Producer" and "/Author" values and the header
 * comment. None of that needs the page tree, encryption or stream
 * decompression, so this parser stops at reducing each object dictionary to a
 * flat list of tokens (XPART::listParts) and answering key/value queries over
 * those, exactly as the reference does.
 *
 * The tokeniser mirrors XPDF's _readPDFStringPart family byte for byte,
 * including its quirks (indirect "N 0 R" references glued into one token, the
 * simple backslash handling in "(...)" strings), because the goal is
 * identical output, not a better PDF reader.
 */

#include "xpdf.h"
#include "inflate.h"

/* The whole file is already in memory; work over it directly. */
typedef struct {
    const unsigned char *pData;
    cd_i64 nSize;
} PdfIO;

/* A token read from the stream: the text plus how many raw bytes it spanned. */
typedef struct {
    char *pStr;      /* heap, owned by caller  */
    cd_i64 nStrLen;
    cd_i64 nSize;    /* bytes consumed in file */
} PdfTok;

/* ------------------------------------------------------- char classes --- */

static int pdf_is_line_ending(int c)
{
    return (c == 10) || (c == 13);
}

static int pdf_is_string_terminator(int c)
{
    return (c == 0) || pdf_is_line_ending(c);
}

static int pdf_is_title_terminator(int c)
{
    return pdf_is_string_terminator(c) || (c == '<');
}

static int pdf_is_structural_delimiter(int c)
{
    return (c == '[') || (c == ']') || (c == '<') || (c == '>');
}

static int pdf_is_name_terminator(int c)
{
    return pdf_is_string_terminator(c) || pdf_is_structural_delimiter(c) || (c == ' ') || (c == 9) || (c == 12) || (c == '(');
}

static int pdf_is_value_terminator(int c)
{
    return pdf_is_string_terminator(c) || pdf_is_structural_delimiter(c) || (c == '/');
}

/* ------------------------------------------------------------- skips ---- */

static cd_i64 pdf_skip_ending(const PdfIO *pIO, cd_i64 nOffset)
{
    cd_i64 nStart = nOffset;

    while (nOffset < pIO->nSize) {
        int c = pIO->pData[nOffset];

        if (c == 10) {
            nOffset++;
        } else if (c == 13) {
            nOffset++;

            if ((nOffset < pIO->nSize) && (pIO->pData[nOffset] == 10)) {
                nOffset++;
            }
        } else {
            break;
        }
    }

    return nOffset - nStart;
}

static cd_i64 pdf_skip_space(const PdfIO *pIO, cd_i64 nOffset)
{
    cd_i64 nStart = nOffset;

    while ((nOffset < pIO->nSize) && (pIO->pData[nOffset] == ' ')) {
        nOffset++;
    }

    return nOffset - nStart;
}

/* ------------------------------------------------------ token readers --- */

/* The reference builds every token with QString::fromLatin1, so a byte >= 0x80
 * becomes that Latin-1 code point and leaves the UTF-8 console as two bytes.
 * Widen here so the text matches; the token LENGTHS the object walk tests stay
 * the raw byte counts, which is what the QString character count is. */
static void pdf_append_latin1(CDBuf *pBuf, unsigned int nChar)
{
    if (nChar < 0x80) {
        cdbuf_append_ch(pBuf, (char)nChar);
    } else {
        cdbuf_append_ch(pBuf, (char)(0xC0 | (nChar >> 6)));
        cdbuf_append_ch(pBuf, (char)(0x80 | (nChar & 0x3F)));
    }
}

static char *pdf_dup_range(const PdfIO *pIO, cd_i64 nOffset, cd_i64 nLen)
{
    CDBuf buf;
    cd_i64 i = 0;

    cdbuf_init(&buf);

    for (i = 0; i < nLen; i++) {
        pdf_append_latin1(&buf, pIO->pData[nOffset + i]);
    }

    return cdbuf_detach(&buf, NULL);
}

/* _readPDFString: bytes up to a line terminator, then the terminator. */
static PdfTok pdf_read_string(const PdfIO *pIO, cd_i64 nOffset, cd_i64 nMax)
{
    PdfTok tok;
    cd_i64 nPos = 0;

    tok.pStr = NULL;
    tok.nStrLen = 0;
    tok.nSize = 0;

    if ((nOffset < 0) || (nOffset >= pIO->nSize)) {
        tok.pStr = cd_strdup("");

        return tok;
    }

    if ((nMax < 0) || (nOffset + nMax > pIO->nSize)) {
        nMax = pIO->nSize - nOffset;
    }

    while ((nPos < nMax) && (!pdf_is_string_terminator(pIO->pData[nOffset + nPos]))) {
        nPos++;
    }

    tok.pStr = pdf_dup_range(pIO, nOffset, nPos);
    tok.nStrLen = nPos;
    tok.nSize = nPos + pdf_skip_ending(pIO, nOffset + nPos);

    return tok;
}

/* _readPDFStringPart_title: like the above but '<' also terminates. */
static PdfTok pdf_read_title(const PdfIO *pIO, cd_i64 nOffset, cd_i64 nMax)
{
    PdfTok tok;
    cd_i64 nPos = 0;

    tok.pStr = NULL;
    tok.nStrLen = 0;
    tok.nSize = 0;

    if ((nOffset < 0) || (nOffset >= pIO->nSize)) {
        tok.pStr = cd_strdup("");

        return tok;
    }

    if ((nMax < 0) || (nOffset + nMax > pIO->nSize)) {
        nMax = pIO->nSize - nOffset;
    }

    while ((nPos < nMax) && (!pdf_is_title_terminator(pIO->pData[nOffset + nPos]))) {
        nPos++;
    }

    tok.pStr = pdf_dup_range(pIO, nOffset, nPos);
    tok.nStrLen = nPos;
    tok.nSize = nPos + pdf_skip_ending(pIO, nOffset + nPos);

    return tok;
}

/* _readPDFStringPart_str: a "(...)" string. The returned text keeps the outer
 * parentheses and drops one backslash from every escape, exactly as the
 * reference does; a leading FE FF marks UTF-16BE content. */
static PdfTok pdf_read_str(const PdfIO *pIO, cd_i64 nOffset)
{
    PdfTok tok;
    CDBuf buf;
    cd_i64 nPos = nOffset;
    int bStart = 0;
    int bEnd = 0;
    int bUnicode = 0;
    int bBackslash = 0;
    int nDepth = 0; /* balanced nested-parenthesis depth (ISO 32000 7.3.4.2) */

    tok.pStr = NULL;
    tok.nStrLen = 0;
    tok.nSize = 0;

    cdbuf_init(&buf);

    while (nPos < pIO->nSize) {
        /* Safety cap for a malformed unterminated '(' so the rest of the file
         * is not swallowed. */
        if (tok.nSize > (32 * 1024 * 1024)) {
            break;
        }

        if (!bUnicode) {
            int c = pIO->pData[nPos];

            /* A "(...)" literal may legally contain NUL/CR/LF bytes, so only a
             * terminator BEFORE the opening '(' ends the read; inside, only the
             * matching unescaped ')' does. */
            if ((!bStart) && pdf_is_string_terminator(c)) {
                break;
            }

            if (!bStart) {
                if (c == '(') {
                    bStart = 1;

                    if ((nPos + 2 < pIO->nSize) && (pIO->pData[nPos + 1] == 0xFE) && (pIO->pData[nPos + 2] == 0xFF)) {
                        bUnicode = 1;
                        tok.nSize += 2;
                        nPos += 2;
                    }

                    cdbuf_append_ch(&buf, '(');
                } else {
                    bBackslash = 0;
                    pdf_append_latin1(&buf, (unsigned int)c);
                }
            } else if ((c == '(') && (!bBackslash)) {
                nDepth++;
                cdbuf_append_ch(&buf, '(');
            } else if ((c == ')') && (!bBackslash)) {
                cdbuf_append_ch(&buf, ')');

                if (nDepth > 0) {
                    nDepth--;
                } else {
                    bEnd = 1;
                }
            } else if (c == '\\') {
                bBackslash = 1;
            } else {
                bBackslash = 0;
                pdf_append_latin1(&buf, (unsigned int)c);
            }

            tok.nSize++;
            nPos++;
        } else {
            cd_u32 nWord = 0;

            if (nPos + 1 >= pIO->nSize) {
                break;
            }

            nWord = ((cd_u32)pIO->pData[nPos] << 8) | pIO->pData[nPos + 1];

            if (((nWord >> 8) == '(') && (!bBackslash)) {
                nDepth++;
                cdbuf_append_ch(&buf, '(');
                tok.nSize++;
            } else if (((nWord >> 8) == ')') && (!bBackslash)) {
                cdbuf_append_ch(&buf, ')');
                tok.nSize++;

                if (nDepth > 0) {
                    nDepth--;
                } else {
                    bEnd = 1;
                }
            } else if (nWord == '\\') {
                bBackslash = 1;
                nPos += 2;
                tok.nSize += 2;

                continue;
            } else if (bBackslash && (nWord == 0x6e29)) {
                bBackslash = 0;
                cdbuf_append_ch(&buf, ')');
                tok.nSize++;
                bEnd = 1;
            } else {
                cd_u32 nUnit = nWord;

                bBackslash = 0;

                if (nUnit < 0x80) {
                    cdbuf_append_ch(&buf, (char)nUnit);
                } else if (nUnit < 0x800) {
                    cdbuf_append_ch(&buf, (char)(0xC0 | (nUnit >> 6)));
                    cdbuf_append_ch(&buf, (char)(0x80 | (nUnit & 0x3F)));
                } else {
                    cdbuf_append_ch(&buf, (char)(0xE0 | (nUnit >> 12)));
                    cdbuf_append_ch(&buf, (char)(0x80 | ((nUnit >> 6) & 0x3F)));
                    cdbuf_append_ch(&buf, (char)(0x80 | (nUnit & 0x3F)));
                }

                nPos += 2;
                tok.nSize += 2;

                continue;
            }

            nPos++;
        }

        if (bStart && bEnd) {
            break;
        }
    }

    tok.nStrLen = (cd_i64)buf.nSize;
    tok.pStr = cdbuf_detach(&buf, NULL);

    return tok;
}

/* _readPDFStringPart_hex: a "<...>" hex string, brackets included. */
static PdfTok pdf_read_hex(const PdfIO *pIO, cd_i64 nOffset)
{
    PdfTok tok;
    cd_i64 nPos = 0;

    tok.pStr = NULL;
    tok.nStrLen = 0;
    tok.nSize = 0;

    while ((nOffset + nPos) < pIO->nSize) {
        int c = pIO->pData[nOffset + nPos];

        nPos++;

        if (c == '>') {
            break;
        }
    }

    tok.pStr = pdf_dup_range(pIO, nOffset, nPos);
    tok.nStrLen = nPos;
    tok.nSize = nPos;

    return tok;
}

/* _readPDFStringPart: the token dispatcher. */
static PdfTok pdf_read_part(const PdfIO *pIO, cd_i64 nOffset)
{
    PdfTok tok;
    int nChar = 0;
    cd_i64 nTokenEnd = 0;
    int bFallback = 0;

    tok.pStr = NULL;
    tok.nStrLen = 0;
    tok.nSize = 0;

    if ((nOffset < 0) || (nOffset >= pIO->nSize)) {
        tok.pStr = cd_strdup("");

        return tok;
    }

    nChar = pIO->pData[nOffset];

    if (nChar == '/') {
        cd_i64 nPos = 0;
        int bFirst = 1;

        while ((nOffset + nPos) < pIO->nSize) {
            int c = pIO->pData[nOffset + nPos];

            if (pdf_is_name_terminator(c)) {
                break;
            }

            if ((!bFirst) && (c == '/')) {
                break;
            }

            bFirst = 0;
            nPos++;
        }

        tok.pStr = pdf_dup_range(pIO, nOffset, nPos);
        tok.nStrLen = nPos;
        nTokenEnd = nPos;
    } else if (nChar == '(') {
        tok = pdf_read_str(pIO, nOffset);
        bFallback = 1;
    } else if (nChar == '<') {
        if ((nOffset + 1 < pIO->nSize) && (pIO->pData[nOffset + 1] == '<')) {
            tok.pStr = cd_strdup("<<");
            tok.nStrLen = 2;
            nTokenEnd = 2;
        } else {
            tok = pdf_read_hex(pIO, nOffset);
            bFallback = 1;
        }
    } else if (nChar == '>') {
        if ((nOffset + 1 < pIO->nSize) && (pIO->pData[nOffset + 1] == '>')) {
            tok.pStr = cd_strdup(">>");
            tok.nStrLen = 2;
            nTokenEnd = 2;
        } else {
            tok.pStr = cd_strdup("");
            tok.nStrLen = 0;
            nTokenEnd = 0;
        }
    } else if (nChar == '[') {
        tok.pStr = cd_strdup("[");
        tok.nStrLen = 1;
        nTokenEnd = 1;
    } else if (nChar == ']') {
        tok.pStr = cd_strdup("]");
        tok.nStrLen = 1;
        nTokenEnd = 1;
    } else {
        cd_i64 nPos = 0;
        int bSpace = 0;

        while ((nOffset + nPos) < pIO->nSize) {
            int c = pIO->pData[nOffset + nPos];

            if (pdf_is_value_terminator(c)) {
                break;
            }

            if ((c == ' ') || (c == 9) || (c == 12)) {
                nPos++;
                bSpace = 1;

                break;
            }

            nPos++;
        }

        {
            cd_i64 nCopy = bSpace ? (nPos - 1) : nPos;

            tok.pStr = pdf_dup_range(pIO, nOffset, nCopy);
            tok.nStrLen = nCopy;
            nTokenEnd = nPos;
        }

        /* Glue an indirect "N 0 R" reference into one token. */
        if (bSpace) {
            if ((nOffset + nPos + 2 < pIO->nSize) && (pIO->pData[nOffset + nPos] == '0') && (pIO->pData[nOffset + nPos + 1] == ' ') && (pIO->pData[nOffset + nPos + 2] == 'R')) {
                cd_free(tok.pStr);
                tok.pStr = pdf_dup_range(pIO, nOffset, nPos + 3);
                tok.nStrLen = nPos + 3;
                nTokenEnd = nPos + 3;
            }
        }
    }

    if (bFallback) {
        cd_i64 nNew = nOffset + tok.nSize;

        tok.nSize += pdf_skip_space(pIO, nNew);
        nNew = nOffset + tok.nSize;
        tok.nSize += pdf_skip_ending(pIO, nNew);

        return tok;
    }

    /* Consume ALL trailing whitespace in any order, so a "value\n  /NextKey"
     * layout does not leave the next read starting on whitespace - which would
     * yield an empty token and end the dictionary early. The reference reads
     * the token out of a 512 byte window, so the run stops there too. */
    {
        cd_i64 nPos = nOffset + nTokenEnd;
        cd_i64 nEnd = nOffset + 512;

        if (nEnd > pIO->nSize) {
            nEnd = pIO->nSize;
        }

        while (nPos < nEnd) {
            int c = pIO->pData[nPos];

            if ((c == ' ') || (c == 9) || (c == 12) || (c == 10) || (c == 13) || (c == 0)) {
                nPos++;
            } else {
                break;
            }
        }

        tok.nSize = (nPos > (nOffset + nTokenEnd)) ? (nPos - nOffset) : nTokenEnd;
    }

    return tok;
}

/* ---------------------------------------------------------- predicates --- */

/* _isObject: an object header "N G obj" at the start of the string, even when
 * the dictionary is glued to the keyword on the same line ("1 0 obj<<...").  */
static int pdf_is_object(const char *pString)
{
    cd_i64 nLen = (cd_i64)x_strlen(pString);
    cd_i64 i = 0;
    int nDigits = 0;
    int nField = 0;

    for (nField = 0; nField < 2; nField++) {
        while ((i < nLen) && ((pString[i] == ' ') || (pString[i] == '\t'))) {
            i++;
        }

        nDigits = 0;

        while ((i < nLen) && (pString[i] >= '0') && (pString[i] <= '9')) {
            i++;
            nDigits++;
        }

        if (nDigits == 0) {
            return 0;
        }

        if ((i >= nLen) || (!((pString[i] == ' ') || (pString[i] == '\t')))) {
            return 0;
        }
    }

    while ((i < nLen) && ((pString[i] == ' ') || (pString[i] == '\t'))) {
        i++;
    }

    if ((i + 3) > nLen) {
        return 0;
    }

    if (!((pString[i] == 'o') && (pString[i + 1] == 'b') && (pString[i + 2] == 'j'))) {
        return 0;
    }

    i += 3;

    if (i < nLen) {
        int c = pString[i];

        if (!((c == ' ') || (c == '\t') || (c == 13) || (c == 10) || (c == '<') || (c == '[') || (c == '(') || (c == '/') || (c == '%'))) {
            return 0;
        }
    }

    return 1;
}

static int pdf_is_end_object(const char *pString)
{
    /* trimmed() == "endobj" */
    cd_i64 nStart = 0;
    cd_i64 nEnd = (cd_i64)x_strlen(pString);

    while ((nStart < nEnd) && (pString[nStart] == ' ')) {
        nStart++;
    }

    while ((nEnd > nStart) && (pString[nEnd - 1] == ' ')) {
        nEnd--;
    }

    return ((nEnd - nStart) == 6) && (x_strncmp(pString + nStart, "endobj", 6) == 0);
}

static int pdf_is_xref(const char *pString)
{
    if (x_strncmp(pString, "xref", 4) != 0) {
        return 0;
    }

    return (pString[4] == 0) || (pString[4] == ' ');
}

static int pdf_is_comment(const char *pString)
{
    return (pString[0] == '%');
}

/* getObjectID: leading optional '-' then decimal digits. */
static cd_i64 pdf_object_id(const char *pString)
{
    cd_i64 n = 0;
    int bNeg = 0;
    cd_i64 i = 0;

    if (pString[i] == '-') {
        bNeg = 1;
        i++;
    }

    while ((pString[i] >= '0') && (pString[i] <= '9')) {
        cd_i64 nDigit = pString[i] - '0';

        /* Stop accumulating instead of overflowing: the id is only ever used as
         * a label, and a wrapped cd_i64 is undefined behaviour. */
        if (n > ((0x7FFFFFFFFFFFFFFFll - nDigit) / 10)) {
            break;
        }

        n = n * 10 + nDigit;
        i++;
    }

    return bNeg ? -n : n;
}

/* nth space-delimited field of a string (empty for out of range). */
static char *pdf_field(const char *pString, int nField)
{
    int n = 0;
    const char *pStart = pString;
    const char *p = pString;

    for (;;) {
        if ((*p == ' ') || (*p == 0)) {
            if (n == nField) {
                return cd_strndup(pStart, (size_t)(p - pStart));
            }

            if (*p == 0) {
                return cd_strdup("");
            }

            n++;
            pStart = p + 1;
        }

        p++;
    }
}

/* QString::toLongLong: whole string (trimmed) must be an integer. */
static cd_i64 pdf_strtoll_strict(const char *pString, int *pbOk)
{
    cd_i64 n = 0;
    int bNeg = 0;
    int bAny = 0;
    int bOverflow = 0;
    const char *p = pString;

    while (*p == ' ') {
        p++;
    }

    if ((*p == '-') || (*p == '+')) {
        bNeg = (*p == '-');
        p++;
    }

    while ((*p >= '0') && (*p <= '9')) {
        cd_i64 nDigit = *p - '0';

        /* QString::toLongLong reports failure on an out-of-range number rather
         * than wrapping; keep consuming digits so the trailing-character test
         * below still sees the whole token. */
        if (n > ((0x7FFFFFFFFFFFFFFFll - nDigit) / 10)) {
            bOverflow = 1;
        } else {
            n = n * 10 + nDigit;
        }

        bAny = 1;
        p++;
    }

    while (*p == ' ') {
        p++;
    }

    if ((!bAny) || bOverflow || (*p != 0)) {
        if (pbOk) {
            *pbOk = 0;
        }

        return 0;
    }

    if (pbOk) {
        *pbOk = 1;
    }

    return bNeg ? -n : n;
}

/* --------------------------------------------------------- object walk --- */

typedef struct {
    cd_i64 nOffset;
    cd_i64 nId;
} PdfObjRef;

typedef struct {
    cd_i64 nXrefOffset;
    cd_i64 nFooterOffset;
    int bIsXref;
    int bIsObject;
} PdfStartxref;

/* handleXpart, reduced to the dictionary token list. The reference goes on to
 * measure stream sizes; nothing here needs that, and the tokens the value
 * queries read are all captured before the stream, so parsing stops once the
 * object's dictionary/array is balanced. */
static CDVec *pdf_handle_object(const PdfIO *pIO, cd_i64 nOffset)
{
    CDVec *pParts = (CDVec *)cd_malloc(sizeof(CDVec));
    PdfTok title = pdf_read_title(pIO, nOffset, 20);

    cdvec_init(pParts);

    nOffset += title.nSize;

    if (pdf_is_object(title.pStr)) {
        int nObj = 0;
        int nCol = 0;

        for (;;) {
            PdfTok part = pdf_read_part(pIO, nOffset);
            cd_i64 nStrLen = part.nStrLen;

            cdvec_push(pParts, part.pStr);
            nOffset += part.nSize;

            if (nStrLen == 0) {
                break;
            }

            if (nStrLen == 1) {
                if (part.pStr[0] == '[') {
                    nCol++;
                } else if (part.pStr[0] == ']') {
                    nCol--;
                }
            } else if (nStrLen == 2) {
                if (part.pStr[0] == '<') {
                    nObj++;
                } else if (part.pStr[0] == '>') {
                    nObj--;
                }
            }

            if ((nObj == 0) && (nCol == 0)) {
                break;
            }
        }
    }

    cd_free(title.pStr);

    return pParts;
}

/* findObjects: linear "N 0 obj ... endobj" scan. With bDeepScan set (the
 * path taken when a startxref points at an object, i.e. a cross-reference
 * stream) a non-object token does not stop the walk; instead it skips forward
 * to the next " obj" and backs up over the "N M " that precedes it. */
static void pdf_find_objects(const PdfIO *pIO, cd_i64 nOffset, cd_i64 nSize, int bDeepScan, CDVec *pRefs)
{
    cd_i64 nEnd = 0;

    if (nSize == -1) {
        nSize = pIO->nSize - nOffset;
    }

    nEnd = nOffset + nSize;

    while (nOffset < nEnd) {
        cd_i64 nIterEntry = nOffset;
        PdfTok os = pdf_read_string(pIO, nOffset, 64);

        if (pdf_is_object(os.pStr)) {
            cd_i64 nId = pdf_object_id(os.pStr);
            cd_i64 nSearchStart = nOffset + os.nSize;
            cd_i64 nEndObj = -1;

            if (nSearchStart < nEnd) {
                cd_i64 i = nSearchStart;
                cd_i64 nStop = nEnd - 6;

                for (; i <= nStop; i++) {
                    if (x_memcmp(pIO->pData + i, "endobj", 6) == 0) {
                        nEndObj = i;

                        break;
                    }
                }
            }

            if (nEndObj != -1) {
                PdfTok osEnd = pdf_read_string(pIO, nEndObj, 32);

                if (pdf_is_end_object(osEnd.pStr)) {
                    PdfObjRef *pRef = (PdfObjRef *)cd_malloc(sizeof(PdfObjRef));

                    pRef->nOffset = nOffset;
                    pRef->nId = nId;
                    cdvec_push(pRefs, pRef);

                    nOffset = nEndObj + osEnd.nSize;
                    cd_free(osEnd.pStr);
                    cd_free(os.pStr);

                    continue;
                }

                cd_free(osEnd.pStr);
                cd_free(os.pStr);

                break;
            }

            cd_free(os.pStr);

            break;
        } else if (pdf_is_comment(os.pStr)) {
            nOffset += os.nSize;
            cd_free(os.pStr);
        } else {
            int bContinue = 0;

            cd_free(os.pStr);

            if (bDeepScan) {
                cd_i64 i = nOffset;
                cd_i64 nStop = nEnd - 4;
                cd_i64 nFound = -1;

                for (; i <= nStop; i++) {
                    if (x_memcmp(pIO->pData + i, " obj", 4) == 0) {
                        nFound = i;

                        break;
                    }
                }

                if (nFound != -1) {
                    cd_i64 nBack = nFound;

                    /* Back up over the "N M " that precedes " obj". */
                    while ((nBack > 0)) {
                        int nPrev = pIO->pData[nBack - 1];

                        if (!(((nPrev >= '0') && (nPrev <= '9')) || (nPrev == ' '))) {
                            break;
                        }

                        nBack--;
                    }

                    /* Guarantee forward progress: if the walk-back lands on (or
                     * before) the offset we already tried this iteration, resume
                     * past the matched " obj" instead of spinning. */
                    if (nBack <= nIterEntry) {
                        nBack = nFound + 4;
                    }

                    nOffset = nBack;
                    bContinue = (nOffset < nEnd);
                }
            }

            if (!bContinue) {
                break;
            }
        }
    }
}

/* getObjectsFromStartxref for a classic "xref" table. Objects come out sorted
 * by file offset (ascending), matching the reference's QMap ordering. */
static void pdf_objects_from_xref(const PdfIO *pIO, const PdfStartxref *pStart, CDVec *pRefs)
{
    cd_i64 nOffset = pStart->nXrefOffset;
    PdfTok os = pdf_read_string(pIO, nOffset, 20);

    if (!pdf_is_xref(os.pStr)) {
        cd_free(os.pStr);

        return;
    }

    nOffset += os.nSize;
    cd_free(os.pStr);

    for (;;) {
        PdfTok section = pdf_read_string(pIO, nOffset, 20);
        char *pIdField = NULL;
        char *pCountField = NULL;
        cd_i64 nId = 0;
        cd_i64 nCount = 0;
        cd_i64 i = 0;

        if (section.nStrLen == 0) {
            cd_free(section.pStr);

            break;
        }

        pIdField = pdf_field(section.pStr, 0);
        pCountField = pdf_field(section.pStr, 1);
        nId = pdf_strtoll_strict(pIdField, NULL);
        nCount = pdf_strtoll_strict(pCountField, NULL);
        cd_free(pIdField);
        cd_free(pCountField);

        nOffset += section.nSize;
        cd_free(section.pStr);

        if (nCount <= 0) {
            break;
        }

        for (i = 0; i < nCount; i++) {
            PdfTok obj = pdf_read_string(pIO, nOffset, 20);
            char *pType = NULL;

            /* No forward progress means we hit EOF: a hostile subsection count
             * (e.g. "0 99999999999") would otherwise spin for billions of no-op
             * iterations. */
            if (obj.nSize <= 0) {
                cd_free(obj.pStr);

                break;
            }

            pType = pdf_field(obj.pStr, 2);

            if (x_strcmp(pType, "n") == 0) {
                char *pOff = pdf_field(obj.pStr, 0);
                cd_i64 nObjOffset = pdf_strtoll_strict(pOff, NULL);

                if ((nObjOffset > 0) && (nObjOffset < pIO->nSize)) {
                    PdfObjRef *pRef = (PdfObjRef *)cd_malloc(sizeof(PdfObjRef));

                    pRef->nOffset = nObjOffset;
                    pRef->nId = nId + i;
                    cdvec_push(pRefs, pRef);
                }

                cd_free(pOff);
            }

            cd_free(pType);
            nOffset += obj.nSize;
            cd_free(obj.pStr);
        }
    }

    /* Sort the collected offsets ascending (insertion sort; object counts are
     * modest and this keeps behaviour obvious). */
    {
        size_t a = 0;

        for (a = 1; a < pRefs->nSize; a++) {
            PdfObjRef *pKey = (PdfObjRef *)pRefs->ppData[a];
            size_t b = a;

            while ((b > 0) && (((PdfObjRef *)pRefs->ppData[b - 1])->nOffset > pKey->nOffset)) {
                pRefs->ppData[b] = pRefs->ppData[b - 1];
                b--;
            }

            pRefs->ppData[b] = pKey;
        }
    }
}

/* findStartxrefs: collect the "startxref -> xref/object" chains. */
static void pdf_find_startxrefs(const PdfIO *pIO, CDVec *pStarts)
{
    cd_i64 nOffset = 0;

    for (;;) {
        cd_i64 nStartXref = -1;
        cd_i64 nCurrent = 0;
        PdfTok osStartXref;
        PdfTok osOffset;
        PdfTok osHref;
        cd_i64 nTarget = 0;
        int bIsXref = 0;
        int bIsObject = 0;

        {
            cd_i64 i = nOffset;
            cd_i64 nStop = pIO->nSize - 9;

            for (; i <= nStop; i++) {
                if (x_memcmp(pIO->pData + i, "startxref", 9) == 0) {
                    nStartXref = i;

                    break;
                }
            }
        }

        if (nStartXref == -1) {
            break;
        }

        nCurrent = nStartXref;
        osStartXref = pdf_read_string(pIO, nCurrent, 20);
        nCurrent += osStartXref.nSize;
        cd_free(osStartXref.pStr);

        osOffset = pdf_read_string(pIO, nCurrent, 20);
        nTarget = pdf_strtoll_strict(osOffset.pStr, NULL);

        osHref = pdf_read_string(pIO, nTarget, 20);
        bIsXref = pdf_is_xref(osHref.pStr);
        bIsObject = pdf_is_object(osHref.pStr);
        cd_free(osHref.pStr);

        if ((bIsXref || bIsObject) && (nTarget < nCurrent)) {
            PdfTok osEnd;

            nCurrent += osOffset.nSize;
            osEnd = pdf_read_string(pIO, nCurrent, 20);

            if (x_strncmp(osEnd.pStr, "%%EOF", 5) == 0) {
                PdfStartxref *pRec = (PdfStartxref *)cd_malloc(sizeof(PdfStartxref));
                int bStopChain = 0;

                pRec->nXrefOffset = nTarget;
                pRec->nFooterOffset = nStartXref;
                pRec->bIsObject = bIsObject;
                pRec->bIsXref = bIsXref;
                cdvec_push(pStarts, pRec);

                nCurrent += 5;

                if ((nCurrent < pIO->nSize) && (pIO->pData[nCurrent] == 13)) {
                    nCurrent++;
                }

                if ((nCurrent < pIO->nSize) && (pIO->pData[nCurrent] == 10)) {
                    nCurrent++;
                }

                if (osEnd.nStrLen != 5) {
                    bStopChain = 1;
                } else {
                    /* An incremental update appends another section; keep
                     * chaining only while what follows still looks like one. */
                    PdfTok osAppend = pdf_read_string(pIO, nCurrent, 20);

                    if ((!pdf_is_object(osAppend.pStr)) && (!pdf_is_comment(osAppend.pStr)) && (!pdf_is_xref(osAppend.pStr))) {
                        bStopChain = 1;
                    }

                    cd_free(osAppend.pStr);
                }

                cd_free(osEnd.pStr);
                cd_free(osOffset.pStr);

                if (bStopChain) {
                    break;
                }

                nOffset = nStartXref + 10;

                continue;
            }

            cd_free(osEnd.pStr);
        }

        cd_free(osOffset.pStr);
        nOffset = nStartXref + 10;
    }
}

/* ---------------------------------------------------------- top level --- */

static void pdf_collect_object_refs(const PdfIO *pIO, CDVec *pRefs)
{
    CDVec starts;
    size_t i = 0;

    cdvec_init(&starts);
    pdf_find_startxrefs(pIO, &starts);

    if (starts.nSize > 0) {
        for (i = 0; i < starts.nSize; i++) {
            PdfStartxref *pStart = (PdfStartxref *)starts.ppData[i];

            if (pStart->bIsXref) {
                pdf_objects_from_xref(pIO, pStart, pRefs);
            } else if (pStart->bIsObject) {
                pdf_find_objects(pIO, 0, pStart->nFooterOffset, 1, pRefs);
            }
        }
    } else {
        pdf_find_objects(pIO, 0, -1, 0, pRefs);
    }

    for (i = 0; i < starts.nSize; i++) {
        cd_free(starts.ppData[i]);
    }

    cdvec_free(&starts);
}

int xpdf_parse(XBFile *pFile, XPDF *pPdf)
{
    PdfIO io;
    CDVec refs;
    size_t i = 0;

    x_memset(pPdf, 0, sizeof(XPDF));

    if (pFile == NULL) {
        return 0;
    }

    pPdf->pFile = pFile;
    cdvec_init(&pPdf->vecObjects);
    cdvec_init(&pPdf->vecRefs);

    io.pData = pFile->pData;
    io.nSize = pFile->nSize;

    cdvec_init(&refs);
    pdf_collect_object_refs(&io, &refs);

    for (i = 0; i < refs.nSize; i++) {
        PdfObjRef *pRef = (PdfObjRef *)refs.ppData[i];
        CDVec *pParts = pdf_handle_object(&io, pRef->nOffset);

        cdvec_push(&pPdf->vecObjects, pParts);
        cdvec_push(&pPdf->vecRefs, pRef);
    }

    cdvec_free(&refs);

    pPdf->bValid = 1;

    return 1;
}

void xpdf_free(XPDF *pPdf)
{
    size_t i = 0;
    size_t j = 0;

    for (i = 0; i < pPdf->vecObjects.nSize; i++) {
        CDVec *pParts = (CDVec *)pPdf->vecObjects.ppData[i];

        for (j = 0; j < pParts->nSize; j++) {
            cd_free(pParts->ppData[j]);
        }

        cdvec_free(pParts);
        cd_free(pParts);
    }

    for (i = 0; i < pPdf->vecRefs.nSize; i++) {
        cd_free(pPdf->vecRefs.ppData[i]);
    }

    cdvec_free(&pPdf->vecRefs);
    cdvec_free(&pPdf->vecObjects);
    pPdf->bValid = 0;
}

char *xpdf_version(XPDF *pPdf)
{
    PdfIO io;
    PdfTok tok;
    char *pResult = NULL;

    io.pData = pPdf->pFile->pData;
    io.nSize = pPdf->pFile->nSize;

    tok = pdf_read_string(&io, 5, 3);
    pResult = tok.pStr;

    return pResult;
}

/* _parseValue's classification, reduced to what the queries need: is this
 * token a string object, and what is its unwrapped text? */
static int pdf_value_is_string(const char *pToken)
{
    cd_i64 nLen = (cd_i64)x_strlen(pToken);

    return (nLen >= 2) && (pToken[0] == '(') && (pToken[nLen - 1] == ')');
}

static int pdf_value_is_pure_int(const char *pToken)
{
    int bOk = 0;
    cd_i64 n = pdf_strtoll_strict(pToken, &bOk);

    return bOk && (n != 0);
}

/* The text _parseValue would expose as var.toString(): strings lose their
 * parentheses, everything else is the token verbatim. */
static char *pdf_value_text(const char *pToken)
{
    if (pdf_value_is_string(pToken)) {
        cd_i64 nLen = (cd_i64)x_strlen(pToken);

        return cd_strndup(pToken + 1, (size_t)(nLen - 2));
    }

    return cd_strdup(pToken);
}

void xpdf_values_by_key(XPDF *pPdf, const char *pKey, int bStringsOnly, int nPartLimit, CDVec *pOut)
{
    size_t i = 0;

    for (i = 0; i < pPdf->vecObjects.nSize; i++) {
        CDVec *pParts = (CDVec *)pPdf->vecObjects.ppData[i];
        size_t nLimit = pParts->nSize;
        size_t j = 0;

        if ((nPartLimit >= 0) && ((size_t)nPartLimit < nLimit)) {
            nLimit = (size_t)nPartLimit;
        }

        for (j = 0; (j + 1) < nLimit; j++) {
            const char *pPart = (const char *)pParts->ppData[j];

            if (x_strcmp(pPart, pKey) == 0) {
                const char *pValueToken = (const char *)pParts->ppData[j + 1];

                /* getStringValuesByKey keeps only string objects; the value
                 * must additionally not be an empty (null) parse. Pure zero
                 * integers parse to a null variant in the reference and are
                 * dropped. */
                if (bStringsOnly && (!pdf_value_is_string(pValueToken))) {
                    continue;
                }

                {
                    char *pText = pdf_value_text(pValueToken);
                    int bDuplicate = 0;
                    size_t k = 0;

                    if ((pText[0] == 0) && (!pdf_value_is_pure_int(pValueToken)) && (!pdf_value_is_string(pValueToken))) {
                        /* Empty non-string value: treated as null, skip. */
                        cd_free(pText);

                        continue;
                    }

                    for (k = 0; k < pOut->nSize; k++) {
                        if (x_strcmp((const char *)pOut->ppData[k], pText) == 0) {
                            bDuplicate = 1;

                            break;
                        }
                    }

                    if (bDuplicate) {
                        cd_free(pText);
                    } else {
                        cdvec_push(pOut, pText);
                    }
                }
            }
        }
    }
}

char *xpdf_filters(XPDF *pPdf)
{
    CDVec values;
    CDBuf out;
    size_t i = 0;

    cdvec_init(&values);
    cdbuf_init(&out);

    /* getFilters uses getParts(100). */
    xpdf_values_by_key(pPdf, "/Filter", 0, 100, &values);

    for (i = 0; i < values.nSize; i++) {
        const char *pValue = (const char *)values.ppData[i];

        if (pValue[0] != 0) {
            if (out.nSize > 0) {
                cdbuf_append_str(&out, ", ");
            }

            cdbuf_append_str(&out, pValue);
        }

        cd_free(values.ppData[i]);
    }

    cdvec_free(&values);

    return cdbuf_detach(&out, NULL);
}

/* ---------------------------------------------------------------- info --- */

/* getInfo reads the document with getParts(256), so every query below sees at
 * most the first 256 tokens of an object. */
#define PDF_INFO_PART_LIMIT 256

/* The keys getMetaInfoString reports, in its order. */
static const char *g_pPdfMetaKeys[8] = {"/Title", "/Author", "/Subject", "/Keywords", "/Creator", "/Producer", "/CreationDate", "/ModDate"};

/* _resolveFilterList: the first "/Filter" in an object wins, and its value is
 * either a single name or a "[ /A /B ]" array. */
static void pdf_resolve_filter_list(const CDVec *pParts, size_t nCount, CDVec *pOut)
{
    size_t i = 0;

    for (i = 0; (i + 1) < nCount; i++) {
        if (x_strcmp((const char *)pParts->ppData[i], "/Filter") == 0) {
            const char *pNext = (const char *)pParts->ppData[i + 1];

            if (x_strcmp(pNext, "[") == 0) {
                size_t q = 0;

                for (q = i + 2; q < nCount; q++) {
                    const char *pToken = (const char *)pParts->ppData[q];

                    if (x_strcmp(pToken, "]") == 0) {
                        break;
                    }

                    if (pToken[0] == '/') {
                        cdvec_push(pOut, cd_strdup(pToken));
                    }
                }
            } else if (pNext[0] == '/') {
                cdvec_push(pOut, cd_strdup(pNext));
            }

            break;
        }
    }
}

/* QStringList::sort() over ASCII names. */
static int pdf_compare_names(const void *pLeft, const void *pRight)
{
    return x_strcmp(*(const char *const *)pLeft, *(const char *const *)pRight);
}

/* The number of tokens getParts(256) would have kept for an object. */
static size_t pdf_info_part_count(const CDVec *pParts)
{
    size_t nCount = pParts->nSize;

    if (nCount > (size_t)PDF_INFO_PART_LIMIT) {
        nCount = (size_t)PDF_INFO_PART_LIMIT;
    }

    return nCount;
}

/* getInfo's first line: the unique filters used across the document, sorted
 * and joined with ", ". Heap string, empty when the set is empty. */
static char *pdf_info_filters(XPDF *pPdf)
{
    CDVec found;
    CDBuf out;
    size_t i = 0;

    cdvec_init(&found);
    cdbuf_init(&out);

    for (i = 0; i < pPdf->vecObjects.nSize; i++) {
        const CDVec *pParts = (const CDVec *)pPdf->vecObjects.ppData[i];
        CDVec resolved;
        size_t f = 0;

        cdvec_init(&resolved);
        pdf_resolve_filter_list(pParts, pdf_info_part_count(pParts), &resolved);

        for (f = 0; f < resolved.nSize; f++) {
            char *pName = (char *)resolved.ppData[f];
            int bDuplicate = 0;
            size_t k = 0;

            for (k = 0; k < found.nSize; k++) {
                if (x_strcmp((const char *)found.ppData[k], pName) == 0) {
                    bDuplicate = 1;

                    break;
                }
            }

            if (bDuplicate) {
                cd_free(pName);
            } else {
                cdvec_push(&found, pName);
            }
        }

        cdvec_free(&resolved);
    }

    if (found.nSize > 1) {
        x_qsort(found.ppData, found.nSize, sizeof(void *), pdf_compare_names);
    }

    for (i = 0; i < found.nSize; i++) {
        if (i > 0) {
            cdbuf_append_str(&out, ", ");
        }

        cdbuf_append_str(&out, (const char *)found.ppData[i]);
        cd_free(found.ppData[i]);
    }

    cdvec_free(&found);

    return cdbuf_detach(&out, NULL);
}

/* QString::trimmed's whitespace, over the bytes a PDF date can hold. */
static int pdf_is_trim_space(int c)
{
    return (c == ' ') || ((c >= 9) && (c <= 13));
}

/* _isDateTime: "(D:...)", at least 18 characters long. */
static int pdf_value_is_datetime(const char *pToken)
{
    cd_i64 nLen = (cd_i64)x_strlen(pToken);

    return (nLen >= 18) && (x_strncmp(pToken, "(D:", 3) == 0) && (pToken[nLen - 1] == ')');
}

/* _isHex: "<...>", and not the "<<" dictionary opener. */
static int pdf_value_is_hex(const char *pToken)
{
    cd_i64 nLen = (cd_i64)x_strlen(pToken);

    return (nLen >= 2) && (pToken[0] == '<') && (pToken[nLen - 1] == '>') && (pToken[0] != pToken[1]);
}

/* QString::toInt() over one timezone field: 0 unless the whole field is an
 * integer. */
static int pdf_field_toint(const char *pString, size_t nStart, size_t nLen)
{
    char sBuffer[8];
    int bOk = 0;
    cd_i64 nValue = 0;
    size_t i = 0;

    for (i = 0; (i < nLen) && ((i + 1) < sizeof(sBuffer)); i++) {
        sBuffer[i] = pString[nStart + i];
    }

    sBuffer[i] = 0;
    nValue = pdf_strtoll_strict(sBuffer, &bOk);

    return bOk ? (int)nValue : 0;
}

static int pdf_is_leap_year(int nYear)
{
    return ((nYear % 4) == 0) && (((nYear % 100) != 0) || ((nYear % 400) == 0));
}

/* QDate::isValid for an already clamped month/day: there is no year 0, and the
 * day must exist in the month. */
static int pdf_date_is_valid(int nYear, int nMonth, int nDay)
{
    static const int nDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int nLimit = nDays[nMonth - 1];

    if ((nMonth == 2) && pdf_is_leap_year(nYear)) {
        nLimit = 29;
    }

    return (nYear != 0) && (nDay <= nLimit);
}

/* _getDateTime followed by QVariant::toString: a "D:YYYYMMDDHHmmSSOHH'mm'"
 * date rendered as Qt::ISODateWithMs. Empty when the date does not parse. */
static char *pdf_datetime_text(const char *pToken)
{
    CDBuf out;
    const char *pColon = x_strchr(pToken, ':');
    size_t nBegin = 0;
    size_t nEnd = 0;
    size_t nTzPos = 0;
    int bHasTz = 0;
    int nTzSign = 0;
    int nTzHour = 0;
    int nTzMin = 0;
    int nOffset = 0;
    char sClean[16];
    size_t nClean = 0;
    size_t i = 0;
    int nYear = 0;
    int nMonth = 1;
    int nDay = 1;
    int nHour = 0;
    int nMin = 0;
    int nSec = 0;

    cdbuf_init(&out);

    if (pColon == NULL) {
        return cdbuf_detach(&out, NULL);
    }

    /* section(':', 1, -1).section(')', 0, 0).trimmed() */
    nBegin = (size_t)(pColon - pToken) + 1;
    nEnd = nBegin;

    while ((pToken[nEnd] != 0) && (pToken[nEnd] != ')')) {
        nEnd++;
    }

    while ((nBegin < nEnd) && (pdf_is_trim_space(pToken[nBegin]))) {
        nBegin++;
    }

    while ((nEnd > nBegin) && (pdf_is_trim_space(pToken[nEnd - 1]))) {
        nEnd--;
    }

    for (i = nBegin; i < nEnd; i++) {
        char c = pToken[i];

        if ((c == '+') || (c == '-') || (c == 'Z') || (c == 'z')) {
            nTzPos = i;
            bHasTz = 1;

            break;
        }
    }

    if (bHasTz) {
        char sTz[8];
        size_t nTz = 0;

        if (pToken[nTzPos] == '+') {
            nTzSign = 1;
        } else if (pToken[nTzPos] == '-') {
            nTzSign = -1;
        }

        for (i = nTzPos + 1; (i < nEnd) && ((nTz + 1) < sizeof(sTz)); i++) {
            if (pToken[i] != '\'') {
                sTz[nTz] = pToken[i];
                nTz++;
            }
        }

        sTz[nTz] = 0;

        if (nTz >= 2) {
            nTzHour = pdf_field_toint(sTz, 0, 2);
        }

        if (nTz >= 4) {
            nTzMin = pdf_field_toint(sTz, 2, 2);
        }

        nEnd = nTzPos;
    }

    for (i = nBegin; (i < nEnd) && (nClean < 14); i++) {
        if ((pToken[i] >= '0') && (pToken[i] <= '9')) {
            sClean[nClean] = pToken[i];
            nClean++;
        }
    }

    sClean[nClean] = 0;

    if (nClean < 4) {
        return cdbuf_detach(&out, NULL);
    }

    nYear = pdf_field_toint(sClean, 0, 4);

    if (nClean >= 6) {
        nMonth = pdf_field_toint(sClean, 4, 2);
    }

    if (nClean >= 8) {
        nDay = pdf_field_toint(sClean, 6, 2);
    }

    if (nClean >= 10) {
        nHour = pdf_field_toint(sClean, 8, 2);
    }

    if (nClean >= 12) {
        nMin = pdf_field_toint(sClean, 10, 2);
    }

    if (nClean >= 14) {
        nSec = pdf_field_toint(sClean, 12, 2);
    }

    if (nMonth < 1) {
        nMonth = 1;
    }

    if (nMonth > 12) {
        nMonth = 12;
    }

    if (nDay < 1) {
        nDay = 1;
    }

    if (nDay > 31) {
        nDay = 31;
    }

    if (nHour > 23) {
        nHour = 23;
    }

    if (nMin > 59) {
        nMin = 59;
    }

    if (nSec > 59) {
        nSec = 59;
    }

    if (!pdf_date_is_valid(nYear, nMonth, nDay)) {
        return cdbuf_detach(&out, NULL);
    }

    /* QDateTime carries a fixed-offset QTimeZone, which is invalid - and with
     * it the whole date - beyond +/-14 hours. */
    nOffset = nTzSign * ((nTzHour * 3600) + (nTzMin * 60));

    if ((nOffset < -14 * 3600) || (nOffset > 14 * 3600)) {
        return cdbuf_detach(&out, NULL);
    }

    cdbuf_appendf(&out, "%04d-%02d-%02dT%02d:%02d:%02d.000", nYear, nMonth, nDay, nHour, nMin, nSec);
    cdbuf_appendf(&out, "%c%02d:%02d", (nOffset >= 0) ? '+' : '-', (nOffset < 0 ? -nOffset : nOffset) / 3600, ((nOffset < 0 ? -nOffset : nOffset) / 60) % 60);

    return cdbuf_detach(&out, NULL);
}

/* _parseValue followed by QVariant::toString: the text getMetaInfoString
 * renders for a value token. */
static char *pdf_info_value_text(const char *pToken)
{
    int bOk = 0;
    cd_i64 nValue = pdf_strtoll_strict(pToken, &bOk);

    if (bOk) {
        CDBuf out;

        cdbuf_init(&out);
        cdbuf_appendf(&out, "%lld", (long long)nValue);

        return cdbuf_detach(&out, NULL);
    }

    if (pdf_value_is_datetime(pToken)) {
        return pdf_datetime_text(pToken);
    }

    if (pdf_value_is_string(pToken) || pdf_value_is_hex(pToken)) {
        cd_i64 nLen = (cd_i64)x_strlen(pToken);

        return cd_strndup(pToken + 1, (size_t)(nLen - 2));
    }

    return cd_strdup(pToken);
}

/* getLinearizedInfoString: the first object carrying /Linearized, plus a note
 * when the /L it declares disagrees with the real file size. Appends one heap
 * line to pLines. */
static void pdf_info_linearized(XPDF *pPdf, CDVec *pLines)
{
    size_t i = 0;

    for (i = 0; i < pPdf->vecObjects.nSize; i++) {
        const CDVec *pParts = (const CDVec *)pPdf->vecObjects.ppData[i];
        size_t nCount = pdf_info_part_count(pParts);
        size_t j = 0;
        int bLinearized = 0;
        CDBuf line;
        cd_i64 nDeclared = 0;

        for (j = 0; j < nCount; j++) {
            if (x_strcmp((const char *)pParts->ppData[j], "/Linearized") == 0) {
                bLinearized = 1;

                break;
            }
        }

        if (!bLinearized) {
            continue;
        }

        /* getFirstStringValueByKey keeps the LAST value that parses. */
        for (j = 0; (j + 1) < nCount; j++) {
            if (x_strcmp((const char *)pParts->ppData[j], "/L") == 0) {
                char *pText = pdf_info_value_text((const char *)pParts->ppData[j + 1]);
                int bOk = 0;
                cd_i64 nValue = pdf_strtoll_strict(pText, &bOk);

                if (pText[0] != 0) {
                    nDeclared = bOk ? nValue : 0;
                }

                cd_free(pText);
            }
        }

        cdbuf_init(&line);
        cdbuf_append_str(&line, "Linearized");

        if ((nDeclared > 0) && (nDeclared != pPdf->pFile->nSize)) {
            cdbuf_appendf(&line, " (declared L=%lld, actual=%lld)", (long long)nDeclared, (long long)pPdf->pFile->nSize);
        }

        cdvec_push(pLines, cdbuf_detach(&line, NULL));

        break;
    }
}

/* ------------------------------------------------------ object streams --- */

/* An /ObjStm carries whole object dictionaries compressed inside its stream,
 * so their keys are invisible to the token walk above. getSuspiciousInfoString
 * decompresses those bodies and looks for the same trigger names in the
 * decoded bytes, which is why its list is a superset of the dictionary-only
 * one. Everything below exists to reproduce that step.                      */

/* Ceilings on decompressed output. This is hostile input: a few hundred bytes
 * of DEFLATE can claim to expand to gigabytes, and a file can carry thousands
 * of such objects, so bound both one stream and the document as a whole. Real
 * object streams decode to kilobytes, so neither cap is ever reached by a
 * genuine file. The reference bounds each stream at 100 MiB and relies on Qt's
 * incremental device; the tighter numbers here keep the triage cost of a bomb
 * comparable to a normal scan. */
#define PDF_OBJSTM_DECODE_LIMIT (16 * 1024 * 1024)
#define PDF_OBJSTM_TOTAL_LIMIT (64 * 1024 * 1024)

/* What the reference refuses to even read: a raw stream larger than its decode
 * output limit (PDF_DEFAULT_DECODE_OUTPUT_LIMIT). */
#define PDF_OBJSTM_RAW_LIMIT (100 * 1024 * 1024)

/* Ceiling on the "endstream" repair scan, summed over the whole document. That
 * scan runs from a stream's start to wherever the keyword turns up, so an
 * object whose stream has no "endstream" at all costs one pass over the rest of
 * the file - and a document made of thousands of such objects costs that many
 * passes. A well-formed stream finds its keyword within its own body, so the
 * document total is bounded by the file size and this budget is unreachable;
 * only the degenerate shape spends it. Without the cap a 20 MB file of 4000
 * headless /ObjStm objects takes 83 s. */
#define PDF_OBJSTM_SCAN_LIMIT (64 * 1024 * 1024)

/* find_ansiString: the first occurrence of pNeedle fully inside
 * [nOffset, nOffset + nSize), or -1. */
static cd_i64 pdf_find_ansi(const PdfIO *pIO, cd_i64 nOffset, cd_i64 nSize, const char *pNeedle)
{
    cd_i64 nLen = (cd_i64)x_strlen(pNeedle);
    cd_i64 nEnd = 0;
    cd_i64 i = 0;

    if ((nOffset < 0) || (nOffset >= pIO->nSize) || (nSize <= 0)) {
        return -1;
    }

    if (nSize > (pIO->nSize - nOffset)) {
        nSize = pIO->nSize - nOffset;
    }

    nEnd = nOffset + nSize - nLen;

    for (i = nOffset; i <= nEnd; i++) {
        if (x_memcmp(pIO->pData + i, pNeedle, (size_t)nLen) == 0) {
            return i;
        }
    }

    return -1;
}

/* _isIndirectRef: "N G R" fused into a single token. */
static int pdf_value_is_indirect_ref(const char *pToken)
{
    char *pId = NULL;
    char *pGen = NULL;
    char *pKeyword = NULL;
    int bOkId = 0;
    int bOkGen = 0;
    int bResult = 0;

    if ((pToken[0] < '0') || (pToken[0] > '9')) {
        return 0;
    }

    pKeyword = pdf_field(pToken, 2);

    if (x_strcmp(pKeyword, "R") == 0) {
        pId = pdf_field(pToken, 0);
        pGen = pdf_field(pToken, 1);
        pdf_strtoll_strict(pId, &bOkId);
        pdf_strtoll_strict(pGen, &bOkGen);
        bResult = bOkId && bOkGen;
        cd_free(pId);
        cd_free(pGen);
    }

    cd_free(pKeyword);

    return bResult;
}

/* resolveObjectOffset, fast path only: the object-number index the structural
 * walk already built. The reference falls back to a bounded header scan when
 * the index misses; here a miss simply leaves the length unresolved, which the
 * "endstream" repair below recovers from anyway. A later entry wins, matching
 * the reference's QMap being filled in enumeration order. */
static cd_i64 pdf_resolve_object_offset(const CDVec *pRefs, cd_i64 nId)
{
    cd_i64 nResult = -1;
    size_t i = 0;

    for (i = 0; i < pRefs->nSize; i++) {
        const PdfObjRef *pRef = (const PdfObjRef *)pRefs->ppData[i];

        if (pRef->nId == nId) {
            nResult = pRef->nOffset;
        }
    }

    return nResult;
}

/* handleXpart(bResolveStreams) reduced to the first stream of one object: walk
 * the object's dictionary for /Length, then size the body from it - validated
 * against the real "endstream" keyword and repaired from it when the declared
 * length does not line up. Returns 1 and fills the body range, 0 when the
 * object carries no usable stream. */
static int pdf_object_stream(const PdfIO *pIO, cd_i64 nObjOffset, const CDVec *pRefs, cd_i64 *pnScanBudget, cd_i64 *pnOffset, cd_i64 *pnSize)
{
    CDVec parts;
    char *pLength = cd_strdup("");
    cd_i64 nOffset = nObjOffset;
    int bLength = 0;
    int bStop = 0;
    int bResult = 0;
    size_t i = 0;

    cdvec_init(&parts);

    while (!bStop) {
        PdfTok title = pdf_read_title(pIO, nOffset, 20);

        nOffset += title.nSize;

        if (pdf_is_object(title.pStr)) {
            int nObj = 0;
            int nCol = 0;

            for (;;) {
                PdfTok part = pdf_read_part(pIO, nOffset);
                cd_i64 nStrLen = part.nStrLen;

                cdvec_push(&parts, part.pStr);
                nOffset += part.nSize;

                if (nStrLen == 0) {
                    break;
                }

                if (nStrLen == 1) {
                    if (part.pStr[0] == '[') {
                        nCol++;
                    } else if (part.pStr[0] == ']') {
                        nCol--;
                    }
                } else if (nStrLen == 2) {
                    if (part.pStr[0] == '<') {
                        nObj++;
                    } else if (part.pStr[0] == '>') {
                        nObj--;
                    }
                }

                /* The token after /Length is the length, whatever its shape:
                 * a direct number or a fused indirect "N G R". */
                if (bLength) {
                    bLength = 0;
                    cd_free(pLength);
                    pLength = cd_strdup(part.pStr);
                } else if (x_strcmp(part.pStr, "/Length") == 0) {
                    bLength = 1;
                }

                if ((nObj == 0) && (nCol == 0)) {
                    break;
                }
            }
        } else if (x_strcmp(title.pStr, "stream") == 0) {
            cd_i64 nStreamOffset = nOffset;
            cd_i64 nStreamSize = 0;
            int bHasSize = 0;
            int bIndirect = 0;

            if (pdf_value_is_indirect_ref(pLength)) {
                bIndirect = 1;
            } else {
                char *pFirst = pdf_field(pLength, 0);
                int bOk = 0;
                cd_i64 nToken = pdf_strtoll_strict(pFirst, &bOk);

                cd_free(pFirst);

                /* The tokeniser only fuses a generation-0 reference, so
                 * "/Length 12 1 R" arrives as three tokens; recognise that
                 * split form by looking the number back up in the dictionary. */
                if (bOk) {
                    CDBuf number;
                    char *pNumber = NULL;

                    cdbuf_init(&number);
                    cdbuf_appendf(&number, "%lld", (long long)nToken);
                    pNumber = cdbuf_detach(&number, NULL);

                    for (i = 0; (i + 2) < parts.nSize; i++) {
                        if ((x_strcmp((const char *)parts.ppData[i], pNumber) == 0) && (x_strcmp((const char *)parts.ppData[i + 2], "R") == 0)) {
                            if ((i > 0) && (x_strcmp((const char *)parts.ppData[i - 1], "/Length") == 0)) {
                                CDBuf ref;

                                cdbuf_init(&ref);
                                cdbuf_append_str(&ref, (const char *)parts.ppData[i]);
                                cdbuf_append_ch(&ref, ' ');
                                cdbuf_append_str(&ref, (const char *)parts.ppData[i + 1]);
                                cdbuf_append_str(&ref, " R");
                                cd_free(pLength);
                                pLength = cdbuf_detach(&ref, NULL);
                                bIndirect = 1;

                                break;
                            }
                        }
                    }

                    cd_free(pNumber);
                }
            }

            if (!bIndirect) {
                int bOk = 0;
                cd_i64 nValue = pdf_strtoll_strict(pLength, &bOk);

                if (bOk && (nValue >= 0)) {
                    nStreamSize = nValue;
                    bHasSize = 1;
                }
            } else {
                char *pKeyword = pdf_field(pLength, 2);

                if (x_strcmp(pKeyword, "R") == 0) {
                    char *pId = pdf_field(pLength, 0);
                    cd_i64 nRefId = pdf_object_id(pId);
                    cd_i64 nRefOffset = pdf_resolve_object_offset(pRefs, nRefId);

                    cd_free(pId);

                    if (nRefOffset != -1) {
                        PdfTok header = pdf_read_string(pIO, nRefOffset, 64);
                        PdfTok value = pdf_read_part(pIO, nRefOffset + header.nSize);
                        int bOk = 0;
                        cd_i64 nValue = pdf_strtoll_strict(value.pStr, &bOk);

                        if (bOk && (nValue >= 0)) {
                            nStreamSize = nValue;
                            bHasSize = 1;
                        }

                        cd_free(header.pStr);
                        cd_free(value.pStr);
                    }
                }

                cd_free(pKeyword);
            }

            /* A declared length only counts when "endstream" really follows it. */
            if (bHasSize) {
                if (nStreamSize > (pIO->nSize - nStreamOffset)) {
                    bHasSize = 0;
                } else {
                    cd_i64 nProbe = nStreamOffset + nStreamSize;
                    cd_i64 nWindow = pIO->nSize - nProbe;

                    if (nWindow > 40) {
                        nWindow = 40;
                    }

                    if ((nWindow <= 0) || (pdf_find_ansi(pIO, nProbe, nWindow, "endstream") == -1)) {
                        bHasSize = 0;
                    }
                }
            }

            if (!bHasSize) {
                cd_i64 nScan = pIO->nSize - nStreamOffset;
                cd_i64 nEndStream = -1;

                if (nScan > *pnScanBudget) {
                    nScan = *pnScanBudget;
                }

                if (nScan > 0) {
                    nEndStream = pdf_find_ansi(pIO, nStreamOffset, nScan, "endstream");
                    /* Charge only the bytes the scan actually walked. */
                    *pnScanBudget -= (nEndStream != -1) ? (nEndStream - nStreamOffset) : nScan;
                }

                if (nEndStream != -1) {
                    cd_i64 nBodyEnd = nEndStream;

                    if ((nBodyEnd > nStreamOffset) && (pIO->pData[nBodyEnd - 1] == 10)) {
                        nBodyEnd--;
                    }

                    if ((nBodyEnd > nStreamOffset) && (pIO->pData[nBodyEnd - 1] == 13)) {
                        nBodyEnd--;
                    }

                    nStreamSize = nBodyEnd - nStreamOffset;
                    bHasSize = 1;
                }
            }

            if (bHasSize) {
                *pnOffset = nStreamOffset;
                *pnSize = nStreamSize;
                bResult = 1;
            }

            bStop = 1;
        } else if (x_strcmp(title.pStr, "endstream") == 0) {
            /* Keep walking: the next line is the object's own end. */
        } else if (pdf_is_end_object(title.pStr) || (title.pStr[0] == 0)) {
            bStop = 1;
        }

        cd_free(title.pStr);
    }

    for (i = 0; i < parts.nSize; i++) {
        cd_free(parts.ppData[i]);
    }

    cdvec_free(&parts);
    cd_free(pLength);

    return bResult;
}

/* pdfFilterToHandleMethod, reduced to what this tree can decode. The reference
 * maps only the LAST name of the chain and feeds that decoder the raw stream,
 * so a cascade such as [/ASCII85Decode /FlateDecode] inflates bytes that are
 * still ASCII85 and simply fails; matching that quirk is what keeps the two
 * lists identical. Returns 1 for FlateDecode, 0 for HANDLE_METHOD_STORE (an
 * image codec, /Crypt, anything unknown - the raw bytes are scanned as they
 * are) and -1 for a filter the reference decodes but this port has no decoder
 * for, where nothing can be scanned. */
static int pdf_objstm_method(const char *pFilter)
{
    if (x_strcmp(pFilter, "/FlateDecode") == 0) {
        return 1;
    }

    if ((x_strcmp(pFilter, "/LZWDecode") == 0) || (x_strcmp(pFilter, "/ASCII85Decode") == 0) || (x_strcmp(pFilter, "/ASCIIHexDecode") == 0) ||
        (x_strcmp(pFilter, "/RunLengthDecode") == 0)) {
        return -1;
    }

    return 0;
}

/* FlateDecode is zlib-wrapped (RFC 1950) and inflate_raw wants the bare
 * DEFLATE stream, so validate and step over the two header bytes. A wrong
 * header is what the reference's zlib stage rejects outright, leaving nothing
 * to scan. Data that goes bad after a good header still leaves the bytes
 * decoded so far in pOut, which is exactly what the reference scans: it
 * ignores the decoder's return value and reads the output buffer either way. */
static void pdf_inflate_zlib(const unsigned char *pData, cd_i64 nSize, cd_i64 nMax, CDBuf *pOut)
{
    unsigned int nCmf = 0;
    unsigned int nFlg = 0;

    if (nSize < 2) {
        return;
    }

    nCmf = pData[0];
    nFlg = pData[1];

    if (((nCmf & 0x0F) != 8) || ((nCmf >> 4) > 7) || ((((nCmf << 8) | nFlg) % 31) != 0) || ((nFlg & 0x20) != 0)) {
        return;
    }

    inflate_raw(pData + 2, (size_t)(nSize - 2), 0, (size_t)nMax, pOut);
}

/* QList::contains over the first nCount tokens of an object. */
static int pdf_parts_contain(const CDVec *pParts, size_t nCount, const char *pToken)
{
    size_t i = 0;

    for (i = 0; i < nCount; i++) {
        if (x_strcmp((const char *)pParts->ppData[i], pToken) == 0) {
            return 1;
        }
    }

    return 0;
}

/* QByteArray::contains over decoded stream bytes, which are not NUL-free. */
static int pdf_bytes_contain(const unsigned char *pData, cd_i64 nSize, const char *pNeedle)
{
    cd_i64 nLen = (cd_i64)x_strlen(pNeedle);
    cd_i64 i = 0;

    for (i = 0; (i + nLen) <= nSize; i++) {
        if (x_memcmp(pData + i, pNeedle, (size_t)nLen) == 0) {
            return 1;
        }
    }

    return 0;
}

/* isEncryptedButLocked: this port has no decryption, so an encrypted document
 * is always locked and its stream bodies are ciphertext. The reference refuses
 * to scan those; refuse here too rather than hunt for trigger names in noise.
 * The /Encrypt dictionary is the object whose /Filter is /Standard, which is
 * how the engine's isEncrypted() finds it as well. */
static int pdf_is_encrypted_doc(XPDF *pPdf)
{
    size_t i = 0;

    for (i = 0; i < pPdf->vecObjects.nSize; i++) {
        const CDVec *pParts = (const CDVec *)pPdf->vecObjects.ppData[i];
        size_t nCount = pdf_info_part_count(pParts);
        size_t j = 0;

        for (j = 0; j < nCount; j++) {
            if (x_strcmp((const char *)pParts->ppData[j], "/Filter") == 0) {
                if (((j + 1) < nCount) && (x_strcmp((const char *)pParts->ppData[j + 1], "/Standard") == 0)) {
                    return 1;
                }

                break;
            }
        }
    }

    return 0;
}

/* getSuspiciousInfoString's triggers, in the order it lists them. */
static const char *g_pPdfSuspiciousKeys[12] = {"/OpenAction", "/AA",         "/JavaScript", "/JS",        "/Launch",   "/EmbeddedFile",
                                               "/URI",        "/SubmitForm", "/GoToR",      "/RichMedia", "/AcroForm", "/XFA"};

/* getSuspiciousInfoString: the trigger names that appear as a token of an
 * enumerated object dictionary (step 1) or in the decompressed body of an
 * /ObjStm object stream (step 2). Appends one heap line to pLines. */
static void pdf_info_suspicious(XPDF *pPdf, CDVec *pLines)
{
    int bFound[12];
    CDBuf line;
    size_t nCount = sizeof(g_pPdfSuspiciousKeys) / sizeof(g_pPdfSuspiciousKeys[0]);
    size_t nFound = 0;
    size_t t = 0;
    size_t i = 0;

    for (t = 0; t < nCount; t++) {
        bFound[t] = 0;
    }

    /* 1) The enumerated dictionaries: their keys are plain tokens here. */
    for (i = 0; i < pPdf->vecObjects.nSize; i++) {
        const CDVec *pParts = (const CDVec *)pPdf->vecObjects.ppData[i];
        size_t nParts = pdf_info_part_count(pParts);
        size_t j = 0;

        for (j = 0; j < nParts; j++) {
            const char *pToken = (const char *)pParts->ppData[j];

            for (t = 0; t < nCount; t++) {
                if ((!bFound[t]) && (x_strcmp(pToken, g_pPdfSuspiciousKeys[t]) == 0)) {
                    bFound[t] = 1;
                    nFound++;
                }
            }
        }
    }

    /* 2) The object streams: the dictionaries they hold are compressed, so the
     * names only turn up once the body is decoded. */
    if ((nFound < nCount) && (!pdf_is_encrypted_doc(pPdf))) {
        PdfIO io;
        cd_i64 nBudget = PDF_OBJSTM_TOTAL_LIMIT;
        cd_i64 nScanBudget = PDF_OBJSTM_SCAN_LIMIT;

        io.pData = pPdf->pFile->pData;
        io.nSize = pPdf->pFile->nSize;

        for (i = 0; (i < pPdf->vecObjects.nSize) && (nFound < nCount) && (nBudget > 0); i++) {
            const CDVec *pParts = (const CDVec *)pPdf->vecObjects.ppData[i];
            const PdfObjRef *pRef = (const PdfObjRef *)pPdf->vecRefs.ppData[i];
            CDVec filters;
            CDBuf decoded;
            const unsigned char *pBody = NULL;
            cd_i64 nBodySize = 0;
            cd_i64 nStreamOffset = 0;
            cd_i64 nStreamSize = 0;
            int nMethod = 0;
            size_t f = 0;

            if (!pdf_parts_contain(pParts, pdf_info_part_count(pParts), "/ObjStm")) {
                continue;
            }

            if (!pdf_object_stream(&io, pRef->nOffset, &pPdf->vecRefs, &nScanBudget, &nStreamOffset, &nStreamSize)) {
                continue;
            }

            if ((nStreamOffset < 0) || (nStreamOffset > io.nSize) || (nStreamSize <= 0) || (nStreamSize > PDF_OBJSTM_RAW_LIMIT) ||
                (nStreamSize > (io.nSize - nStreamOffset))) {
                continue;
            }

            /* The filter comes from the object's whole token list, not the
             * 256-token view step 1 works against. */
            cdvec_init(&filters);
            pdf_resolve_filter_list(pParts, pParts->nSize, &filters);

            if (filters.nSize > 0) {
                nMethod = pdf_objstm_method((const char *)filters.ppData[filters.nSize - 1]);
            }

            for (f = 0; f < filters.nSize; f++) {
                cd_free(filters.ppData[f]);
            }

            cdvec_free(&filters);

            cdbuf_init(&decoded);

            if (nMethod == 0) {
                pBody = io.pData + nStreamOffset;
                nBodySize = nStreamSize;
            } else if (nMethod == 1) {
                cd_i64 nMax = (nBudget < PDF_OBJSTM_DECODE_LIMIT) ? nBudget : PDF_OBJSTM_DECODE_LIMIT;

                pdf_inflate_zlib(io.pData + nStreamOffset, nStreamSize, nMax, &decoded);
                pBody = (const unsigned char *)decoded.pData;
                nBodySize = (cd_i64)decoded.nSize;
                nBudget -= nBodySize;
            }

            for (t = 0; t < nCount; t++) {
                if ((!bFound[t]) && pdf_bytes_contain(pBody, nBodySize, g_pPdfSuspiciousKeys[t])) {
                    bFound[t] = 1;
                    nFound++;
                }
            }

            cdbuf_free(&decoded);
        }
    }

    if (nFound == 0) {
        return;
    }

    cdbuf_init(&line);
    cdbuf_append_str(&line, "Suspicious: ");

    for (t = 0, i = 0; t < nCount; t++) {
        if (bFound[t]) {
            if (i > 0) {
                cdbuf_append_str(&line, ", ");
            }

            cdbuf_append_str(&line, g_pPdfSuspiciousKeys[t]);
            i++;
        }
    }

    cdvec_push(pLines, cdbuf_detach(&line, NULL));
}

/* getMetaInfoString: for each key in turn the first value that renders to a
 * non-empty text, as "Key: Value". Appends heap strings to pLines. */
static void pdf_info_meta(XPDF *pPdf, CDVec *pLines)
{
    size_t k = 0;

    for (k = 0; k < (sizeof(g_pPdfMetaKeys) / sizeof(g_pPdfMetaKeys[0])); k++) {
        const char *pKey = g_pPdfMetaKeys[k];
        char *pValue = NULL;
        size_t i = 0;

        for (i = 0; (i < pPdf->vecObjects.nSize) && (pValue == NULL); i++) {
            const CDVec *pParts = (const CDVec *)pPdf->vecObjects.ppData[i];
            size_t nCount = pdf_info_part_count(pParts);
            size_t j = 0;

            for (j = 0; (j + 1) < nCount; j++) {
                if (x_strcmp((const char *)pParts->ppData[j], pKey) == 0) {
                    char *pText = pdf_info_value_text((const char *)pParts->ppData[j + 1]);

                    if (pText[0] != 0) {
                        pValue = pText;

                        break;
                    }

                    cd_free(pText);
                }
            }
        }

        if (pValue != NULL) {
            CDBuf line;

            cdbuf_init(&line);
            cdbuf_append_str(&line, pKey + 1);
            cdbuf_append_str(&line, ": ");
            cdbuf_append_str(&line, pValue);
            cd_free(pValue);
            cdvec_push(pLines, cdbuf_detach(&line, NULL));
        }
    }
}

char *xpdf_info(XPDF *pPdf)
{
    CDVec lines;
    CDBuf out;
    char *pFilters = NULL;
    size_t i = 0;

    cdvec_init(&lines);
    cdbuf_init(&out);

    pFilters = pdf_info_filters(pPdf);

    if (pFilters[0] != 0) {
        CDBuf line;

        cdbuf_init(&line);
        cdbuf_append_str(&line, "Filters: ");
        cdbuf_append_str(&line, pFilters);
        cdvec_push(&lines, cdbuf_detach(&line, NULL));
    }

    cd_free(pFilters);

    pdf_info_meta(pPdf, &lines);
    pdf_info_linearized(pPdf, &lines);
    pdf_info_suspicious(pPdf, &lines);

    /* The lines are joined with "; ", and any newline inside one becomes "; "
     * as well: an embedded newline would break the "PDF(ver)[...]" rendering. */
    for (i = 0; i < lines.nSize; i++) {
        const char *pLine = (const char *)lines.ppData[i];
        size_t c = 0;

        if (i > 0) {
            cdbuf_append_str(&out, "; ");
        }

        for (c = 0; pLine[c] != 0; c++) {
            if (pLine[c] == '\n') {
                cdbuf_append_str(&out, "; ");
            } else {
                cdbuf_append_ch(&out, pLine[c]);
            }
        }

        cd_free(lines.ppData[i]);
    }

    cdvec_free(&lines);

    return cdbuf_detach(&out, NULL);
}

char *xpdf_header_comment_hex(XPDF *pPdf)
{
    PdfIO io;
    PdfTok os;
    cd_i64 nOffset = 0;
    CDBuf out;

    io.pData = pPdf->pFile->pData;
    io.nSize = pPdf->pFile->nSize;

    cdbuf_init(&out);

    os = pdf_read_string(&io, 0, 100);
    nOffset = os.nSize;
    cd_free(os.pStr);

    if ((nOffset < io.nSize) && (io.pData[nOffset] == '%')) {
        cd_i64 nMaxRead = 40;
        cd_i64 nLen = 0;

        nOffset++;

        if (io.nSize - nOffset < nMaxRead) {
            nMaxRead = io.nSize - nOffset;
        }

        while (nLen < nMaxRead) {
            if (pdf_is_string_terminator(io.pData[nOffset + nLen])) {
                break;
            }

            nLen++;
        }

        {
            cd_i64 i = 0;

            for (i = 0; i < nLen; i++) {
                cdbuf_appendf(&out, "%02x", io.pData[nOffset + i]);
            }
        }
    }

    return cdbuf_detach(&out, NULL);
}
