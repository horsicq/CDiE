# Limitations and deliberate simplifications

`cdie` reproduces the console behaviour of Detect It Easy for the formats it
parses. Everything below is a conscious scope decision, not an accident, and
each item lists what a script sees instead.

## Formats

| Area | State |
| --- | --- |
| PE32 / PE64 | fully parsed, including the .NET metadata tables |
| MSDOS / Rich header | parsed as part of PE |
| JPEG | fully parsed: segments, JFIF version, COM comment, DQT hash, EXIF camera |
| PNG | chunk walk with IHDR, pHYs and bKGD, so the dimensions/depth/colour-model detail line matches |
| Plain text | `isPlainText()` implements `XBinary::isPlainTextType` — the 32 KB sample, the BOM rejections and the three ratio tests |
| APK | the AndroidManifest.xml is located in the ZIP, DEFLATE-decompressed and decoded from Android binary XML, so `getAndroidManifestRecord(...)` returns the package name and version |
| PDF | objects reduced to token lists (classic `xref` tables, cross-reference-stream deep scan, and incremental-update chains), backing `getStringValuesByKey`, the `/Filter` list, the version and the header comment |
| ELF | header, section table (names via `.shstrtab`), program headers, dynamic libraries (`DT_NEEDED`), the general-options string and the entry-point offset for `compareEP`, backing the compiler/library detections |
| Mach-O | load-command walk for `LC_LOAD_DYLIB` libraries, sections and the `LC_MAIN` entry point |
| DEX | version, the map-items CRC hash (the format detail), and the string / type-descriptor tables behind the obfuscator/protector detections |
| PYC | interpreter release from the magic (`getFileFormatVersion`), and the module code object's top-level string constants for `isConstPresent` (up to the first nested code object, as in `XPYC::getCodeObject`) |
| ISO 9660 | the Primary Volume Descriptor Application / Data Preparer Identifier fields, backing the tool/library detections (CDIMAGE, genisoimage, xorriso, Nero, libburn, ...) |
| ZIP / JAR / APK / NPM / IPA | central-directory record-name list for `isArchiveRecordPresent` / `isArchiveRecordPresentExp`; `META-INF/MANIFEST.MF` decompressed for `getManifestRecord`; and `package/package.json` for `getPackageJsonRecord` (top-level string fields) |
| `Util` 64-bit arithmetic | `div64`, `divu64`, `shlu64`, `shru64` are implemented (the database uses them because JS numbers are doubles) |
| NE, LE/LX, CFBF, RAR, JavaClass, Amiga | **detected** by magic so the file type line is right, but no dedicated parser: their database directories run against the generic `Binary` API (plus `MSDOS.isNE/isLE/isLX` for the NE/LE/LX family) |
| Archive recursion (scanning files inside ZIP/RAR/CAB, or DEX inside an APK) | not implemented — only the manifest members are read, not arbitrary nested files |
| `--verbose` "Operation system" line | reproduced for PE, DEX (`Android(9.0)[Dalvik, 32-bit]`), Mach-O (`macOS(13.0.0)[X86_64, 64-bit]`), NE (`Windows[286, 16SEG]`) and ELF (`Ubuntu Linux(22.04.3, ABI: 3.2.0)[AMD64, 64-bit]`, `Unix[MIPS, 32-bit]` — OSABI/interpreter/`.comment` distro/GNU-note ABI/e_machine arch). The exotic ELF distro/note branches are ported from the reference but validated only on the available samples (Unix, Ubuntu, GNU-note) |
| PDF streams | not decompressed — key/value queries read the plaintext object dictionaries, which is where the database looks; `/Filter` is reported, but stream contents are not searched |
| UTF-8 / UTF-16 text detection | `isUTF8Text()` and `isUnicodeText()` return `false`; `getHeaderString()` returns `""`. `isText()` therefore reports only what `isPlainText()` finds |

The scan driver picks the same preferred file type as the reference engine,
so a PE file is scanned with the `PE` database, an ELF file with `ELF`, and
anything unrecognised with `Binary`.

## PE details

Fully implemented: sections, imports (with per-library position hashes and
both import hashes), export names, the resource tree, `VS_VERSIONINFO`,
the manifest, the debug directory, the Rich header, the security directory
(so `isSigned()` works), overlay computation and RVA/VA translation.

