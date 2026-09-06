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

/* api.c - the DIE script API exposed to the JavaScript engine.
 *
 * The global helpers (_setResult, includeScript, ...) and the format objects
 * (Binary, PE, MSDOS) are implemented here. Every method is dispatched
 * through a single native function keyed by an identifier, which keeps the
 * binding table compact and easy to extend.                                */

#include "cdie.h"
#include "../global.h"
#include "../core/cd_fs.h"
#include "../format/xft.h"
#include "../format/xdisasm.h"
#include "../format/xiso9660.h"
#include "../js/js_internal.h"


typedef enum {
    A_NONE = 0,

    /* ---- generic binary ---- */
    A_getSize,
    A_compare,
    A_compareEP,
    A_compareOverlay,
    A_readByte,
    A_readSByte,
    A_readWord,
    A_readSWord,
    A_readDword,
    A_readSDword,
    A_readQword,
    A_readSQword,
    A_getString,
    A_findSignature,
    A_findString,
    A_findByte,
    A_findWord,
    A_findDword,
    A_getEntryPointOffset,
    A_getOverlayOffset,
    A_getOverlaySize,
    A_getAddressOfEntryPoint,
    A_isOverlayPresent,
    A_isSignaturePresent,
    A_swapBytes,
    A_RVAToOffset,
    A_VAToOffset,
    A_OffsetToVA,
    A_OffsetToRVA,
    A_getFileDirectory,
    A_getFileBaseName,
    A_getFileCompleteSuffix,
    A_getFileSuffix,
    A_getSignature,
    A_calculateEntropy,
    A_isZeroFilled,
    A_calculateMD5,
    A_calculateCRC32,
    A_crc16,
    A_crc32,
    A_adler32,
    A_isSignatureInSectionPresent,
    A_getImageBase,
    A_upperCase,
    A_lowerCase,
    A_isPlainText,
    A_isUTF8Text,
    A_isUnicodeText,
    A_isText,
    A_getHeaderString,
    A_getDisasmLength,
    A_getDisasmString,
    A_getDisasmNextAddress,
    A_is16,
    A_is32,
    A_is64,
    A_isDeepScan,
    A_isHeuristicScan,
    A_isFirstWrapperScan,
    A_isAggressiveScan,
    A_isRecursiveScan,
    A_isOverlayScan,
    A_isVerbose,
    A_isProfiling,
    A_getScanID,
    A_getStartOffset,
    A_read_uint8,
    A_read_int8,
    A_read_uint16,
    A_read_int16,
    A_read_uint24,
    A_read_int24,
    A_read_uint32,
    A_read_int32,
    A_read_uint64,
    A_read_int64,
    A_read_ansiString,
    A_read_unicodeString,
    A_read_utf8String,
    A_read_codePageString,
    A_read_float,
    A_read_double,
    A_read_float16,
    A_read_UUID,
    A_read_ucsdString,
    A_isNE,
    A_isLE,
    A_isLX,
    A_getApplicationIdentifier,
    A_getDataPreparerIdentifier,
    A_isArchiveRecordPresent,
    A_isArchiveRecordPresentExp,
    A_getManifestRecord,
    A_getPackageJsonRecord,
    A_isConstPresent,
    A_div64,
    A_divu64,
    A_shlu64,
    A_shru64,
    A_shl64,
    A_shr64,
    A_secondsToTimeStr,
    A_bytesCountToString,
    A_find_ansiString,
    A_find_unicodeString,
    A_find_utf8String,
    A_getOperationSystemName,
    A_getOperationSystemVersion,
    A_getOperationSystemOptions,
    A_getFileFormatName,
    A_getFileFormatVersion,
    A_getFileFormatOptions,
    A_isSigned,
    A_cleanString,
    A_readBytes,
    A_isOverlay,
    A_isResource,
    A_isDebugData,
    A_isFilePart,
    A_isReleaseBuild,
    A_isDebugBuild,
    A_isChecksumCorrect,
    A_isEntryPointCorrect,
    A_isSectionAlignmentCorrect,
    A_isFileAlignmentCorrect,
    A_isHeaderCorrect,
    A_isRelocsTableCorrect,
    A_isImportTableCorrect,
    A_isExportTableCorrect,
    A_isResourcesTableCorrect,
    A_isSectionsTableCorrect,
    A_getFormatMessages,
    A_startTiming,
    A_endTiming,
    A_detectZLIB,
    A_detectGZIP,
    A_detectZIP,
    A_getListOfCompressionMethods,

    /* ---- MSDOS ---- */
    A_isRichSignaturePresent,
    A_getNumberOfRichIDs,
    A_isRichVersionPresent,
    A_getRichVersion,
    A_getRichID,
    A_getRichCount,
    A_getDosStubOffset,
    A_getDosStubSize,
    A_isDosStubPresent,

    /* ---- PE ---- */
    A_getNumberOfSections,
    A_getSectionName,
    A_getSectionVirtualSize,
    A_getSectionVirtualAddress,
    A_getSectionFileSize,
    A_getSectionFileOffset,
    A_getSectionCharacteristics,
    A_getNumberOfResources,
    A_isSectionNamePresent,
    A__isSectionNamePresentExp,
    A_isNET,
    A_isPE32,
    A_isPEPlus,
    A_getGeneralOptions,
    A_getResourceIdByNumber,
    A_getResourceNameByNumber,
    A_getResourceOffsetByNumber,
    A_getResourceSizeByNumber,
    A_getResourceTypeByNumber,
    A_isNetObjectPresent,
    A_isNetUStringPresent,
    A_isNetGlobalCctorPresent,
    A_isNetTypePresent,
    A_isNetMethodPresent,
    A_isNetFieldPresent,
    A_findSignatureInBlob_NET,
    A_isSignatureInBlobPresent_NET,
    A_getNetModuleName,
    A_getNetAssemblyName,
    A_getNumberOfImports,
    A_getImportLibraryName,
    A_isLibraryPresent,
    A_isLibraryFunctionPresent,
    A_isFunctionPresent,
    A_getImportFunctionName,
    A_getImportSection,
    A_getExportSection,
    A_getResourceSection,
    A_getEntryPointSection,
    A_getRelocsSection,
    A_getTLSSection,
    A_getMajorLinkerVersion,
    A_getMinorLinkerVersion,
    A_getManifest,
    A_getVersionStringInfo,
    A_getNumberOfImportThunks,
    A_getResourceNameOffset,
    A_isResourceNamePresent,
    A_isResourceGroupNamePresent,
    A_isResourceGroupIdPresent,
    A_getCompilerVersion,
    A_isConsole,
    A_isSignedFile,
    A_getSectionNameCollision,
    A_getSectionNumber,
    A_getSectionNumberExp,
    A_isDll,
    A_isDriver,
    A_getNETVersion,
    A_compareEP_NET,
    A_getSizeOfCode,
    A_getSizeOfUninitializedData,
    A_getPEFileVersion,
    A_getFileVersion,
    A_getFileVersionMS,
    A_calculateSizeOfHeaders,
    A_isExportFunctionPresent,
    A_getNumberOfExportFunctions,
    A_getExportFunctionName,
    A_isExportPresent,
    A_isTLSPresent,
    A_isImportPresent,
    A_isResourcesPresent,
    A_getImportHash32,
    A_getImportHash64,
    A_isImportPositionHashPresent,
    A_getImageFileHeader,
    A_getImageOptionalHeader,
    A_getNumberOfDebugDataRecords,
    A_getDebugDataType,
    A_getDebugDataOffset,
    A_getDebugDataSize,

    /* ---- JPEG ---- */
    A_getComment,
    A_getDqtMD5,
    A_isChunkPresent,
    A_isExifPresent,
    A_getExifCameraName,

    /* ---- APK ---- */
    A_getAndroidManifestRecord,
    A_getAndroidManifest,

    /* ---- PDF ---- */
    A_getHeaderCommentAsHex,
    A_getStringValuesByKey,
    A_getValuesByKey,
    A_isValuesHexByKey,
    A_isEncrypted,

    /* ---- DEX ---- */
    A_isDexStringPresent,
    A_isDexItemStringPresent,
    A_getMapItemsHash,

    /* ---- ELF / Mach-O ---- */
    A_isStringInTablePresent,
    A_getNumberOfPrograms,
    A_getProgramFileOffset,
    A_getProgramFileSize,
    A_getElfHeader_type,
    A_getElfHeader_machine,
    A_getElfHeader_entry,
    A_getElfHeader_phoff,
    A_getElfHeader_shoff,
    A_getElfHeader_phnum,
    A_getElfHeader_shnum,
    A_getElfHeader_shentsize,
    A_getElfHeader_phentsize,
    A_getElfHeader_shstrndx,
    A_getRunPath,
    A_isNotePresent,
    A_getLibraryCurrentVersion
} ApiId;

typedef struct {
    const char *pName;
    ApiId id;
    int nArgc;
} ApiEntry;

/* ------------------------------------------------------------ profiling  */

/* -l/--profiling. DiE_Script brackets every detection script and
 * Binary_Script every search primitive with a QElapsedTimer, and both report
 * through warningMessage, which the reference console prints to stdout as
 * "[WARNING] <text>"; a measured step appends " [<n> ms]".
 *
 * Two deliberate differences from the reference. The trace follows -l on its
 * own, where diec also wants -M, because a flag documented as showing
 * profiling information that shows none is the defect this closes; and
 * endTiming() does not report an unknown handle, because nothing in the
 * database calls it and the error would be a new line in -M output.        */

cd_i64 cdie_profile_start(DieEngine *pEngine)
{
    if (!pEngine->pOptions->bProfiling) {
        return -1;
    }

    return (cd_i64)x_clock_ms();
}

void cdie_profile_text(DieEngine *pEngine, const char *pText)
{
    if (!pEngine->pOptions->bProfiling) {
        return;
    }

    x_printf("[WARNING] %s\n", pText);
}

X_PRINTF_LIKE(3, 4) void cdie_profile_end(DieEngine *pEngine, cd_i64 nStart, const char *pFormat, ...)
{
    char sInfo[1024];
    X_VA_LIST args;

    if ((!pEngine->pOptions->bProfiling) || (nStart < 0)) {
        return;
    }

    X_VA_START(args, pFormat);
    x_vsnprintf(sInfo, sizeof(sInfo), pFormat, args);
    X_VA_END(args);

    x_printf("[WARNING] %s [%lld ms]\n", sInfo, (long long)((cd_i64)x_clock_ms() - nStart));
}

void cdie_profile_free(DieEngine *pEngine)
{
    cd_free(pEngine->pTimings);
    pEngine->pTimings = NULL;
    pEngine->nTimingCount = 0;
    pEngine->nTimingCapacity = 0;
}

/* XBinary::valueToHexEx: the field is only as wide as the value needs, in
 * lowercase - two hex digits below 0xFF, then four, eight and sixteen.     */
static void value_to_hex_ex(cd_u64 nValue, char *pBuffer, size_t nBufferSize)
{
    int nWidth = 2;

    if (nValue >= 0xFFFFFFFFull) {
        nWidth = 16;
    } else if (nValue >= 0xFFFFull) {
        nWidth = 8;
    } else if (nValue >= 0xFFull) {
        nWidth = 4;
    }

    x_snprintf(pBuffer, nBufferSize, "%0*llx", nWidth, (unsigned long long)nValue);
}

/* -------------------------------------------------------------- helpers  */

static DieEngine *engine_of(JSCtx *pCtx)
{
    return (DieEngine *)js_get_user(pCtx);
}

static char *arg_string(JSCtx *pCtx, int nArgc, JSVal *pArgv, int nIndex)
{
    if (nIndex >= nArgc) {
        return cd_strdup("");
    }

    return js_to_cstr(pCtx, pArgv[nIndex]);
}

static cd_i64 arg_i64(JSCtx *pCtx, int nArgc, JSVal *pArgv, int nIndex, cd_i64 nDefault)
{
    if ((nIndex >= nArgc) || (pArgv[nIndex].tag == JT_UNDEF)) {
        return nDefault;
    }

    return js_to_int64(pCtx, pArgv[nIndex]);
}

/* A JS number coerced to unsigned 64-bit the way the Qt engine hands a double
 * to a quint64 slot parameter: truncate toward zero, saturating out of range.
 * Used by the Util 64-bit helpers. */
static cd_u64 arg_u64(JSCtx *pCtx, int nArgc, JSVal *pArgv, int nIndex)
{
    double dValue = 0;

    if ((nIndex >= nArgc) || (pArgv[nIndex].tag == JT_UNDEF)) {
        return 0;
    }

    dValue = js_to_number(pCtx, pArgv[nIndex]);

    if (dValue != dValue) {
        return 0; /* NaN */
    }

    if (dValue <= 0.0) {
        if (dValue <= -9223372036854775808.0) {
            return (cd_u64)0x8000000000000000ULL; /* <= -2^63 or -inf: out of int64 range */
        }

        return (cd_u64)(cd_i64)dValue; /* two's-complement of the truncated value */
    }

    if (dValue >= 18446744073709551616.0) {
        return (cd_u64)0xFFFFFFFFFFFFFFFFULL; /* >= 2^64 */
    }

    return (cd_u64)dValue;
}

static int arg_bool(JSCtx *pCtx, int nArgc, JSVal *pArgv, int nIndex, int bDefault)
{
    if ((nIndex >= nArgc) || (pArgv[nIndex].tag == JT_UNDEF)) {
        return bDefault;
    }

    return js_to_bool(pCtx, pArgv[nIndex]);
}

static JSVal str_take(JSCtx *pCtx, char *pString)
{
    JSVal result = js_str(pCtx, pString ? pString : "");

    cd_free(pString);

    return result;
}

/* The NE target-OS byte (IMAGE_OS2_HEADER::ne_exetyp at NE-header offset 0x36),
 * read straight from the file as XNE does. */
static cd_u8 ne_target_os(XBFile *pFile)
{
    cd_i64 nLfanew = (cd_i64)xb_u32(pFile, 0x3C, 0);

    return xb_u8(pFile, nLfanew + 0x36);
}

/* XNE::getOsName. */
static const char *ne_os_name(cd_u8 nOS)
{
    if ((nOS == 1) || (nOS == 0x81)) return "OS/2";
    if ((nOS == 2) || (nOS == 4) || (nOS == 0x82)) return "Windows";
    if (nOS == 3) return "MS-DOS";
    if (nOS == 5) return "Borland OS Services";

    return "Unknown";
}

/* XNE::getArch. */
static const char *ne_arch_name(cd_u8 nOS)
{
    return ((nOS == 4) || (nOS == 5)) ? "386" : "286";
}

/* Work budget for one isArchiveRecordPresentExp call.
 *
 * jsregexp_exec caps the backtracking of a single match, but the cap is per
 * subject: an archive with many long entry names multiplies it, and the
 * total grows with the entry count without limit. Measured against the
 * Obfuscapk pattern over 64 KB names: 8.6 s for 40 entries, 32 s for 200,
 * and on from there in proportion to the entry count.
 *
 * The regular expression engine reports no step count, so a budget over the
 * expensive names can only be wall clock, and a wall-clock budget must never
 * be what decides whether a name matches: the same file would answer
 * differently on a different machine, or on the same machine twice. A plain
 * deadline over the whole walk does exactly that - with 6 crafted names in
 * front of a matching one, cdie answered "detected" on some runs and "not
 * detected" on others, where the reference always answers "detected".
 *
 * So the walk goes over the short names first, with no budget at all. Every
 * name a real archive holds is short - a ZIP entry name is a path, and paths
 * stop at PATH_MAX - so for real input the second pass has nothing to do and
 * the answer is exactly the reference's, whatever the machine. Only names
 * longer than any path, which is to say only names that were built to be
 * slow, are left to the deadline, and only when the short names produced no
 * match.                                                                    */
#define ARCHIVE_EXP_SHORT_NAME 4096
#define ARCHIVE_EXP_BUDGET_MS 1500

/* isArchiveRecordPresentExp: true if any archive record name matches the
 * regular expression pPattern with a non-empty captured(0). Mirrors
 * XArchive::isArchiveRecordPresentExp over XBinary::isRegExpPresent (the same
 * unanchored QRegularExpression match). */
