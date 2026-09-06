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

/* xb.h - in-memory binary access, memory maps and the DIE signature engine. */

#ifndef XB_H
#define XB_H

#include "../core/cd_common.h"

/* File types, ordered like XBinary::FT so that database sorting matches. */
typedef enum {
    XFT_UNKNOWN = 0,
    XFT_BINARY,
    XFT_COM,
    XFT_MSDOS,
    XFT_NE,
    XFT_LE,
    XFT_LX,
    XFT_PE,
    XFT_PE32,
    XFT_PE64,
    XFT_ELF,
    XFT_ELF32,
    XFT_ELF64,
    XFT_MACHO,
    XFT_MACHO32,
    XFT_MACHO64,
    XFT_ZIP,
    XFT_JAR,
    XFT_APK,
    XFT_IPA,
    XFT_DEX,
    XFT_NPM,
    XFT_MACHOFAT,
    XFT_ARCHIVE,
    XFT_PDF,
    XFT_CFBF,
    XFT_IMAGE,
    XFT_JPEG,
    XFT_PNG,
    XFT_RAR,
    XFT_ISO9660,
    XFT_AMIGAHUNK,
    XFT_ATARIST,
    XFT_JAVACLASS,
    XFT_PYC,
    XFT_DOS16M,
    XFT_DOS4G,
    XFT_CLI_ASSEMBLY,
    XFT_COUNT
} XFileType;

const char *xft_to_string(XFileType type);

/* File part identifiers used by the memory map. */
typedef enum { XPART_HEADER = 0, XPART_SECTION, XPART_OVERLAY, XPART_DATA } XFilePart;

typedef struct {
    cd_i64 nOffset;
    cd_i64 nSize;
    cd_u64 nAddress;
    cd_u64 nVirtualSize;
    XFilePart filePart;
    char sName[40];
} XBRegion;

typedef struct {
    XBRegion *pRecords;
    int nCount;
    int nCapacity;
    cd_u64 nModuleAddress;
    cd_i64 nBinarySize;
    XFileType fileType;
    int nBits;
    int bBigEndian;
} XBMemoryMap;

/* A whole file loaded into memory. */
typedef struct {
    unsigned char *pData;
    cd_i64 nSize;
    char *pFileName;
} XBFile;

int xb_open(XBFile *pFile, const char *pFileName);
void xb_close(XBFile *pFile);

cd_u8 xb_u8(XBFile *pFile, cd_i64 nOffset);
cd_i8 xb_i8(XBFile *pFile, cd_i64 nOffset);
cd_u16 xb_u16(XBFile *pFile, cd_i64 nOffset, int bBigEndian);
cd_i16 xb_i16(XBFile *pFile, cd_i64 nOffset, int bBigEndian);
cd_u32 xb_u24(XBFile *pFile, cd_i64 nOffset, int bBigEndian);
cd_u32 xb_u32(XBFile *pFile, cd_i64 nOffset, int bBigEndian);
cd_i32 xb_i32(XBFile *pFile, cd_i64 nOffset, int bBigEndian);
cd_u64 xb_u64(XBFile *pFile, cd_i64 nOffset, int bBigEndian);
cd_i64 xb_i64(XBFile *pFile, cd_i64 nOffset, int bBigEndian);
float xb_f32(XBFile *pFile, cd_i64 nOffset, int bBigEndian);
double xb_f64(XBFile *pFile, cd_i64 nOffset, int bBigEndian);
/* IEEE-754 half at nOffset, converted to float (XBinary::read_float16). */
float xb_f16(XBFile *pFile, cd_i64 nOffset, int bBigEndian);

/* Reads a NUL-terminated Latin-1 string (caller frees). */
char *xb_ansi_string(XBFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize);
/* UCSD/Pascal string: a uint8 length at nOffset then that many bytes, with
 * embedded NULs shown as spaces (XBinary::read_ucsdString). Caller frees. */
char *xb_ucsd_string(XBFile *pFile, cd_i64 nOffset);
/* A 16-byte GUID at nOffset as "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee" lower
 * case (XBinary::read_UUID on a little-endian host). Caller frees. */
