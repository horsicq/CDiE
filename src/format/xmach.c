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

/* xmach.c - Mach-O reader, ported from XMACH.
 *
 * Walks the load commands to collect the LC_LOAD_DYLIB libraries (matched by
 * basename, as MACH_Script does), the sections (by sectname), and the
 * entry-point file offset from LC_MAIN. Fat binaries are a different file type
 * (Mach-O FAT) and are not handled here.
 */

#include "xmach.h"
#include "../core/utils.h"

#define MACH_MAGIC 0xFEEDFACEu
#define MACH_MAGIC_64 0xFEEDFACFu
#define MACH_CIGAM 0xCEFAEDFEu
#define MACH_CIGAM_64 0xCFFAEDFEu

#define MACH_LC_SEGMENT 0x1
#define MACH_LC_LOAD_DYLIB 0xC
#define MACH_LC_SEGMENT_64 0x19
#define MACH_LC_MAIN 0x80000028u
#define MACH_LC_VERSION_MIN_MACOSX 0x24
#define MACH_LC_VERSION_MIN_IPHONEOS 0x25
#define MACH_LC_VERSION_MIN_TVOS 0x2F
#define MACH_LC_VERSION_MIN_WATCHOS 0x30
#define MACH_LC_BUILD_VERSION 0x32

/* CPU types (XMACH_DEF). */
#define MACH_CPU_MC680x0 0x6
#define MACH_CPU_I386 0x7
#define MACH_CPU_X86_64 0x1000007
#define MACH_CPU_ARM 0xC
#define MACH_CPU_ARM64 0x100000C
#define MACH_CPU_POWERPC 0x12
#define MACH_CPU_POWERPC64 0x1000012

#define MACH_SUBTYPE_ARM_V6 6
#define MACH_SUBTYPE_ARM_V7 9

/* Platforms (build_version_command). */
#define MACH_PLAT_MACOS 1
#define MACH_PLAT_IOS 2
#define MACH_PLAT_TVOS 3
#define MACH_PLAT_WATCHOS 4
#define MACH_PLAT_BRIDGEOS 5
#define MACH_PLAT_MACCATALYST 6
#define MACH_PLAT_IOSSIMULATOR 7
#define MACH_PLAT_TVOSSIMULATOR 8
#define MACH_PLAT_WATCHOSSIMULATOR 9
#define MACH_PLAT_DRIVERKIT 10
#define MACH_PLAT_FIRMWARE 13
#define MACH_PLAT_SEPOS 14

#define MACH_FULL_VERSION(a, b, c) (((cd_u32)(a) << 16) | ((cd_u32)(b) << 8) | (cd_u32)(c))

/* getHeaderCpuTypesS: CPU type -> arch name. */
static const char *mach_cpu_type_name(cd_u32 nType)
{
    switch (nType) {
        case 1: return "VAX";
        case 2: return "ROMP";
        case 4: return "NS32032";
        case 5: return "NS32332";
        case 6: return "MC680x0";
        case 7: return "I386";
        case 0x1000007: return "X86_64";
        case 8: return "MIPS";
        case 9: return "NS32532";
        case 0xB: return "HPPA";
        case 0xC: return "ARM";
        case 0x100000C: return "ARM64";
        case 0x200000C: return "ARM64_32";
        case 0xD: return "MC88000";
        case 0xE: return "SPARC";
        case 0xF: return "I860";
        case 0x10: return "I860_LITTLE";
        case 0x11: return "RS6000";
        case 0x12: return "POWERPC";
        case 0x1000012: return "POWERPC64";
        case 255: return "VEO";
        default: return "Unknown";
    }
}

static const char *mach_mc680_subtype(cd_u32 nSub)
{
    switch (nSub) {
        case 1: return "MC68030";
        case 2: return "MC68040";
        case 3: return "MC68030_ONLY";
        default: return NULL;
    }
}

static const char *mach_arm_subtype(cd_u32 nSub)
{
    switch (nSub) {
        case 0: return "ARM_ALL";
        case 1: return "ARM_A500_ARCH";
        case 2: return "ARM_A500";
        case 3: return "ARM_A440";
        case 4: return "ARM_M4";
        case 5: return "ARM_V4T";
        case 6: return "ARM_V6";
        case 7: return "ARM_V5TEJ";
        case 8: return "ARM_XSCALE";
        case 9: return "ARM_V7";
        case 10: return "ARM_V7F";
        case 11: return "ARM_V7S";
        case 12: return "ARM_V7K";
        case 14: return "ARM_V6M";
        case 15: return "ARM_V7M";
        case 16: return "ARM_V7EM";
        case 0x80000002: return "ARM64E";
        default: return NULL;
    }
}

