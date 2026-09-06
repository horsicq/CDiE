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

/* xelf.c - ELF reader, ported from XELF.
 *
 * Covers what the ELF database directory queries: the header, the section
 * table (with names resolved through .shstrtab), the program headers, the
 * dynamic library list (DT_NEEDED via the mapped string table), the
 * "TYPE MACHINE-BITS" general-options string, the entry-point file offset for
 * compareEP, and a raw-size/overlay estimate.
 */

#include "xelf.h"
#include "../core/utils.h"

#define ELF_PT_NOTE 4
#define ELF_SHT_NOTE 7

static char *elf_read_asciiz(XBFile *pFile, cd_i64 nOffset);

/* _TABLE_XELF_Machines: e_machine -> arch name (XELF::getArch). */
static const char *elf_machine_name(cd_u16 m)
{
    switch (m) {
        case 0: return "NONE"; case 1: return "M32"; case 2: return "SPARC"; case 3: return "386";
        case 4: return "68K"; case 5: return "88K"; case 6: return "486"; case 7: return "860";
        case 8: return "MIPS"; case 9: return "S370"; case 10: return "MIPS_RS3_LE"; case 11: return "RS6000";
        case 15: return "PARISC"; case 16: return "nCUBE"; case 17: return "VPP500"; case 18: return "SPARC32PLUS";
        case 19: return "960"; case 20: return "PPC"; case 21: return "PPC64"; case 22: return "S390";
        case 23: return "SPU"; case 36: return "V800"; case 37: return "FR20"; case 38: return "RH32";
        case 39: return "RCE"; case 40: return "ARM"; case 41: return "ALPHA"; case 42: return "SH";
        case 43: return "SPARCV9"; case 44: return "TRICORE"; case 45: return "ARC"; case 46: return "H8_300";
        case 47: return "H8_300H"; case 48: return "H8S"; case 49: return "H8_500"; case 50: return "IA_64";
        case 51: return "MIPS_X"; case 52: return "COLDFIRE"; case 53: return "68HC12"; case 54: return "MMA";
        case 55: return "PCP"; case 56: return "NCPU"; case 57: return "NDR1"; case 58: return "STARCORE";
        case 59: return "ME16"; case 60: return "ST100"; case 61: return "TINYJ"; case 62: return "AMD64";
        case 63: return "PDSP"; case 66: return "FX66"; case 67: return "ST9PLUS"; case 68: return "ST7";
        case 69: return "68HC16"; case 70: return "68HC11"; case 71: return "68HC08"; case 72: return "68HC05";
        case 73: return "SVX"; case 74: return "ST19"; case 75: return "VAX"; case 76: return "CRIS";
        case 77: return "JAVELIN"; case 78: return "FIREPATH"; case 79: return "ZSP"; case 80: return "MMIX";
        case 81: return "HUANY"; case 82: return "PRISM"; case 83: return "AVR"; case 84: return "FR30";
        case 85: return "D10V"; case 86: return "D30V"; case 87: return "V850"; case 88: return "M32R";
        case 89: return "MN10300"; case 90: return "MN10200"; case 91: return "PJ"; case 92: return "OPENRISC";
        case 93: return "ARC_A5"; case 94: return "XTENSA"; case 95: return "VIDEOCORE"; case 96: return "TMM_GPP";
        case 97: return "NS32K"; case 98: return "TPC"; case 99: return "SNP1K"; case 100: return "ST200";
        case 101: return "IP2K"; case 102: return "MAX"; case 103: return "CR"; case 104: return "F2MC16";
        case 105: return "MSP430"; case 106: return "BLACKFIN"; case 107: return "SE_C33"; case 108: return "SEP";
        case 109: return "ARCA"; case 110: return "UNICORE"; case 111: return "EXCESS"; case 112: return "DXP";
        case 113: return "ALTERA_NIOS2"; case 114: return "CRX"; case 115: return "XGATE"; case 116: return "C166";
        case 117: return "M16C"; case 118: return "DSPIC30F"; case 119: return "CE"; case 120: return "M32C";
        case 140: return "TI_C6000"; case 183: return "AARCH64"; case 243: return "RISC_V"; case 258: return "LOONGARCH";
        case 0x5441: return "FRV"; case 0x18ad: return "AVR32"; case 0x9026: return "ALPHA"; case 0x9080: return "CYGNUS_V850";
        case 0x9041: return "CYGNUS_M32R"; case 0xA390: return "S390_OLD"; case 0xbeef: return "CYGNUS_MN10300";
        default: return "Unknown";
    }
}

/* e_ident[EI_OSABI] -> OS name, or NULL when it leaves the default "Unix". */
static const char *elf_osabi_osname(cd_u8 nOsAbi)
{
    switch (nOsAbi) {
        case 1: return "Hewlett-Packard HP-UX";
        case 2: return "NetBSD";
        case 3: return "Linux";
        case 6: return "Sun Solaris";
        case 7: return "AIX";
        case 8: return "IRIX";
        case 9: return "FreeBSD";
        case 10: return "Compaq TRU64 UNIX";
        case 11: return "Novell Modesto";
        case 12: return "OpenBSD";
        case 13: return "Open VMS";
        case 14: return "Hewlett-Packard Non-Stop Kernel";
        case 15: return "Amiga Research OS";
        case 16: return "FenixOS";
        case 18: return "Open VOS";
        default: return NULL;
    }
}