static int archive_record_present_exp(DieEngine *pEngine, const char *pPattern)
{
    JSRegExp *pRegExp = NULL;
    char *pError = NULL;
    cd_i32 *pnCaps = NULL;
    int bFound = 0;
    int nPass = 0;
    size_t i = 0;
    cd_i64 nDeadline = 0;

    if ((!pEngine->bHasZip) || (pPattern == NULL)) {
        return 0;
    }

    pRegExp = jsregexp_compile(pPattern, "", &pError);

    if (pError != NULL) {
        cd_free(pError);
    }

    if (pRegExp == NULL) {
        return 0;
    }

    pnCaps = (cd_i32 *)cd_malloc((size_t)(jsregexp_ngroups(pRegExp) + 1) * 2 * sizeof(cd_i32));

    /* Pass 0 takes the names no longer than a path, pass 1 the rest. The
     * reference walks the record list under isPdStructNotCanceled, so a
     * cancelled scan drops out of the loop instead of running the expression
     * over every remaining name.                                           */
    for (nPass = 0; (nPass < 2) && (!bFound); nPass++) {
        if (nPass == 1) {
            nDeadline = (cd_i64)x_clock_ms() + ARCHIVE_EXP_BUDGET_MS;
        }

        for (i = 0; (i < pEngine->zip.vecNames.nSize) && (!pEngine->bStop); i++) {
            const char *pName = (const char *)pEngine->zip.vecNames.ppData[i];
            size_t nLength = 0;

            if (pName == NULL) {
                continue;
            }

            nLength = x_strlen(pName);

            if ((nLength > ARCHIVE_EXP_SHORT_NAME) != (nPass == 1)) {
                continue;
            }

            /* Checked once per name rather than in batches: one name can be
             * the whole budget on its own, and a clock reading costs nothing
             * next to a match attempt on a name this long. */
            if ((nPass == 1) && ((cd_i64)x_clock_ms() > nDeadline)) {
                break;
            }

            if (jsregexp_exec(pRegExp, pName, nLength, 0, pnCaps)) {
                if (pnCaps[1] > pnCaps[0]) { /* captured(0) non-empty */
                    bFound = 1;

                    break;
                }
            }
        }
    }

    cd_free(pnCaps);
    jsregexp_free(pRegExp);

    return bFound;
}

/* Clamps an offset/size pair to the file, mirroring _fixOffsetAndSize.
 * Scripts may pass arbitrary numbers, so the arithmetic is overflow-safe.  */
static void fix_offset_size(DieEngine *pEngine, cd_i64 *pnOffset, cd_i64 *pnSize)
{
    if ((*pnOffset >= 0) && (*pnOffset < pEngine->file.nSize)) {
        if ((*pnSize < 0) || (*pnSize > pEngine->file.nSize - *pnOffset)) {
            *pnSize = pEngine->file.nSize - *pnOffset;
        }
    }
}

static const char *section_name(DieEngine *pEngine, int nNumber)
{
    if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pEngine->pe.nSectionCount)) {
        return "";
    }

    return pEngine->pe.pSections[nNumber].sName;
}

static char *clean_string(const char *pString)
{
    CDBuf buf;
    size_t i = 0;
    size_t nSize = pString ? x_strlen(pString) : 0;

    cdbuf_init(&buf);

    for (i = 0; i < nSize; i++) {
        unsigned char nChar = (unsigned char)pString[i];

        if ((nChar >= 0x20) && (nChar != 0x7F)) {
            cdbuf_append_ch(&buf, (char)nChar);
        }
    }

    return cdbuf_detach(&buf, NULL);
}

/* ---------------------------------------------------------- PE accessors */

static cd_i64 pe_entry_point_offset(DieEngine *pEngine)
{
    if (pEngine->bHasElf) {
        return pEngine->elf.nEntryPointOffset;
    }

    if (pEngine->bHasMach) {
        return pEngine->mach.nEntryPointOffset;
    }

    if (!pEngine->bHasPE) {
        return -1;
    }

    return pEngine->pe.nEntryPointOffset;
}

static int pe_directory_section(DieEngine *pEngine, int nDirectory)
{
    if (!pEngine->bHasPE) {
        return -1;
    }

    if (pEngine->pe.pDirRVA[nDirectory] == 0) {
        return -1;
    }

    return xpe_section_number_by_rva(&pEngine->pe, pEngine->pe.pDirRVA[nDirectory]);
}

/* PE image types, mirroring XPE::getType() / XPE::typeIdToString(). */
typedef enum {
    PETYPE_APPLICATION = 0,
    PETYPE_XBOX_APPLICATION,
    PETYPE_EFI_APPLICATION,
    PETYPE_GUI,
    PETYPE_CE_GUI,
    PETYPE_CONSOLE,
    PETYPE_DLL,
    PETYPE_DRIVER,
    PETYPE_NATIVE,
    PETYPE_EFI_RUNTIMEDRIVER,
    PETYPE_EFI_BOOTSERVICEDRIVER
} PEType;

static int pe_imports_ntdll(XPE *pPE)
{
    int i = 0;

    for (i = 0; i < pPE->nImportCount; i++) {
        if (cd_stricmp_ascii(pPE->pImports[i].pName, "ntdll.dll") == 0) {
            return 1;
        }
    }

    return 0;
}

static PEType pe_type(XPE *pPE)
{
    PEType result = PETYPE_APPLICATION;

    switch (pPE->nSubsystem) {
        case 1:
        case 8: result = pe_imports_ntdll(pPE) ? PETYPE_NATIVE : PETYPE_DRIVER; break;
        case 3:
        case 5:
        case 7: result = PETYPE_CONSOLE; break;
        case 2: result = PETYPE_GUI; break;
        case 9: result = PETYPE_CE_GUI; break;
        case 14: result = PETYPE_XBOX_APPLICATION; break;
        case 10: result = PETYPE_EFI_APPLICATION; break;
        case 11: result = PETYPE_EFI_BOOTSERVICEDRIVER; break;
        case 12: result = PETYPE_EFI_RUNTIMEDRIVER; break;
        default: break;
    }

    if ((result != PETYPE_DRIVER) && (pPE->nCharacteristics & 0x2000)) {
        result = PETYPE_DLL;
    }

    return result;
}

static const char *pe_type_name(PEType type)
{
    switch (type) {
        case PETYPE_APPLICATION: return "Application";
        case PETYPE_XBOX_APPLICATION: return "XBOX Application";
        case PETYPE_EFI_APPLICATION: return "EFI Application";
        case PETYPE_GUI: return "GUI";
        case PETYPE_CE_GUI: return "CE GUI";
        case PETYPE_CONSOLE: return "Console";
        case PETYPE_DLL: return "DLL";
        case PETYPE_DRIVER: return "Driver";
        case PETYPE_NATIVE: return "Native";
        case PETYPE_EFI_RUNTIMEDRIVER: return "EFI Runtime driver";
        case PETYPE_EFI_BOOTSERVICEDRIVER: return "EFI Boot service driver";
        default: break;
    }

    return "Unknown";
}

static const char *version_value(DieEngine *pEngine, const char *pKey)
{
    int i = 0;

    if (!pEngine->bHasPE) {
        return "";
    }

    for (i = 0; i < pEngine->pe.nVersionCount; i++) {
        if (x_strcmp(pEngine->pe.pVersionRecords[i].pKey, pKey) == 0) {
            return pEngine->pe.pVersionRecords[i].pValue;
        }
    }

    return "";
}

static cd_u64 image_file_header_value(XPE *pPE, const char *pKey)
{
    if (x_strcmp(pKey, "Machine") == 0) return pPE->nMachine;
    if (x_strcmp(pKey, "NumberOfSections") == 0) return pPE->nNumberOfSections;
    if (x_strcmp(pKey, "TimeDateStamp") == 0) return pPE->nTimeDateStamp;
    if (x_strcmp(pKey, "PointerToSymbolTable") == 0) return pPE->nPointerToSymbolTable;
    if (x_strcmp(pKey, "NumberOfSymbols") == 0) return pPE->nNumberOfSymbols;
    if (x_strcmp(pKey, "SizeOfOptionalHeader") == 0) return pPE->nSizeOfOptionalHeader;
    if (x_strcmp(pKey, "Characteristics") == 0) return pPE->nCharacteristics;

    return 0;
}

static cd_u64 image_optional_header_value(XPE *pPE, const char *pKey)
{
    if (x_strcmp(pKey, "Magic") == 0) return pPE->nMagic;
    if (x_strcmp(pKey, "MajorLinkerVersion") == 0) return pPE->nMajorLinkerVersion;
    if (x_strcmp(pKey, "MinorLinkerVersion") == 0) return pPE->nMinorLinkerVersion;
    if (x_strcmp(pKey, "SizeOfCode") == 0) return pPE->nSizeOfCode;
    if (x_strcmp(pKey, "SizeOfInitializedData") == 0) return pPE->nSizeOfInitializedData;
    if (x_strcmp(pKey, "SizeOfUninitializedData") == 0) return pPE->nSizeOfUninitializedData;
    if (x_strcmp(pKey, "AddressOfEntryPoint") == 0) return pPE->nAddressOfEntryPoint;
    if (x_strcmp(pKey, "BaseOfCode") == 0) return pPE->nBaseOfCode;
    if (x_strcmp(pKey, "BaseOfData") == 0) return pPE->nBaseOfData;
    if (x_strcmp(pKey, "ImageBase") == 0) return pPE->nImageBase;
    if (x_strcmp(pKey, "SectionAlignment") == 0) return pPE->nSectionAlignment;
    if (x_strcmp(pKey, "FileAlignment") == 0) return pPE->nFileAlignment;
    if (x_strcmp(pKey, "MajorOperatingSystemVersion") == 0) return pPE->nMajorOperatingSystemVersion;
    if (x_strcmp(pKey, "MinorOperatingSystemVersion") == 0) return pPE->nMinorOperatingSystemVersion;
    if (x_strcmp(pKey, "MajorImageVersion") == 0) return pPE->nMajorImageVersion;
    if (x_strcmp(pKey, "MinorImageVersion") == 0) return pPE->nMinorImageVersion;
    if (x_strcmp(pKey, "MajorSubsystemVersion") == 0) return pPE->nMajorSubsystemVersion;
    if (x_strcmp(pKey, "MinorSubsystemVersion") == 0) return pPE->nMinorSubsystemVersion;
    if (x_strcmp(pKey, "Win32VersionValue") == 0) return pPE->nWin32VersionValue;
    if (x_strcmp(pKey, "SizeOfImage") == 0) return pPE->nSizeOfImage;
    if (x_strcmp(pKey, "SizeOfHeaders") == 0) return pPE->nSizeOfHeaders;
    if (x_strcmp(pKey, "CheckSum") == 0) return pPE->nCheckSum;
    if (x_strcmp(pKey, "Subsystem") == 0) return pPE->nSubsystem;
    if (x_strcmp(pKey, "DllCharacteristics") == 0) return pPE->nDllCharacteristics;
    if (x_strcmp(pKey, "SizeOfStackReserve") == 0) return pPE->nSizeOfStackReserve;
    if (x_strcmp(pKey, "SizeOfStackCommit") == 0) return pPE->nSizeOfStackCommit;
    if (x_strcmp(pKey, "SizeOfHeapReserve") == 0) return pPE->nSizeOfHeapReserve;
    if (x_strcmp(pKey, "SizeOfHeapCommit") == 0) return pPE->nSizeOfHeapCommit;
    if (x_strcmp(pKey, "LoaderFlags") == 0) return pPE->nLoaderFlags;
    if (x_strcmp(pKey, "NumberOfRvaAndSizes") == 0) return pPE->nNumberOfRvaAndSizes;

    return 0;
}

/* ------------------------------------------------------- plain text  -----
 *
 * XBinary::isPlainTextType, byte for byte. It looks at the first 32 KB and
 * classifies every byte, then applies three ratio tests. Any NUL disqualifies
 * the file outright, as does a UTF-8 or UTF-16 byte order mark - those are
 * reported by isUTF8Text and isUnicodeText instead, neither of which this
 * port implements.
 */
static int is_plain_text(XBFile *pFile)
{
    cd_i64 nSize = 0;
    cd_i64 i = 0;
    cd_i64 nControl = 0;
    cd_i64 nPrintable = 0;
    cd_i64 nExtended = 0;
    const unsigned char *pData = NULL;

    if (pFile == NULL) {
        return 0;
    }

    nSize = pFile->nSize;

    if (nSize > 0x8000) {
        nSize = 0x8000;
    }

    if (nSize <= 0) {
        return 0;
    }

    pData = (const unsigned char *)pFile->pData;

    if ((nSize >= 3) && (pData[0] == 0xEF) && (pData[1] == 0xBB) && (pData[2] == 0xBF)) {
        return 0;
    }

    if (nSize >= 2) {
        if (((pData[0] == 0xFF) && (pData[1] == 0xFE)) || ((pData[0] == 0xFE) && (pData[1] == 0xFF))) {
            return 0;
        }
    }

    for (i = 0; i < nSize; i++) {
        unsigned char nByte = pData[i];

        if (nByte == 0) {
            return 0;
        } else if (nByte < 0x09) {
            nControl++;
        } else if ((nByte == 0x09) || (nByte == 0x0A) || (nByte == 0x0D)) {
            nPrintable++;
        } else if ((nByte >= 0x20) && (nByte <= 0x7E)) {
            nPrintable++;
        } else if (nByte >= 0x80) {
            nExtended++;
        } else {
            /* 0x0B, 0x0C and 0x0E..0x1F. */
            nControl++;
        }
    }

    {
        double nTotal = (double)nSize;
        double nPrintableRatio = (double)(nPrintable + nExtended) / nTotal;
        double nControlRatio = (double)nControl / nTotal;
        double nExtendedRatio = (double)nExtended / nTotal;

        return ((nPrintableRatio >= 0.85) && (nControlRatio <= 0.05) && (nExtendedRatio <= 0.50)) ? 1 : 0;
    }
}

/* XBinary::isUTF8TextType over the first 0x2000 bytes: the whole sample has to
 * decode as UTF-8, and without a byte order mark it also needs a few
 * multi-byte sequences and a mostly printable body.                        */
static int is_utf8_text(XBFile *pFile)
{
    cd_i64 nSize = 0;
    cd_i64 i = 0;
    cd_i64 nStart = 0;
    cd_i64 nValid = 0;
    cd_i64 nMultiByte = 0;
    cd_i64 nPrintable = 0;
    int bHasBOM = 0;
    const unsigned char *pData = NULL;

    if (pFile == NULL) {
        return 0;
    }

    nSize = pFile->nSize;

    if (nSize > 0x2000) {
        nSize = 0x2000;
    }

    if (nSize <= 0) {
        return 0;
    }

    pData = (const unsigned char *)pFile->pData;

    if ((nSize >= 3) && (pData[0] == 0xEF) && (pData[1] == 0xBB) && (pData[2] == 0xBF)) {
        bHasBOM = 1;
        nStart = 3;
    }

    for (i = nStart; i < nSize;) {
        unsigned char nByte = pData[i];

        if (nByte == 0) {
            return 0;
        } else if (nByte < 0x80) {
            if ((nByte >= 0x20) || (nByte == 0x09) || (nByte == 0x0A) || (nByte == 0x0D)) {
                nPrintable++;
            }

            nValid++;
            i++;
        } else if ((nByte & 0xE0) == 0xC0) {
            if (((i + 1) >= nSize) || ((pData[i + 1] & 0xC0) != 0x80) || (nByte < 0xC2)) {
                return 0;
            }

            nMultiByte++;
            nValid++;
            i += 2;
        } else if ((nByte & 0xF0) == 0xE0) {
            if (((i + 2) >= nSize) || ((pData[i + 1] & 0xC0) != 0x80) || ((pData[i + 2] & 0xC0) != 0x80)) {
                return 0;
            }

            if ((nByte == 0xE0) && (pData[i + 1] < 0xA0)) {
                return 0;
            }

            nMultiByte++;
            nValid++;
            i += 3;
        } else if ((nByte & 0xF8) == 0xF0) {
            if (((i + 3) >= nSize) || ((pData[i + 1] & 0xC0) != 0x80) || ((pData[i + 2] & 0xC0) != 0x80) || ((pData[i + 3] & 0xC0) != 0x80)) {
                return 0;
            }

            if ((nByte == 0xF0) && (pData[i + 1] < 0x90)) {
                return 0;
            }

            if ((nByte > 0xF4) || ((nByte == 0xF4) && (pData[i + 1] > 0x8F))) {
                return 0;
            }

            nMultiByte++;
            nValid++;
            i += 4;
        } else {
            return 0;
        }
    }

    if (bHasBOM) {
        return (nValid > 0) ? 1 : 0;
    }

    if (nValid > 0) {
        double nPrintableRatio = (double)nPrintable / (double)nValid;
        double nMultiByteRatio = (double)nMultiByte / (double)nValid;

        return ((nMultiByteRatio > 0.05) && (nPrintableRatio >= 0.70)) ? 1 : 0;
    }

    return 0;
}

/* XBinary::getUnicodeType over the first 0x1000 bytes: the byte order mark
 * first, then the alternating-NUL heuristic over a 512 byte sample.
 * 0 is UNICODE_TYPE_NONE, 1 is _LE and 2 is _BE.                           */