/* XMACH::_getArch: the CPU-type name, refined by the sub-type for the MC680x0
 * and ARM families. */
static const char *mach_arch_name(cd_u32 nType, cd_u32 nSub)
{
    const char *pName = mach_cpu_type_name(nType);

    if (nType == MACH_CPU_MC680x0) {
        const char *pSub = mach_mc680_subtype(nSub);

        if (pSub != NULL) {
            pName = pSub;
        }
    } else if ((nType == MACH_CPU_ARM) || (nType == MACH_CPU_ARM64)) {
        if (nSub != 0) {
            const char *pSub = mach_arm_subtype(nSub);

            if (pSub != NULL) {
                pName = pSub;
            }
        }
    }

    return pName;
}

/* XBinary::get_uint32_full_version: "X.Y.Z" from packed nibbles. */
static void mach_full_version(cd_u32 nValue, char *pOut, size_t nOutSize)
{
    x_snprintf(pOut, nOutSize, "%u.%u.%u", (unsigned)((nValue >> 16) & 0xFFFF), (unsigned)((nValue >> 8) & 0xFF), (unsigned)(nValue & 0xFF));
}

static void mach_set_str(char *pDst, size_t nDstSize, const char *pSrc)
{
    x_strncpy(pDst, pSrc, nDstSize - 1);
    pDst[nDstSize - 1] = 0;
}

/* The Foundation-library current version maps to a macOS/iOS release when no
 * LC_BUILD_VERSION / LC_VERSION_MIN command carries the version. Mirrors the
 * fallback branch of XMACH::getFileFormatInfo. */
