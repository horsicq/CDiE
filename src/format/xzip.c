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

#include "xzip.h"
#include "inflate.h"
#include "../core/utils.h"

/* XArchive::getRecords stops after this many members. */
#define XZIP_MAX_RECORDS 20000

/* XJAR::getFileFormatInfo reads only the head of each *.class candidate:
 * XArchive::decompress(&record, pPdStruct, 0, 0x100). */
#define XZIP_CLASS_PROBE_SIZE 0x100

/* Decompresses the member described by a central-directory entry (only the
 * STORE and DEFLATE methods, which is all the manifest ever uses). nMaxSize
 * caps the produced bytes the way XArchive::decompress's nDecompressedLimit
 * does; 0 means the whole member. */
static int zip_read_member(XBFile *pFile, cd_u16 nMethod, cd_u32 nCompSize, cd_u32 nUncompSize, cd_u32 nLocalOffset, size_t nMaxSize, CDBuf *pOut)
{
    cd_i64 nSize = pFile->nSize;
    const unsigned char *pData = pFile->pData;
    cd_u16 nLocalNameLen = 0;
    cd_u16 nLocalExtraLen = 0;
    cd_i64 nDataOffset = 0;

    if (((cd_i64)nLocalOffset + 30) > nSize) {
        return 0;
    }

    if (!((pData[nLocalOffset] == 0x50) && (pData[nLocalOffset + 1] == 0x4B) && (pData[nLocalOffset + 2] == 0x03) && (pData[nLocalOffset + 3] == 0x04))) {
        return 0;
    }

    nLocalNameLen = xb_u16(pFile, (cd_i64)nLocalOffset + 26, 0);
    nLocalExtraLen = xb_u16(pFile, (cd_i64)nLocalOffset + 28, 0);
    nDataOffset = (cd_i64)nLocalOffset + 30 + nLocalNameLen + nLocalExtraLen;

    if ((nDataOffset + (cd_i64)nCompSize) > nSize) {
        return 0;
    }

    if (nMethod == 0) {
        size_t nCopy = nCompSize;

        if ((nMaxSize != 0) && (nCopy > nMaxSize)) {
            nCopy = nMaxSize;
        }

        cdbuf_append(pOut, pData + nDataOffset, nCopy);

        return 1;
    }

    if (nMethod == 8) {
        return inflate_raw(pData + nDataOffset, nCompSize, nUncompSize, nMaxSize, pOut);
    }

    return 0;
}

/* XJavaClass::_getJDKVersion: class-file major version -> Java release name, or
 * NULL for an unrecognised major (the reference leaves sResult empty). */
static const char *jvm_base_version(cd_u16 nMajor)
{
    switch (nMajor) {
        case 0x2D: return "JDK 1.1";
        case 0x2E: return "JDK 1.2";
        case 0x2F: return "JDK 1.3";
        case 0x30: return "JDK 1.4";
        case 0x31: return "Java SE 5.0";
        case 0x32: return "Java SE 6";
        case 0x33: return "Java SE 7";
        case 0x34: return "Java SE 8";
        case 0x35: return "Java SE 9";
        case 0x36: return "Java SE 10";
        case 0x37: return "Java SE 11";
        case 0x38: return "Java SE 12";
        case 0x39: return "Java SE 13";
        case 0x3A: return "Java SE 14";
        case 0x3B: return "Java SE 15";
        case 0x3C: return "Java SE 16";
        case 0x3D: return "Java SE 17";
        case 0x3E: return "Java SE 18";
        case 0x3F: return "Java SE 19";
        case 0x40: return "Java SE 20";
        case 0x41: return "Java SE 21";
        case 0x42: return "Java SE 22";
        case 0x43: return "Java SE 23";
        case 0x44: return "Java SE 24";
        case 0x45: return "Java SE 25";
        case 0x46: return "Java SE 26";
        case 0x47: return "Java SE 27";
        case 0x48: return "Java SE 28";
        case 0x49: return "Java SE 29";
        case 0x4A: return "Java SE 30";
        default: break;
    }

    return NULL;
}