static int unicode_type(XBFile *pFile)
{
    cd_i64 nSize = 0;
    cd_i64 nSample = 0;
    cd_i64 i = 0;
    cd_i64 nNull = 0;
    cd_i64 nEvenNulls = 0;
    cd_i64 nOddNulls = 0;
    cd_i64 nPrintable = 0;
    const unsigned char *pData = NULL;

    if (pFile == NULL) {
        return 0;
    }

    nSize = pFile->nSize;

    if (nSize > 0x1000) {
        nSize = 0x1000;
    }

    if (nSize <= 0) {
        return 0;
    }

    pData = (const unsigned char *)pFile->pData;

    if (nSize >= 2) {
        cd_u16 nSymbol = (cd_u16)(pData[0] | ((cd_u16)pData[1] << 8));

        if (nSymbol == 0xFFFE) {
            return 2;
        } else if (nSymbol == 0xFEFF) {
            return 1;
        }
    }

    if (nSize < 4) {
        return 0;
    }

    nSample = (nSize < 512) ? nSize : 512;

    for (i = 0; i < nSample; i++) {
        unsigned char nByte = pData[i];

        if (nByte == 0) {
            nNull++;

            if ((i % 2) == 0) {
                nEvenNulls++;
            } else {
                nOddNulls++;
            }
        } else if ((nByte >= 0x20) && (nByte <= 0x7E)) {
            nPrintable++;
        }
    }

    if ((nNull > 0) && (nSample > 4)) {
        double nNullRatio = (double)nNull / (double)nSample;
        double nPrintableRatio = (double)nPrintable / (double)nSample;

        if ((nNullRatio >= 0.30) && (nPrintableRatio >= 0.30)) {
            cd_i64 nLEPairs = 0;
            cd_i64 nBEPairs = 0;

            if (nEvenNulls > (nOddNulls * 2)) {
                return 1;
            } else if (nOddNulls > (nEvenNulls * 2)) {
                return 2;
            }

            for (i = 0; i < (nSample - 1); i += 2) {
                if ((pData[i + 1] == 0) && (pData[i] >= 0x20) && (pData[i] <= 0x7E)) {
                    nLEPairs++;
                }

                if ((pData[i] == 0) && (pData[i + 1] >= 0x20) && (pData[i + 1] <= 0x7E)) {
                    nBEPairs++;
                }
            }

            if (nLEPairs > nBEPairs) {
                return 1;
            } else if (nBEPairs > nLEPairs) {
                return 2;
            }
        }
    }

    return 0;
}

/* --------------------------------------------------- operation system  ---
 *
 * Reproduces XPE::getOperatingSystemVersionsS and the IMAGE_FILE_HEADER
 * machine table, which together make up the "Operation system" line that
 * db/PE/_PE.0.sg prints under --verbose.
 */

typedef struct {
    unsigned int nId;
    const char *pName;
} XIdName;

static const XIdName g_windowsVersions[] = {
    {0x0003000A, "NT 3.1"},   {0x00030032, "NT 3.5"},     {0x00030033, "NT 3.51"},
    {0x00040000, "95"},       {0x00040001, "98"},         {0x00040009, "Millenium"},
    {0x00050000, "2000"},     {0x00050001, "XP"},         {0x00050002, "Server 2003"},
    {0x00060000, "Vista"},    {0x00060001, "7"},          {0x00060002, "8"},
    {0x00060003, "8.1"},      {0x000A0000, "10"}
};

static const XIdName g_peMachines[] = {
    {0x0000, "UNKNOWN"},     {0x014c, "I386"},        {0x014d, "I486"},      {0x014e, "PENTIUM"},
    {0x0160, "R3000_BE"},    {0x0162, "R3000"},       {0x0166, "R4000"},     {0x0168, "R10000"},
    {0x0169, "WCEMIPSV2"},   {0x0184, "ALPHA"},       {0x01F0, "POWERPC"},   {0x01a2, "SH3"},
    {0x01a3, "SH3DSP"},      {0x01a4, "SH3E"},        {0x01a6, "SH4"},       {0x01a8, "SH5"},
    {0x01c0, "ARM"},         {0x01c2, "THUMB"},       {0x01c4, "ARMNT"},     {0x01d3, "AM33"},
    {0x01f1, "POWERPCFP"},   {0x01f2, "POWERPCBE"},   {0x0200, "IA64"},      {0x0266, "MIPS16"},
    {0x0284, "ALPHA64"},     {0x0366, "MIPSFPU"},     {0x0466, "MIPSFPU16"}, {0x0520, "TRICORE"},
    {0x0CEF, "CEF"},         {0x0EBC, "EBC"},         {0x5032, "RISCV32"},   {0x5064, "RISCV64"},
    {0x5128, "RISCV128"},    {0x6232, "LOONGARCH32"}, {0x6264, "LOONGARCH64"},
    {0x8664, "AMD64"},       {0x9041, "M32R"},        {0xAA64, "ARM64"},     {0xC0EE, "CEE"},
    {0xfd1d, "AMD64_LINUX_NI"}
};

static const char *windows_version_name(unsigned int nVersion)
{
    size_t i = 0;

    if (nVersion == 0) {
        return NULL;
    }

    for (i = 0; i < sizeof(g_windowsVersions) / sizeof(g_windowsVersions[0]); i++) {
        if (g_windowsVersions[i].nId == nVersion) {
            return g_windowsVersions[i].pName;
        }
    }

    return NULL;
}

static const char *pe_machine_name(unsigned int nMachine)
{
    size_t i = 0;

    /* XPE::_getMachine folds the Linux native-OS override onto AMD64. */
    if (nMachine == (0xFD1D ^ 0x8664)) {
        nMachine = 0x8664;
    }

    for (i = 0; i < sizeof(g_peMachines) / sizeof(g_peMachines[0]); i++) {
        if (g_peMachines[i].nId == nMachine) {
            return g_peMachines[i].pName;
        }
    }

    return "Unknown";
}

/* --------------------------------------------------------- PDF encryption -
 *
 * XPDF::isEncrypted is "getEncryption() is not empty", and getEncryption
 * produces a descriptor exactly when XPDF::findEncryptObjectIndex finds an
 * object whose first "/Filter" part is followed by "/Standard". The trailer
 * /Encrypt reference the reference then prefers only picks between such
 * objects - it never decides whether one exists - so the boolean the script
 * asks for needs nothing beyond the token lists. getEncryption reads the
 * objects with getParts(256).
 */
static int pdf_is_encrypted(XPDF *pPdf)
{
    size_t i = 0;

    for (i = 0; i < pPdf->vecObjects.nSize; i++) {
        CDVec *pParts = (CDVec *)pPdf->vecObjects.ppData[i];
        size_t nLimit = pParts->nSize;
        size_t j = 0;

        if (nLimit > 256) {
            nLimit = 256;
        }

        for (j = 0; j < nLimit; j++) {
            if (x_strcmp((const char *)pParts->ppData[j], "/Filter") == 0) {
                if (((j + 1) < nLimit) && (x_strcmp((const char *)pParts->ppData[j + 1], "/Standard") == 0)) {
                    return 1;
                }

                break;
            }
        }
    }

    return 0;
}

/* ------------------------------------------------------------ dispatcher */

