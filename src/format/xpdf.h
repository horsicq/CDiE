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

#ifndef CDIE_XPDF_H
#define CDIE_XPDF_H

#include "xb.h"
#include "../core/cd_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A parsed PDF: the object dictionaries reduced to flat token lists, which is
 * the representation the database's key/value queries work against. Mirrors
 * XPDF's XPART list.                                                        */
typedef struct {
    XBFile *pFile;
    CDVec vecObjects; /* each entry a CDVec* of char* tokens */
    CDVec vecRefs;    /* parallel to vecObjects: where each object starts, so
                       * the /ObjStm scan can go back to the file for the
                       * stream body. Entries are private to xpdf.c. */
    int bValid;
} XPDF;

int xpdf_parse(XBFile *pFile, XPDF *pPdf);
void xpdf_free(XPDF *pPdf);

/* "%PDF-x.y" version, the three characters after "%PDF-". Heap string. */
char *xpdf_version(XPDF *pPdf);

/* The "/Filter" values joined with ", " — what appears in the format line's
 * brackets. Heap string, possibly empty. */
char *xpdf_filters(XPDF *pPdf);

/* getInfo: the format-line options - the sorted filter list, the
 * /Title.../ModDate metadata block, the linearization note and the suspicious
 * keys - joined with "; ". Heap string, possibly empty. */
char *xpdf_info(XPDF *pPdf);

/* The bytes of the second-line "%..." comment as hex. Heap string. */
char *xpdf_header_comment_hex(XPDF *pPdf);

/* Appends to pOut (a CDVec of heap char*) the distinct string values for a
 * key. bStringsOnly restricts to PDF string objects "(...)", matching
 * getStringValuesByKey; when 0 every value type is included, matching
 * getValuesByKey. nPartLimit caps how many tokens per object are considered,
 * as the reference's getParts(limit) does. */
void xpdf_values_by_key(XPDF *pPdf, const char *pKey, int bStringsOnly, int nPartLimit, CDVec *pOut);

#ifdef __cplusplus
}
#endif

#endif