static void mach_foundation_fallback(XMACH *pMach, const char **ppOsName, char *pVer, size_t nVerSize)
{
    cd_u32 v = 0;

    if (!xmach_library_present(pMach, "Foundation")) {
        return;
    }

    v = xmach_library_current_version(pMach, "Foundation");

    if ((x_strcmp(*ppOsName, "Mac OS X") == 0) || (x_strcmp(*ppOsName, "OS X") == 0) || (x_strcmp(*ppOsName, "macOS") == 0)) {
        if ((v >= MACH_FULL_VERSION(397, 40, 0)) && (v < MACH_FULL_VERSION(425, 0, 0))) mach_set_str(pVer, nVerSize, "10.0.0");
        else if (v < MACH_FULL_VERSION(567, 0, 0)) mach_set_str(pVer, nVerSize, "10.3.0");
        else if (v < MACH_FULL_VERSION(677, 0, 0)) mach_set_str(pVer, nVerSize, "10.4.0");
        else if (v < MACH_FULL_VERSION(677, 24, 0)) mach_set_str(pVer, nVerSize, "10.5.0");
        else if (v < MACH_FULL_VERSION(751, 0, 0)) mach_set_str(pVer, nVerSize, "10.5.7");
        else if (v < MACH_FULL_VERSION(833, 10, 0)) mach_set_str(pVer, nVerSize, "10.6.0");
        else if (v < MACH_FULL_VERSION(833, 25, 0)) mach_set_str(pVer, nVerSize, "10.7.0");
        else if (v < MACH_FULL_VERSION(945, 18, 0)) mach_set_str(pVer, nVerSize, "10.7.4");
        else if (v < MACH_FULL_VERSION(1151, 16, 0)) mach_set_str(pVer, nVerSize, "10.8.4");
        else if (v < MACH_FULL_VERSION(1200, 0, 0)) mach_set_str(pVer, nVerSize, "10.10.0");

        if (v < MACH_FULL_VERSION(833, 10, 0)) {
            *ppOsName = "Mac OS X";
        }
    } else if ((x_strcmp(*ppOsName, "iPhone OS") == 0) || (x_strcmp(*ppOsName, "iOS") == 0) || (x_strcmp(*ppOsName, "iPadOS") == 0)) {
        if (v < MACH_FULL_VERSION(678, 24, 0)) mach_set_str(pVer, nVerSize, "1.0.0");
        else if (v < MACH_FULL_VERSION(678, 26, 0)) mach_set_str(pVer, nVerSize, "2.0.0");
        else if (v < MACH_FULL_VERSION(678, 29, 0)) mach_set_str(pVer, nVerSize, "2.1.0");
        else if (v < MACH_FULL_VERSION(678, 47, 0)) mach_set_str(pVer, nVerSize, "2.2.0");
        else if (v < MACH_FULL_VERSION(678, 51, 0)) mach_set_str(pVer, nVerSize, "3.0.0");
        else if (v < MACH_FULL_VERSION(678, 60, 0)) mach_set_str(pVer, nVerSize, "3.1.0");
        else if (v < MACH_FULL_VERSION(751, 32, 0)) mach_set_str(pVer, nVerSize, "3.2.0");
        else if (v < MACH_FULL_VERSION(751, 37, 0)) mach_set_str(pVer, nVerSize, "4.0.0");
        else if (v < MACH_FULL_VERSION(751, 49, 0)) mach_set_str(pVer, nVerSize, "4.1.0");
        else if (v < MACH_FULL_VERSION(881, 0, 0)) mach_set_str(pVer, nVerSize, "4.2.0");
        else if (v < MACH_FULL_VERSION(890, 10, 0)) mach_set_str(pVer, nVerSize, "5.0.0");
        else if (v < MACH_FULL_VERSION(992, 0, 0)) mach_set_str(pVer, nVerSize, "5.1.0");
        else if (v < MACH_FULL_VERSION(993, 0, 0)) mach_set_str(pVer, nVerSize, "6.0.0");
        else if (v < MACH_FULL_VERSION(1047, 20, 0)) mach_set_str(pVer, nVerSize, "6.1.0");
        else if (v < MACH_FULL_VERSION(1047, 25, 0)) mach_set_str(pVer, nVerSize, "7.0.0");
        else if (v < MACH_FULL_VERSION(1140, 11, 0)) mach_set_str(pVer, nVerSize, "7.1.0");
        else if (v < MACH_FULL_VERSION(1141, 1, 0)) mach_set_str(pVer, nVerSize, "8.0.0");
        else if (v < MACH_FULL_VERSION(1142, 14, 0)) mach_set_str(pVer, nVerSize, "8.1.0");
        else if (v < MACH_FULL_VERSION(1144, 17, 0)) mach_set_str(pVer, nVerSize, "8.2.0");
        else if (v < MACH_FULL_VERSION(1200, 0, 0)) mach_set_str(pVer, nVerSize, "8.3.0");

        *ppOsName = (v < MACH_FULL_VERSION(751, 32, 0)) ? "iPhone OS" : "iOS";
    }
}