static JSVal api_dispatch(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    ApiId id = (ApiId)(size_t)pUser;
    XBFile *pFile = &pEngine->file;
    XPE *pPE = &pEngine->pe;
    XBMemoryMap *pMap = pEngine->pMap;

    (void)thisVal;

    switch (id) {
        /* ------------------------------------------------ generic binary */
        case A_getSize: return js_num((double)pFile->nSize);

        case A_compare: {
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 0);
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            int bResult = xb_compare_signature(pFile, pMap, pSignature, nOffset);

            cd_free(pSignature);

            return js_bool(bResult);
        }

        case A_compareEP: {
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 0);
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            cd_i64 nEntryPoint = pe_entry_point_offset(pEngine);
            int bResult = 0;

            if (nEntryPoint != -1) {
                bResult = xb_compare_signature(pFile, pMap, pSignature, nEntryPoint + nOffset);
            }

            cd_free(pSignature);

            return js_bool(bResult);
        }

        case A_compareOverlay: {
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 0);
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            int bResult = 0;

            if (pEngine->bHasPE && (pPE->nOverlaySize > 0)) {
                bResult = xb_compare_signature(pFile, pMap, pSignature, pPE->nOverlayOffset + nOffset);
            }

            cd_free(pSignature);

            return js_bool(bResult);
        }

        case A_readByte:
        case A_read_uint8: return js_num((double)xb_u8(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0)));

        case A_readSByte:
        case A_read_int8: return js_num((double)xb_i8(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0)));

        case A_readWord: return js_num((double)xb_u16(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), 0));
        case A_readSWord: return js_num((double)xb_i16(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), 0));
        case A_readDword: return js_num((double)xb_u32(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), 0));
        case A_readSDword: return js_num((double)xb_i32(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), 0));
        case A_readQword: return js_num((double)xb_u64(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), 0));
        case A_readSQword: return js_num((double)xb_i64(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), 0));

        case A_read_uint16: return js_num((double)xb_u16(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0)));
        case A_read_int16: return js_num((double)xb_i16(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0)));
        case A_read_uint24: return js_num((double)xb_u24(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0)));
        case A_read_int24: {
            cd_u32 nValue = xb_u24(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0));

            if (nValue & 0x800000u) {
                return js_num((double)(cd_i32)(nValue | 0xFF000000u));
            }

            return js_num((double)nValue);
        }
        case A_read_uint32: return js_num((double)xb_u32(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0)));
        case A_read_int32: return js_num((double)xb_i32(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0)));
        case A_read_uint64: return js_num((double)xb_u64(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0)));
        case A_read_int64: return js_num((double)xb_i64(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0)));
        case A_read_float: return js_num((double)xb_f32(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0)));
        case A_read_double: return js_num(xb_f64(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0)));
        case A_read_float16: return js_num((double)xb_f16(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_bool(pCtx, nArgc, pArgv, 1, 0)));
        case A_read_UUID: return str_take(pCtx, xb_uuid(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0)));
        case A_read_ucsdString: return str_take(pCtx, xb_ucsd_string(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0)));

        case A_isNE: return js_bool(xb_u16(pFile, (cd_i64)xb_u32(pFile, 0x3C, 0), 0) == 0x454E);
        case A_isLE: return js_bool(xb_u16(pFile, (cd_i64)xb_u32(pFile, 0x3C, 0), 0) == 0x454C);
        case A_isLX: return js_bool(xb_u16(pFile, (cd_i64)xb_u32(pFile, 0x3C, 0), 0) == 0x584C);

        case A_getApplicationIdentifier: return str_take(pCtx, iso9660_identifier(pFile, 574, 128));
        case A_getDataPreparerIdentifier: return str_take(pCtx, iso9660_identifier(pFile, 446, 128));

        case A_isArchiveRecordPresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasZip ? xzip_record_present(&pEngine->zip, pName) : 0;

            cd_free(pName);

            return js_bool(bResult);
        }

        case A_isArchiveRecordPresentExp: {
            char *pPattern = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = archive_record_present_exp(pEngine, pPattern);

            cd_free(pPattern);

            return js_bool(bResult);
        }

        case A_getManifestRecord: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal result = str_take(pCtx, pEngine->bHasZip ? xzip_manifest_record(&pEngine->zip, pKey) : cd_strdup(""));

            cd_free(pKey);

            return result;
        }

        case A_getPackageJsonRecord: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal result = str_take(pCtx, pEngine->bHasZip ? xzip_packagejson_record(&pEngine->zip, pKey) : cd_strdup(""));

            cd_free(pKey);

            return result;
        }

        case A_isConstPresent: {
            char *pValue = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasPyc ? xpyc_const_present(&pEngine->pyc, pValue) : 0;

            cd_free(pValue);

            return js_bool(bResult);
        }

        /* Util: exact 64-bit arithmetic (JS numbers are doubles). Mirrors
         * Util_script; a zero divisor yields -1 as in the reference, and the
         * shifts take the low 6 bits of the count as x86 does. */
        case A_div64: {
            cd_i64 nA = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nB = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            cd_i64 nResult = (cd_i64)-1;

            if (nB != 0) {
                /* Avoid the INT64_MIN / -1 overflow (a hardware trap on x86);
                 * the database never divides those, so this only guards a
                 * crafted call. */
                if ((nA == (-0x7FFFFFFFFFFFFFFFll - 1)) && (nB == (cd_i64)-1)) {
                    nResult = nA;
                } else {
                    nResult = nA / nB;
                }
            }

            return js_num((double)nResult);
        }
        case A_divu64: {
            cd_u64 nA = arg_u64(pCtx, nArgc, pArgv, 0);
            cd_u64 nB = arg_u64(pCtx, nArgc, pArgv, 1);

            return js_num((double)(nB ? (nA / nB) : (cd_u64)0xFFFFFFFFFFFFFFFFULL));
        }
        case A_shlu64: {
            cd_u64 nValue = arg_u64(pCtx, nArgc, pArgv, 0);
            cd_u64 nShift = arg_u64(pCtx, nArgc, pArgv, 1);

            return js_num((double)(nValue << (nShift & 63)));
        }
        case A_shru64: {
            cd_u64 nValue = arg_u64(pCtx, nArgc, pArgv, 0);
            cd_u64 nShift = arg_u64(pCtx, nArgc, pArgv, 1);

            return js_num((double)(nValue >> (nShift & 63)));
        }
        case A_shl64: {
            cd_i64 nValue = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nShift = arg_i64(pCtx, nArgc, pArgv, 1, 0);

            /* Shifting into the sign bit is undefined for a signed left
             * operand; the unsigned shift has the two's-complement result
             * the reference gets out of qint64 << qint64.                 */
            return js_num((double)(cd_i64)((cd_u64)nValue << (nShift & 63)));
        }
        case A_shr64: {
            cd_i64 nValue = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nShift = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            int nCount = (int)(nShift & 63);

            /* qint64 >> n is an arithmetic shift; spelled out through the
             * complement so it does not rest on implementation-defined
             * behaviour for a negative left operand.                      */
            if (nValue < 0) {
                return js_num((double)(cd_i64)~((cd_u64)(~nValue) >> nCount));
            }

            return js_num((double)(nValue >> nCount));
        }
        case A_secondsToTimeStr: {
            /* QTime(0, 0).addSecs(n).toString(): addSecs reduces the count
             * modulo a day, addMSecs folds a negative result back into the
             * day, and the default format is HH:mm:ss.                    */
            cd_i32 nValue = (cd_i32)((nArgc > 0) ? js_to_int32(pCtx, pArgv[0]) : 0);
            cd_i32 nSeconds = nValue % 86400;
            char sBuf[32];

            if (nSeconds < 0) {
                nSeconds += 86400;
            }

            x_snprintf(sBuf, sizeof(sBuf), "%02d:%02d:%02d", (int)(nSeconds / 3600), (int)((nSeconds / 60) % 60), (int)(nSeconds % 60));

            return js_str(pCtx, sBuf);
        }

        case A_getString:
        case A_read_ansiString: return str_take(pCtx, xb_ansi_string(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 50)));

        case A_read_unicodeString: return str_take(pCtx, xb_unicode_string(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 50), 0));

        case A_read_utf8String: return str_take(pCtx, xb_utf8_string(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 50)));

        case A_read_codePageString: return str_take(pCtx, xb_ansi_string(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 256)));

        case A_findSignature: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 2);
            cd_i64 nResult = 0;
            cd_i64 nStart = cdie_profile_start(pEngine);

            fix_offset_size(pEngine, &nOffset, &nSize);
            nResult = xb_find_signature(pFile, pMap, nOffset, nSize, pSignature);

            if (nStart >= 0) {
                char sOffset[24];
                char sSize[24];

                value_to_hex_ex((cd_u64)nOffset, sOffset, sizeof(sOffset));
                value_to_hex_ex((cd_u64)nSize, sSize, sizeof(sSize));
                /* The reference spells this one with an underscore. */
                cdie_profile_end(pEngine, nStart, "find_signature[%s]: %s %s", pSignature, sOffset, sSize);
            }

            cd_free(pSignature);

            return js_num((double)nResult);
        }

        case A_findString:
        case A_find_ansiString: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            char *pString = arg_string(pCtx, nArgc, pArgv, 2);
            cd_i64 nResult = 0;
            /* Only findString is measured; find_ansiString is the same search
             * under a second name and the reference does not profile it. */
            cd_i64 nStart = (id == A_findString) ? cdie_profile_start(pEngine) : -1;

            fix_offset_size(pEngine, &nOffset, &nSize);
            nResult = xb_find_ansi_string(pFile, nOffset, nSize, pString);

            if (nStart >= 0) {
                char sOffset[24];
                char sSize[24];

                value_to_hex_ex((cd_u64)nOffset, sOffset, sizeof(sOffset));
                value_to_hex_ex((cd_u64)nSize, sSize, sizeof(sSize));
                cdie_profile_end(pEngine, nStart, "findString[%s]: %s %s", pString, sOffset, sSize);
            }

            cd_free(pString);

            return js_num((double)nResult);
        }

        case A_find_unicodeString: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            char *pString = arg_string(pCtx, nArgc, pArgv, 2);
            cd_i64 nResult = 0;

            fix_offset_size(pEngine, &nOffset, &nSize);
            nResult = xb_find_unicode_string(pFile, nOffset, nSize, pString, 0);
            cd_free(pString);

            return js_num((double)nResult);
        }

        case A_find_utf8String: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            char *pString = arg_string(pCtx, nArgc, pArgv, 2);
            cd_i64 nResult = 0;

            fix_offset_size(pEngine, &nOffset, &nSize);
            nResult = xb_find_ansi_string(pFile, nOffset, nSize, pString);
            cd_free(pString);

            return js_num((double)nResult);
        }

        case A_findByte: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            cd_u8 nValue = (cd_u8)arg_i64(pCtx, nArgc, pArgv, 2, 0);
            cd_i64 nResult = 0;
            cd_i64 nStart = cdie_profile_start(pEngine);

            fix_offset_size(pEngine, &nOffset, &nSize);
            nResult = xb_find_u8(pFile, nOffset, nSize, nValue);

            if (nStart >= 0) {
                char sOffset[24];
                char sSize[24];

                value_to_hex_ex((cd_u64)nOffset, sOffset, sizeof(sOffset));
                value_to_hex_ex((cd_u64)nSize, sSize, sizeof(sSize));
                /* XBinary::valueToHex of a fixed-width integer: always the
                 * full field, unlike valueToHexEx above. */
                cdie_profile_end(pEngine, nStart, "findByte[%0*llx]: %s %s", 2, (unsigned long long)nValue, sOffset, sSize);
            }

            return js_num((double)nResult);
        }

        case A_findWord: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            cd_u16 nValue = (cd_u16)arg_i64(pCtx, nArgc, pArgv, 2, 0);
            cd_i64 nResult = 0;
            cd_i64 nStart = cdie_profile_start(pEngine);

            fix_offset_size(pEngine, &nOffset, &nSize);
            nResult = xb_find_u16(pFile, nOffset, nSize, nValue);

            if (nStart >= 0) {
                char sOffset[24];
                char sSize[24];

                value_to_hex_ex((cd_u64)nOffset, sOffset, sizeof(sOffset));
                value_to_hex_ex((cd_u64)nSize, sSize, sizeof(sSize));
                /* XBinary::valueToHex of a fixed-width integer: always the
                 * full field, unlike valueToHexEx above. */
                cdie_profile_end(pEngine, nStart, "findWord[%0*llx]: %s %s", 4, (unsigned long long)nValue, sOffset, sSize);
            }

            return js_num((double)nResult);
        }

        case A_findDword: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            cd_u32 nValue = (cd_u32)arg_i64(pCtx, nArgc, pArgv, 2, 0);
            cd_i64 nResult = 0;
            cd_i64 nStart = cdie_profile_start(pEngine);

            fix_offset_size(pEngine, &nOffset, &nSize);
            nResult = xb_find_u32(pFile, nOffset, nSize, nValue);

            if (nStart >= 0) {
                char sOffset[24];
                char sSize[24];

                value_to_hex_ex((cd_u64)nOffset, sOffset, sizeof(sOffset));
                value_to_hex_ex((cd_u64)nSize, sSize, sizeof(sSize));
                /* XBinary::valueToHex of a fixed-width integer: always the
                 * full field, unlike valueToHexEx above. */
                cdie_profile_end(pEngine, nStart, "findDword[%0*llx]: %s %s", 8, (unsigned long long)nValue, sOffset, sSize);
            }

            return js_num((double)nResult);
        }

        case A_getEntryPointOffset: return js_num((double)pe_entry_point_offset(pEngine));
        case A_getOverlayOffset:
            if (pEngine->bHasElf) {
                return js_num((double)pEngine->elf.nOverlayOffset);
            }

            return js_num((double)(pEngine->bHasPE ? pPE->nOverlayOffset : -1));

        case A_getOverlaySize:
            if (pEngine->bHasElf) {
                return js_num((double)pEngine->elf.nOverlaySize);
            }

            return js_num((double)(pEngine->bHasPE ? pPE->nOverlaySize : 0));

        case A_isOverlayPresent:
            if (pEngine->bHasElf) {
                return js_bool(pEngine->elf.nOverlaySize != 0);
            }

            return js_bool(pEngine->bHasPE && (pPE->nOverlaySize != 0));
        case A_getAddressOfEntryPoint: return js_num((double)(pEngine->bHasPE ? pPE->nEntryPointAddress : 0));

        case A_isSignaturePresent: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 2);
            int bResult = 0;
            cd_i64 nStart = cdie_profile_start(pEngine);

            fix_offset_size(pEngine, &nOffset, &nSize);
            bResult = (xb_find_signature(pFile, pMap, nOffset, nSize, pSignature) != -1) ? 1 : 0;

            if (nStart >= 0) {
                char sOffset[24];
                char sSize[24];

                value_to_hex_ex((cd_u64)nOffset, sOffset, sizeof(sOffset));
                value_to_hex_ex((cd_u64)nSize, sSize, sizeof(sSize));
                cdie_profile_end(pEngine, nStart, "isSignaturePresent[%s]: %s %s", pSignature, sOffset, sSize);
            }

            cd_free(pSignature);

            return js_bool(bResult);
        }

        case A_swapBytes: {
            cd_u32 nValue = (cd_u32)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            return js_num((double)(cd_u32)(((nValue & 0xFF) << 24) | ((nValue & 0xFF00) << 8) | ((nValue >> 8) & 0xFF00) | ((nValue >> 24) & 0xFF)));
        }

        case A_RVAToOffset: return js_num((double)xbmap_address_to_offset(pMap, pMap->nModuleAddress + (cd_u64)arg_i64(pCtx, nArgc, pArgv, 0, 0)));
        case A_VAToOffset: return js_num((double)xbmap_address_to_offset(pMap, (cd_u64)arg_i64(pCtx, nArgc, pArgv, 0, 0)));
        case A_OffsetToVA: return js_num((double)xbmap_offset_to_address(pMap, arg_i64(pCtx, nArgc, pArgv, 0, 0)));
        case A_OffsetToRVA: {
            cd_i64 nAddress = xbmap_offset_to_address(pMap, arg_i64(pCtx, nArgc, pArgv, 0, 0));

            if (nAddress == -1) {
                return js_num(-1);
            }

            return js_num((double)(nAddress - (cd_i64)pMap->nModuleAddress));
        }

        case A_getFileDirectory: return str_take(pCtx, cd_path_dir(pFile->pFileName));
        case A_getFileBaseName: return str_take(pCtx, cd_path_base_name(pFile->pFileName));
        case A_getFileCompleteSuffix: return str_take(pCtx, cd_path_complete_suffix(pFile->pFileName));
        case A_getFileSuffix: return str_take(pCtx, cd_path_suffix(pFile->pFileName));

        case A_getSignature: return str_take(pCtx, xb_signature_hex(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 0)));

        case A_calculateEntropy: return js_num(xb_entropy(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 0)));
        case A_isZeroFilled: return js_bool(xb_is_zero_filled(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 0)));
        case A_calculateMD5: return str_take(pCtx, xb_md5(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 0)));
        case A_calculateCRC32: return js_num((double)xb_crc32(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 0), 0));
        case A_crc32: return js_num((double)xb_crc32(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 0), (cd_u32)arg_i64(pCtx, nArgc, pArgv, 2, 0)));
        case A_crc16: return js_num((double)xb_crc16(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 0), (cd_u16)arg_i64(pCtx, nArgc, pArgv, 2, 0)));
        case A_adler32: return js_num((double)xb_adler32(pFile, arg_i64(pCtx, nArgc, pArgv, 0, 0), arg_i64(pCtx, nArgc, pArgv, 1, 0)));

        case A_isSignatureInSectionPresent: {
            cd_i64 nNumber = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 1);
            int bResult = 0;
            int nIndex = (int)nNumber + (pEngine->bHasPE ? 1 : 0);
            cd_i64 nStart = cdie_profile_start(pEngine);

            if ((nIndex >= 0) && (nIndex < pMap->nCount)) {
                bResult = (xb_find_signature(pFile, pMap, pMap->pRecords[nIndex].nOffset, pMap->pRecords[nIndex].nSize, pSignature) != -1) ? 1 : 0;
            }

            /* The reference passes the section number as a plain decimal and
             * leaves the trailing space of its two-argument format. */
            cdie_profile_end(pEngine, nStart, "isSignatureInSectionPresent[%s]: %lld ", pSignature, (long long)nNumber);
            cd_free(pSignature);

            return js_bool(bResult);
        }

        case A_getImageBase: return js_num((double)pMap->nModuleAddress);

        case A_upperCase:
        case A_lowerCase: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            size_t i = 0;

            for (i = 0; pString[i]; i++) {
                if (id == A_upperCase) {
                    if ((pString[i] >= 'a') && (pString[i] <= 'z')) {
                        pString[i] = (char)(pString[i] - 'a' + 'A');
                    }
                } else {
                    if ((pString[i] >= 'A') && (pString[i] <= 'Z')) {
                        pString[i] = (char)(pString[i] - 'A' + 'a');
                    }
                }
            }

            return str_take(pCtx, pString);
        }

        case A_isPlainText: return js_bool(is_plain_text(pFile));

        case A_isUTF8Text: return js_bool(is_utf8_text(pFile));
        /* m_bIsUnicodeText is set only on the getUnicodeType branch of the
         * constructor, so it means "not UNICODE_TYPE_NONE".                */
        case A_isUnicodeText: return js_bool(unicode_type(pFile) != 0);
        /* isText ORs the three flags together, as the reference does. */
        case A_isText: return js_bool(is_plain_text(pFile) || is_utf8_text(pFile) || (unicode_type(pFile) != 0));

        /* Binary_Script's constructor fills m_sHeaderString with the head of
         * the file in the first encoding that answers: read_unicodeString
         * past the byte order mark, then read_utf8String past a UTF-8 mark,
         * then read_ansiString from the start. Anything that is not text at
         * all keeps the empty string the member was constructed with. The
         * length is always qMin(size, 0x1000).                             */
        case A_getHeaderString: {
            cd_i64 nMaxSize = pFile ? pFile->nSize : 0;
            int nUnicode = unicode_type(pFile);

            if (nMaxSize > 0x1000) {
                nMaxSize = 0x1000;
            }

            if (nUnicode != 0) {
                return str_take(pCtx, xb_unicode_string(pFile, 2, nMaxSize, (nUnicode == 2) ? 1 : 0));
            }

            if (is_utf8_text(pFile)) {
                return str_take(pCtx, xb_utf8_string(pFile, 3, nMaxSize));
            }

            if (is_plain_text(pFile)) {
                return str_take(pCtx, xb_ansi_string(pFile, 0, nMaxSize));
            }

            return js_str(pCtx, "");
        }

        case A_getDisasmLength: {
            cd_i64 nAddress = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nOffset = xbmap_address_to_offset(pMap, (cd_u64)nAddress);
            XDisasmResult disasm;

            if (nOffset == -1) {
                return js_num(0);
            }

            disasm = xdisasm(pFile, nOffset, pMap->nBits);

            return js_num((double)disasm.nSize);
        }

        case A_getDisasmString: {
            cd_i64 nAddress = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nOffset = xbmap_address_to_offset(pMap, (cd_u64)nAddress);
            XDisasmResult disasm;

            if (nOffset == -1) {
                return js_str(pCtx, "");
            }

            disasm = xdisasm(pFile, nOffset, pMap->nBits);

            return js_str(pCtx, disasm.sMnemonic);
        }

        case A_getDisasmNextAddress: {
            cd_i64 nAddress = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nOffset = xbmap_address_to_offset(pMap, (cd_u64)nAddress);
            XDisasmResult disasm;

            if (nOffset == -1) {
                return js_num((double)(nAddress + 1));
            }

            disasm = xdisasm(pFile, nOffset, pMap->nBits);

            return js_num((double)(nAddress + disasm.nSize));
        }

        case A_is16: return js_bool(pMap->nBits == 16);
        case A_is32: return js_bool(pMap->nBits == 32);
        case A_is64: return js_bool(pMap->nBits == 64);

        case A_isDeepScan: return js_bool(pEngine->pOptions->bDeepScan);
        case A_isHeuristicScan: return js_bool(pEngine->pOptions->bHeuristicScan);
        case A_isFirstWrapperScan: return js_bool(0);
        case A_isAggressiveScan: return js_bool(pEngine->pOptions->bAggressiveScan);
        case A_isRecursiveScan: return js_bool(pEngine->pOptions->bRecursiveScan);
        case A_isOverlayScan: return js_bool(0);
        case A_isVerbose: return js_bool(pEngine->pOptions->bVerbose);
        case A_isProfiling: return js_bool(pEngine->pOptions->bProfiling);
        case A_getScanID: return js_str(pCtx, "");
        case A_getStartOffset: return js_num(0);

        /* XBinary::bytesCountToString with the default base of 1024 that
         * Binary_Script::bytesCountToString leaves in place: the plain byte
         * count below the base, otherwise the scaled value with exactly two
         * fraction digits. QString::number(d, 'f', 2) rounds ties away from
         * zero, which is what x_dtoa_fixed does.                           */
        case A_bytesCountToString: {
            cd_u64 nValue = arg_u64(pCtx, nArgc, pArgv, 0);
            const cd_u64 nBase = 1024;
            char sValue[64];
            char sBuf[96];

            if (nValue < nBase) {
                x_snprintf(sBuf, sizeof(sBuf), "%llu Bytes", (unsigned long long)nValue);
            } else if (nValue < (nBase * nBase)) {
                x_dtoa_fixed((double)nValue / (double)nBase, 2, sValue, sizeof(sValue));
                x_snprintf(sBuf, sizeof(sBuf), "%s KiB", sValue);
            } else if (nValue < (nBase * nBase * nBase)) {
                x_dtoa_fixed((double)nValue / (double)(nBase * nBase), 2, sValue, sizeof(sValue));
                x_snprintf(sBuf, sizeof(sBuf), "%s MiB", sValue);
            } else if (nValue < (nBase * nBase * nBase * nBase)) {
                x_dtoa_fixed((double)nValue / (double)(nBase * nBase * nBase), 2, sValue, sizeof(sValue));
                x_snprintf(sBuf, sizeof(sBuf), "%s GiB", sValue);
            } else {
                x_dtoa_fixed((double)nValue / (double)(nBase * nBase * nBase * nBase), 2, sValue, sizeof(sValue));
                x_snprintf(sBuf, sizeof(sBuf), "%s TiB", sValue);
            }

            return js_str(pCtx, sBuf);
        }

        case A_getOperationSystemName:
            if (pEngine->bHasDex) {
                return js_str(pCtx, "Android");
            }

            if (pEngine->bHasMach) {
                return js_str(pCtx, pEngine->mach.sOsName);
            }

            if (pEngine->bHasElf) {
                return js_str(pCtx, pEngine->elf.sOsName);
            }

            if (pEngine->fileType == XFT_NE) {
                return js_str(pCtx, ne_os_name(ne_target_os(pFile)));
            }

            if (pEngine->fileType == XFT_JAR) {
                /* XJAR::getFileFormatInfo: osName = OSNAME_JVM. */
                return js_str(pCtx, "JVM");
            }

            if (pEngine->fileType == XFT_COM) {
                /* XCOM::getOsName: OSNAME_MSDOS. */
                return js_str(pCtx, "MS-DOS");
            }

            return js_str(pCtx, "Windows");

        /* XPE::getFileFormatInfo: the raw version number is mapped to a
         * marketing name, and a 64-bit image is floored at Server 2003
         * because no earlier Windows had a 64-bit edition. Anything the
         * table does not know falls back to Server 2003 or XP by bitness,
         * so this never reports a bare number.                            */
        case A_getOperationSystemVersion: {
            unsigned int nVersion = 0;
            int bIs64 = 0;
            const char *pName = NULL;

            if (pEngine->bHasDex) {
                return js_str(pCtx, xdex_android_version(&pEngine->dex));
            }

            if (pEngine->bHasMach) {
                return js_str(pCtx, pEngine->mach.sOsVersion);
            }

            if (pEngine->bHasElf) {
                return js_str(pCtx, pEngine->elf.sOsVersion);
            }

            if (pEngine->fileType == XFT_JAR) {
                /* XJAR: Java release read from the first *.class member. */
                return js_str(pCtx, pEngine->zip.sJvmVersion);
            }

            if (!pEngine->bHasPE) {
                return js_str(pCtx, "");
            }

            bIs64 = (pMap->nBits == 64) ? 1 : 0;
            nVersion = ((unsigned int)pPE->nMajorOperatingSystemVersion << 16) | (unsigned int)pPE->nMinorOperatingSystemVersion;

            if (bIs64 && (nVersion < 0x00050002)) {
                nVersion = 0x00050002;
            }

            pName = windows_version_name(nVersion);

            if (pName == NULL) {
                pName = bIs64 ? "Server 2003" : "XP";
            }

            return js_str(pCtx, pName);
        }

        /* XBinary::getOperationSystemInfoString: "<arch>, <mode>". */
        case A_getOperationSystemOptions: {
            char sBuf[64];

            if (pEngine->bHasDex) {
                /* XDEX: arch is always Dalvik, mode MODE_32. */
                return js_str(pCtx, "Dalvik, 32-bit");
            }

            if (pEngine->bHasMach) {
                x_snprintf(sBuf, sizeof(sBuf), "%s, %d-bit", pEngine->mach.sArch, pEngine->mach.bIs64 ? 64 : 32);

                return js_str(pCtx, sBuf);
            }

            if (pEngine->bHasElf) {
                x_snprintf(sBuf, sizeof(sBuf), "%s, %d-bit", pEngine->elf.sArch, pEngine->elf.bIs64 ? 64 : 32);

                return js_str(pCtx, sBuf);
            }

            if (pEngine->fileType == XFT_NE) {
                /* XNE: mode is always MODE_16SEG. */
                x_snprintf(sBuf, sizeof(sBuf), "%s, 16SEG", ne_arch_name(ne_target_os(pFile)));

                return js_str(pCtx, sBuf);
            }

            if (pEngine->fileType == XFT_JAR) {
                /* XJAR: getArch() = "Universal", getMode() = MODE_DATA. */
                return js_str(pCtx, "Universal, Data");
            }

            if (pEngine->fileType == XFT_COM) {
                /* XCOM: getArch() = "8086", getMode() = MODE_16. */
                return js_str(pCtx, "8086, 16-bit");
            }

            if (!pEngine->bHasPE) {
                return js_str(pCtx, "");
            }

            x_snprintf(sBuf, sizeof(sBuf), "%s, %d-bit", pe_machine_name(pPE->nMachine), (int)pMap->nBits);

            return js_str(pCtx, sBuf);
        }
        case A_getFileFormatName: return js_str(pCtx, xft_to_string(pEngine->fileType));

        case A_getFileFormatVersion:
            if (pEngine->bHasJpeg && pEngine->jpeg.pVersion) {
                return js_str(pCtx, pEngine->jpeg.pVersion);
            }

            if (pEngine->bHasPdf) {
                char *pVersion = xpdf_version(&pEngine->pdf);
                JSVal result = js_str(pCtx, pVersion);

                cd_free(pVersion);

                return result;
            }

            if (pEngine->bHasDex) {
                return js_str(pCtx, pEngine->dex.sVersion);
            }

            if (pEngine->bHasPyc) {
                return js_str(pCtx, pEngine->pyc.sVersion);
            }

            return js_str(pCtx, "");

        case A_getFileFormatOptions:
            if (pEngine->bHasPng) {
                char *pInfo = xpng_info_string(&pEngine->png);
                JSVal result = js_str(pCtx, pInfo);

                cd_free(pInfo);

                return result;
            }

            if (pEngine->bHasPdf) {
                char *pInfo = xpdf_info(&pEngine->pdf);
                JSVal result = js_str(pCtx, pInfo);

                cd_free(pInfo);

                return result;
            }

            if (pEngine->bHasDex) {
                char *pHex = xdex_map_hash_hex(&pEngine->dex);
                JSVal result = js_str(pCtx, pHex);

                cd_free(pHex);

                return result;
            }

            return js_str(pCtx, "");

        /* ------------------------------------------------------- JPEG */
        case A_getComment: return js_str(pCtx, (pEngine->bHasJpeg && pEngine->jpeg.pComment) ? pEngine->jpeg.pComment : "");
        case A_getDqtMD5: return js_str(pCtx, (pEngine->bHasJpeg && pEngine->jpeg.pDqtMD5) ? pEngine->jpeg.pDqtMD5 : "");
        case A_isExifPresent: return js_bool(pEngine->bHasJpeg && (pEngine->jpeg.nExifSize > 0));
        case A_getExifCameraName: return js_str(pCtx, (pEngine->bHasJpeg && pEngine->jpeg.pExifCameraName) ? pEngine->jpeg.pExifCameraName : "");

        case A_isChunkPresent: {
            cd_i64 nId = arg_i64(pCtx, nArgc, pArgv, 0, 0);

            return js_bool(pEngine->bHasJpeg && xjpeg_is_chunk_present(&pEngine->jpeg, (cd_u8)nId));
        }

        /* ------------------------------------------------------- APK  */
        case A_getAndroidManifestRecord: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            char *pValue = pEngine->bHasApk ? xapk_manifest_record(&pEngine->apk, pKey) : cd_strdup("");
            JSVal result = js_str(pCtx, pValue);

            cd_free(pKey);
            cd_free(pValue);

            return result;
        }

        case A_getAndroidManifest:
            return js_str(pCtx, (pEngine->bHasApk && pEngine->apk.pManifestText) ? pEngine->apk.pManifestText : "");

        /* ------------------------------------------------------- PDF  */
        case A_getHeaderCommentAsHex: {
            char *pHex = pEngine->bHasPdf ? xpdf_header_comment_hex(&pEngine->pdf) : cd_strdup("");
            JSVal result = js_str(pCtx, pHex);

            cd_free(pHex);

            return result;
        }

        case A_getStringValuesByKey:
        case A_getValuesByKey: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal result = js_new_array(pCtx);

            if (pEngine->bHasPdf) {
                CDVec values;
                size_t i = 0;

                cdvec_init(&values);
                /* pdf_script builds its object list with getParts(20). */
                xpdf_values_by_key(&pEngine->pdf, pKey, (id == A_getStringValuesByKey) ? 1 : 0, 20, &values);

                for (i = 0; i < values.nSize; i++) {
                    js_array_push(pCtx, result, js_str(pCtx, (const char *)values.ppData[i]));
                    cd_free(values.ppData[i]);
                }

                cdvec_free(&values);
            }

            cd_free(pKey);

            return result;
        }

        case A_isValuesHexByKey:
            /* Would report whether a key's value is a PDF hex string "<...>".
             * No stock signature calls it, so it is reported as false rather
             * than threading value-type information through the token list.  */
            return js_bool(0);

        case A_isEncrypted: return js_bool(pEngine->bHasPdf ? pdf_is_encrypted(&pEngine->pdf) : 0);

        /* ------------------------------------------------------- DEX  */
        case A_isDexStringPresent: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasDex ? xdex_string_present(&pEngine->dex, pString) : 0;

            cd_free(pString);

            return js_bool(bResult);
        }

        case A_isDexItemStringPresent: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = pEngine->bHasDex ? xdex_item_string_present(&pEngine->dex, pString) : 0;

            cd_free(pString);

            return js_bool(bResult);
        }

        case A_getMapItemsHash: return js_num((double)(pEngine->bHasDex ? pEngine->dex.nMapHash : 0));

        /* ------------------------------------------------- ELF / Mach-O */
        case A_isStringInTablePresent: {
            char *pSectionName = arg_string(pCtx, nArgc, pArgv, 0);
            char *pString = arg_string(pCtx, nArgc, pArgv, 1);
            int bResult = pEngine->bHasElf ? xelf_string_in_table_present(pFile, &pEngine->elf, pSectionName, pString) : 0;

            cd_free(pSectionName);
            cd_free(pString);

            return js_bool(bResult);
        }

        case A_getNumberOfPrograms: return js_num((double)(pEngine->bHasElf ? pEngine->elf.nPhnum : 0));

        case A_getProgramFileOffset:
            return js_num((double)(pEngine->bHasElf ? xelf_program_offset(&pEngine->elf, (int)arg_i64(pCtx, nArgc, pArgv, 0, 0)) : 0));

        case A_getProgramFileSize:
            return js_num((double)(pEngine->bHasElf ? xelf_program_size(&pEngine->elf, (int)arg_i64(pCtx, nArgc, pArgv, 0, 0)) : 0));

        case A_getElfHeader_type: return js_num((double)(pEngine->bHasElf ? pEngine->elf.nType : 0));
        case A_getElfHeader_machine: return js_num((double)(pEngine->bHasElf ? pEngine->elf.nMachine : 0));
        case A_getElfHeader_entry: return js_num((double)(pEngine->bHasElf ? pEngine->elf.nEntry : 0));
        case A_getElfHeader_phoff: return js_num((double)(pEngine->bHasElf ? pEngine->elf.nPhoff : 0));
        case A_getElfHeader_shoff: return js_num((double)(pEngine->bHasElf ? pEngine->elf.nShoff : 0));
        case A_getElfHeader_phnum: return js_num((double)(pEngine->bHasElf ? pEngine->elf.nPhnum : 0));
        case A_getElfHeader_shnum: return js_num((double)(pEngine->bHasElf ? pEngine->elf.nShnum : 0));
        case A_getElfHeader_shentsize: return js_num((double)(pEngine->bHasElf ? pEngine->elf.nShentsize : 0));
        case A_getElfHeader_phentsize: return js_num((double)(pEngine->bHasElf ? pEngine->elf.nPhentsize : 0));
        case A_getElfHeader_shstrndx: return js_num((double)(pEngine->bHasElf ? pEngine->elf.nShstrndx : 0));

        case A_getRunPath: return js_str(pCtx, (pEngine->bHasElf && pEngine->elf.pRunPath) ? pEngine->elf.pRunPath : "");

        case A_isNotePresent: return js_bool(0);

        case A_getLibraryCurrentVersion: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            cd_u32 nVersion = pEngine->bHasMach ? xmach_library_current_version(&pEngine->mach, pName) : 0;

            cd_free(pName);

            return js_num((double)nVersion);
        }

        case A_isSigned:
        case A_isSignedFile: {
            /* The security directory stores a file offset, not an RVA, and
             * XPE::isSigned() validates that the range is inside the file.  */
            cd_i64 nOffset = 0;
            cd_i64 nSize = 0;

            if (!pEngine->bHasPE) {
                return js_bool(0);
            }

            nOffset = (cd_i64)pPE->pDirRVA[XPE_DIR_SECURITY];
            nSize = (cd_i64)pPE->pDirSize[XPE_DIR_SECURITY];

            return js_bool((nSize > 0) && (nOffset > 0) && (nOffset < pFile->nSize) && (nSize <= pFile->nSize - nOffset));
        }

        case A_cleanString: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            char *pClean = clean_string(pString);

            cd_free(pString);

            return str_take(pCtx, pClean);
        }

        case A_readBytes: {
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            cd_i64 nSize = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            int bReplaceZero = arg_bool(pCtx, nArgc, pArgv, 2, 0);
            JSVal result = js_new_array(pCtx);
            cd_i64 i = 0;

            fix_offset_size(pEngine, &nOffset, &nSize);

            if ((nOffset < 0) || (nOffset >= pFile->nSize) || (nSize < 0)) {
                return result;
            }

            for (i = 0; i < nSize; i++) {
                cd_u8 nByte = xb_u8(pFile, nOffset + i);

                js_set_index(pCtx, result, i, js_num((bReplaceZero && (nByte == 0)) ? 32 : (double)nByte));
            }

            return result;
        }

        case A_isOverlay:
        case A_isResource:
        case A_isDebugData:
        case A_isFilePart: return js_bool(0);

        case A_isReleaseBuild: return js_bool(1);
        case A_isDebugBuild: return js_bool(0);

        case A_isChecksumCorrect:
        case A_isEntryPointCorrect:
        case A_isSectionAlignmentCorrect:
        case A_isFileAlignmentCorrect:
        case A_isHeaderCorrect:
        case A_isRelocsTableCorrect:
        case A_isImportTableCorrect:
        case A_isExportTableCorrect:
        case A_isResourcesTableCorrect:
        case A_isSectionsTableCorrect: return js_bool(1);

        case A_getFormatMessages: return js_new_array(pCtx);
        case A_getListOfCompressionMethods: return js_new_array(pCtx);

        case A_startTiming: {
            /* The reference hands out a random 32-bit handle and keeps the
             * running timer in a map; a counter is the same contract without
             * the collisions. The handle is issued whether or not profiling
             * is on, so a script's endTiming() always finds it. */
            cd_i64 nHandle = ++pEngine->nNextTimingHandle;

            if (pEngine->nTimingCount >= pEngine->nTimingCapacity) {
                pEngine->nTimingCapacity = pEngine->nTimingCapacity ? (pEngine->nTimingCapacity * 2) : 8;
                pEngine->pTimings = (TimingRecord *)cd_realloc(pEngine->pTimings, (size_t)pEngine->nTimingCapacity * sizeof(TimingRecord));
            }

            pEngine->pTimings[pEngine->nTimingCount].nHandle = nHandle;
            pEngine->pTimings[pEngine->nTimingCount].nStart = cdie_profile_start(pEngine);
            pEngine->nTimingCount++;

            return js_num((double)nHandle);
        }

        case A_endTiming: {
            cd_i64 nHandle = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            char *pInfo = arg_string(pCtx, nArgc, pArgv, 1);
            int i = 0;

            for (i = 0; i < pEngine->nTimingCount; i++) {
                if (pEngine->pTimings[i].nHandle != nHandle) {
                    continue;
                }

                cdie_profile_end(pEngine, pEngine->pTimings[i].nStart, "%s", pInfo);
                x_memmove(&pEngine->pTimings[i], &pEngine->pTimings[i + 1], (size_t)(pEngine->nTimingCount - i - 1) * sizeof(TimingRecord));
                pEngine->nTimingCount--;

                break;
            }

            cd_free(pInfo);

            return js_num(0);
        }

        case A_detectZLIB:
        case A_detectGZIP:
        case A_detectZIP: return js_num(-1);

        /* -------------------------------------------------------- MSDOS */
        case A_isRichSignaturePresent: return js_bool(pEngine->bHasPE && pPE->bRichPresent);
        case A_getNumberOfRichIDs: return js_num((double)(pEngine->bHasPE ? pPE->nRichCount : 0));

        case A_isRichVersionPresent: {
            cd_i64 nVersion = arg_i64(pCtx, nArgc, pArgv, 0, 0);
            int i = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nRichCount; i++) {
                    if (pPE->pRichRecords[i].nVersion == (cd_u16)nVersion) {
                        return js_bool(1);
                    }
                }
            }

            return js_bool(0);
        }

        case A_getRichVersion:
        case A_getRichID:
        case A_getRichCount: {
            cd_i64 nIndex = arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nIndex < 0) || (nIndex >= pPE->nRichCount)) {
                return js_num(0);
            }

            if (id == A_getRichVersion) {
                return js_num((double)pPE->pRichRecords[nIndex].nVersion);
            }

            if (id == A_getRichID) {
                return js_num((double)pPE->pRichRecords[nIndex].nId);
            }

            return js_num((double)pPE->pRichRecords[nIndex].nCount);
        }

        /* XMSDOS::getDosStubSize clamps e_lfanew - sizeof(IMAGE_DOS_HEADEREX)
         * at zero, and MSDOS_Script's constructor keeps offset and size at 0
         * unless isDosStubPresent(). A PE whose e_lfanew is at or below 0x40
         * therefore reports offset 0 and size 0, not offset 64 and a negative
         * size.                                                            */
        case A_getDosStubOffset:
        case A_getDosStubSize:
        case A_isDosStubPresent: {
            cd_i64 nStubSize = pEngine->bHasPE ? (pPE->nLfanew - 64) : 0;

            if (nStubSize < 0) {
                nStubSize = 0;
            }

            if (id == A_getDosStubSize) {
                return js_num((double)nStubSize);
            }

            if (id == A_isDosStubPresent) {
                return js_bool(nStubSize != 0);
            }

            return js_num((double)((nStubSize != 0) ? 64 : 0));
        }

        /* ----------------------------------------------------------- PE */
        case A_getNumberOfSections:
            if (pEngine->bHasElf) {
                return js_num((double)pEngine->elf.nShnum);
            }

            if (pEngine->bHasMach) {
                return js_num((double)pEngine->mach.nSectionCount);
            }

            return js_num((double)(pEngine->bHasPE ? pPE->nSectionCount : 0));

        case A_getSectionName: return js_str(pCtx, section_name(pEngine, (int)arg_i64(pCtx, nArgc, pArgv, 0, 0)));

        case A_getSectionVirtualSize:
        case A_getSectionVirtualAddress:
        case A_getSectionFileSize:
        case A_getSectionFileOffset:
        case A_getSectionCharacteristics: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if (pEngine->bHasElf) {
                switch (id) {
                    case A_getSectionFileSize: return js_num((double)xelf_section_size(&pEngine->elf, nNumber));
                    case A_getSectionFileOffset: return js_num((double)xelf_section_offset(&pEngine->elf, nNumber));
                    default: return js_num(0);
                }
            }

            if (pEngine->bHasMach) {
                switch (id) {
                    case A_getSectionFileSize: return js_num((double)xmach_section_size(&pEngine->mach, nNumber));
                    case A_getSectionFileOffset: return js_num((double)xmach_section_offset(&pEngine->mach, nNumber));
                    default: return js_num(0);
                }
            }

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nSectionCount)) {
                return js_num(0);
            }

            switch (id) {
                case A_getSectionVirtualSize: return js_num((double)pPE->pSections[nNumber].nVirtualSize);
                case A_getSectionVirtualAddress: return js_num((double)pPE->pSections[nNumber].nVirtualAddress);
                case A_getSectionFileSize: return js_num((double)pPE->pSections[nNumber].nSizeOfRawData);
                case A_getSectionFileOffset: return js_num((double)pPE->pSections[nNumber].nPointerToRawData);
                default: return js_num((double)pPE->pSections[nNumber].nCharacteristics);
            }
        }

        case A_getNumberOfResources: return js_num((double)(pEngine->bHasPE ? pPE->nResourceCount : 0));

        case A_isSectionNamePresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int bResult = 0;

            if (pEngine->bHasElf) {
                bResult = xelf_section_present(&pEngine->elf, pName);
            } else if (pEngine->bHasMach) {
                bResult = xmach_section_present(&pEngine->mach, pName);
            } else if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nSectionCount; i++) {
                    if (x_strcmp(pPE->pSections[i].sName, pName) == 0) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pName);

            return js_bool(bResult);
        }

        case A_getSectionNumber: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int nResult = -1;

            if (pEngine->bHasElf) {
                nResult = xelf_section_number(&pEngine->elf, pName);
            } else if (pEngine->bHasMach) {
                nResult = xmach_section_number(&pEngine->mach, pName);
            } else if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nSectionCount; i++) {
                    if (x_strcmp(pPE->pSections[i].sName, pName) == 0) {
                        nResult = i;
                        break;
                    }
                }
            }

            cd_free(pName);

            return js_num((double)nResult);
        }

        case A_getSectionNameCollision: {
            char *pString1 = arg_string(pCtx, nArgc, pArgv, 0);
            char *pString2 = arg_string(pCtx, nArgc, pArgv, 1);
            int i = 0;
            const char *pResult = "";

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nSectionCount; i++) {
                    if (x_strcmp(pPE->pSections[i].sName, pString1) == 0) {
                        pResult = pString1;
                        break;
                    }

                    if (x_strcmp(pPE->pSections[i].sName, pString2) == 0) {
                        pResult = pString2;
                        break;
                    }
                }
            }

            {
                JSVal result = js_str(pCtx, pResult);

                cd_free(pString1);
                cd_free(pString2);

                return result;
            }
        }

        case A_isNET: return js_bool(pEngine->bHasPE && pPE->bIsNet);
        case A_isPE32: return js_bool(pEngine->bHasPE && (!pPE->bIs64));
        case A_isPEPlus: return js_bool(pEngine->bHasPE && pPE->bIs64);

        case A_getGeneralOptions: {
            char sBuf[64];

            if (pEngine->bHasElf) {
                return js_str(pCtx, pEngine->elf.sGeneralOptions);
            }

            if (!pEngine->bHasPE) {
                return js_str(pCtx, "");
            }

            x_snprintf(sBuf, sizeof(sBuf), "%s%s", pe_type_name(pe_type(pPE)), pPE->bIs64 ? "64" : "32");

            return js_str(pCtx, sBuf);
        }

        case A_getResourceIdByNumber:
        case A_getResourceNameByNumber:
        case A_getResourceOffsetByNumber:
        case A_getResourceSizeByNumber:
        case A_getResourceTypeByNumber: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nResourceCount)) {
                if (id == A_getResourceNameByNumber) {
                    return js_str(pCtx, "");
                }

                if (id == A_getResourceOffsetByNumber) {
                    return js_num(-1);
                }

                return js_num(0);
            }

            switch (id) {
                case A_getResourceIdByNumber: return js_num((double)pPE->pResources[nNumber].nNameId);
                case A_getResourceNameByNumber:
                    if (pPE->pResources[nNumber].pName) {
                        return js_str(pCtx, pPE->pResources[nNumber].pName);
                    }

                    return js_str(pCtx, pPE->pResources[nNumber].pTypeName ? pPE->pResources[nNumber].pTypeName : "");
                case A_getResourceOffsetByNumber: return js_num((double)pPE->pResources[nNumber].nOffset);
                case A_getResourceSizeByNumber: return js_num((double)pPE->pResources[nNumber].nSize);
                default: return js_num((double)pPE->pResources[nNumber].nTypeId);
            }
        }

        case A_getResourceNameOffset:
        case A_isResourceNamePresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            cd_i64 nResult = -1;
            int i = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nResourceCount; i++) {
                    if (pPE->pResources[i].pName && (x_strcmp(pPE->pResources[i].pName, pName) == 0)) {
                        nResult = pPE->pResources[i].nOffset;
                        break;
                    }
                }
            }

            cd_free(pName);

            if (id == A_isResourceNamePresent) {
                return js_bool(nResult != -1);
            }

            return js_num((double)nResult);
        }

        case A_isResourceGroupNamePresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int bResult = 0;
            int i = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nResourceCount; i++) {
                    if (pPE->pResources[i].pTypeName && (x_strcmp(pPE->pResources[i].pTypeName, pName) == 0)) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pName);

            return js_bool(bResult);
        }

        case A_isResourceGroupIdPresent: {
            cd_u32 nId = (cd_u32)arg_i64(pCtx, nArgc, pArgv, 0, 0);
            int i = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nResourceCount; i++) {
                    if ((pPE->pResources[i].pTypeName == NULL) && (pPE->pResources[i].nTypeId == nId)) {
                        return js_bool(1);
                    }
                }
            }

            return js_bool(0);
        }

        case A_isNetObjectPresent: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int bResult = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nNetAnsiCount; i++) {
                    if (x_strcmp(pPE->ppNetAnsiStrings[i], pString) == 0) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pString);

            return js_bool(bResult);
        }

        case A_isNetUStringPresent: {
            char *pString = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int bResult = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nNetUnicodeCount; i++) {
                    if (x_strcmp(pPE->ppNetUnicodeStrings[i], pString) == 0) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pString);

            return js_bool(bResult);
        }

        case A_isNetGlobalCctorPresent: return js_bool(pEngine->bHasPE && xpe_net_global_cctor_present(pPE));

        case A_isNetTypePresent: {
            char *pNamespace = arg_string(pCtx, nArgc, pArgv, 0);
            char *pTypeName = arg_string(pCtx, nArgc, pArgv, 1);
            int bResult = pEngine->bHasPE && xpe_net_type_present(pPE, pNamespace, pTypeName);

            cd_free(pNamespace);
            cd_free(pTypeName);

            return js_bool(bResult);
        }

        case A_isNetMethodPresent: {
            char *pNamespace = arg_string(pCtx, nArgc, pArgv, 0);
            char *pTypeName = arg_string(pCtx, nArgc, pArgv, 1);
            char *pMethodName = arg_string(pCtx, nArgc, pArgv, 2);
            int bResult = pEngine->bHasPE && xpe_net_method_present(pPE, pNamespace, pTypeName, pMethodName);

            cd_free(pNamespace);
            cd_free(pTypeName);
            cd_free(pMethodName);

            return js_bool(bResult);
        }

        case A_isNetFieldPresent: {
            char *pNamespace = arg_string(pCtx, nArgc, pArgv, 0);
            char *pTypeName = arg_string(pCtx, nArgc, pArgv, 1);
            char *pFieldName = arg_string(pCtx, nArgc, pArgv, 2);
            int bResult = pEngine->bHasPE && xpe_net_field_present(pPE, pNamespace, pTypeName, pFieldName);

            cd_free(pNamespace);
            cd_free(pTypeName);
            cd_free(pFieldName);

            return js_bool(bResult);
        }

        case A_findSignatureInBlob_NET:
        case A_isSignatureInBlobPresent_NET: {
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 0);
            cd_i64 nResult = -1;

            if (pEngine->bHasPE && pPE->cli.bValid && (pPE->cli.nBlobSize > 0)) {
                nResult = xb_find_signature(pFile, pMap, pPE->cli.nBlobOffset, pPE->cli.nBlobSize, pSignature);
            }

            cd_free(pSignature);

            if (id == A_isSignatureInBlobPresent_NET) {
                return js_bool(nResult != -1);
            }

            return js_num((double)nResult);
        }

        case A_getNetModuleName:
            if (!pEngine->bHasPE) {
                return js_str(pCtx, "");
            }

            return str_take(pCtx, xpe_net_module_name(pPE));

        case A_getNetAssemblyName:
            if (!pEngine->bHasPE) {
                return js_str(pCtx, "");
            }

            return str_take(pCtx, xpe_net_assembly_name(pPE));

        case A_getNumberOfImports: return js_num((double)(pEngine->bHasPE ? pPE->nImportCount : 0));

        case A_getImportLibraryName: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nImportCount)) {
                return js_str(pCtx, "");
            }

            return js_str(pCtx, pPE->pImports[nNumber].pName);
        }

        case A_getNumberOfImportThunks: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nImportCount)) {
                return js_num(0);
            }

            return js_num((double)pPE->pImports[nNumber].nFunctionCount);
        }

        case A_getImportFunctionName: {
            int nImport = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);
            int nFunction = (int)arg_i64(pCtx, nArgc, pArgv, 1, 0);

            if ((!pEngine->bHasPE) || (nImport < 0) || (nImport >= pPE->nImportCount)) {
                return js_str(pCtx, "");
            }

            if ((nFunction < 0) || (nFunction >= pPE->pImports[nImport].nFunctionCount)) {
                return js_str(pCtx, "");
            }

            return js_str(pCtx, pPE->pImports[nImport].ppFunctions[nFunction]);
        }

        case A_isLibraryPresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int bCheckCase = arg_bool(pCtx, nArgc, pArgv, 1, 0);
            int i = 0;
            int bResult = 0;

            if (pEngine->bHasElf) {
                bResult = xelf_library_present(&pEngine->elf, pName);
            } else if (pEngine->bHasMach) {
                bResult = xmach_library_present(&pEngine->mach, pName);
            } else if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nImportCount; i++) {
                    int bMatch = bCheckCase ? (x_strcmp(pPE->pImports[i].pName, pName) == 0) : (cd_stricmp_ascii(pPE->pImports[i].pName, pName) == 0);

                    if (bMatch) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pName);

            return js_bool(bResult);
        }

        case A_isLibraryFunctionPresent: {
            char *pLibrary = arg_string(pCtx, nArgc, pArgv, 0);
            char *pFunction = arg_string(pCtx, nArgc, pArgv, 1);
            int i = 0;
            int j = 0;
            int bResult = 0;

            if (pEngine->bHasPE) {
                for (i = 0; (i < pPE->nImportCount) && (!bResult); i++) {
                    if (cd_stricmp_ascii(pPE->pImports[i].pName, pLibrary) != 0) {
                        continue;
                    }

                    for (j = 0; j < pPE->pImports[i].nFunctionCount; j++) {
                        if (cd_stricmp_ascii(pPE->pImports[i].ppFunctions[j], pFunction) == 0) {
                            bResult = 1;
                            break;
                        }
                    }
                }
            }

            cd_free(pLibrary);
            cd_free(pFunction);

            return js_bool(bResult);
        }

        case A_isFunctionPresent: {
            char *pFunction = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int j = 0;
            int bResult = 0;

            if (pEngine->bHasPE) {
                for (i = 0; (i < pPE->nImportCount) && (!bResult); i++) {
                    for (j = 0; j < pPE->pImports[i].nFunctionCount; j++) {
                        if (x_strcmp(pPE->pImports[i].ppFunctions[j], pFunction) == 0) {
                            bResult = 1;
                            break;
                        }
                    }
                }
            }

            cd_free(pFunction);

            return js_bool(bResult);
        }

        case A_getImportSection: return js_num((double)pe_directory_section(pEngine, XPE_DIR_IMPORT));
        case A_getExportSection: return js_num((double)pe_directory_section(pEngine, XPE_DIR_EXPORT));
        case A_getResourceSection: return js_num((double)pe_directory_section(pEngine, XPE_DIR_RESOURCE));
        case A_getRelocsSection: return js_num((double)pe_directory_section(pEngine, XPE_DIR_BASERELOC));
        case A_getTLSSection: return js_num((double)pe_directory_section(pEngine, XPE_DIR_TLS));

        case A_getEntryPointSection:
            if (!pEngine->bHasPE) {
                return js_num(-1);
            }

            return js_num((double)xpe_section_number_by_rva(pPE, pPE->nAddressOfEntryPoint));

        case A_getMajorLinkerVersion: return js_num((double)(pEngine->bHasPE ? pPE->nMajorLinkerVersion : 0));
        case A_getMinorLinkerVersion: return js_num((double)(pEngine->bHasPE ? pPE->nMinorLinkerVersion : 0));

        case A_getManifest: return js_str(pCtx, (pEngine->bHasPE && pPE->pManifest) ? pPE->pManifest : "");

        case A_getVersionStringInfo: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal result = js_str(pCtx, version_value(pEngine, pKey));

            cd_free(pKey);

            return result;
        }

        case A_getCompilerVersion: {
            char sBuf[64];

            if (!pEngine->bHasPE) {
                return js_str(pCtx, "");
            }

            x_snprintf(sBuf, sizeof(sBuf), "%u.%u", (unsigned)pPE->nMajorLinkerVersion, (unsigned)pPE->nMinorLinkerVersion);

            return js_str(pCtx, sBuf);
        }

        case A_isConsole: return js_bool(pEngine->bHasPE && (pe_type(pPE) == PETYPE_CONSOLE));
        case A_isDll: return js_bool(pEngine->bHasPE && (pe_type(pPE) == PETYPE_DLL));
        case A_isDriver: return js_bool(pEngine->bHasPE && (pe_type(pPE) == PETYPE_DRIVER));

        case A_getNETVersion: return js_str(pCtx, (pEngine->bHasPE && pPE->pNetVersion) ? pPE->pNetVersion : "");

        case A_compareEP_NET: {
            char *pSignature = arg_string(pCtx, nArgc, pArgv, 0);
            cd_i64 nOffset = arg_i64(pCtx, nArgc, pArgv, 1, 0);
            int bResult = 0;

            if (pEngine->bHasPE && pPE->cli.bValid) {
                cd_i64 nTarget = xbmap_address_to_offset(pMap, pPE->nImageBase + pPE->cli.nEntryPointRVA + (cd_u64)nOffset);

                if (nTarget != -1) {
                    bResult = xb_compare_signature(pFile, pMap, pSignature, nTarget);
                }
            }

            cd_free(pSignature);

            return js_bool(bResult);
        }

        case A_getSizeOfCode: return js_num((double)(pEngine->bHasPE ? pPE->nSizeOfCode : 0));
        case A_getSizeOfUninitializedData: return js_num((double)(pEngine->bHasPE ? pPE->nSizeOfUninitializedData : 0));

        case A_getPEFileVersion: {
            char *pFileName = arg_string(pCtx, nArgc, pArgv, 0);
            XBFile other;
            JSVal result = js_str(pCtx, "");

            if (xb_open(&other, pFileName)) {
                XPE otherPE;

                if (xpe_parse(&otherPE, &other)) {
                    int i = 0;

                    for (i = 0; i < otherPE.nVersionCount; i++) {
                        if (x_strcmp(otherPE.pVersionRecords[i].pKey, "FileVersion") == 0) {
                            js_release(pCtx, result);
                            result = js_str(pCtx, otherPE.pVersionRecords[i].pValue);
                            break;
                        }
                    }

                    xpe_free(&otherPE);
                }

                xb_close(&other);
            }

            cd_free(pFileName);

            return result;
        }

        case A_getFileVersion: return js_str(pCtx, version_value(pEngine, "FileVersion"));

        case A_getFileVersionMS: {
            char sBuf[64];

            if (!pEngine->bHasPE) {
                return js_str(pCtx, "");
            }

            x_snprintf(sBuf, sizeof(sBuf), "%u.%u", (unsigned)(pPE->nFileVersionMS >> 16), (unsigned)(pPE->nFileVersionMS & 0xFFFF));

            return js_str(pCtx, sBuf);
        }

        case A_calculateSizeOfHeaders: {
            cd_i64 nSize = 0;

            if (!pEngine->bHasPE) {
                return js_num(0);
            }

            nSize = pPE->nLfanew + 4 + 20 + pPE->nSizeOfOptionalHeader + (cd_i64)pPE->nSectionCount * 40;

            if (pPE->nFileAlignment) {
                nSize = ((nSize + pPE->nFileAlignment - 1) / pPE->nFileAlignment) * pPE->nFileAlignment;
            }

            return js_num((double)nSize);
        }

        case A_isExportFunctionPresent: {
            char *pName = arg_string(pCtx, nArgc, pArgv, 0);
            int i = 0;
            int bResult = 0;

            if (pEngine->bHasPE) {
                for (i = 0; i < pPE->nExportCount; i++) {
                    if (x_strcmp(pPE->ppExportFunctions[i], pName) == 0) {
                        bResult = 1;
                        break;
                    }
                }
            }

            cd_free(pName);

            return js_bool(bResult);
        }

        case A_getNumberOfExportFunctions: return js_num((double)(pEngine->bHasPE ? pPE->nExportCount : 0));

        case A_getExportFunctionName: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nExportCount)) {
                return js_str(pCtx, "");
            }

            return js_str(pCtx, pPE->ppExportFunctions[nNumber]);
        }

        case A_isExportPresent: return js_bool(pEngine->bHasPE && (pPE->pDirRVA[XPE_DIR_EXPORT] != 0));
        case A_isTLSPresent: return js_bool(pEngine->bHasPE && (pPE->pDirRVA[XPE_DIR_TLS] != 0));
        case A_isImportPresent: return js_bool(pEngine->bHasPE && (pPE->pDirRVA[XPE_DIR_IMPORT] != 0));
        case A_isResourcesPresent: return js_bool(pEngine->bHasPE && (pPE->pDirRVA[XPE_DIR_RESOURCE] != 0));

        case A_getImportHash32: return js_num((double)(pEngine->bHasPE ? pPE->nImportHash32 : 0));
        case A_getImportHash64: return js_num((double)(pEngine->bHasPE ? pPE->nImportHash64 : 0));

        case A_isImportPositionHashPresent: {
            cd_i64 nIndex = arg_i64(pCtx, nArgc, pArgv, 0, -1);
            cd_u32 nHash = (cd_u32)arg_i64(pCtx, nArgc, pArgv, 1, 0);
            int i = 0;

            if (!pEngine->bHasPE) {
                return js_bool(0);
            }

            if (nIndex == -1) {
                for (i = 0; i < pPE->nImportCount; i++) {
                    if (pPE->pImports[i].nPositionHash == nHash) {
                        return js_bool(1);
                    }
                }

                return js_bool(0);
            }

            if ((nIndex >= 0) && (nIndex < pPE->nImportCount)) {
                return js_bool(pPE->pImports[nIndex].nPositionHash == nHash);
            }

            return js_bool(0);
        }

        case A_getImageFileHeader: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal result = js_num(pEngine->bHasPE ? (double)image_file_header_value(pPE, pKey) : 0);

            cd_free(pKey);

            return result;
        }

        case A_getImageOptionalHeader: {
            char *pKey = arg_string(pCtx, nArgc, pArgv, 0);
            JSVal result = js_num(pEngine->bHasPE ? (double)image_optional_header_value(pPE, pKey) : 0);

            cd_free(pKey);

            return result;
        }

        case A_getNumberOfDebugDataRecords: return js_num((double)(pEngine->bHasPE ? pPE->nDebugCount : 0));

        case A_getDebugDataType: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nDebugCount)) {
                return js_str(pCtx, "");
            }

            return js_str(pCtx, xpe_debug_type_name(pPE->pDebugRecords[nNumber].nType));
        }

        case A_getDebugDataOffset:
        case A_getDebugDataSize: {
            int nNumber = (int)arg_i64(pCtx, nArgc, pArgv, 0, 0);

            if ((!pEngine->bHasPE) || (nNumber < 0) || (nNumber >= pPE->nDebugCount)) {
                return js_num(-1);
            }

            if (id == A_getDebugDataOffset) {
                return js_num((double)pPE->pDebugRecords[nNumber].nOffset);
            }

            return js_num((double)pPE->pDebugRecords[nNumber].nSize);
        }

        default: break;
    }

    return js_undefined();
}