/* XBinary::getAndroidVersionFromApi (used by the ELF Android note). */
static const char *elf_android_version(cd_u32 nApi)
{
    switch (nApi) {
        case 1: return "1.0"; case 2: return "1.1"; case 3: return "1.5"; case 4: return "1.6";
        case 5: return "2.0"; case 6: return "2.0.1"; case 7: return "2.1"; case 8: return "2.2.X";
        case 9: return "2.3-2.3.2"; case 10: return "2.3.3-2.3.7"; case 11: return "3.0"; case 12: return "3.1";
        case 13: return "3.2.X"; case 14: return "4.0.1-4.0.2"; case 15: return "4.0.3-4.0.4"; case 16: return "4.1.X";
        case 17: return "4.2.X"; case 18: return "4.3.X"; case 19: return "4.4-4.4.4"; case 20: return "4.4W";
        case 21: return "5.0"; case 22: return "5.1"; case 23: return "6.0"; case 24: return "7.0";
        case 25: return "7.1"; case 26: return "8.0"; case 27: return "8.1"; case 28: return "9.0";
        case 29: return "10.0"; case 30: return "11.0"; case 31: return "12.0"; case 32: return "12.1";
        case 33: return "13.0"; case 34: return "14.0"; case 35: return "15.0"; case 36: return "16.0";
        default: return "Unknown";
    }
}

static void elf_set_str(char *pDst, size_t nDstSize, const char *pSrc)
{
    x_strncpy(pDst, pSrc, nDstSize - 1);
    pDst[nDstSize - 1] = 0;
}

/* appendText(sVer, sABI, ", "): join with ", " when both are non-empty. */
static void elf_append_version(char *pVer, size_t nVerSize, const char *pAdd)
{
    size_t nLen = 0;

    if ((pAdd == NULL) || (pAdd[0] == 0)) {
        return;
    }

    nLen = x_strlen(pVer);

    if (nLen > 0) {
        x_strncpy(pVer + nLen, ", ", nVerSize - nLen - 1);
        pVer[nVerSize - 1] = 0; /* x_strncpy need not NUL-terminate when full */
        nLen = x_strlen(pVer);
    }

    x_strncpy(pVer + nLen, pAdd, nVerSize - nLen - 1);
    pVer[nVerSize - 1] = 0;
}

static int elf_section_index(XELF *pElf, const char *pName)
{
    int i = 0;

    for (i = 0; i < pElf->nSectionCount; i++) {
        if ((pElf->pSections[i].pName != NULL) && (x_strcmp(pElf->pSections[i].pName, pName) == 0)) {
            return i;
        }
    }

    return -1;
}

/* The program interpreter (PT_INTERP), else the .interp section. Owned. */
static char *elf_interp(XBFile *pFile, XELF *pElf)
{
    int i = 0;
    int nSec = -1;

    for (i = 0; i < pElf->nProgramCount; i++) {
        if (pElf->pPrograms[i].nType == 3 /* PT_INTERP */) {
            return elf_read_asciiz(pFile, (cd_i64)pElf->pPrograms[i].nOffset);
        }
    }

    nSec = elf_section_index(pElf, ".interp");

    if (nSec >= 0) {
        return elf_read_asciiz(pFile, (cd_i64)pElf->pSections[nSec].nOffset);
    }

    return cd_strdup("");
}

/* Clamps a raw offset/size pair taken from a header to the end of the file.
 * The test is done in cd_u64 because adding the two 64-bit fields as cd_i64
 * overflows on a malformed header. */
static cd_i64 elf_clamp_end(XBFile *pFile, cd_u64 nOffset, cd_u64 nSize)
{
    if ((nOffset > (cd_u64)pFile->nSize) || (nSize > (cd_u64)pFile->nSize - nOffset)) {
        return pFile->nSize;
    }

    return (cd_i64)(nOffset + nSize);
}

/* Finds an ELF note by (type, name); nWantType < 0 matches any type. Notes are
 * taken from the PT_NOTE segments, or the SHT_NOTE sections when there is no
 * PT_NOTE. On a match returns 1 and the descriptor offset/size. */
static int elf_scan_range_note(XBFile *pFile, cd_i64 nStart, cd_i64 nEnd, int bBE, int nWantType, const char *pWantName, cd_i64 *pnDescOff, cd_u32 *pnDescSize)
{
    cd_i64 p = nStart;

    /* A malformed 64-bit ELF can store an offset with bit 63 set, which is a
     * large negative i64. Reject it so the walk stays bounded (nEnd is already
     * clamped to the file size by the callers). */
    if (nStart < 0) {
        return 0;
    }

    while (p + 12 <= nEnd) {
        cd_u32 nNameSz = xb_u32(pFile, p, bBE);
        cd_u32 nDescSz = xb_u32(pFile, p + 4, bBE);
        cd_u32 nType = xb_u32(pFile, p + 8, bBE);
        cd_i64 nNameOff = p + 12;
        cd_i64 nDescOff = nNameOff + ((cd_i64)((nNameSz + 3) & ~3u));
        cd_i64 nNext = nDescOff + ((cd_i64)((nDescSz + 3) & ~3u));

        if ((nNameSz > 0x100) || (nDescSz > 0x10000) || (nNext <= p) || (nDescOff > nEnd)) {
            break;
        }

        if (((nWantType < 0) || ((cd_u32)nWantType == nType)) && (nNameSz > 0)) {
            char *pName = elf_read_asciiz(pFile, nNameOff);
            int bMatch = (x_strcmp(pName, pWantName) == 0);

            cd_free(pName);

            if (bMatch) {
                *pnDescOff = nDescOff;
                *pnDescSize = nDescSz;

                return 1;
            }
        }

        p = nNext;
    }

    return 0;
}