char *xb_uuid(XBFile *pFile, cd_i64 nOffset);
/* Reads a NUL-terminated UTF-16 string, converted to UTF-8 (caller frees). */
char *xb_unicode_string(XBFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize, int bBigEndian);
/* As xb_unicode_string, but also reports the number of UTF-16 code units the
 * string occupies on disk - which is not the length of the UTF-8 result. */
char *xb_unicode_string_n(XBFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize, int bBigEndian, cd_i64 *pnUnits);
/* Reads a NUL-terminated UTF-8 string (caller frees). */
char *xb_utf8_string(XBFile *pFile, cd_i64 nOffset, cd_i64 nMaxSize);

/* Uppercase hex representation of a region (caller frees). */
char *xb_signature_hex(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize);

cd_i64 xb_find_bytes(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, const unsigned char *pNeedle, cd_i64 nNeedleSize);
cd_i64 xb_find_ansi_string(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, const char *pString);
cd_i64 xb_find_unicode_string(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, const char *pString, int bBigEndian);
cd_i64 xb_find_u8(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u8 nValue);
cd_i64 xb_find_u16(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u16 nValue);
cd_i64 xb_find_u32(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u32 nValue);

double xb_entropy(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize);
int xb_is_zero_filled(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize);
char *xb_md5(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize);
cd_u32 xb_crc32(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u32 nInit);
cd_u32 xb_adler32(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize);
cd_u16 xb_crc16(XBFile *pFile, cd_i64 nOffset, cd_i64 nSize, cd_u16 nInit);
/* CRC32C-style hash used by the DIE import hashes. */
cd_u32 xb_string_custom_crc32(const char *pString);

/* -------------------------------------------------------------- mem map  */

void xbmap_init(XBMemoryMap *pMap);
void xbmap_free(XBMemoryMap *pMap);
void xbmap_add(XBMemoryMap *pMap, cd_i64 nOffset, cd_i64 nSize, cd_u64 nAddress, cd_u64 nVirtualSize, XFilePart filePart, const char *pName);
cd_i64 xbmap_address_to_offset(XBMemoryMap *pMap, cd_u64 nAddress);
cd_i64 xbmap_offset_to_address(XBMemoryMap *pMap, cd_i64 nOffset);
cd_i64 xbmap_raw_size(XBMemoryMap *pMap);

/* ------------------------------------------------------------ signature  */

/* Converts the DIE signature syntax into the canonical lowercase form.
 * 'text' becomes hex, '?' becomes '.', spaces are removed.                 */
char *xb_convert_signature(const char *pSignature);

typedef enum {
    XSIG_BYTES = 0,
    XSIG_SKIP,
    XSIG_NOTNULL,
    XSIG_ANSI,
    XSIG_NOTANSI,
    XSIG_NOTANSIANDNULL,
    XSIG_ANSINUMBER,
    XSIG_FINDBYTES,
    XSIG_RELOFFSET,
    XSIG_ADDRESS
} XSigRecordType;

typedef struct {
    XSigRecordType type;
    unsigned char *pData;
    cd_i64 nDataSize;
    cd_i64 nWindowSize;
    cd_i64 nFindDelta;
    int nSizeOfAddr;
    cd_u64 nBaseAddress;
} XSigRecord;

typedef struct {
    XSigRecord *pRecords;
    int nCount;
    int bValid;
} XSignature;

int xb_signature_parse(XSignature *pSignature, const char *pConverted);
void xb_signature_free(XSignature *pSignature);
int xb_signature_compare(XBFile *pFile, XBMemoryMap *pMap, XSignature *pSignature, cd_i64 nOffset);

/* Compares two hex signature strings; '.' is a wildcard on both sides. */
int xb_compare_signature_strings(const char *pBase, const char *pOpt);

/* Convenience: convert + parse + compare. */
int xb_compare_signature(XBFile *pFile, XBMemoryMap *pMap, const char *pSignature, cd_i64 nOffset);
cd_i64 xb_find_signature(XBFile *pFile, XBMemoryMap *pMap, cd_i64 nOffset, cd_i64 nSize, const char *pSignature);

#endif /* XB_H */