/* ------------------------------------------------------- global helpers  */

static void engine_add_result(DieEngine *pEngine, const char *pType, const char *pName, const char *pVersion, const char *pInfo);

static JSVal fn_set_result(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    char *pType = arg_string(pCtx, nArgc, pArgv, 0);
    char *pName = arg_string(pCtx, nArgc, pArgv, 1);
    char *pVersion = arg_string(pCtx, nArgc, pArgv, 2);
    char *pInfo = arg_string(pCtx, nArgc, pArgv, 3);

    (void)thisVal;
    (void)pUser;

    engine_add_result(pEngine, pType, pName, pVersion, pInfo);

    cd_free(pType);
    cd_free(pName);
    cd_free(pVersion);
    cd_free(pInfo);

    return js_undefined();
}

static JSVal fn_is_result_present(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    char *pType = arg_string(pCtx, nArgc, pArgv, 0);
    char *pName = arg_string(pCtx, nArgc, pArgv, 1);
    int i = 0;
    int bResult = 0;

    (void)thisVal;
    (void)pUser;

    for (i = 0; i < pEngine->pResult->nCount; i++) {
        ScanRecord *pRecord = &pEngine->pResult->pRecords[i];

        if ((cd_stricmp_ascii(pRecord->pType, pType) == 0) && ((cd_stricmp_ascii(pRecord->pName, pName) == 0) || (pName[0] == 0))) {
            bResult = 1;
            break;
        }
    }

    cd_free(pType);
    cd_free(pName);

    return js_bool(bResult);
}