static int elf_find_note(XBFile *pFile, XELF *pElf, int nWantType, const char *pWantName, cd_i64 *pnDescOff, cd_u32 *pnDescSize)
{
    int i = 0;
    int bHavePtNote = 0;

    for (i = 0; i < pElf->nProgramCount; i++) {
        if (pElf->pPrograms[i].nType == ELF_PT_NOTE) {
            cd_i64 nStart = (cd_i64)pElf->pPrograms[i].nOffset;
            cd_i64 nEnd = elf_clamp_end(pFile, pElf->pPrograms[i].nOffset, pElf->pPrograms[i].nFileSize);

            bHavePtNote = 1;

            if (elf_scan_range_note(pFile, nStart, nEnd, pElf->bBigEndian, nWantType, pWantName, pnDescOff, pnDescSize)) {
                return 1;
            }
        }
    }

    if (bHavePtNote) {
        return 0;
    }

    for (i = 0; i < pElf->nSectionCount; i++) {
        if (pElf->pSections[i].nType == ELF_SHT_NOTE) {
            cd_i64 nStart = (cd_i64)pElf->pSections[i].nOffset;
            cd_i64 nEnd = elf_clamp_end(pFile, pElf->pSections[i].nOffset, pElf->pSections[i].nSize);

            if (elf_scan_range_note(pFile, nStart, nEnd, pElf->bBigEndian, nWantType, pWantName, pnDescOff, pnDescSize)) {
                return 1;
            }
        }
    }

    return 0;
}

/* Copies the .comment slice after pAfter up to (but not including) the first
 * pStop character (or end of the string) into pOut. */
static void elf_section_after(const char *pComment, const char *pAfter, char cStop, char *pOut, size_t nOutSize)
{
    char *pAt = x_strstr(pComment, pAfter);

    pOut[0] = 0;

    if (pAt != NULL) {
        const char *pStart = pAt + x_strlen(pAfter);
        const char *pEnd = pStart;

        while ((*pEnd != 0) && (*pEnd != cStop)) {
            pEnd++;
        }

        {
            size_t nLen = (size_t)(pEnd - pStart);

            if (nLen >= nOutSize) {
                nLen = nOutSize - 1;
            }

            x_memcpy(pOut, pStart, nLen);
            pOut[nLen] = 0;
        }
    }
}

/* Walks the .comment strings and sets the distribution name/version. Mirrors
 * the XELF::getFileFormatInfo .comment loop. Returns 1 on a match. */
static int elf_comment_distro(XBFile *pFile, XELF *pElf, const char **ppOsName, char *pVer, size_t nVerSize)
{
    int nSec = elf_section_index(pElf, ".comment");
    cd_i64 nStart = 0;
    cd_i64 nEnd = 0;
    cd_i64 p = 0;

    if (nSec < 0) {
        return 0;
    }

    nStart = (cd_i64)pElf->pSections[nSec].nOffset;
    nEnd = elf_clamp_end(pFile, pElf->pSections[nSec].nOffset, pElf->pSections[nSec].nSize);

    if (nStart < 0) {
        return 0; /* a bit-63 offset in a malformed 64-bit ELF */
    }

    for (p = nStart; p < nEnd;) {
        char *pC = elf_read_asciiz(pFile, p);
        size_t nAdvance = x_strlen(pC) + 1;
        int bFound = 0;

        if (x_strstr(pC, "Ubuntu") || x_strstr(pC, "ubuntu")) {
            *ppOsName = "Ubuntu Linux";

            if (x_strstr(pC, "ubuntu1~")) {
                elf_section_after(pC, "ubuntu1~", ')', pVer, nVerSize);
            }

            bFound = 1;
        } else if (x_strstr(pC, "Debian") || x_strstr(pC, "debian")) {
            *ppOsName = "Debian Linux";
            bFound = 1;
        } else if (x_strstr(pC, "StartOS")) {
            *ppOsName = "StartOS Linux";
            bFound = 1;
        } else if (x_strstr(pC, "Gentoo")) {
            *ppOsName = "Gentoo Linux";
            bFound = 1;
        } else if (x_strstr(pC, "Alpine")) {
            *ppOsName = "Alpine Linux";
            bFound = 1;
        } else if (x_strstr(pC, "Wind River Linux")) {
            *ppOsName = "Wind River Linux";
            bFound = 1;
        } else if (x_strstr(pC, "SuSE") || x_strstr(pC, "SUSE Linux")) {
            *ppOsName = "SUSE Linux";
            bFound = 1;
        } else if (x_strstr(pC, "Mandrakelinux") || x_strstr(pC, "Linux-Mandrake") || x_strstr(pC, "Mandrake Linux")) {
            *ppOsName = "Mandrake Linux";
            bFound = 1;
        } else if (x_strstr(pC, "ASPLinux")) {
            *ppOsName = "ASPLinux";
            bFound = 1;
        } else if (x_strstr(pC, "Red Hat")) {
            *ppOsName = "Red Hat Linux";
            bFound = 1;
        } else if (x_strstr(pC, "Hancom Linux")) {
            *ppOsName = "Hancom Linux";
            bFound = 1;
        } else if (x_strstr(pC, "TurboLinux")) {
            *ppOsName = "Turbolinux";
            bFound = 1;
        } else if (x_strstr(pC, "Vine Linux")) {
            *ppOsName = "Vine Linux";
            bFound = 1;
        }

        if (x_strcmp(*ppOsName, "Linux") != 0) {
            if (x_strstr(pC, "SunOS")) {
                *ppOsName = "SunOS";

                if (x_strstr(pC, "@(#)SunOS ")) {
                    elf_section_after(pC, "@(#)SunOS ", 0, pVer, nVerSize);
                }

                bFound = 1;
            }
        }

        cd_free(pC);

        if (bFound) {
            return 1;
        }

        if (nAdvance == 0) {
            break;
        }

        p += (cd_i64)nAdvance;
    }

    return 0;
}

