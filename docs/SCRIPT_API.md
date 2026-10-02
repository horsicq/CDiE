# Script API reference

Everything a signature script can call. The format object is reachable as
`Binary` and — for PE inputs — also as `PE` and `MSDOS`. The database
framework files additionally alias it as `File` and `X`.

Default argument values match the reference engine (moc generates one
overload per default, so `PE.getString(off)` really does read at most 50
bytes).

---

## Global helpers

| Function | Description |
| --- | --- |
| `_setResult(type, name, version, options)` | append a detection record |
| `_isResultPresent(type, name)` | `true` if such a record exists; empty `name` matches any |
| `_getNumberOfResults(type)` | number of records of a type; empty `type` counts all |
| `_removeResult(type, name)` | remove a record and blacklist it for the rest of the scan |
| `includeScript(name)` | evaluate a database-root script in the current global scope |
| `_log(text)` | print a message (visible with `-m`) |
| `_isStop()` | always `false` in the console port |
| `_breakScan()` | stop the scan after the current script |
| `_encodingList()` | no-op |
| `_isConsoleMode()` | `true` |
| `_isGuiMode()`, `_isLiteMode()`, `_isLibraryMode()` | `false` |
| `_getEngineVersion()`, `_getQtVersion()` | version string |
| `_getOS()` | `"win32"`, `"macos"` or `"linux"` |

The framework file `db/_init` builds `meta()`, `result()`, `init()` and the
`bDetected` / `sType` / `sName` / `sVersion` / `sOptions` / `sLang`
convention on top of `_setResult`. `db/language` adds `_setLang`,
`_isLangPresent` and `_isLangDetected`. `db/_debug` adds `_debug` and
`_error`.

---

## Reading data

| Function | Returns |
| --- | --- |
| `getSize()` / `Sz()` | file size |
| `readByte(off)` / `read_uint8(off)` / `U8(off)` | unsigned 8 bit |
| `readSByte(off)` / `read_int8(off)` / `I8(off)` | signed 8 bit |
| `readWord(off)` / `read_uint16(off[, be])` / `U16` | unsigned 16 bit |
| `readSWord(off)` / `read_int16(off[, be])` / `I16` | signed 16 bit |
| `read_uint24(off[, be])` / `U24`, `read_int24` / `I24` | 24 bit |
| `readDword(off)` / `read_uint32(off[, be])` / `U32` | unsigned 32 bit |
| `readSDword(off)` / `read_int32(off[, be])` / `I32` | signed 32 bit |
| `readQword(off)` / `read_uint64(off[, be])` / `U64` | unsigned 64 bit |
| `readSQword(off)` / `read_int64(off[, be])` / `I64` | signed 64 bit |
| `read_float(off[, be])` / `F32`, `read_double(off[, be])` / `F64` | floating point |
| `read_float16(off[, be])` / `F16` | IEEE-754 half → float |
| `getString(off[, max=50])` / `read_ansiString` / `SA` | Latin-1 string |
| `read_unicodeString(off[, max=50])` / `SU16` | UTF-16 → UTF-8 |
| `read_utf8String(off[, max=50])` / `SU8` | UTF-8 string |
| `read_codePageString(off[, max=256][, cp])` / `SC` | treated as Latin-1 |
| `read_ucsdString(off)` / `UCSD` | UCSD/Pascal string (1-byte length prefix; embedded NULs shown as spaces) |
| `read_UUID(off)` | 16-byte GUID as `aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee` |
| `readBytes(off, size[, replaceZero])` / `BA` | array of byte values |
| `getSignature(off, size)` | uppercase hex string |
| `getHeaderString()` | `""` (text-file support is not needed for binaries) |

## Searching and comparing