/* Formats _getJDKVersion(nMajor, nMinor) into pOut: the release name, with
 * ".<minor>" appended when the name is known and nMinor is non-zero. */
static void jvm_version(cd_u16 nMajor, cd_u16 nMinor, char *pOut, size_t nOutSize)
{
    const char *pBase = jvm_base_version(nMajor);

    if (pBase == NULL) {
        if (nOutSize > 0) {
            pOut[0] = 0;
        }

        return;
    }

    if (nMinor != 0) {
        x_snprintf(pOut, nOutSize, "%s.%u", pBase, (unsigned int)nMinor);
    } else {
        x_snprintf(pOut, nOutSize, "%s", pBase);
    }
}

/* XArchive::RECORD::sRecordName.section(".", -1, -1) == "class": the text after
 * the final '.' (the whole name when it has no '.') equals "class". */
static int name_ext_is_class(const char *pName)
{
    const char *pDot = NULL;
    const char *p = NULL;
    const char *pExt = NULL;

    if (pName == NULL) {
        return 0;
    }

    for (p = pName; *p; p++) {
        if (*p == '.') {
            pDot = p;
        }
    }

    pExt = (pDot != NULL) ? (pDot + 1) : pName;

    return x_strcmp(pExt, "class") == 0;
}

int xzip_parse(XBFile *pFile, XZip *pZip)
{
    cd_i64 nSize = 0;
    const unsigned char *pData = NULL;
    cd_i64 nEocd = -1;
    cd_i64 i = 0;
    cd_i64 nCentralOffset = 0;
    cd_u32 nEntries = 0;
    int bJvmFound = 0;

    x_memset(pZip, 0, sizeof(*pZip));
    cdvec_init(&pZip->vecNames);

    if (pFile == NULL) {
        return 0;
    }

    nSize = pFile->nSize;
    pData = pFile->pData;

    if (nSize < 22) {
        return 0;
    }

    /* Locate the End Of Central Directory record from the tail. */
    for (i = nSize - 22; i >= 0; i--) {
        if ((nSize - i) > (65536 + 22)) {
            break;
        }

        if ((pData[i] == 0x50) && (pData[i + 1] == 0x4B) && (pData[i + 2] == 0x05) && (pData[i + 3] == 0x06)) {
            nEocd = i;

            break;
        }
    }

    if (nEocd < 0) {
        return 0;
    }

    nEntries = xb_u16(pFile, nEocd + 10, 0);
    nCentralOffset = xb_u32(pFile, nEocd + 16, 0);

    for (i = 0; ((cd_u32)i < nEntries) && (i < XZIP_MAX_RECORDS); i++) {
        cd_u16 nMethod = 0;
        cd_u32 nCompSize = 0;
        cd_u32 nUncompSize = 0;
        cd_u16 nNameLen = 0;
        cd_u16 nExtraLen = 0;
        cd_u16 nCommentLen = 0;
        cd_u32 nLocalOffset = 0;
        cd_i64 nNameOffset = 0;
        char *pName = NULL;

        if ((nCentralOffset + 46) > nSize) {
            break;
        }

        if (!((pData[nCentralOffset] == 0x50) && (pData[nCentralOffset + 1] == 0x4B) && (pData[nCentralOffset + 2] == 0x01) && (pData[nCentralOffset + 3] == 0x02))) {
            break;
        }

        nMethod = xb_u16(pFile, nCentralOffset + 10, 0);
        nCompSize = xb_u32(pFile, nCentralOffset + 20, 0);
        nUncompSize = xb_u32(pFile, nCentralOffset + 24, 0);
        nNameLen = xb_u16(pFile, nCentralOffset + 28, 0);
        nExtraLen = xb_u16(pFile, nCentralOffset + 30, 0);
        nCommentLen = xb_u16(pFile, nCentralOffset + 32, 0);
        nLocalOffset = xb_u32(pFile, nCentralOffset + 42, 0);
        nNameOffset = nCentralOffset + 46;

        if ((nNameOffset + nNameLen) > nSize) {
            break;
        }

        pName = cd_strndup((const char *)(pData + nNameOffset), nNameLen);
        cdvec_push(&pZip->vecNames, pName);

        if ((pZip->pManifestText == NULL) && (nNameLen == 20) && (x_memcmp(pData + nNameOffset, "META-INF/MANIFEST.MF", 20) == 0)) {
            CDBuf manifest;

            cdbuf_init(&manifest);

            if (zip_read_member(pFile, nMethod, nCompSize, nUncompSize, nLocalOffset, 0, &manifest)) {
                pZip->pManifestText = cdbuf_detach(&manifest, NULL);
            } else {
                cdbuf_free(&manifest);
            }
        }

        if ((pZip->pPackageJson == NULL) && (nNameLen == 20) && (x_memcmp(pData + nNameOffset, "package/package.json", 20) == 0)) {
            CDBuf json;

            cdbuf_init(&json);

            if (zip_read_member(pFile, nMethod, nCompSize, nUncompSize, nLocalOffset, 0, &json)) {
                pZip->pPackageJson = cdbuf_detach(&json, NULL);
            } else {
                cdbuf_free(&json);
            }
        }

        /* XJAR::getFileFormatInfo: the Java release is read from the first
         * *.class member whose decompressed head carries the 0xCAFEBABE magic
         * (major at +6, minor at +4, both big-endian). */
        if ((!bJvmFound) && name_ext_is_class(pName)) {
            CDBuf klass;

            cdbuf_init(&klass);

            if (zip_read_member(pFile, nMethod, nCompSize, nUncompSize, nLocalOffset, XZIP_CLASS_PROBE_SIZE, &klass) && (klass.nSize > 10)) {
                const unsigned char *pClass = (const unsigned char *)klass.pData;
                cd_u32 nMagic = ((cd_u32)pClass[0] << 24) | ((cd_u32)pClass[1] << 16) | ((cd_u32)pClass[2] << 8) | (cd_u32)pClass[3];

                if (nMagic == 0xCAFEBABE) {
                    cd_u16 nMinor = (cd_u16)(((cd_u16)pClass[4] << 8) | pClass[5]);
                    cd_u16 nMajor = (cd_u16)(((cd_u16)pClass[6] << 8) | pClass[7]);

                    jvm_version(nMajor, nMinor, pZip->sJvmVersion, sizeof(pZip->sJvmVersion));
                    bJvmFound = 1;
                }
            }

            cdbuf_free(&klass);
        }

        nCentralOffset += 46 + nNameLen + nExtraLen + nCommentLen;
    }

    pZip->bValid = 1;

    return 1;
}

