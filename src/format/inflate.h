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

#ifndef CDIE_INFLATE_H
#define CDIE_INFLATE_H

#include "../core/cd_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Raw DEFLATE (RFC 1951), no zlib/gzip wrapper. This is what a ZIP member
 * stored with method 8 contains. The no-dependency rule forbids linking zlib,
 * so the algorithm is implemented here.
 *
 * Inflates pSource[0..nSourceSize) into pOut. nExpectedSize is the known
 * uncompressed size (from the ZIP header) and is used to bound the output so
 * a corrupt stream cannot grow without limit; pass 0 if unknown. nMaxSize is
 * an absolute ceiling on the output, mirroring the nDecompressedLimit of
 * XArchive::decompress: decoding stops once that many bytes are produced and
 * the truncated result is a success; pass 0 for no ceiling. Returns 1 on
 * success, 0 on any malformed input. On success pOut holds the bytes and its
 * NUL terminator is in place.
 */
int inflate_raw(const unsigned char *pSource, size_t nSourceSize, size_t nExpectedSize, size_t nMaxSize, CDBuf *pOut);

#ifdef __cplusplus
}
#endif

#endif
