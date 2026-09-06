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

/* xpe.h - Portable Executable parser. */

#ifndef XPE_H
#define XPE_H

#include "xb.h"

#define XPE_DIR_EXPORT 0
#define XPE_DIR_IMPORT 1
#define XPE_DIR_RESOURCE 2
#define XPE_DIR_EXCEPTION 3
#define XPE_DIR_SECURITY 4
#define XPE_DIR_BASERELOC 5
#define XPE_DIR_DEBUG 6
#define XPE_DIR_TLS 9
#define XPE_DIR_LOADCONFIG 10
#define XPE_DIR_IAT 12
#define XPE_DIR_DELAYIMPORT 13
#define XPE_DIR_COMHEADER 14

/* Import-walk budgets, mirroring XPE::getImports. */
#define XPE_MAX_POSITIONS_PER_LIBRARY 16384
#define XPE_MAX_IMPORT_POSITIONS 65536

typedef struct {
    char sName[16];
    cd_u32 nVirtualSize;
    cd_u32 nVirtualAddress;
    cd_u32 nSizeOfRawData;
    cd_u32 nPointerToRawData;
    cd_u32 nCharacteristics;
} XPESection;

typedef struct {
    char *pName;
    char **ppFunctions;
    int nFunctionCount;
    cd_u32 nPositionHash;
} XPEImport;

typedef struct {
    cd_u32 nTypeId;
    char *pTypeName;
    cd_u32 nNameId;
    char *pName;
    cd_u32 nLangId;
    cd_i64 nOffset;
    cd_i64 nSize;
} XPEResource;

typedef struct {
    cd_u32 nType;
    cd_i64 nOffset;
    cd_i64 nSize;
} XPEDebugRecord;

typedef struct {
    cd_u16 nId;
    cd_u16 nVersion;
    cd_u32 nCount;
} XPERichRecord;

typedef struct {
    char *pKey;   /* e.g. "FileVersion" */
    char *pValue; /* e.g. "3.13.3.0"    */
} XPEVersionRecord;

/* ------------------------------------------------------- .NET metadata --- */

/* Metadata table indices (ECMA-335 II.22). */
#define MDT_Module        0x00
#define MDT_TypeRef       0x01
#define MDT_TypeDef       0x02
#define MDT_Field         0x04
#define MDT_MethodPtr     0x05
#define MDT_MethodDef     0x06
#define MDT_ParamPtr      0x07
#define MDT_Param         0x08
#define MDT_InterfaceImpl 0x09
#define MDT_MemberRef     0x0A
#define MDT_Constant      0x0B
#define MDT_CustomAttribute 0x0C
#define MDT_FieldMarshal  0x0D
#define MDT_DeclSecurity  0x0E
#define MDT_ClassLayout   0x0F
#define MDT_FieldLayout   0x10
#define MDT_StandAloneSig 0x11
#define MDT_EventMap      0x12
#define MDT_EventPtr      0x13
#define MDT_Event         0x14
#define MDT_PropertyMap   0x15
#define MDT_PropertyPtr   0x16
#define MDT_Property      0x17
#define MDT_MethodSemantics 0x18
#define MDT_MethodImpl    0x19
#define MDT_ModuleRef     0x1A
#define MDT_TypeSpec      0x1B
#define MDT_ImplMap       0x1C
#define MDT_FieldRVA      0x1D
#define MDT_ENCLog        0x1E
#define MDT_ENCMap        0x1F
#define MDT_Assembly      0x20
#define MDT_AssemblyProcessor 0x21
#define MDT_AssemblyOS    0x22
#define MDT_AssemblyRef   0x23
#define MDT_AssemblyRefProcessor 0x24
#define MDT_AssemblyRefOS 0x25
#define MDT_File          0x26
#define MDT_ExportedType  0x27
#define MDT_ManifestResource 0x28
#define MDT_NestedClass   0x29
#define MDT_GenericParam  0x2A
#define MDT_MethodSpec    0x2B
#define MDT_GenericParamConstraint 0x2C

typedef struct {
    int bValid;

    cd_i64 nMetaOffset;   /* metadata root                     */
    cd_i64 nTablesOffset; /* "#~" / "#-" stream                */
    cd_i64 nTablesSize;
    cd_i64 nStringsOffset;
    cd_i64 nStringsSize;
    cd_i64 nBlobOffset;
    cd_i64 nBlobSize;
    cd_i64 nGuidOffset;
    cd_i64 nGuidSize;
    cd_i64 nUSOffset;
    cd_i64 nUSSize;

    cd_u32 nEntryPointRVA;

    cd_u32 pRows[64];         /* row counts                        */
    int pElementSize[64];     /* bytes per row                     */
    cd_i64 pTableOffset[64];  /* absolute file offset of each table */
    int pIndexSize[64];       /* 2 or 4 for a simple table index   */

    int nStringIndexSize;
    int nGuidIndexSize;
    int nBlobIndexSize;
    int nResolutionScopeSize;
    int nTypeDefOrRefSize;
    int nMemberRefParentSize;
    int nHasConstantSize;
    int nHasCustomAttributeSize;
    int nCustomAttributeTypeSize;
    int nHasFieldMarshalSize;
    int nHasDeclSecuritySize;
    int nHasSemanticsSize;
    int nMethodDefOrRefSize;
    int nMemberForwardedSize;
} XPECli;