void xzip_free(XZip *pZip)
{
    size_t i = 0;

    for (i = 0; i < pZip->vecNames.nSize; i++) {
        cd_free(pZip->vecNames.ppData[i]);
    }

    cdvec_free(&pZip->vecNames);

    if (pZip->pManifestText) {
        cd_free(pZip->pManifestText);
        pZip->pManifestText = NULL;
    }

    if (pZip->pPackageJson) {
        cd_free(pZip->pPackageJson);
        pZip->pPackageJson = NULL;
    }

    pZip->bValid = 0;
}

int xzip_record_present(XZip *pZip, const char *pName)
{
    size_t i = 0;

    if ((pZip == NULL) || (pName == NULL)) {
        return 0;
    }

    for (i = 0; i < pZip->vecNames.nSize; i++) {
        const char *pRecord = (const char *)pZip->vecNames.ppData[i];

        if ((pRecord != NULL) && (x_strcmp(pRecord, pName) == 0)) {
            return 1;
        }
    }

    return 0;
}

/* ------------------------------------------------- package.json (minimal) */

static const char *json_skip_ws(const char *p)
{
    while ((*p == ' ') || (*p == '\t') || (*p == '\n') || (*p == '\r')) {
        p++;
    }

    return p;
}

static void json_append_cp(CDBuf *pOut, cd_u32 nCode)
{
    if (nCode < 0x80) {
        cdbuf_append_ch(pOut, (char)nCode);
    } else if (nCode < 0x800) {
        cdbuf_append_ch(pOut, (char)(0xC0 | (nCode >> 6)));
        cdbuf_append_ch(pOut, (char)(0x80 | (nCode & 0x3F)));
    } else if (nCode < 0x10000) {
        cdbuf_append_ch(pOut, (char)(0xE0 | (nCode >> 12)));
        cdbuf_append_ch(pOut, (char)(0x80 | ((nCode >> 6) & 0x3F)));
        cdbuf_append_ch(pOut, (char)(0x80 | (nCode & 0x3F)));
    } else {
        cdbuf_append_ch(pOut, (char)(0xF0 | (nCode >> 18)));
        cdbuf_append_ch(pOut, (char)(0x80 | ((nCode >> 12) & 0x3F)));
        cdbuf_append_ch(pOut, (char)(0x80 | ((nCode >> 6) & 0x3F)));
        cdbuf_append_ch(pOut, (char)(0x80 | (nCode & 0x3F)));
    }
}