Implemented: the **.NET metadata tables**. `format/xpe.c` computes the heap
index widths, the coded-index widths, every table's row size and each table's
file offset, exactly as `XPE::getCliInfo` does. A .NET PE is additionally typed
as a CLI assembly, so scripts under `db/PE/DOTNET` run for it against a
`DOTNET` object (the reference's XCLIAssembly split); the same methods stay on
`PE` too. On top of that:

| Function | Backed by |
| --- | --- |
| `isNetObjectPresent()`, `isNetUStringPresent()` | the `#Strings` / `#US` heaps |
| `isNetTypePresent()` | the `TypeDef` table |
| `isNetMethodPresent()` | `TypeDef` → `MethodPtr` → `MethodDef` |
| `isNetFieldPresent()` | `TypeDef` → `Field` |
| `isNetGlobalCctorPresent()` | `isNetMethodPresent("", "<Module>", ".cctor")` |
| `getNetModuleName()` | the `Module` table |
| `getNetAssemblyName()` | the `Assembly` table |
| `findSignatureInBlob_NET()`, `isSignatureInBlobPresent_NET()` | the `#Blob` heap |
| `compareEP_NET()` | the CLI header entry point RVA |

The `MethodPtr` indirection (present only in edit-and-continue style
assemblies) follows the reference implementation but has not been exercised
against a real sample.

Not implemented:

* **The deep format validator.** `isChecksumCorrect()`,
  `isEntryPointCorrect()`, `isSectionAlignmentCorrect()`,
  `isFileAlignmentCorrect()`, `isHeaderCorrect()`,
  `isRelocsTableCorrect()`, `isImportTableCorrect()`,
  `isExportTableCorrect()`, `isResourcesTableCorrect()` and
  `isSectionsTableCorrect()` all report "correct";
  `getFormatMessages()` returns an empty array.

* **Decompression.** `detectZLIB()`, `detectGZIP()` and `detectZIP()` return
  `-1`; `decompressBytes()` and `getCompressedDataSize()` are not exposed and
  `getListOfCompressionMethods()` returns an empty array. The one place the
  port does decompress is the APK manifest: `format/inflate.c` is a
  self-contained DEFLATE decoder (RFC 1951) with no zlib dependency. bzip2 and
  LZMA are still out of scope.

## Disassembler

The script API uses xxfclib's `cdisasm` adapter to decode x86 instructions in
16-, 32- and 64-bit modes and format complete Intel instructions with operands.
`getDisasmString()` returns uppercase text. `getDisasmNextAddress()` advances
by the decoded instruction length, including for relative jumps, and returns
zero for an invalid-byte fallback.

The cdisasm decoder does not cover every x86 instruction. Database scripts
that use disassembly, including protector and heuristic rules, need a new
corpus comparison after the decoder migration.

## JavaScript

See [JS_ENGINE.md](JS_ENGINE.md) for the supported grammar. The two
observable deviations:

* **Byte strings.** `.length`, `charAt` and index access count bytes rather
  than UTF-16 code units for non-ASCII text.
* **No incremental garbage collection.** Objects live until the context is
  destroyed at the end of the file scan. A single file's scripts allocate on
  the order of a few megabytes; a directory scan creates one context per
  file, so memory does not accumulate across files.

Missing language features (`with`, destructuring, arrow functions, classes,
generators, `Symbol`, `Proxy`, typed arrays, `Date`) do not appear anywhere in
the stock database — all 2098 signature scripts parse.

## Runtime

The Windows build calls Win32 only and links no C runtime (see
[ARCHITECTURE.md](ARCHITECTURE.md#the-runtime-layer)). Three consequences are
worth stating outright:

* **`CDIE_NO_CRT` defaults to ON only for 64-bit MSVC.** A 32-bit build needs
  the compiler's 64-bit arithmetic helpers (`_alldiv`, `_allshr` and
  friends), which live in `libcmt` and have no Win32 equivalent, so it links
  the CRT as usual. The option can be turned off explicitly on any target.

* **Transcendental functions are accurate to roughly 1 ULP, not correctly
  rounded.** `utils_math.c` carries the full-length fdlibm minimax
  polynomials, but evaluates them in the plain nested form rather than with
  fdlibm's hi/lo error compensation. `x_exp(1.0)` returns
  `2.7182818284590455` where a correctly rounded implementation gives
  `2.718281828459045`.

  `x_sqrt` is the exception and *is* correctly rounded, because it comes from
  the hardware. So are the text conversions — `x_strtod` and the three
  `x_dtoa_*` functions — which is the property the version-number comparisons
  in the database actually depend on. No stock signature calls `Math.exp`,
  `Math.log`, `Math.sin`, `Math.cos` or `Math.tan`.

  If you extend these, keep the whole coefficient list. Dropping the last
  term of `__kernel_sin` looks harmless — it contributes `x·z⁶·1.6e-10` — but
  costs ~180 ULP at `x = 0.5`, which is 200× worse than the polynomial the
  coefficients were fitted for.

* **No `%f` or `%g` in `x_snprintf`.** The formatter is integer-only by
  design; floating-point text goes through `x_dtoa_shortest`,
  `x_dtoa_fixed` and `x_dtoa_precision`. New code that wants to print a
  `double` must call one of those.

## Console features

The following `diec` options are not ported: `--entropy` (`-e`), `--info`
(`-i`), `--struct` (`-S`), `--showstructs` (`-w`), `--test` and
`--createtest`. They belong to the `XFileInfo` / `EntropyProcess` modules
rather than the scan engine.

Everything else is present and produces byte-identical output, including the
four structured formats (`--json`, `--xml`, `--csv`, `--tsv`) and
`--verbose`. The short options are `diec`'s, which are not always the
mnemonic letter — `-a` is `--alltypes`, `-p` is `--plaintext`, `-l` is
`--profiling` and `-s` is `--showdatabase`. Where this port previously used a
different letter, that spelling still works as an alias.

`--profiling` sets the flag that scripts observe but prints no timings.

`--alltypes` is accepted but inert. In the reference engine it runs a second
scan with a different file type (`MSDOS` for a PE, `ZIP` for an APK) and
prints the results as a **second group** under its own format heading. The
result model here is a single flat list per file, so honouring the flag would
change the output shape rather than match it; leaving the flag inert keeps
the default output identical, which is the property the port is built for.

`--recursivescan` is implemented for walking directories on disk. Scanning
*inside* archives is a different feature and is covered above.