/* Fills pElf->sOsName / sOsVersion / sArch for the verbose "Operation system"
 * line, following XELF::getFileFormatInfo. */
static void elf_compute_os(XBFile *pFile, XELF *pElf)
{
    const char *pOsName = "Unix";
    const char *pAbi = elf_osabi_osname(pElf->nOsAbi);
    char sVer[24];
    char *pInterp = NULL;
    cd_i64 nDescOff = 0;
    cd_u32 nDescSize = 0;
    int bBE = pElf->bBigEndian;

    x_memset(sVer, 0, sizeof(sVer));

    elf_set_str(pElf->sArch, sizeof(pElf->sArch), elf_machine_name(pElf->nMachine));

    if (pAbi != NULL) {
        pOsName = pAbi;
    }

    pInterp = elf_interp(pFile, pElf);

    if ((x_strcmp(pOsName, "Unix") == 0) && x_strstr(pInterp, "ld-elf.so")) pOsName = "FreeBSD";
    if ((x_strcmp(pOsName, "Unix") == 0) && x_strstr(pInterp, "linux")) pOsName = "Linux";
    if ((x_strcmp(pOsName, "Unix") == 0) && x_strstr(pInterp, "ldqnx")) pOsName = "QNX";
    if ((x_strcmp(pOsName, "Unix") == 0) && x_strstr(pInterp, "uClibc")) pOsName = "mClinux";

    if ((x_strcmp(pOsName, "Unix") == 0) || (x_strcmp(pOsName, "Linux") == 0)) {
        elf_comment_distro(pFile, pElf, &pOsName, sVer, sizeof(sVer));
    }

    if (x_strcmp(pOsName, "FreeBSD") == 0) {
        int nSec = elf_section_index(pElf, ".comment");

        if (nSec >= 0) {
            char *pC = elf_read_asciiz(pFile, (cd_i64)pElf->pSections[nSec].nOffset);

            if (x_strstr(pC, "FreeBSD: release/")) {
                elf_section_after(pC, "FreeBSD: release/", '/', sVer, sizeof(sVer));
            }

            cd_free(pC);
        }
    }

    /* Android: an "Android" note, liblog.so, or the Android linker. */
    if (x_strcmp(pOsName, "Unix") == 0) {
        if (elf_find_note(pFile, pElf, -1, "Android", &nDescOff, &nDescSize)) {
            pOsName = "Android";

            if (nDescSize >= 4) {
                elf_set_str(sVer, sizeof(sVer), elf_android_version(xb_u32(pFile, nDescOff, bBE)));
            }
        } else if (xelf_library_present(pElf, "liblog.so") || (x_strcmp(pInterp, "system/bin/linker") == 0) || (x_strcmp(pInterp, "system/bin/linker64") == 0)) {
            pOsName = "Android";
        }
    }

    /* GNU note (type 1): OS id when still Unix, plus the "ABI: x.y.z" suffix. */
    if (elf_find_note(pFile, pElf, 1, "GNU", &nDescOff, &nDescSize) && (nDescSize >= 16)) {
        cd_u32 nOS = xb_u32(pFile, nDescOff, bBE);
        cd_u32 nMajor = xb_u32(pFile, nDescOff + 4, bBE);
        cd_u32 nMinor = xb_u32(pFile, nDescOff + 8, bBE);
        cd_u32 nSub = xb_u32(pFile, nDescOff + 12, bBE);
        char sAbi[32];

        if (x_strcmp(pOsName, "Unix") == 0) {
            if (nOS == 0) pOsName = "Linux";
            else if (nOS == 2) pOsName = "Sun Solaris";
            else if (nOS == 3) pOsName = "FreeBSD";
            else if (nOS == 4) pOsName = "NetBSD";
            else if (nOS == 5) pOsName = "Syllable";
        }

        x_snprintf(sAbi, sizeof(sAbi), "ABI: %u.%u.%u", (unsigned)nMajor, (unsigned)nMinor, (unsigned)nSub);
        elf_append_version(sVer, sizeof(sVer), sAbi);
    }

    if (x_strcmp(pOsName, "Unix") == 0) {
        if (elf_section_index(pElf, ".note.android.ident") >= 0) pOsName = "Android";
        else if (elf_section_index(pElf, ".note.minix.ident") >= 0) pOsName = "Minix";
        else if (elf_section_index(pElf, ".note.netbsd.ident") >= 0) pOsName = "NetBSD";
        else if (elf_section_index(pElf, ".note.openbsd.ident") >= 0) pOsName = "OpenBSD";
    }

    if (x_strcmp(pOsName, "Unix") == 0) {
        x_snprintf(sVer, sizeof(sVer), "%u", (unsigned)pElf->nOsAbi);
    }

    cd_free(pInterp);

    elf_set_str(pElf->sOsName, sizeof(pElf->sOsName), pOsName);
    elf_set_str(pElf->sOsVersion, sizeof(pElf->sOsVersion), sVer);
}