static int json_hex4(const char *p, cd_u32 *pnValue)
{
    cd_u32 nValue = 0;
    int i = 0;

    for (i = 0; i < 4; i++) {
        char c = p[i];

        nValue <<= 4;

        if ((c >= '0') && (c <= '9')) {
            nValue |= (cd_u32)(c - '0');
        } else if ((c >= 'a') && (c <= 'f')) {
            nValue |= (cd_u32)(c - 'a' + 10);
        } else if ((c >= 'A') && (c <= 'F')) {
            nValue |= (cd_u32)(c - 'A' + 10);
        } else {
            return 0;
        }
    }

    *pnValue = nValue;

    return 1;
}

/* Parses a JSON string starting at the opening quote *pp. On success advances
 * *pp past the closing quote and, when pOut is non-NULL, appends the decoded
 * UTF-8. Returns 1 on success. */
static int json_parse_string(const char **pp, CDBuf *pOut)
{
    const char *p = *pp;

    if (*p != '"') {
        return 0;
    }

    p++;

    while (*p && (*p != '"')) {
        if (*p == '\\') {
            p++;

            switch (*p) {
                case '"': if (pOut) cdbuf_append_ch(pOut, '"'); p++; break;
                case '\\': if (pOut) cdbuf_append_ch(pOut, '\\'); p++; break;
                case '/': if (pOut) cdbuf_append_ch(pOut, '/'); p++; break;
                case 'b': if (pOut) cdbuf_append_ch(pOut, '\b'); p++; break;
                case 'f': if (pOut) cdbuf_append_ch(pOut, '\f'); p++; break;
                case 'n': if (pOut) cdbuf_append_ch(pOut, '\n'); p++; break;
                case 'r': if (pOut) cdbuf_append_ch(pOut, '\r'); p++; break;
                case 't': if (pOut) cdbuf_append_ch(pOut, '\t'); p++; break;
                case 'u': {
                    cd_u32 nCode = 0;

                    p++;

                    if (!json_hex4(p, &nCode)) {
                        return 0;
                    }

                    p += 4;

                    if ((nCode >= 0xD800) && (nCode <= 0xDBFF) && (p[0] == '\\') && (p[1] == 'u')) {
                        cd_u32 nLow = 0;

                        if (json_hex4(p + 2, &nLow) && (nLow >= 0xDC00) && (nLow <= 0xDFFF)) {
                            nCode = 0x10000 + ((nCode - 0xD800) << 10) + (nLow - 0xDC00);
                            p += 6;
                        }
                    }

                    if (pOut) {
                        json_append_cp(pOut, nCode);
                    }

                    break;
                }
                default: return 0;
            }
        } else {
            if (pOut) {
                cdbuf_append_ch(pOut, *p);
            }

            p++;
        }
    }

    if (*p != '"') {
        return 0;
    }

    *pp = p + 1;

    return 1;
}