static void mach_compute_os(XMACH *pMach, int bBuildVer, cd_u32 nPlatform, cd_u32 nMinos, int bVersionMin, cd_u32 nVersionMinCmd, cd_u32 nVersionMinValue)
{
    cd_u32 nType = pMach->nCpuType;
    cd_u32 nSub = pMach->nCpuSubType;
    const char *pOsName = "Mac OS";
    char sVer[24];

    sVer[0] = 0;

    mach_set_str(pMach->sArch, sizeof(pMach->sArch), mach_arch_name(nType, nSub));

    /* CPU-type default OS + version range. */
    if (nType == MACH_CPU_MC680x0) {
        pOsName = "Mac OS";
        mach_set_str(sVer, sizeof(sVer), "1.0-8.1");
    } else if (nType == MACH_CPU_POWERPC) {
        pOsName = "Mac OS";
        mach_set_str(sVer, sizeof(sVer), "7.1.2-9.22");
    } else if (nType == MACH_CPU_POWERPC64) {
        pOsName = "Mac OS X";
        mach_set_str(sVer, sizeof(sVer), "10.4-10.6");
    } else if ((nType == MACH_CPU_I386) || (nType == MACH_CPU_X86_64)) {
        pOsName = "Mac OS X";
        mach_set_str(sVer, sizeof(sVer), "10.4-10.14");
    } else if ((nType == MACH_CPU_ARM) || (nType == MACH_CPU_ARM64)) {
        pOsName = "iOS";

        if (nSub == MACH_SUBTYPE_ARM_V6) {
            pOsName = "iPhone OS";
            mach_set_str(sVer, sizeof(sVer), "1.0-4.2.1");
        } else if (nSub == MACH_SUBTYPE_ARM_V7) {
            pOsName = "iPhone OS";
            mach_set_str(sVer, sizeof(sVer), "3.0-10.3.4");
        } else if (nType == MACH_CPU_ARM64) {
            pOsName = "iOS";
            mach_set_str(sVer, sizeof(sVer), "7.0-16.0");
        }
    }

    /* A LC_VERSION_MIN command names the OS even before its version is read. */
    if (!bBuildVer && bVersionMin) {
        if (nVersionMinCmd == MACH_LC_VERSION_MIN_IPHONEOS) pOsName = "iOS";
        else if (nVersionMinCmd == MACH_LC_VERSION_MIN_MACOSX) pOsName = "macOS";
        else if (nVersionMinCmd == MACH_LC_VERSION_MIN_TVOS) pOsName = "tvOS";
        else if (nVersionMinCmd == MACH_LC_VERSION_MIN_WATCHOS) pOsName = "watchOS";
    }

    if (bBuildVer) {
        if (nPlatform == MACH_PLAT_MACOS) pOsName = "macOS";
        else if ((nPlatform == MACH_PLAT_IOS) || (nPlatform == MACH_PLAT_IOSSIMULATOR)) pOsName = "iOS";
        else if ((nPlatform == MACH_PLAT_TVOS) || (nPlatform == MACH_PLAT_TVOSSIMULATOR)) pOsName = "tvOS";
        else if ((nPlatform == MACH_PLAT_WATCHOS) || (nPlatform == MACH_PLAT_WATCHOSSIMULATOR)) pOsName = "watchOS";
        else if (nPlatform == MACH_PLAT_BRIDGEOS) pOsName = "bridgeOS";
        else if (nPlatform == MACH_PLAT_MACCATALYST) pOsName = "Mac Catalyst";
        else if (nPlatform == MACH_PLAT_DRIVERKIT) pOsName = "Mac DriverKit";
        else if (nPlatform == MACH_PLAT_FIRMWARE) pOsName = "Mac Firmware";
        else if (nPlatform == MACH_PLAT_SEPOS) pOsName = "sepOS";

        if (nMinos) {
            mach_full_version(nMinos, sVer, sizeof(sVer));
        }
    } else if (bVersionMin) {
        mach_full_version(nVersionMinValue, sVer, sizeof(sVer));
    } else {
        mach_foundation_fallback(pMach, &pOsName, sVer, sizeof(sVer));
    }

    mach_set_str(pMach->sOsName, sizeof(pMach->sOsName), pOsName);
    mach_set_str(pMach->sOsVersion, sizeof(pMach->sOsVersion), sVer);
}

static char *mach_read_asciiz(XBFile *pFile, cd_i64 nOffset)
{
    cd_i64 nEnd = nOffset;

    if ((nOffset < 0) || (nOffset >= pFile->nSize)) {
        return cd_strdup("");
    }

    while ((nEnd < pFile->nSize) && (pFile->pData[nEnd] != 0)) {
        nEnd++;
    }

    return cd_strndup((const char *)pFile->pData + nOffset, (size_t)(nEnd - nOffset));
}

/* basename after the last '/'. */
static char *mach_basename(const char *pPath)
{
    const char *pLast = pPath;
    const char *p = pPath;

    for (; *p; p++) {
        if (*p == '/') {
            pLast = p + 1;
        }
    }

    return cd_strdup(pLast);
}