| Function | Description |
| --- | --- |
| `compare(sig[, off=0])` / `c(...)` | compare a signature at an offset |
| `compareEP(sig[, off=0])` | compare relative to the entry point |
| `compareOverlay(sig[, off=0])` | compare relative to the overlay start |
| `findSignature(off, size, sig)` / `fSig` | offset of the first match, or `-1` |
| `findString(off, size, str)` / `fStr` / `find_ansiString` | ANSI substring search |
| `find_unicodeString(off, size, str)`, `find_utf8String(...)` | wide / UTF-8 search |
| `findByte`, `findWord`, `findDword` `(off, size, value)` | numeric search |
| `isSignaturePresent(off, size, sig)` | `findSignature(...) != -1` |
| `isSignatureInSectionPresent(n, sig)` | search inside section *n* |

## Addresses and layout

| Function | Description |
| --- | --- |
| `getEntryPointOffset()`, `getAddressOfEntryPoint()` | entry point |
| `getOverlayOffset()`, `getOverlaySize()`, `isOverlayPresent()` | overlay |
| `RVAToOffset(rva)`, `VAToOffset(va)`, `OffsetToVA(off)`, `OffsetToRVA(off)` | translation |
| `getImageBase()` | module base address |
| `is16()`, `is32()`, `is64()` | word size |
| `calculateSizeOfHeaders()` | aligned header size |

## Hashes and statistics

`calculateEntropy(off, size)`, `isZeroFilled(off, size)`,
`calculateMD5(off, size)`, `calculateCRC32(off, size)`,
`crc32(off, size[, init])`, `crc16(off, size[, init])`,
`adler32(off, size)`.

## File information

`getFileDirectory()`, `getFileBaseName()`, `getFileCompleteSuffix()`,
`getFileSuffix()`, `getFileFormatName()`, `getFileFormatVersion()`,
`getFileFormatOptions()`, `getOperationSystemName()`,
`getOperationSystemVersion()`, `getOperationSystemOptions()`.

| Function | Notes |
| --- | --- |
| `getFileFormatVersion()` | the JFIF version for JPEG, `""` otherwise |
| `getFileFormatOptions()` | the PNG description string (`723x464, 8 bits, RGB, …`), `""` otherwise |
| `getOperationSystemVersion()` | the mapped Windows name — `Vista`, `7`, `10` — never a bare number; 64-bit images are floored at `Server 2003` |
| `getOperationSystemOptions()` | `"<arch>, <mode>"`, e.g. `AMD64, 64-bit` |

## Text detection

| Function | Notes |
| --- | --- |
| `isPlainText()` | implements `XBinary::isPlainTextType`: rejects any NUL and any UTF-8/UTF-16 BOM, then requires ≥85% printable, ≤5% control and ≤50% extended over the first 32 KB |
| `isText()` | equals `isPlainText()` here — the reference ORs in the two below |
| `isUTF8Text()`, `isUnicodeText()` | always `false`; see [LIMITATIONS.md](LIMITATIONS.md) |
| `getHeaderString()` | `""` |

## APK

Available on `APK` inputs; the manifest is decompressed and decoded on open.

| Function | Notes |
| --- | --- |
| `getAndroidManifestRecord(key)` | value of the first `key="…"` attribute in the decoded AndroidManifest.xml — e.g. `"package"`, `"android:versionName"` |
| `getAndroidManifest()` | the whole decoded manifest as text |

## PDF

Available on `PDF` inputs. Values come from the object dictionaries without
decompressing streams.

| Function | Notes |
| --- | --- |
| `getStringValuesByKey(key)` | array of distinct PDF-string values for a key — `getStringValuesByKey("/Creator")` |
| `getValuesByKey(key)` | as above but every value type, not only strings |
| `getHeaderCommentAsHex()` | the second-line `%…` comment bytes as hex |
| `getFileFormatVersion()` | the `%PDF-x.y` version |
| `getFileFormatOptions()` | the `/Filter` values joined with `, ` |
| `isValuesHexByKey(key)` | always `false` — see [LIMITATIONS.md](LIMITATIONS.md); no stock script uses it |

## ELF

Available on `ELF` inputs.