/* Advances *pp past one JSON value (used to skip the values of non-matching
 * top-level keys, tracking nested braces/brackets). */
static int json_skip_value(const char **pp)
{
    const char *p = json_skip_ws(*pp);

    if (*p == '"') {
        if (!json_parse_string(&p, NULL)) {
            return 0;
        }
    } else if ((*p == '{') || (*p == '[')) {
        int nDepth = 0;

        do {
            if (*p == '{' || *p == '[') {
                nDepth++;
                p++;
            } else if (*p == '}' || *p == ']') {
                nDepth--;
                p++;
            } else if (*p == '"') {
                if (!json_parse_string(&p, NULL)) {
                    return 0;
                }
            } else if (*p == 0) {
                return 0;
            } else {
                p++;
            }
        } while (nDepth > 0);
    } else {
        /* number / true / false / null: run to the next delimiter. */
        while (*p && (*p != ',') && (*p != '}') && (*p != ']')) {
            p++;
        }
    }

    *pp = p;

    return 1;
}

char *xzip_packagejson_record(XZip *pZip, const char *pKey)
{
    const char *p = NULL;
    CDBuf value;

    if ((pZip == NULL) || (pZip->pPackageJson == NULL) || (pKey == NULL)) {
        return cd_strdup("");
    }

    p = json_skip_ws(pZip->pPackageJson);

    if (*p != '{') {
        return cd_strdup(""); /* root is not an object */
    }

    p++;

    for (;;) {
        CDBuf key;
        int bMatch = 0;

        p = json_skip_ws(p);

        if (*p == '}' || *p == 0) {
            break;
        }

        if (*p != '"') {
            break; /* malformed */
        }

        cdbuf_init(&key);

        if (!json_parse_string(&p, &key)) {
            cdbuf_free(&key);

            break;
        }

        bMatch = (x_strcmp(key.pData ? key.pData : "", pKey) == 0);
        cdbuf_free(&key);

        p = json_skip_ws(p);

        if (*p != ':') {
            break;
        }

        p++;
        p = json_skip_ws(p);

        if (bMatch) {
            /* QJsonValue::toString() is non-empty only for a string value. */
            if (*p == '"') {
                cdbuf_init(&value);

                if (json_parse_string(&p, &value)) {
                    return cdbuf_detach(&value, NULL);
                }

                cdbuf_free(&value);
            }

            return cd_strdup("");
        }

        if (!json_skip_value(&p)) {
            break;
        }

        p = json_skip_ws(p);

        if (*p == ',') {
            p++;
        } else {
            break;
        }
    }

    return cd_strdup("");
}

char *xzip_manifest_record(XZip *pZip, const char *pKey)
{
    const char *pText = NULL;
    size_t nKeyLen = 0;
    const char *p = NULL;

    if ((pZip == NULL) || (pZip->pManifestText == NULL) || (pKey == NULL)) {
        return cd_strdup("");
    }

    pText = pZip->pManifestText;
    nKeyLen = x_strlen(pKey);

    /* Regex `pKey + ": (.*?)\n"`, unanchored, group 1: find `pKey: `, capture up
     * to the next '\n' (required — no newline means no match), strip '\r'. */
    for (p = pText; *p; p++) {
        if ((x_strncmp(p, pKey, nKeyLen) == 0) && (p[nKeyLen] == ':') && (p[nKeyLen + 1] == ' ')) {
            const char *pStart = p + nKeyLen + 2;
            const char *pEnd = pStart;
            CDBuf value;

            while (*pEnd && (*pEnd != '\n')) {
                pEnd++;
            }

            if (*pEnd != '\n') {
                return cd_strdup(""); /* the regex requires a terminating '\n' */
            }

            cdbuf_init(&value);

            while (pStart < pEnd) {
                if (*pStart != '\r') {
                    cdbuf_append_ch(&value, *pStart);
                }

                pStart++;
            }

            return cdbuf_detach(&value, NULL);
        }
    }

    return cd_strdup("");
}