/* ELF constants used here. */
#define ELF_SHT_NOBITS 8
#define ELF_PT_LOAD 1
#define ELF_PT_DYNAMIC 2
#define ELF_DT_NULL 0
#define ELF_DT_NEEDED 1
#define ELF_DT_STRTAB 5
#define ELF_DT_STRSZ 10
#define ELF_DT_RUNPATH 29

typedef struct {
    cd_u32 nId;
    const char *pName;
} ElfIdName;

static const ElfIdName g_elfTypes[] = {
    {0, "NONE"}, {1, "REL"}, {2, "EXEC"}, {3, "DYN"}, {4, "CORE"}, {5, "NUM"}, {0xff00, "LOPROC"}, {0xffff, "HIPROC"}
};

static const ElfIdName g_elfMachines[] = {
    {0, "NONE"}, {1, "M32"}, {2, "SPARC"}, {3, "386"}, {4, "68K"}, {5, "88K"}, {6, "486"}, {7, "860"}, {8, "MIPS"},
    {9, "S370"}, {10, "MIPS_RS3_LE"}, {11, "RS6000"}, {15, "PARISC"}, {16, "nCUBE"}, {17, "VPP500"}, {18, "SPARC32PLUS"},
    {19, "960"}, {20, "PPC"}, {21, "PPC64"}, {22, "S390"}, {23, "SPU"}, {36, "V800"}, {37, "FR20"}, {38, "RH32"},
    {39, "RCE"}, {40, "ARM"}, {41, "ALPHA"}, {42, "SH"}, {43, "SPARCV9"}, {44, "TRICORE"}, {45, "ARC"}, {46, "H8_300"},
    {47, "H8_300H"}, {48, "H8S"}, {49, "H8_500"}, {50, "IA_64"}, {51, "MIPS_X"}, {52, "COLDFIRE"}, {53, "68HC12"},
    {54, "MMA"}, {55, "PCP"}, {56, "NCPU"}, {57, "NDR1"}, {58, "STARCORE"}, {59, "ME16"}, {60, "ST100"}, {61, "TINYJ"},
    {62, "AMD64"}, {63, "PDSP"}, {66, "FX66"}, {67, "ST9PLUS"}, {68, "ST7"}, {69, "68HC16"}, {70, "68HC11"}, {71, "68HC08"},
    {72, "68HC05"}, {73, "SVX"}, {74, "ST19"}, {75, "VAX"}, {76, "CRIS"}, {77, "JAVELIN"}, {78, "FIREPATH"}, {79, "ZSP"},
    {80, "MMIX"}, {81, "HUANY"}, {82, "PRISM"}, {83, "AVR"}, {84, "FR30"}, {85, "D10V"}, {86, "D30V"}, {87, "V850"},
    {88, "M32R"}, {89, "MN10300"}, {90, "MN10200"}, {91, "PJ"}, {92, "OPENRISC"}, {93, "ARC_A5"}, {94, "XTENSA"},
    {95, "VIDEOCORE"}, {96, "TMM_GPP"}, {97, "NS32K"}, {98, "TPC"}, {99, "SNP1K"}, {100, "ST200"}, {101, "IP2K"},
    {102, "MAX"}, {103, "CR"}, {104, "F2MC16"}, {105, "MSP430"}, {106, "BLACKFIN"}, {107, "SE_C33"}, {108, "SEP"},
    {109, "ARCA"}, {110, "UNICORE"}, {111, "EXCESS"}, {112, "DXP"}, {113, "ALTERA_NIOS2"}, {114, "CRX"}, {115, "XGATE"},
    {116, "C166"}, {117, "M16C"}, {118, "DSPIC30F"}, {119, "CE"}, {120, "M32C"}, {140, "TI_C6000"}, {183, "AARCH64"},
    {243, "RISC_V"}, {258, "LOONGARCH"}, {0x5441, "FRV"}, {0x18ad, "AVR32"}, {0x9026, "ALPHA"}, {0x9080, "CYGNUS_V850"},
    {0x9041, "CYGNUS_M32R"}, {0xA390, "S390_OLD"}, {0xbeef, "CYGNUS_MN10300"}
};

static const char *elf_lookup(const ElfIdName *pTable, size_t nCount, cd_u32 nId)
{
    size_t i = 0;

    for (i = 0; i < nCount; i++) {
        if (pTable[i].nId == nId) {
            return pTable[i].pName;
        }
    }

    return "";
}

/* Walks the PT_LOAD segments to turn a virtual address into a file offset;
 * returns -1 when no segment covers it. */
static cd_i64 elf_addr_to_offset(XELF *pElf, cd_u64 nAddress)
{
    int i = 0;

    for (i = 0; i < pElf->nProgramCount; i++) {
        XElfProgram *pProgram = &pElf->pPrograms[i];

        if ((pProgram->nType == ELF_PT_LOAD) && (pProgram->nFileSize > 0)) {
            if ((nAddress >= pProgram->nVaddr) && (nAddress < pProgram->nVaddr + pProgram->nFileSize)) {
                return (cd_i64)(pProgram->nOffset + (nAddress - pProgram->nVaddr));
            }
        }
    }

    return -1;
}