| Function | Notes |
| --- | --- |
| `getNumberOfSections()`, `getNumberOfPrograms()` | header counts |
| `getSectionNumber(name)`, `isSectionNamePresent(name)` | section lookup by name (resolved via `.shstrtab`) |
| `getSectionFileOffset(n)`, `getSectionFileSize(n)` | section file range |
| `getProgramFileOffset(n)`, `getProgramFileSize(n)` | program-header file range |
| `isLibraryPresent(name)` | a `DT_NEEDED` dynamic library |
| `isStringInTablePresent(section, str)` | exact whole-string match inside a named string-table section |
| `getGeneralOptions()` | `"TYPE MACHINE-BITS"`, e.g. `DYN AMD64-64` |
| `getElfHeader_type/machine/entry/phoff/shoff/phnum/shnum/shentsize/phentsize/shstrndx()` | raw header fields |
| `getRunPath()` | `DT_RUNPATH`, or `""` |
| `compareEP(sig[, off])` | signature compare at the entry point (address mapped through `PT_LOAD`) |
| `isOverlayPresent()`, `getOverlayOffset()`, `getOverlaySize()` | overlay past the last section/header |

## Mach-O

Available on `Mach-O` inputs.

| Function | Notes |
| --- | --- |
| `isLibraryPresent(name)` | an `LC_LOAD_DYLIB` library, matched by basename |
| `getLibraryCurrentVersion(name)` | that library's `current_version` |
| `getNumberOfSections()`, `getSectionNumber(name)`, `isSectionNamePresent(name)` | sections by `sectname` |
| `getSectionFileOffset(n)`, `getSectionFileSize(n)` | section file range |
| `compareEP(sig[, off])` | signature compare at the `LC_MAIN` entry offset |

## DEX

Available on `DEX` inputs.

| Function | Notes |
| --- | --- |
| `getFileFormatVersion()` | the three-digit version from the magic (`035`, `037`, …) |
| `getFileFormatOptions()` | the map-items hash, 8 lowercase hex digits |
| `getMapItemsHash()` | that hash as a number |
| `isDexStringPresent(str)` | exact match in the string pool |
| `isDexItemStringPresent(str)` | exact match among the type descriptors |

## Scan mode flags

`isDeepScan()`, `isHeuristicScan()`, `isAggressiveScan()`,
`isRecursiveScan()`, `isVerbose()`, `isProfiling()`, `isOverlayScan()`,
`isFirstWrapperScan()`, `getScanID()`, `getStartOffset()`,
`isOverlay()`, `isResource()`, `isDebugData()`, `isFilePart()`.

## Utilities

`upperCase(s)`, `lowerCase(s)`, `cleanString(s)`, `swapBytes(v)`,
`bytesCountToString(v)`, `startTiming()`, `endTiming(handle, info)`.

The `Util` object exposes exact 64-bit arithmetic (JS numbers are doubles):
`Util.div64(a, b)` (signed), `Util.divu64(a, b)` (unsigned; both return `-1`
when `b == 0`), `Util.shlu64(v, n)` and `Util.shru64(v, n)` (unsigned shifts,
low 6 bits of `n`).

---

## MSDOS / Rich header

| Function | Description |
| --- | --- |
| `isRichSignaturePresent()` | the DOS stub contains a Rich block |
| `getNumberOfRichIDs()` | number of Rich entries |
| `getRichID(i)`, `getRichVersion(i)`, `getRichCount(i)` | one entry |
| `isRichVersionPresent(v)` | any entry with that build number |
| `getDosStubOffset()`, `getDosStubSize()`, `isDosStubPresent()` | DOS stub |
| `isNE()`, `isLE()`, `isLX()` | the new-exe magic at `e_lfanew` is `NE`/`LE`/`LX` |

`getNEOffset(off)`, `getBaseOffset()` and `addressToOffset(addr)` are supplied
by the `db/MSDOS/_init` framework script on top of `readDword`/`readWord`.

## Archive (ZIP / JAR / APK / NPM / IPA)

The central directory of a ZIP-family container is walked into a record-name
list, and `META-INF/MANIFEST.MF` (STORE or DEFLATE) is decompressed.