int xmach_parse(XBFile *pFile, XMACH *pMach)
{
    cd_u32 nMagic = 0;
    int bBE = 0;
    int b64 = 0;
    cd_u32 nCmds = 0;
    cd_i64 nHeaderSize = 0;
    cd_i64 nOffset = 0;
    cd_u32 i = 0;
    CDVec vecLibs;
    CDVec vecSections;
    int bBuildVer = 0;
    cd_u32 nPlatform = 0;
    cd_u32 nMinos = 0;
    int bVersionMin = 0;
    cd_u32 nVersionMinCmd = 0;
    cd_u32 nVersionMinValue = 0;

    x_memset(pMach, 0, sizeof(XMACH));
    pMach->nEntryPointOffset = -1;
    cdvec_init(&vecLibs);
    cdvec_init(&vecSections);

    if ((pFile == NULL) || (pFile->nSize < 0x1C)) {
        return 0;
    }

    nMagic = xb_u32(pFile, 0, 0);

    if ((nMagic == MACH_MAGIC) || (nMagic == MACH_MAGIC_64)) {
        bBE = 0;
    } else if ((nMagic == MACH_CIGAM) || (nMagic == MACH_CIGAM_64)) {
        bBE = 1;
    } else {
        return 0;
    }

    b64 = ((nMagic == MACH_MAGIC_64) || (nMagic == MACH_CIGAM_64)) ? 1 : 0;
    pMach->bIs64 = b64;
    pMach->bBigEndian = bBE;
    pMach->nCpuType = xb_u32(pFile, 4, bBE);
    pMach->nCpuSubType = xb_u32(pFile, 8, bBE);

    nCmds = xb_u32(pFile, 16, bBE); /* ncmds */
    nHeaderSize = b64 ? 32 : 28;
    nOffset = nHeaderSize;

    if (nCmds > 0x10000) {
        nCmds = 0x10000;
    }

    for (i = 0; i < nCmds; i++) {
        cd_u32 nCmd = 0;
        cd_u32 nCmdSize = 0;

        if (nOffset + 8 > pFile->nSize) {
            break;
        }

        nCmd = xb_u32(pFile, nOffset, bBE);
        nCmdSize = xb_u32(pFile, nOffset + 4, bBE);

        if ((nCmdSize < 8) || (nOffset + nCmdSize > pFile->nSize)) {
            break;
        }

        if (nCmd == MACH_LC_LOAD_DYLIB) {
            /* dylib_command: load_command(8) + name(u32 offset from cmd),
             * timestamp, current_version, compatibility_version. */
            cd_u32 nNameOffset = xb_u32(pFile, nOffset + 8, bBE);
            cd_u32 nCurrentVersion = xb_u32(pFile, nOffset + 16, bBE);
            char *pFullName = mach_read_asciiz(pFile, nOffset + (cd_i64)nNameOffset);
            XMachLibrary *pLib = (XMachLibrary *)cd_malloc(sizeof(XMachLibrary));

            pLib->pName = mach_basename(pFullName);
            pLib->nCurrentVersion = nCurrentVersion;
            cd_free(pFullName);
            cdvec_push(&vecLibs, pLib);
        } else if ((nCmd == MACH_LC_SEGMENT) || (nCmd == MACH_LC_SEGMENT_64)) {
            int bSeg64 = (nCmd == MACH_LC_SEGMENT_64) ? 1 : 0;
            cd_i64 nNsectsOffset = nOffset + (bSeg64 ? 64 : 48);
            cd_u32 nNsects = xb_u32(pFile, nNsectsOffset, bBE);
            cd_i64 nSectOffset = nOffset + (bSeg64 ? 72 : 56);
            cd_i64 nSectSize = bSeg64 ? 80 : 68;
            cd_i64 nCmdEnd = nOffset + (cd_i64)nCmdSize;
            cd_u32 s = 0;

            /* XMACH::getSectionRecords rejects any nsects that does not fit
             * in a byte, so the walk stays inside the segment command.      */
            if (nNsects & 0xFFFFFF00u) {
                nNsects = 0;
            }

            for (s = 0; s < nNsects; s++) {
                XMachSection *pSection = NULL;

                if ((nSectOffset + nSectSize > pFile->nSize) || (nSectOffset + nSectSize > nCmdEnd)) {
                    break;
                }

                pSection = (XMachSection *)cd_calloc(1, sizeof(XMachSection));
                x_memcpy(pSection->sName, pFile->pData + nSectOffset, 16);
                pSection->sName[16] = 0;

                if (bSeg64) {
                    pSection->nSize = xb_u64(pFile, nSectOffset + 40, bBE);
                    pSection->nOffset = xb_u32(pFile, nSectOffset + 48, bBE);
                } else {
                    pSection->nSize = xb_u32(pFile, nSectOffset + 36, bBE);
                    pSection->nOffset = xb_u32(pFile, nSectOffset + 40, bBE);
                }

                cdvec_push(&vecSections, pSection);
                nSectOffset += nSectSize;
            }
        } else if (nCmd == MACH_LC_MAIN) {
            /* entry_point_command: entryoff is a file offset. */
            pMach->nEntryPointOffset = (cd_i64)xb_u64(pFile, nOffset + 8, bBE);
        } else if (nCmd == MACH_LC_BUILD_VERSION) {
            if (!bBuildVer) {
                bBuildVer = 1;
                nPlatform = xb_u32(pFile, nOffset + 8, bBE);
                nMinos = xb_u32(pFile, nOffset + 12, bBE);
            }
        } else if ((nCmd == MACH_LC_VERSION_MIN_MACOSX) || (nCmd == MACH_LC_VERSION_MIN_IPHONEOS) || (nCmd == MACH_LC_VERSION_MIN_TVOS) ||
                   (nCmd == MACH_LC_VERSION_MIN_WATCHOS)) {
            if (!bVersionMin) {
                bVersionMin = 1;
                nVersionMinCmd = nCmd;
                nVersionMinValue = xb_u32(pFile, nOffset + 8, bBE);
            }
        }

        nOffset += nCmdSize;
    }

    mach_compute_os(pMach, bBuildVer, nPlatform, nMinos, bVersionMin, nVersionMinCmd, nVersionMinValue);

    /* Flatten the vectors into arrays owned by the struct. */
    pMach->nLibraryCount = (int)vecLibs.nSize;

    if (pMach->nLibraryCount > 0) {
        pMach->pLibraries = (XMachLibrary *)cd_malloc((size_t)pMach->nLibraryCount * sizeof(XMachLibrary));

        for (i = 0; (int)i < pMach->nLibraryCount; i++) {
            XMachLibrary *pLib = (XMachLibrary *)vecLibs.ppData[i];

            pMach->pLibraries[i] = *pLib;
            cd_free(pLib);
        }
    }

    pMach->nSectionCount = (int)vecSections.nSize;

    if (pMach->nSectionCount > 0) {
        pMach->pSections = (XMachSection *)cd_malloc((size_t)pMach->nSectionCount * sizeof(XMachSection));

        for (i = 0; (int)i < pMach->nSectionCount; i++) {
            XMachSection *pSection = (XMachSection *)vecSections.ppData[i];

            pMach->pSections[i] = *pSection;
            cd_free(pSection);
        }
    }

    cdvec_free(&vecLibs);
    cdvec_free(&vecSections);

    pMach->bValid = 1;

    return 1;
}