static JSVal fn_get_number_of_results(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    char *pType = arg_string(pCtx, nArgc, pArgv, 0);
    int i = 0;
    int nResult = 0;

    (void)thisVal;
    (void)pUser;

    for (i = 0; i < pEngine->pResult->nCount; i++) {
        if ((cd_stricmp_ascii(pEngine->pResult->pRecords[i].pType, pType) == 0) || (pType[0] == 0)) {
            nResult++;
        }
    }

    cd_free(pType);

    return js_num((double)nResult);
}

static JSVal fn_remove_result(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    char *pType = arg_string(pCtx, nArgc, pArgv, 0);
    char *pName = arg_string(pCtx, nArgc, pArgv, 1);
    int i = 0;

    (void)thisVal;
    (void)pUser;

    pEngine->pBlackList = (BLRecord *)cd_realloc(pEngine->pBlackList, (size_t)(pEngine->nBlackListCount + 1) * sizeof(BLRecord));
    pEngine->pBlackList[pEngine->nBlackListCount].pType = cd_strdup(pType);
    pEngine->pBlackList[pEngine->nBlackListCount].pName = cd_strdup(pName);
    pEngine->nBlackListCount++;

    for (i = 0; i < pEngine->pResult->nCount; i++) {
        ScanRecord *pRecord = &pEngine->pResult->pRecords[i];

        if ((cd_stricmp_ascii(pRecord->pType, pType) == 0) && ((cd_stricmp_ascii(pRecord->pName, pName) == 0) || (pRecord->pName[0] == 0))) {
            cd_free(pRecord->pType);
            cd_free(pRecord->pName);
            cd_free(pRecord->pVersion);
            cd_free(pRecord->pInfo);
            x_memmove(pEngine->pResult->pRecords + i, pEngine->pResult->pRecords + i + 1, (size_t)(pEngine->pResult->nCount - i - 1) * sizeof(ScanRecord));
            pEngine->pResult->nCount--;
            break;
        }
    }

    cd_free(pType);
    cd_free(pName);

    return js_undefined();
}