/* Reads a NUL-terminated string at nOffset, bounded by the file. */
static char *elf_read_asciiz(XBFile *pFile, cd_i64 nOffset)
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

static void elf_parse_dynamic(XBFile *pFile, XELF *pElf)
{
    int i = 0;
    cd_i64 nDynOffset = -1;
    cd_i64 nDynSize = 0;
    cd_i64 nStrTabOffset = -1;
    cd_i64 nStrTabSize = 0;
    cd_i64 nRunPathValue = -1;
    cd_i64 nStep = pElf->bIs64 ? 16 : 8;

    for (i = 0; i < pElf->nProgramCount; i++) {
        if (pElf->pPrograms[i].nType == ELF_PT_DYNAMIC) {
            nDynOffset = (cd_i64)pElf->pPrograms[i].nOffset;
            nDynSize = (cd_i64)pElf->pPrograms[i].nFileSize;

            break;
        }
    }

    if ((nDynOffset < 0) || (nDynOffset > pFile->nSize)) {
        return;
    }

    /* First pass: locate the string table and its size. */
    {
        cd_i64 nOffset = nDynOffset;
        cd_i64 nRemaining = nDynSize;

        while (nRemaining >= nStep) {
            cd_i64 nTag = 0;
            cd_u64 nValue = 0;

            if (pElf->bIs64) {
                nTag = (cd_i64)xb_u64(pFile, nOffset, pElf->bBigEndian);
                nValue = xb_u64(pFile, nOffset + 8, pElf->bBigEndian);
            } else {
                nTag = (cd_i64)xb_u32(pFile, nOffset, pElf->bBigEndian);
                nValue = xb_u32(pFile, nOffset + 4, pElf->bBigEndian);
            }

            if (nTag == ELF_DT_NULL) {
                break;
            }

            if (nTag == ELF_DT_STRTAB) {
                nStrTabOffset = elf_addr_to_offset(pElf, nValue);
            } else if (nTag == ELF_DT_STRSZ) {
                nStrTabSize = (cd_i64)nValue;
            }

            nOffset += nStep;
            nRemaining -= nStep;
        }
    }

    if ((nStrTabOffset < 0) || (nStrTabOffset >= pFile->nSize) || (nStrTabSize <= 0)) {
        return;
    }

    /* Second pass: collect DT_NEEDED names (and DT_RUNPATH). */
    {
        cd_i64 nOffset = nDynOffset;
        cd_i64 nRemaining = nDynSize;

        while (nRemaining >= nStep) {
            cd_i64 nTag = 0;
            cd_u64 nValue = 0;

            if (pElf->bIs64) {
                nTag = (cd_i64)xb_u64(pFile, nOffset, pElf->bBigEndian);
                nValue = xb_u64(pFile, nOffset + 8, pElf->bBigEndian);
            } else {
                nTag = (cd_i64)xb_u32(pFile, nOffset, pElf->bBigEndian);
                nValue = xb_u32(pFile, nOffset + 4, pElf->bBigEndian);
            }

            if (nTag == ELF_DT_NULL) {
                break;
            }

            if ((nTag == ELF_DT_NEEDED) && ((cd_i64)nValue >= 0) && ((cd_i64)nValue < nStrTabSize)) {
                cdvec_push(&pElf->vecLibraries, elf_read_asciiz(pFile, nStrTabOffset + (cd_i64)nValue));
            } else if (nTag == ELF_DT_RUNPATH) {
                nRunPathValue = (cd_i64)nValue;
            }

            nOffset += nStep;
            nRemaining -= nStep;
        }
    }

    if ((nRunPathValue >= 0) && (nRunPathValue < nStrTabSize)) {
        pElf->pRunPath = elf_read_asciiz(pFile, nStrTabOffset + nRunPathValue);
    }
}

/* Raw size = the highest file offset any header or section reaches; anything
 * past it is overlay. */
static void elf_compute_overlay(XELF *pElf, cd_i64 nFileSize)
{
    cd_i64 nRawSize = pElf->nEhsize;
    int i = 0;

    if ((cd_i64)(pElf->nShoff + (cd_u64)pElf->nShnum * pElf->nShentsize) > nRawSize) {
        nRawSize = (cd_i64)(pElf->nShoff + (cd_u64)pElf->nShnum * pElf->nShentsize);
    }

    if ((cd_i64)(pElf->nPhoff + (cd_u64)pElf->nPhnum * pElf->nPhentsize) > nRawSize) {
        nRawSize = (cd_i64)(pElf->nPhoff + (cd_u64)pElf->nPhnum * pElf->nPhentsize);
    }

    for (i = 0; i < pElf->nSectionCount; i++) {
        if (pElf->pSections[i].nType != ELF_SHT_NOBITS) {
            cd_i64 nEnd = (cd_i64)(pElf->pSections[i].nOffset + pElf->pSections[i].nSize);

            if (nEnd > nRawSize) {
                nRawSize = nEnd;
            }
        }
    }

    for (i = 0; i < pElf->nProgramCount; i++) {
        cd_i64 nEnd = (cd_i64)(pElf->pPrograms[i].nOffset + pElf->pPrograms[i].nFileSize);

        if (nEnd > nRawSize) {
            nRawSize = nEnd;
        }
    }

    if ((nRawSize > 0) && (nRawSize < nFileSize)) {
        pElf->nOverlayOffset = nRawSize;
        pElf->nOverlaySize = nFileSize - nRawSize;
    } else {
        pElf->nOverlayOffset = 0;
        pElf->nOverlaySize = 0;
    }
}