| Function | Description |
| --- | --- |
| `isArchiveRecordPresent(name)` | some record name equals `name` exactly |
| `isArchiveRecordPresentExp(re)` | some record name matches the regular expression `re` |
| `getManifestRecord(key)` | value of `key: …` in MANIFEST.MF up to end of line, `\r` removed |
| `getPackageJsonRecord(key)` | top-level `key` in `package/package.json` when it is a string, else `""` |

## ISO 9660

Reads the Primary Volume Descriptor (offset `0x8000`); each field is Latin-1
up to the first NUL, then trimmed.

| Function | Description |
| --- | --- |
| `getApplicationIdentifier()` | PVD Application Identifier (+574) |
| `getDataPreparerIdentifier()` | PVD Data Preparer Identifier (+446) |

## PYC (Python bytecode)

| Function | Description |
| --- | --- |
| `getFileFormatVersion()` | interpreter release from the two-byte magic (e.g. `3.13b1`) |
| `isConstPresent(s)` | a top-level string constant of the module code object equals `s` (reached up to the first nested code object, as in `XPYC::getCodeObject`) |

---

## PE

### Sections

`getNumberOfSections()`, `getSectionName(n)`, `getSectionVirtualSize(n)`,
`getSectionVirtualAddress(n)`, `getSectionFileSize(n)`,
`getSectionFileOffset(n)`, `getSectionCharacteristics(n)`,
`isSectionNamePresent(name)`, `getSectionNumber(name)`,
`getSectionNameCollision(a, b)`, `getEntryPointSection()`,
`getImportSection()`, `getExportSection()`, `getResourceSection()`,
`getRelocsSection()`, `getTLSSection()`.

`PE/_init` builds a `PE.section` array on top of these, indexed by number and
by name:

```js
var rsrc = PE.section[".rsrc"];
if (rsrc && rsrc.Characteristics == 0x42000802) { ... }
```

Each entry has `Number`, `Name`, `VirtualSize`, `VirtualAddress`, `FileSize`,
`FileOffset`, `Characteristics`, `Size` and `Offset`.

### Imports and exports

`getNumberOfImports()`, `getImportLibraryName(n)`,
`getNumberOfImportThunks(n)`, `getImportFunctionName(import, func)`,
`isLibraryPresent(name[, checkCase])`,
`isLibraryFunctionPresent(lib, func)`, `isFunctionPresent(func)`,
`getImportHash32()`, `getImportHash64()`,
`isImportPositionHashPresent(index, hash)` (index `-1` matches any library),
`isImportPresent()`.

`isExportFunctionPresent(name)`, `getNumberOfExportFunctions()` /
`getNumberOfExports()`, `getExportFunctionName(n)` /
`getExportNameByNumber(n)`, `isExportPresent()`.

`PE/_init` adds the regular-expression variants `isLibraryPresentExp`,
`isExportFunctionPresentExp`, `isSectionNamePresentExp` and
`isResourceNamePresentExp`.

### Resources and version info

`getNumberOfResources()`, `getResourceIdByNumber(n)`,
`getResourceNameByNumber(n)`, `getResourceOffsetByNumber(n)`,
`getResourceSizeByNumber(n)`, `getResourceTypeByNumber(n)`,
`getResourceNameOffset(name)`, `isResourceNamePresent(name)`,
`isResourceGroupNamePresent(name)`, `isResourceGroupIdPresent(id)`,
`isResourcesPresent()`, `getManifest()`,
`getVersionStringInfo(key)`, `getFileVersion()`, `getFileVersionMS()`,
`getPEFileVersion(path)`.

`PE/_init` builds `PE.resource[...]` with `Number`, `Id`, `Name`,
`FileOffset`, `FileSize`, `Type`, `Size` and `Offset`.

### Headers

`getMajorLinkerVersion()`, `getMinorLinkerVersion()`,
`getCompilerVersion()`, `getSizeOfCode()`,
`getSizeOfUninitializedData()`, `getGeneralOptions()`, `isDll()`,
`isDriver()`, `isConsole()`, `isPE32()`, `isPEPlus()`, `isSigned()` /
`isSignedFile()`, `isTLSPresent()`,
`getImageFileHeader(field)`, `getImageOptionalHeader(field)`.