static JSVal fn_include_script(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    char *pScript = arg_string(pCtx, nArgc, pArgv, 0);
    int i = 0;
    int bFound = 0;

    (void)thisVal;
    (void)pUser;

    for (i = 0; i < pEngine->pDb->nCount; i++) {
        DBSignature *pRecord = &pEngine->pDb->pRecords[i];

        if ((pRecord->fileType == XFT_UNKNOWN) && (cd_stricmp_ascii(pRecord->pName, pScript) == 0)) {
            js_eval_nested(pCtx, pRecord->pText, pRecord->pName);
            bFound = 1;
            break;
        }
    }

    if ((!bFound) && pEngine->pOptions->bShowMessages) {
        x_fprintf(x_stderr(), "Cannot find: %s\n", pScript);
    }

    cd_free(pScript);

    return js_undefined();
}

static JSVal fn_log(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);
    char *pText = arg_string(pCtx, nArgc, pArgv, 0);

    (void)thisVal;
    (void)pUser;

    if (pEngine->pOptions->bShowMessages) {
        x_fprintf(x_stderr(), "%s\n", pText);
    }

    cd_free(pText);

    return js_undefined();
}

static JSVal fn_is_stop(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pCtx;
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_bool(0);
}

static JSVal fn_break_scan(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    DieEngine *pEngine = engine_of(pCtx);

    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    pEngine->bStop = 1;

    return js_undefined();
}

static JSVal fn_true(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pCtx;
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;

    return js_bool((int)(size_t)pUser);
}

static JSVal fn_engine_version(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_str(pCtx, X_APPLICATIONVERSION);
}

static JSVal fn_get_os(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

#if defined(_WIN32)
    return js_str(pCtx, "win32");
#elif defined(__APPLE__)
    return js_str(pCtx, "macos");
#else
    return js_str(pCtx, "linux");
#endif
}

static JSVal fn_noop(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser)
{
    (void)pCtx;
    (void)thisVal;
    (void)nArgc;
    (void)pArgv;
    (void)pUser;

    return js_undefined();
}

/* ------------------------------------------------------- result handling */

static void engine_add_result(DieEngine *pEngine, const char *pType, const char *pName, const char *pVersion, const char *pInfo)
{
    ScanResult *pResult = pEngine->pResult;
    ScanRecord *pRecord = NULL;
    int i = 0;

    for (i = 0; i < pEngine->nBlackListCount; i++) {
        if ((cd_stricmp_ascii(pEngine->pBlackList[i].pType, pType) == 0) &&
            ((cd_stricmp_ascii(pEngine->pBlackList[i].pName, pName) == 0) || (pEngine->pBlackList[i].pName[0] == 0))) {
            return;
        }
    }

    if (pResult->nCount + 1 > pResult->nCapacity) {
        pResult->nCapacity = pResult->nCapacity ? (pResult->nCapacity * 2) : 16;
        pResult->pRecords = (ScanRecord *)cd_realloc(pResult->pRecords, (size_t)pResult->nCapacity * sizeof(ScanRecord));
    }

    pRecord = &pResult->pRecords[pResult->nCount++];
    x_memset(pRecord, 0, sizeof(*pRecord));
    pRecord->pType = cd_strdup(pType);
    pRecord->pName = cd_strdup(pName);
    pRecord->pVersion = cd_strdup(pVersion);
    pRecord->pInfo = cd_strdup(pInfo);
    pRecord->nPrio = cdie_type_to_prio(pType);
    pRecord->bIsHeuristic = ((x_strlen(pType) > 1) && (pType[0] == '~')) ? 1 : 0;
    pRecord->bIsAHeuristic = ((x_strlen(pType) > 1) && (pType[0] == '!')) ? 1 : 0;
}

/* ------------------------------------------------------------- install  */