int xelf_parse(XBFile *pFile, XELF *pElf)
{
    int bBE = 0;
    int b64 = 0;
    int i = 0;
    cd_i64 nShStrTabOffset = -1;

    x_memset(pElf, 0, sizeof(XELF));
    cdvec_init(&pElf->vecLibraries);
    pElf->nEntryPointOffset = -1;

    if ((pFile == NULL) || (pFile->nSize < 0x40)) {
        return 0;
    }

    /* e_ident: 0x7F 'E' 'L' 'F' */
    if ((xb_u8(pFile, 0) != 0x7F) || (xb_u8(pFile, 1) != 'E') || (xb_u8(pFile, 2) != 'L') || (xb_u8(pFile, 3) != 'F')) {
        return 0;
    }

    b64 = (xb_u8(pFile, 4) == 2) ? 1 : 0;
    bBE = (xb_u8(pFile, 5) == 2) ? 1 : 0;
    pElf->bIs64 = b64;
    pElf->bBigEndian = bBE;

    pElf->nType = xb_u16(pFile, 16, bBE);
    pElf->nMachine = xb_u16(pFile, 18, bBE);
    pElf->nOsAbi = xb_u8(pFile, 7); /* e_ident[EI_OSABI] */
    pElf->nVersion = xb_u32(pFile, 20, bBE);

    if (b64) {
        pElf->nEntry = xb_u64(pFile, 24, bBE);
        pElf->nPhoff = xb_u64(pFile, 32, bBE);
        pElf->nShoff = xb_u64(pFile, 40, bBE);
        pElf->nFlags = xb_u32(pFile, 48, bBE);
        pElf->nEhsize = xb_u16(pFile, 52, bBE);
        pElf->nPhentsize = xb_u16(pFile, 54, bBE);
        pElf->nPhnum = xb_u16(pFile, 56, bBE);
        pElf->nShentsize = xb_u16(pFile, 58, bBE);
        pElf->nShnum = xb_u16(pFile, 60, bBE);
        pElf->nShstrndx = xb_u16(pFile, 62, bBE);
    } else {
        pElf->nEntry = xb_u32(pFile, 24, bBE);
        pElf->nPhoff = xb_u32(pFile, 28, bBE);
        pElf->nShoff = xb_u32(pFile, 32, bBE);
        pElf->nFlags = xb_u32(pFile, 36, bBE);
        pElf->nEhsize = xb_u16(pFile, 40, bBE);
        pElf->nPhentsize = xb_u16(pFile, 42, bBE);
        pElf->nPhnum = xb_u16(pFile, 44, bBE);
        pElf->nShentsize = xb_u16(pFile, 46, bBE);
        pElf->nShnum = xb_u16(pFile, 48, bBE);
        pElf->nShstrndx = xb_u16(pFile, 50, bBE);
    }

    /* Program headers (nPhnum is a u16, so it is already <= 0xFFFF, and every
     * entry is bounds-checked before it is read). */
    if (pElf->nPhnum > 0) {
        pElf->pPrograms = (XElfProgram *)cd_calloc((size_t)pElf->nPhnum, sizeof(XElfProgram));

        for (i = 0; i < pElf->nPhnum; i++) {
            cd_i64 nBase = (cd_i64)pElf->nPhoff + (cd_i64)i * pElf->nPhentsize;

            if (nBase + pElf->nPhentsize > pFile->nSize) {
                break;
            }

            pElf->pPrograms[i].nType = xb_u32(pFile, nBase, bBE);

            if (b64) {
                pElf->pPrograms[i].nOffset = xb_u64(pFile, nBase + 8, bBE);
                pElf->pPrograms[i].nVaddr = xb_u64(pFile, nBase + 16, bBE);
                pElf->pPrograms[i].nFileSize = xb_u64(pFile, nBase + 32, bBE);
            } else {
                pElf->pPrograms[i].nOffset = xb_u32(pFile, nBase + 4, bBE);
                pElf->pPrograms[i].nVaddr = xb_u32(pFile, nBase + 8, bBE);
                pElf->pPrograms[i].nFileSize = xb_u32(pFile, nBase + 16, bBE);
            }

            pElf->nProgramCount++;
        }
    }

    /* Section headers (nShnum is u16, so it is already <= 0xFFFF). */
    if (pElf->nShnum > 0) {
        pElf->pSections = (XElfSection *)cd_calloc((size_t)pElf->nShnum, sizeof(XElfSection));

        for (i = 0; i < pElf->nShnum; i++) {
            cd_i64 nBase = (cd_i64)pElf->nShoff + (cd_i64)i * pElf->nShentsize;

            if (nBase + pElf->nShentsize > pFile->nSize) {
                break;
            }

            pElf->pSections[i].nNameIndex = xb_u32(pFile, nBase, bBE);
            pElf->pSections[i].nType = xb_u32(pFile, nBase + 4, bBE);

            if (b64) {
                pElf->pSections[i].nAddr = xb_u64(pFile, nBase + 16, bBE);
                pElf->pSections[i].nOffset = xb_u64(pFile, nBase + 24, bBE);
                pElf->pSections[i].nSize = xb_u64(pFile, nBase + 32, bBE);
            } else {
                pElf->pSections[i].nAddr = xb_u32(pFile, nBase + 12, bBE);
                pElf->pSections[i].nOffset = xb_u32(pFile, nBase + 16, bBE);
                pElf->pSections[i].nSize = xb_u32(pFile, nBase + 20, bBE);
            }

            pElf->pSections[i].pName = NULL;
            pElf->nSectionCount++;
        }
    }

    /* Resolve section names via the .shstrtab section. */
    if ((pElf->nShstrndx < pElf->nSectionCount)) {
        nShStrTabOffset = (cd_i64)pElf->pSections[pElf->nShstrndx].nOffset;
    }

    for (i = 0; i < pElf->nSectionCount; i++) {
        if (nShStrTabOffset >= 0) {
            pElf->pSections[i].pName = elf_read_asciiz(pFile, nShStrTabOffset + (cd_i64)pElf->pSections[i].nNameIndex);
        } else {
            pElf->pSections[i].pName = cd_strdup("");
        }
    }

    elf_parse_dynamic(pFile, pElf);

    /* General options: "TYPE MACHINE-BITS", e.g. "DYN AMD64-64". */
    x_snprintf(pElf->sGeneralOptions, sizeof(pElf->sGeneralOptions), "%s %s-%s",
               elf_lookup(g_elfTypes, sizeof(g_elfTypes) / sizeof(g_elfTypes[0]), pElf->nType),
               elf_lookup(g_elfMachines, sizeof(g_elfMachines) / sizeof(g_elfMachines[0]), pElf->nMachine),
               b64 ? "64" : "32");

    pElf->nEntryPointOffset = elf_addr_to_offset(pElf, pElf->nEntry);
    elf_compute_overlay(pElf, pFile->nSize);

    elf_compute_os(pFile, pElf);

    pElf->bValid = 1;

    return 1;
}