void xmach_free(XMACH *pMach)
{
    int i = 0;

    for (i = 0; i < pMach->nLibraryCount; i++) {
        cd_free(pMach->pLibraries[i].pName);
    }

    cd_free(pMach->pLibraries);
    cd_free(pMach->pSections);
    pMach->bValid = 0;
}

int xmach_library_present(XMACH *pMach, const char *pName)
{
    int i = 0;

    for (i = 0; i < pMach->nLibraryCount; i++) {
        if (x_strcmp(pMach->pLibraries[i].pName, pName) == 0) {
            return 1;
        }
    }

    return 0;
}

cd_u32 xmach_library_current_version(XMACH *pMach, const char *pName)
{
    int i = 0;

    for (i = 0; i < pMach->nLibraryCount; i++) {
        if (x_strcmp(pMach->pLibraries[i].pName, pName) == 0) {
            return pMach->pLibraries[i].nCurrentVersion;
        }
    }

    return 0;
}

int xmach_section_number(XMACH *pMach, const char *pName)
{
    int i = 0;

    for (i = 0; i < pMach->nSectionCount; i++) {
        if (x_strcmp(pMach->pSections[i].sName, pName) == 0) {
            return i;
        }
    }

    return -1;
}

int xmach_section_present(XMACH *pMach, const char *pName)
{
    return (xmach_section_number(pMach, pName) != -1) ? 1 : 0;
}

cd_u64 xmach_section_offset(XMACH *pMach, int nNumber)
{
    if ((nNumber < 0) || (nNumber >= pMach->nSectionCount)) {
        return 0;
    }

    return pMach->pSections[nNumber].nOffset;
}

cd_u64 xmach_section_size(XMACH *pMach, int nNumber)
{
    if ((nNumber < 0) || (nNumber >= pMach->nSectionCount)) {
        return 0;
    }

    return pMach->pSections[nNumber].nSize;
}