typedef struct {
    XBFile *pFile;
    XBMemoryMap map;

    int bValid;
    int bIs64;
    cd_i64 nLfanew;

    /* IMAGE_FILE_HEADER */
    cd_u16 nMachine;
    cd_u16 nNumberOfSections;
    cd_u32 nTimeDateStamp;
    cd_u32 nPointerToSymbolTable;
    cd_u32 nNumberOfSymbols;
    cd_u16 nSizeOfOptionalHeader;
    cd_u16 nCharacteristics;

    /* IMAGE_OPTIONAL_HEADER */
    cd_u16 nMagic;
    cd_u8 nMajorLinkerVersion;
    cd_u8 nMinorLinkerVersion;
    cd_u32 nSizeOfCode;
    cd_u32 nSizeOfInitializedData;
    cd_u32 nSizeOfUninitializedData;
    cd_u32 nAddressOfEntryPoint;
    cd_u32 nBaseOfCode;
    cd_u32 nBaseOfData;
    cd_u64 nImageBase;
    cd_u32 nSectionAlignment;
    cd_u32 nFileAlignment;
    cd_u16 nMajorOperatingSystemVersion;
    cd_u16 nMinorOperatingSystemVersion;
    cd_u16 nMajorImageVersion;
    cd_u16 nMinorImageVersion;
    cd_u16 nMajorSubsystemVersion;
    cd_u16 nMinorSubsystemVersion;
    cd_u32 nWin32VersionValue;
    cd_u32 nSizeOfImage;
    cd_u32 nSizeOfHeaders;
    cd_u32 nCheckSum;
    cd_u16 nSubsystem;
    cd_u16 nDllCharacteristics;
    cd_u64 nSizeOfStackReserve;
    cd_u64 nSizeOfStackCommit;
    cd_u64 nSizeOfHeapReserve;
    cd_u64 nSizeOfHeapCommit;
    cd_u32 nLoaderFlags;
    cd_u32 nNumberOfRvaAndSizes;

    cd_u32 pDirRVA[16];
    cd_u32 pDirSize[16];

    XPESection *pSections;
    int nSectionCount;

    XPEImport *pImports;
    int nImportCount;

    char **ppExportFunctions;
    int nExportCount;

    XPEResource *pResources;
    int nResourceCount;

    XPEDebugRecord *pDebugRecords;
    int nDebugCount;

    XPERichRecord *pRichRecords;
    int nRichCount;
    int bRichPresent;

    XPEVersionRecord *pVersionRecords;
    int nVersionCount;
    cd_u32 nFileVersionMS;
    cd_u32 nFileVersionLS;

    char *pManifest;

    /* .NET */
    int bIsNet;
    char *pNetVersion;
    char **ppNetAnsiStrings;
    int nNetAnsiCount;
    char **ppNetUnicodeStrings;
    int nNetUnicodeCount;
    XPECli cli;

    cd_i64 nEntryPointOffset;
    cd_u64 nEntryPointAddress;
    cd_i64 nOverlayOffset;
    cd_i64 nOverlaySize;

    cd_u32 nImportHash32;
    cd_u64 nImportHash64;
} XPE;

int xpe_parse(XPE *pPE, XBFile *pFile);
void xpe_free(XPE *pPE);

const char *xpe_debug_type_name(cd_u32 nType);
int xpe_section_number_by_rva(XPE *pPE, cd_u32 nRVA);
cd_i64 xpe_rva_to_offset(XPE *pPE, cd_u32 nRVA);

/* .NET metadata queries. All return neutral values when the file has no
 * usable CLI metadata.                                                     */
int xpe_net_type_present(XPE *pPE, const char *pNamespace, const char *pTypeName);
int xpe_net_method_present(XPE *pPE, const char *pNamespace, const char *pTypeName, const char *pMethodName);
int xpe_net_field_present(XPE *pPE, const char *pNamespace, const char *pTypeName, const char *pFieldName);
int xpe_net_global_cctor_present(XPE *pPE);
/* Both return a newly allocated string (possibly empty). */
char *xpe_net_module_name(XPE *pPE);
char *xpe_net_assembly_name(XPE *pPE);

#endif /* XPE_H */