void xelf_free(XELF *pElf)
{
    int i = 0;
    size_t j = 0;

    for (i = 0; i < pElf->nSectionCount; i++) {
        cd_free(pElf->pSections[i].pName);
    }

    cd_free(pElf->pSections);
    cd_free(pElf->pPrograms);

    for (j = 0; j < pElf->vecLibraries.nSize; j++) {
        cd_free(pElf->vecLibraries.ppData[j]);
    }

    cdvec_free(&pElf->vecLibraries);
    cd_free(pElf->pRunPath);
    pElf->bValid = 0;
}

int xelf_section_number(XELF *pElf, const char *pName)
{
    int i = 0;

    for (i = 0; i < pElf->nSectionCount; i++) {
        if (pElf->pSections[i].pName && (x_strcmp(pElf->pSections[i].pName, pName) == 0)) {
            return i;
        }
    }

    return -1;
}

int xelf_section_present(XELF *pElf, const char *pName)
{
    return (xelf_section_number(pElf, pName) != -1) ? 1 : 0;
}

cd_u64 xelf_section_offset(XELF *pElf, int nNumber)
{
    if ((nNumber < 0) || (nNumber >= pElf->nSectionCount)) {
        return 0;
    }

    return pElf->pSections[nNumber].nOffset;
}

cd_u64 xelf_section_size(XELF *pElf, int nNumber)
{
    if ((nNumber < 0) || (nNumber >= pElf->nSectionCount)) {
        return 0;
    }

    return pElf->pSections[nNumber].nSize;
}

cd_u64 xelf_program_offset(XELF *pElf, int nNumber)
{
    if ((nNumber < 0) || (nNumber >= pElf->nProgramCount)) {
        return 0;
    }

    return pElf->pPrograms[nNumber].nOffset;
}

cd_u64 xelf_program_size(XELF *pElf, int nNumber)
{
    if ((nNumber < 0) || (nNumber >= pElf->nProgramCount)) {
        return 0;
    }

    return pElf->pPrograms[nNumber].nFileSize;
}

int xelf_library_present(XELF *pElf, const char *pName)
{
    size_t i = 0;

    for (i = 0; i < pElf->vecLibraries.nSize; i++) {
        if (x_strcmp((const char *)pElf->vecLibraries.ppData[i], pName) == 0) {
            return 1;
        }
    }

    return 0;
}

int xelf_string_in_table_present(XBFile *pFile, XELF *pElf, const char *pSectionName, const char *pString)
{
    int nSection = xelf_section_number(pElf, pSectionName);
    cd_i64 nOffset = 0;
    cd_i64 nEnd = 0;
    size_t nQueryLen = 0;

    if (nSection < 0) {
        return 0;
    }

    nOffset = (cd_i64)pElf->pSections[nSection].nOffset;
    nQueryLen = x_strlen(pString);

    /* Range-checked in cd_u64: adding the two raw header fields as cd_i64
     * overflows on a malformed section header. */
    if ((pElf->pSections[nSection].nOffset > (cd_u64)pFile->nSize) ||
        (pElf->pSections[nSection].nSize > (cd_u64)pFile->nSize - pElf->pSections[nSection].nOffset)) {
        return 0;
    }

    nEnd = nOffset + (cd_i64)pElf->pSections[nSection].nSize;

    /* Walk the NUL-delimited entries; the match must be a whole entry. */
    while (nOffset < nEnd) {
        cd_i64 nStart = nOffset;

        while ((nOffset < nEnd) && (pFile->pData[nOffset] != 0)) {
            nOffset++;
        }

        if (((size_t)(nOffset - nStart) == nQueryLen) && (x_memcmp(pFile->pData + nStart, pString, nQueryLen) == 0)) {
            return 1;
        }

        nOffset++; /* skip the NUL */
    }

    return 0;
}