static const ApiEntry g_apiTable[] = {
    {"getSize", A_getSize, 0},
    {"Sz", A_getSize, 0},
    {"compare", A_compare, 2},
    {"c", A_compare, 2},
    {"compareEP", A_compareEP, 2},
    {"compareOverlay", A_compareOverlay, 2},
    {"readByte", A_readByte, 1},
    {"readSByte", A_readSByte, 1},
    {"readWord", A_readWord, 1},
    {"readSWord", A_readSWord, 1},
    {"readDword", A_readDword, 1},
    {"readSDword", A_readSDword, 1},
    {"readQword", A_readQword, 1},
    {"readSQword", A_readSQword, 1},
    {"getString", A_getString, 2},
    {"findSignature", A_findSignature, 3},
    {"fSig", A_findSignature, 3},
    {"findString", A_findString, 3},
    {"fStr", A_findString, 3},
    {"findByte", A_findByte, 3},
    {"findWord", A_findWord, 3},
    {"findDword", A_findDword, 3},
    {"getEntryPointOffset", A_getEntryPointOffset, 0},
    {"getOverlayOffset", A_getOverlayOffset, 0},
    {"getOverlaySize", A_getOverlaySize, 0},
    {"getAddressOfEntryPoint", A_getAddressOfEntryPoint, 0},
    {"isOverlayPresent", A_isOverlayPresent, 0},
    {"isSignaturePresent", A_isSignaturePresent, 3},
    {"swapBytes", A_swapBytes, 1},
    {"RVAToOffset", A_RVAToOffset, 1},
    {"VAToOffset", A_VAToOffset, 1},
    {"OffsetToVA", A_OffsetToVA, 1},
    {"OffsetToRVA", A_OffsetToRVA, 1},
    {"getFileDirectory", A_getFileDirectory, 0},
    {"getFileBaseName", A_getFileBaseName, 0},
    {"getFileCompleteSuffix", A_getFileCompleteSuffix, 0},
    {"getFileSuffix", A_getFileSuffix, 0},
    {"getSignature", A_getSignature, 2},
    {"calculateEntropy", A_calculateEntropy, 2},
    {"isZeroFilled", A_isZeroFilled, 2},
    {"calculateMD5", A_calculateMD5, 2},
    {"calculateCRC32", A_calculateCRC32, 2},
    {"crc16", A_crc16, 3},
    {"crc32", A_crc32, 3},
    {"adler32", A_adler32, 2},
    {"isSignatureInSectionPresent", A_isSignatureInSectionPresent, 2},
    {"getImageBase", A_getImageBase, 0},
    {"upperCase", A_upperCase, 1},
    {"lowerCase", A_lowerCase, 1},
    {"isPlainText", A_isPlainText, 0},
    {"isUTF8Text", A_isUTF8Text, 0},
    {"isUnicodeText", A_isUnicodeText, 0},
    {"isText", A_isText, 0},
    {"getHeaderString", A_getHeaderString, 0},
    {"getDisasmLength", A_getDisasmLength, 1},
    {"getDisasmString", A_getDisasmString, 1},
    {"getDisasmNextAddress", A_getDisasmNextAddress, 1},
    {"is16", A_is16, 0},
    {"is32", A_is32, 0},
    {"is64", A_is64, 0},
    {"isDeepScan", A_isDeepScan, 0},
    {"isHeuristicScan", A_isHeuristicScan, 0},
    {"isFirstWrapperScan", A_isFirstWrapperScan, 0},
    {"isAggressiveScan", A_isAggressiveScan, 0},
    {"isRecursiveScan", A_isRecursiveScan, 0},
    {"isOverlayScan", A_isOverlayScan, 0},
    {"isVerbose", A_isVerbose, 0},
    {"isProfiling", A_isProfiling, 0},
    {"getScanID", A_getScanID, 0},
    {"getStartOffset", A_getStartOffset, 0},
    {"read_uint8", A_read_uint8, 1},
    {"U8", A_read_uint8, 1},
    {"read_int8", A_read_int8, 1},
    {"I8", A_read_int8, 1},
    {"read_uint16", A_read_uint16, 2},
    {"U16", A_read_uint16, 2},
    {"read_int16", A_read_int16, 2},
    {"I16", A_read_int16, 2},
    {"read_uint24", A_read_uint24, 2},
    {"U24", A_read_uint24, 2},
    {"read_int24", A_read_int24, 2},
    {"I24", A_read_int24, 2},
    {"read_uint32", A_read_uint32, 2},
    {"U32", A_read_uint32, 2},
    {"read_int32", A_read_int32, 2},
    {"I32", A_read_int32, 2},
    {"read_uint64", A_read_uint64, 2},
    {"U64", A_read_uint64, 2},
    {"read_int64", A_read_int64, 2},
    {"I64", A_read_int64, 2},
    {"read_float", A_read_float, 2},
    {"read_float32", A_read_float, 2},
    {"F32", A_read_float, 2},
    {"read_double", A_read_double, 2},
    {"read_float64", A_read_double, 2},
    {"F64", A_read_double, 2},
    {"read_float16", A_read_float16, 2},
    {"F16", A_read_float16, 2},
    {"read_UUID", A_read_UUID, 2},
    {"read_ucsdString", A_read_ucsdString, 1},
    {"UCSD", A_read_ucsdString, 1},
    {"read_ansiString", A_read_ansiString, 2},
    {"SA", A_read_ansiString, 2},
    {"read_unicodeString", A_read_unicodeString, 2},
    {"SU16", A_read_unicodeString, 2},
    {"read_utf8String", A_read_utf8String, 2},
    {"SU8", A_read_utf8String, 2},
    {"read_codePageString", A_read_codePageString, 3},
    {"SC", A_read_codePageString, 3},
    {"bytesCountToString", A_bytesCountToString, 1},
    {"find_ansiString", A_find_ansiString, 3},
    {"find_unicodeString", A_find_unicodeString, 3},
    {"find_utf8String", A_find_utf8String, 3},
    {"getOperationSystemName", A_getOperationSystemName, 0},
    {"getOperationSystemVersion", A_getOperationSystemVersion, 0},
    {"getOperationSystemOptions", A_getOperationSystemOptions, 0},
    {"getFileFormatName", A_getFileFormatName, 0},
    {"getFileFormatVersion", A_getFileFormatVersion, 0},
    {"getFileFormatOptions", A_getFileFormatOptions, 0},
    {"isSigned", A_isSigned, 0},
    {"cleanString", A_cleanString, 1},
    {"readBytes", A_readBytes, 3},
    {"BA", A_readBytes, 3},
    {"isOverlay", A_isOverlay, 0},
    {"isResource", A_isResource, 0},
    {"isDebugData", A_isDebugData, 0},
    {"isFilePart", A_isFilePart, 0},
    {"isReleaseBuild", A_isReleaseBuild, 0},
    {"isDebugBuild", A_isDebugBuild, 0},
    {"isChecksumCorrect", A_isChecksumCorrect, 0},
    {"isEntryPointCorrect", A_isEntryPointCorrect, 0},
    {"isSectionAlignmentCorrect", A_isSectionAlignmentCorrect, 0},
    {"isFileAlignmentCorrect", A_isFileAlignmentCorrect, 0},
    {"isHeaderCorrect", A_isHeaderCorrect, 0},
    {"isRelocsTableCorrect", A_isRelocsTableCorrect, 0},
    {"isImportTableCorrect", A_isImportTableCorrect, 0},
    {"isExportTableCorrect", A_isExportTableCorrect, 0},
    {"isResourcesTableCorrect", A_isResourcesTableCorrect, 0},
    {"isSectionsTableCorrect", A_isSectionsTableCorrect, 0},
    {"getFormatMessages", A_getFormatMessages, 0},
    {"getListOfCompressionMethods", A_getListOfCompressionMethods, 0},
    {"startTiming", A_startTiming, 0},
    {"endTiming", A_endTiming, 2},
    {"detectZLIB", A_detectZLIB, 2},
    {"detectGZIP", A_detectGZIP, 2},
    {"detectZIP", A_detectZIP, 2},

    {"isRichSignaturePresent", A_isRichSignaturePresent, 0},
    {"getNumberOfRichIDs", A_getNumberOfRichIDs, 0},
    {"isRichVersionPresent", A_isRichVersionPresent, 1},
    {"getRichVersion", A_getRichVersion, 1},
    {"getRichID", A_getRichID, 1},
    {"getRichCount", A_getRichCount, 1},
    {"getDosStubOffset", A_getDosStubOffset, 0},
    {"getDosStubSize", A_getDosStubSize, 0},
    {"isDosStubPresent", A_isDosStubPresent, 0},

    {"getNumberOfSections", A_getNumberOfSections, 0},
    {"getSectionName", A_getSectionName, 1},
    {"getSectionVirtualSize", A_getSectionVirtualSize, 1},
    {"getSectionVirtualAddress", A_getSectionVirtualAddress, 1},
    {"getSectionFileSize", A_getSectionFileSize, 1},
    {"getSectionFileOffset", A_getSectionFileOffset, 1},
    {"getSectionCharacteristics", A_getSectionCharacteristics, 1},
    {"getNumberOfResources", A_getNumberOfResources, 0},
    {"isSectionNamePresent", A_isSectionNamePresent, 1},
    {"isNET", A_isNET, 0},
    {"isNet", A_isNET, 0},
    {"isPE32", A_isPE32, 0},
    {"isPEPlus", A_isPEPlus, 0},
    {"isNE", A_isNE, 0},
    {"isLE", A_isLE, 0},
    {"isLX", A_isLX, 0},
    {"getApplicationIdentifier", A_getApplicationIdentifier, 0},
    {"getDataPreparerIdentifier", A_getDataPreparerIdentifier, 0},
    {"getGeneralOptions", A_getGeneralOptions, 0},
    {"getResourceIdByNumber", A_getResourceIdByNumber, 1},
    {"getResourceNameByNumber", A_getResourceNameByNumber, 1},
    {"getResourceOffsetByNumber", A_getResourceOffsetByNumber, 1},
    {"getResourceSizeByNumber", A_getResourceSizeByNumber, 1},
    {"getResourceTypeByNumber", A_getResourceTypeByNumber, 1},
    {"isNETStringPresent", A_isNetObjectPresent, 1},
    {"isNetObjectPresent", A_isNetObjectPresent, 1},
    {"isNetStringPresent", A_isNetObjectPresent, 1},
    {"isNETUnicodeStringPresent", A_isNetUStringPresent, 1},
    {"isNetUStringPresent", A_isNetUStringPresent, 1},
    {"isNetUnicodeStringPresent", A_isNetUStringPresent, 1},
    {"isNetGlobalCctorPresent", A_isNetGlobalCctorPresent, 0},
    {"isNetTypePresent", A_isNetTypePresent, 2},
    {"isNetMethodPresent", A_isNetMethodPresent, 3},
    {"isNetFieldPresent", A_isNetFieldPresent, 3},
    {"findSignatureInBlob_NET", A_findSignatureInBlob_NET, 1},
    {"isSignatureInBlobPresent_NET", A_isSignatureInBlobPresent_NET, 1},
    {"getNetModuleName", A_getNetModuleName, 0},
    {"getNetAssemblyName", A_getNetAssemblyName, 0},
    {"getNetVersion", A_getNETVersion, 0},
    {"getNumberOfImports", A_getNumberOfImports, 0},
    {"getImportLibraryName", A_getImportLibraryName, 1},
    {"isLibraryPresent", A_isLibraryPresent, 2},
    {"isLibraryFunctionPresent", A_isLibraryFunctionPresent, 2},
    {"isFunctionPresent", A_isFunctionPresent, 1},
    {"getImportFunctionName", A_getImportFunctionName, 2},
    {"getImportSection", A_getImportSection, 0},
    {"getExportSection", A_getExportSection, 0},
    {"getResourceSection", A_getResourceSection, 0},
    {"getEntryPointSection", A_getEntryPointSection, 0},
    {"getRelocsSection", A_getRelocsSection, 0},
    {"getTLSSection", A_getTLSSection, 0},
    {"getMajorLinkerVersion", A_getMajorLinkerVersion, 0},
    {"getMinorLinkerVersion", A_getMinorLinkerVersion, 0},
    {"getManifest", A_getManifest, 0},
    {"getVersionStringInfo", A_getVersionStringInfo, 1},
    {"getNumberOfImportThunks", A_getNumberOfImportThunks, 1},
    {"getResourceNameOffset", A_getResourceNameOffset, 1},
    {"isResourceNamePresent", A_isResourceNamePresent, 1},
    {"isResourceGroupNamePresent", A_isResourceGroupNamePresent, 1},
    {"isResourceGroupIdPresent", A_isResourceGroupIdPresent, 1},
    {"getCompilerVersion", A_getCompilerVersion, 0},
    {"isConsole", A_isConsole, 0},
    {"isSignedFile", A_isSignedFile, 0},
    {"getSectionNameCollision", A_getSectionNameCollision, 2},
    {"getSectionNumber", A_getSectionNumber, 1},
    {"isDll", A_isDll, 0},
    {"isDriver", A_isDriver, 0},
    {"getNETVersion", A_getNETVersion, 0},
    {"compareEP_NET", A_compareEP_NET, 2},
    {"getSizeOfCode", A_getSizeOfCode, 0},
    {"getSizeOfUninitializedData", A_getSizeOfUninitializedData, 0},
    {"getPEFileVersion", A_getPEFileVersion, 1},
    {"getFileVersion", A_getFileVersion, 0},
    {"getFileVersionMS", A_getFileVersionMS, 0},
    {"calculateSizeOfHeaders", A_calculateSizeOfHeaders, 0},
    {"isExportFunctionPresent", A_isExportFunctionPresent, 1},
    {"getNumberOfExportFunctions", A_getNumberOfExportFunctions, 0},
    {"getNumberOfExports", A_getNumberOfExportFunctions, 0},
    {"getExportFunctionName", A_getExportFunctionName, 1},
    {"getExportNameByNumber", A_getExportFunctionName, 1},
    {"isExportPresent", A_isExportPresent, 0},
    {"isTLSPresent", A_isTLSPresent, 0},
    {"isImportPresent", A_isImportPresent, 0},
    {"isResourcesPresent", A_isResourcesPresent, 0},
    {"getImportHash32", A_getImportHash32, 0},
    {"getImportHash64", A_getImportHash64, 0},
    {"isImportPositionHashPresent", A_isImportPositionHashPresent, 2},
    {"getImageFileHeader", A_getImageFileHeader, 1},
    {"getImageOptionalHeader", A_getImageOptionalHeader, 1},
    {"getNumberOfDebugDataRecords", A_getNumberOfDebugDataRecords, 0},
    {"getDebugDataType", A_getDebugDataType, 1},
    {"getDebugDataOffset", A_getDebugDataOffset, 1},
    {"getDebugDataSize", A_getDebugDataSize, 1},

    {"getComment", A_getComment, 0},
    {"getDqtMD5", A_getDqtMD5, 0},
    {"isChunkPresent", A_isChunkPresent, 1},
    {"isExifPresent", A_isExifPresent, 0},
    {"getExifCameraName", A_getExifCameraName, 0},

    {"getAndroidManifestRecord", A_getAndroidManifestRecord, 1},
    {"getAndroidManifest", A_getAndroidManifest, 0},
    {"isArchiveRecordPresent", A_isArchiveRecordPresent, 1},
    {"isArchiveRecordPresentExp", A_isArchiveRecordPresentExp, 1},
    {"getManifestRecord", A_getManifestRecord, 1},
    {"getPackageJsonRecord", A_getPackageJsonRecord, 1},
    {"isConstPresent", A_isConstPresent, 1},

    {"getHeaderCommentAsHex", A_getHeaderCommentAsHex, 0},
    {"getStringValuesByKey", A_getStringValuesByKey, 1},
    {"getValuesByKey", A_getValuesByKey, 1},
    {"isValuesHexByKey", A_isValuesHexByKey, 1},
    {"isEncrypted", A_isEncrypted, 0},

    {"isDexStringPresent", A_isDexStringPresent, 1},
    {"isDexItemStringPresent", A_isDexItemStringPresent, 1},
    {"getMapItemsHash", A_getMapItemsHash, 0},

    {"isStringInTablePresent", A_isStringInTablePresent, 2},
    {"getNumberOfPrograms", A_getNumberOfPrograms, 0},
    {"getProgramFileOffset", A_getProgramFileOffset, 1},
    {"getProgramFileSize", A_getProgramFileSize, 1},
    {"getElfHeader_type", A_getElfHeader_type, 0},
    {"getElfHeader_machine", A_getElfHeader_machine, 0},
    {"getElfHeader_entry", A_getElfHeader_entry, 0},
    {"getElfHeader_phoff", A_getElfHeader_phoff, 0},
    {"getElfHeader_shoff", A_getElfHeader_shoff, 0},
    {"getElfHeader_phnum", A_getElfHeader_phnum, 0},
    {"getElfHeader_shnum", A_getElfHeader_shnum, 0},
    {"getElfHeader_shentsize", A_getElfHeader_shentsize, 0},
    {"getElfHeader_phentsize", A_getElfHeader_phentsize, 0},
    {"getElfHeader_shstrndx", A_getElfHeader_shstrndx, 0},
    {"getRunPath", A_getRunPath, 0},
    {"isNotePresent", A_isNotePresent, 1},
    {"getLibraryCurrentVersion", A_getLibraryCurrentVersion, 1},

    {NULL, A_NONE, 0}};

void cdie_install_api(DieEngine *pEngine)
{
    JSCtx *pCtx = pEngine->pJs;
    JSVal object = js_new_object(pCtx);
    JSVal global = js_global(pCtx);
    int i = 0;

    for (i = 0; g_apiTable[i].pName; i++) {
        js_def_method(pCtx, object, g_apiTable[i].pName, api_dispatch, g_apiTable[i].nArgc, (void *)(size_t)g_apiTable[i].id);
    }

    /* The scripts address the format object by several names: always
     * `Binary`, plus the name of the detected format so that the per-format
     * `_init` file (`var File = PE;`) resolves.                            */
    js_set(pCtx, global, "Binary", js_dup(object));

    {
        static const struct {
            XFileType type;
            const char *pName;
        } pAliases[] = {{XFT_PE, "PE"},          {XFT_PE32, "PE"},       {XFT_PE64, "PE"},        {XFT_MSDOS, "MSDOS"},
                        {XFT_NE, "NE"},          {XFT_LE, "LE"},         {XFT_LX, "LX"},          {XFT_COM, "COM"},
                        {XFT_ELF, "ELF"},        {XFT_ELF32, "ELF"},     {XFT_ELF64, "ELF"},      {XFT_MACHO, "MACH"},
                        {XFT_MACHO32, "MACH"},   {XFT_MACHO64, "MACH"},  {XFT_MACHOFAT, "MACHOFAT"}, {XFT_ZIP, "ZIP"},
                        {XFT_JAR, "JAR"},        {XFT_APK, "APK"},       {XFT_IPA, "IPA"},        {XFT_NPM, "NPM"},
                        {XFT_DEX, "DEX"},        {XFT_PDF, "PDF"},       {XFT_CFBF, "CFBF"},      {XFT_JPEG, "Jpeg"},
                        {XFT_PNG, "PNG"},        {XFT_RAR, "RAR"},       {XFT_ISO9660, "ISO9660"}, {XFT_ARCHIVE, "Archive"},
                        {XFT_IMAGE, "Image"},    {XFT_AMIGAHUNK, "Amiga"}, {XFT_ATARIST, "AtariST"}, {XFT_JAVACLASS, "JavaClass"},
                        {XFT_PYC, "PYC"},        {XFT_DOS16M, "DOS16M"}, {XFT_DOS4G, "DOS4G"},    {XFT_UNKNOWN, NULL}};
        int j = 0;

        for (j = 0; pAliases[j].pName; j++) {
            if (pAliases[j].type == pEngine->fileType) {
                js_set(pCtx, global, pAliases[j].pName, js_dup(object));
            }
        }

        /* A PE file is also an MSDOS file for the script API. */
        if ((pEngine->fileType == XFT_PE32) || (pEngine->fileType == XFT_PE64)) {
            js_set(pCtx, global, "MSDOS", js_dup(object));
        }

        /* A .NET PE additionally answers to DOTNET, so the PE/DOTNET scripts
         * can call DOTNET.isNetTypePresent(...) etc. The .NET methods are on
         * the same object. */
        if (pEngine->bIsCliAssembly) {
            js_set(pCtx, global, "DOTNET", js_dup(object));
        }
    }

    {
        /* Util exposes the seven public slots of Util_script. */
        JSVal util = js_new_object(pCtx);

        js_def_method(pCtx, util, "div64", api_dispatch, 2, (void *)(size_t)A_div64);
        js_def_method(pCtx, util, "divu64", api_dispatch, 2, (void *)(size_t)A_divu64);
        js_def_method(pCtx, util, "shlu64", api_dispatch, 2, (void *)(size_t)A_shlu64);
        js_def_method(pCtx, util, "shru64", api_dispatch, 2, (void *)(size_t)A_shru64);
        js_def_method(pCtx, util, "shl64", api_dispatch, 2, (void *)(size_t)A_shl64);
        js_def_method(pCtx, util, "shr64", api_dispatch, 2, (void *)(size_t)A_shr64);
        js_def_method(pCtx, util, "secondsToTimeStr", api_dispatch, 1, (void *)(size_t)A_secondsToTimeStr);
        js_set(pCtx, global, "Util", util);
        js_release(pCtx, util);
    }

    js_release(pCtx, object);
    js_release(pCtx, global);

    js_def_fn(pCtx, "_setResult", fn_set_result, 4, NULL);
    js_def_fn(pCtx, "_isResultPresent", fn_is_result_present, 2, NULL);
    js_def_fn(pCtx, "_getNumberOfResults", fn_get_number_of_results, 1, NULL);
    js_def_fn(pCtx, "_removeResult", fn_remove_result, 2, NULL);
    js_def_fn(pCtx, "includeScript", fn_include_script, 1, NULL);
    js_def_fn(pCtx, "_log", fn_log, 1, NULL);
    js_def_fn(pCtx, "_isStop", fn_is_stop, 0, NULL);
    js_def_fn(pCtx, "_breakScan", fn_break_scan, 0, NULL);
    js_def_fn(pCtx, "_encodingList", fn_noop, 0, NULL);
    js_def_fn(pCtx, "_isConsoleMode", fn_true, 0, (void *)(size_t)1);
    js_def_fn(pCtx, "_isGuiMode", fn_true, 0, (void *)(size_t)0);
    js_def_fn(pCtx, "_isLiteMode", fn_true, 0, (void *)(size_t)0);
    js_def_fn(pCtx, "_isLibraryMode", fn_true, 0, (void *)(size_t)0);
    js_def_fn(pCtx, "_getEngineVersion", fn_engine_version, 0, NULL);
    js_def_fn(pCtx, "_getOS", fn_get_os, 0, NULL);
    js_def_fn(pCtx, "_getQtVersion", fn_engine_version, 0, NULL);
}