`field` is the struct member name, e.g.
`PE.getImageOptionalHeader("DllCharacteristics")`.

### Debug directory

`getNumberOfDebugDataRecords()`, `getDebugDataType(n)`,
`getDebugDataOffset(n)`, `getDebugDataSize(n)`.

`getDebugDataType` returns the DIE spelling: `UNKNOWN`, `COFF`, `CODEVIEW`,
`FPO`, `MISC`, `EXCEPTION`, `FIXUP`, `OMAP_TO_SRC`, `OMAP_FROM_SRC`,
`BORLAND`, `RESERVED10`, `CLSID`, `VC_FEATURE`, `POGO`, `ILTCG`, `MPX`,
`REPRO`, `EX_DLLCHARACTERISTICS`.

### .NET / DOTNET

A .NET PE (one with a non-empty COM-descriptor data directory) is additionally
typed as a **CLI assembly**. Scripts under `db/PE/DOTNET` load as that type and
run **only** for .NET PEs, addressing a **`DOTNET`** object. The same methods
also remain on `PE`, so existing signatures keep working. This mirrors the
reference's XCLIAssembly split and `dotnet_script`.

| Function (on `DOTNET` and `PE`) | Description |
| --- | --- |
| `isNet()` / `isNET()` | the file has a valid CLI header |
| `getNetVersion()` / `getNETVersion()` | the metadata version string, e.g. `v4.0.30319` |
| `isNetObjectPresent(s)` / `isNetStringPresent(s)` / `isNETStringPresent(s)` | search the `#Strings` heap |
| `isNetUStringPresent(s)` / `isNetUnicodeStringPresent(s)` / `isNETUnicodeStringPresent(s)` | search the `#US` heap |
| `isNetTypePresent(ns, name)` | search the `TypeDef` table |
| `isNetMethodPresent(ns, type, method)` | walk that type's `MethodDef` range |
| `isNetFieldPresent(ns, type, field)` | walk that type's `Field` range |
| `isNetGlobalCctorPresent()` | `isNetMethodPresent("", "<Module>", ".cctor")` |
| `getNetModuleName()` | the `Module` table name |
| `getNetAssemblyName()` | the `Assembly` table name |
| `findSignatureInBlob_NET(sig)` | search the `#Blob` heap |
| `isSignatureInBlobPresent_NET(sig)` | as above, as a boolean |
| `compareEP_NET(sig[, off])` | compare at the CLI entry point |

An empty `ns` or `name` argument means "do not compare this component",
matching the reference implementation — that is how
`isNetTypePresent("", "<Module>")` works.

### JPEG

| Function | Description |
| --- | --- |
| `getComment()` | concatenated `COM` segments, CR/LF stripped, capped at 100 chars |
| `getDqtMD5()` | lowercase MD5 over every `DQT` payload |
| `isChunkPresent(id)` | a segment with that marker id exists, e.g. `0xED` for Photoshop |
| `isExifPresent()` | the `APP1` segment carries an EXIF block |
| `getExifCameraName()` | `"Make(Model)"` from EXIF tags `0x10F` / `0x110` |

`getFileFormatVersion()` returns the JFIF version (e.g. `1.1`) for JPEG
inputs.

### Disassembly

`getDisasmLength(address)`, `getDisasmString(address)`,
`getDisasmNextAddress(address)` — backed by xxfclib's `cdisasm` x86 decoder and
Intel formatter. The string includes uppercase operands. The next-address
call advances by the decoded instruction length, even for relative jumps; an
invalid-byte fallback returns zero.

### Format checks

`isChecksumCorrect()`, `isEntryPointCorrect()`,
`isSectionAlignmentCorrect()`, `isFileAlignmentCorrect()`,
`isHeaderCorrect()`, `isRelocsTableCorrect()`, `isImportTableCorrect()`,
`isExportTableCorrect()`, `isResourcesTableCorrect()`,
`isSectionsTableCorrect()`, `getFormatMessages()` — the deep format
validator is not ported; these report "correct".
