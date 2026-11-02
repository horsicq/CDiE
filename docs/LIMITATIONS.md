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
| Archive member scanning | `--archivesscan` expands ZIP, JAR, APK and IPA members as Detect It Easy 4.0.0 does, nested archives included (down to 20 archive levels). Detect It Easy's engine also expands 7z, RAR, CAB and ISO 9660 containers when its build can read them; cdie does not expand them yet. GZIP, BZIP2, LHA and Mach-O Fat files are scanned but not expanded, as in Detect It Easy. A ZIP without end records (a stream or a cut-short file) is read from its local headers for Store and Deflate members only. Compressed members of any size a ZIP entry can declare without ZIP64 (under 4 GiB) are scanned; neither cdie nor Detect It Easy reads ZIP64 archives. A member is decoded in memory when it is smaller than the file buffer size (64 KiB by default, set by `diec --filebuffersize` and by the GUI's *File buffer size* option), and through a temporary file in the system temporary folder from that size on, so a large member needs free temporary space equal to its decoded size for each archive level being expanded (Detect It Easy keeps members under 16 MiB in memory and needs twice that space above it). Stored members are read in place, also when no temporary file can be created (Detect It Easy then skips a stored member of 16 MiB or more, so its 21-member window can differ). A compressed member is skipped without a message, as in Detect It Easy, when no temporary file can be created for it (a ZIP method it does not map is then not reported either); with no usable temporary folder that skips members from 64 KiB on in `diec` by default, where Detect It Easy still scans those under 16 MiB (`--filebuffersize 16M` keeps them in memory as well). Every open file and decoder allocates a whole file buffer however small its input, so a large `--filebuffersize` costs that much memory per open file (about 1 GiB with `1G`; a 32-bit `diec` accepts at most 64M), and if that memory cannot be allocated the result changes without an error message. cdie also skips, without a message, a member whose decoded size does not fit in the temporary folder, or, from 16 MiB on (where Detect It Easy also moves a member to disk), would leave less than 1 GiB free there, or whose temporary file cannot be written or read back, so a full disk loses that member, not the input's result (Detect It Easy instead prints `Last error: Cannot create unpack verification buffer` when only its second file does not fit). On a nearly full temporary volume a member under 16 MiB therefore only needs room for itself; one from 64 KiB on that does not fit is skipped in `diec` by default, which Detect It Easy scans from memory below 16 MiB. The free space is checked when a member's temporary file is created; several cdie processes at once, or a disk quota on Linux and macOS, can still fill the volume or take it below that margin, in which case a failing write skips the member. A stream that decodes to more than 4 GiB, past anything its entry can declare, is skipped without a message too (Detect It Easy decodes it to its end and prints `Last error: Invalid CRC` when its CRC is wrong). A Zstandard frame whose window is larger than 128 MiB is skipped without a message, as in Detect It Easy. As a deliberate safety limit that Detect It Easy does not have, one input scans at most 10,000 archive members, all nesting levels together (100,000 with `-g`); reaching it stops the expansion and adds `Last error: Archive member scan limit reached (10000)`, with exit code 0. Without it, four levels of ZIPs that each hold 21 ZIPs (204,204 member scans) would take about 25 minutes with the full database; with it, the scan stops after a minute or so. |
| `--verbose` "Operation system" line | reproduced for PE, DEX (`Android(9.0)[Dalvik, 32-bit]`), Mach-O (`macOS(13.0.0)[X86_64, 64-bit]`), NE (`Windows[286, 16SEG]`) and ELF (`Ubuntu Linux(22.04.3, ABI: 3.2.0)[AMD64, 64-bit]`, `Unix[MIPS, 32-bit]` — OSABI/interpreter/`.comment` distro/GNU-note ABI/e_machine arch). The exotic ELF distro/note branches are ported from the reference but validated only on the available samples (Unix, Ubuntu, GNU-note) |
| PDF streams | not decompressed — key/value queries read the plaintext object dictionaries, which is where the database looks; `/Filter` is reported, but stream contents are not searched |
| UTF-8 / UTF-16 text detection | `isUTF8Text()`, `isUnicodeText()` and `getHeaderString()` are implemented in xxfclib; the previous stub limitation no longer applies. |

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

* **`CDIE_NO_CRT` is supported by native MSVC x64 and ARM64.** It defaults to
  ON for those two architectures. A 32-bit build needs the compiler's 64-bit
  arithmetic helpers (`_alldiv`, `_allshr` and friends), which live in
  `libcmt`, while ARM64EC uses a different hybrid ABI; both link the CRT as
  usual. The option can be turned off explicitly on a supported target.

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

## GUI Hex View

The native GUI deliberately starts scans with Deep, Heuristic, Verbose and
Overlay enabled. This differs from the current Qt GUI defaults; it preserves
cdie's established default chosen for this port. The Advanced File type list
also includes Qt's generic Archive, Text, Unicode, 7-Zip and RAR choices while
keeping the most specific concrete detected format selected. Generic Text,
Unicode and 7-Zip choices run the Binary signature pass, as `diec --filetype`
does for those categories.

Hex View is a read-only file browser in the Windows/macOS GUI. It reuses the
shared hex control with 64 KiB windows, 64-bit physical offsets, Go/paging,
Refresh, selection, encoding and row-width controls, copying and byte export.
ASCII/UTF-8/UTF-16LE/BE are exposed in its toolbar; additional existing numeric
and display functions remain those of the underlying widget. The app opens
the input `rb`; source editing is unavailable. It requires a known-size seekable
device and does not expose process-memory or non-seekable streams. Read/parse
failures retain the previous bytes. Byte/text clipboard selections are capped
at 1 MiB; streamed Save selection can export larger ranges. No console Hex View
command is added. Native chooser/clipboard availability depends on the backend.

## GUI Disasm

Disasm is a read-only linear decoder for x86 16/32/64-bit and, by default,
ARM32, Thumb and AArch64. `CDIE_DISASM_ARM=OFF` omits ARM modes (private assembly
test builds still enable them). Other architectures, symbols, control-flow
analysis, execution, editing and console disassembly are unavailable.
Auto uses native PE, ELF and thin Mach-O headers; raw/unsupported files require
manual mode selection. Big-endian ARM also requires an explicit instruction
byte order; container byte order can differ. Mach-O Fat slices are not selected.
Instructions unavailable in the configured decoder are shown as single-byte
`db` rows along with invalid/truncated input.

Go uses physical offsets. Mapped virtual addresses are shown in the table;
unmapped rows use physical offsets instead. Metadata is detected only on open.
Refresh reloads bounded page bytes and size, but changed headers require
reopening. A failed read or an offset beyond a shrunken file retains the previous
page; Go to a valid offset or reopen to recover. History retains at most 256
page starts. Clipboard availability is backend-dependent. Native Windows/macOS
runtime validation requires those hosts; shared controls are exercised through
the terminal backend locally.

## GUI Memory Map

Memory Map shows the native default mapping of a file: PE sections, ELF segments
and thin Mach-O segments. It does not offer alternate map modes, Mach-O Fat
slice selection, archive-member expansion, process-memory inspection or access
permissions. Other/unrecognized inputs (including headers rejected by native
validation) have a flat unmapped Data region. An empty raw file has no rows.
Native parsing failures after recognition, invalid ranges and maps over 1024
records fail without returning a partial snapshot.

The table preserves order, overlap, virtual-only and invisible records. Offset
lookup selects the first physical interval; address lookup also includes virtual
tails. Both are half-open and first-match. Unknown addresses and absent physical
offsets are shown as `-`; there is no inferred VA for flat Data. Snapshot names
are escaped from fixed-width native byte strings. Reopen after file changes;
live refresh is not provided. Clipboard availability depends on the backend.
The viewer is read-only and has no new console command.

## GUI Imports

Imports supports native PE, ELF and thin Mach-O. Supported executables without
imports display an empty table; unsupported/raw files and Mach-O Fat report an
unsupported-format error. PE includes ordinary and delay-load tables, names,
ordinals and IAT slot locations. Displayed PE addresses identify slots; resolved
function addresses are not available from this view.

ELF uses named undefined global/weak symbols in DYNSYM, or SYMTAB for relocatable
objects. Sectionless ELF dynamic tables and version metadata are not enumerated.
Library associations and import slot locations stay unknown. Mach-O uses
undefined external nlist symbols, with ordinary library ordinals mapped to
dylib basenames. Common/local/debug/defined symbols are excluded. Dyld bind
streams, chained fixups and slot locations are not enumerated; a file relying
only on those mechanisms can therefore show an empty list.

All names escape control/non-ASCII bytes as `\xNN`. The snapshot is limited to
10,000 records, 16 KiB per displayed name and 8 MiB of aggregate NUL-inclusive
names. Bound violations and failed/cancelled reads return no partial model.
Filter is an ASCII-case-insensitive substring match in library/name with a
256-byte input limit; it selects the first visible row. TSV export of visible
rows is capped at 32 MiB and includes full names, hints and offsets. Hints are
meaningful for named PE imports. Clipboard availability is backend-dependent.
Reopen after file changes. No console Imports command is added.

## GUI Exports

Exports supports native PE, ELF and thin Mach-O. Raw/other formats and Mach-O
Fat report unsupported; supported files without exports display an empty table.
PE enumerates nonempty function slots, preserves ordinal-only and forwarded
entries, and returns the first native name for aliases sharing a slot. A
forwarder's offset/address locates its string; its target is not resolved.

ELF retains named, defined global/weak symbols with default/protected visibility
from DYNSYM, or SYMTAB for relocatable objects. Hidden/internal/local/undefined
and file/section symbols are omitted. Sectionless dynamic tables and version
metadata are not enumerated. Addresses follow native symbol conventions;
absolute values have no physical offset. Mach-O uses native nlist/export-trie
metadata, including reexports and indirect aliases. An available export trie
determines the public export set. Special trie flags/resolver metadata are not
separate columns. Unknown/non-applicable ordinals use `-` for ELF/Mach-O.

The snapshot is capped at 10,000 records, 16 KiB per displayed name/forwarder
and 8 MiB of aggregate NUL-inclusive strings. Names escape control/non-ASCII
bytes. Malformed, cancelled and over-limit reads return no partial result.
Filtering matches name/forwarder substrings with ASCII case folding, accepts
up to 256 UTF-8 bytes and selects the first visible row. Copy/TSV includes full
visible values and offsets, capped at 32 MiB. Clipboard support depends on the
backend. Reopen after file changes. No console Exports command is added.

## GUI Resources

Resources supports native PE resource leaves and ELF `.gresource.*` bundles/
supported value leaves. Other formats, including Mach-O/Fat, report unsupported.
Supported PE/ELF inputs without resources show an empty table. PE preserves
named/numeric IDs, code pages and physical ranges; zero-byte resources can have
no physical offset. UTF-8 names are retained and control/invalid UTF-8 bytes are
escaped. There is no payload preview, extraction, editing or console Resources
command in this release.

ELF bundle rows cover the complete stored bundle. Leaf ranges describe stored
bytes, so compressed leaves show compressed sizes rather than decoded lengths.
Compression state is not a separate field in the native resource record. The
native provider omits unsupported variants/flags and invalid leaf-name graphs
(including parent cycles), retaining the bundle when its root is valid. Invalid
bundle roots/read failures are fatal. There is no GResource decompression.

Snapshots retain at most 10,000 records, 16 KiB per displayed name and 8 MiB of
aggregate NUL-inclusive type/name/language strings. Native reader failures,
cancellation and limits return no partial snapshot. Filtering matches displayed
type/name/language or decimal IDs using ASCII case folding, accepts up to 256
UTF-8 bytes and selects the first visible row. Copied TSV includes all three
numeric IDs, code page and full visible names/ranges, capped at 32 MiB.
Clipboard availability depends on the backend. Reopen after file changes.

## GUI Symbols

Symbols supports native PE COFF, ELF and thin Mach-O symbol metadata. Raw
formats, standalone COFF objects and Mach-O Fat report unsupported; supported
files without symbol tables show an empty table. PE skips auxiliary entries as
rows but retains their count and supported file/function/section metadata. ELF
includes native symbol tables without export-visibility filtering. Sectionless
dynamic tables and symbol version metadata are not enumerated. ELF table
indices can repeat across tables; record offsets identify the source entries.

Mach-O includes nlist symbols and trie-only exports, with unknown kind or size
where native metadata does not provide them. Indirect-symbol native values can
be string-table indices. Trie-only entries have no nlist index/record offset;
reexport target details are available in the Exports view. Separate debug files,
DWARF/PDB decoding, demangling and process-memory symbol resolution are not
provided. Size zero can mean unknown; absolute values and debug records have no
physical target offset. Values are native metadata, not resolved runtime values.

The snapshot is capped at 10,000 records, 16 KiB per displayed name/filename
and 8 MiB of aggregate NUL-inclusive strings. Valid UTF-8 is retained and
control/invalid bytes are escaped. Native failures, cancellation and bounds
return no partial snapshot. Filter matches name/filename substrings with ASCII
case folding, accepts up to 256 UTF-8 bytes and selects the first visible row.
TSV copying includes full typed metadata, capped at 32 MiB. Clipboard support
depends on the backend. Reopen after changes. No console Symbols mode is added.

## Shared-library compatibility boundary

`die.dll` matches the 14-export ABI of horsicq/die_library 0.1.0, including
the x86/x64 calling conventions, VB progress callback, ownership functions,
database reuse and the reference formatter's flag precedence. The library
adapts cdie's XML/CSV console spelling to ScanItemModel's spelling, so the
same detection records serialize byte-for-byte as the Qt library.

The DLL does not replace cdie's detection engine with Qt. Therefore an engine
or script-API deviation documented in this file can still produce a different
record set for the same input and database. In a direct current-database check,
ordinary PE output matched, while a deep scan of the cdie DLL itself produced
one additional `VMProtect(new)[DS]` record in the Qt reference. The serializer
then formatted each engine's record set correctly, but it cannot manufacture a
detection the pure-C engine did not return. This is the remaining behavioral
boundary, not an ABI, ownership or output-format difference.

The reference exports no version query function. Its 0.1.0 version is Windows
file/build metadata; language bindings obtain their displayed versions from
compile-time definitions.

## Console features

The following `diec` options are not ported: `--test` and `--createtest`.

`--info` (`-i`), native structure selection with `--struct` (`-S`) and
`--showstructs` (`-w`) use DataStructs for PE, ELF and thin Mach-O only.
Other formats and Mach-O Fat containers return an unsupported-format error
for those structure operations. The DataStructs schema differs from Qt's
XFileInfo report. Entropy and legacy XFileInfo method selectors remain
unavailable through `--struct`; `Hash` / `Hash#ALGORITHM` and `Strings` are handled separately
as a whole-file byte operation. Structure reports are bounded to 1,048,576
visited/emitted structures, 4,194,304 visited fields and 64 MiB of output per
file; exceeding a console report bound fails without a partial report. The GUI
separately retains up to 64 MiB of model data (and at most 262,144 structures);
if a larger valid file has more rows it displays the prefix that fits instead
of showing nothing.

`--hash` and `-S Hash` currently expose CRC32, MD5, SHA1 and SHA256. CRC32 is
ISO-HDLC/ZIP. MD4, SHA224, SHA384 and SHA512 are not implemented by this command;
unknown selectors fail explicitly. All four use existing xxfclib contexts in
one checked streaming pass over the whole file. No format parser is involved.
Digests are lowercase hex, including standard digests for empty files, where
older DIE 3.21 returns blank strings. JSON/XML structure is compatible for the
supported selected fields; the full default set differs from DIE's seven hashes.
The GUI shows those same four results and allows selection/copy, with progress
and cancellation. Clipboard availability depends on the widget backend.

Strings uses a separate raw-byte operation (`--strings` / `-S Strings`) for
all formats. ASCII, strict UTF-8 and UTF-16LE/BE are implemented; other legacy
code pages and UTF-32 are unavailable. Both UTF-16 alignments are scanned.
Automatic mode requires a BOM or a consecutive ASCII run meeting the minimum;
explicit UTF-16 mode omits that heuristic and may return more binary candidates.
The scalar acceptance policy excludes ASCII/C1 controls, surrogates, BOMs,
noncharacters and selected format-control ranges; it does not use a complete
Unicode category database. Offsets are physical, lengths count scalars, and
terminators/BOMs are excluded. Overlapping interpretations are retained.
Results are bounded to 10,000 records, 8 MiB UTF-8 text and 1 MiB per string;
formatted console output is capped at 32 MiB. A bound fails without partial
file output. The GUI uses default extraction settings (minimum 4/all), and
provides filtering, Copy, full-text View and native Save CSV. Extraction options
are currently console-only. DIE 3.21 provides no Strings command to compare.

Entropy streams all file types with bounded working memory. Native region maps
are used for PE, ELF and thin Mach-O; other formats (including archives and
Mach-O Fat) currently use one Data region. At most 1024 physical regions are
accepted; exceeding this limit fails without partial output. The GUI graph has
up to 100 automatic blocks and retains no file bytes, so custom Count/Reload
is unavailable. The reusable widget exports CSV/SVG when embedded in a host;
CDIE's read-only modal hides export buttons. JSON/XML follow DIE's entropy
shape; CSV/TSV quote names for safe delimiter handling. Float comparisons use
1e-12 tolerance. Packed status is an entropy heuristic, not proof of packing.

`--buffersize` (the GUI's *Buffer size*, `Feature/ReadBufferSize`) is the read
chunk of `--hash`, `--entropy` and `--strings` (16 MiB at most) and of the
scan's MD5, SHA-1, SHA-256, CRC and BLAKE2sp loops and byte searches that read
into a buffer of their own. Detect It Easy also reads its decoders' input and
its file copies in chunks of that size; cdie's decoders, signature engine and
CRC32, Adler-32 and entropy script functions read by the file buffer size,
and the extractor's copies and search window, the Hex View and Memory Map
saves and the strings text decoder by a fixed 64 KiB. The size changes how
often the input is read, never a result.

Scan-output compatibility is checked separately from File Information; see
TESTING.md for the reference-version differences and historical corpus results.
Fat Mach-O File Information is a documented exception: cdie shows the fat table
and the selected slice, while Detect It Easy shows only the generic rows.
The short options are `diec`'s, which are not always the
mnemonic letter — `-a` is `--alltypes`, `-p` is `--plaintext`, `-l` is
`--profiling` and `-s` is `--showdatabase`. Where this port previously used a
different letter, that spelling still works as an alias.

`--profiling` now emits timings through xxfclib's profiling implementation.

`-a` / `--alltypes` runs applicable parent-format passes and retains separate
result groups. `--overlayscan`, `--resourcesscan` and `--archivesscan` expose
the engine's bounded child scans. Native GUI Scan options and TUI F2 expose
the same modes. As in Detect It Easy, a child's own overlay and resources are
not scanned, but with `--archivesscan` an overlay, resource or archive member
that is itself an archive is expanded, down to 20 archive levels.

Archive members follow Detect It Easy 4.0.0: every member the walk reaches is
CRC-checked, stored ones included, and one that fails is not scanned. The
error is printed after the detections as `Last error: Invalid CRC` (or
`Last error: Unknown compression method: Unknown` for a ZIP method Detect It
Easy does not map, even one cdie could decode), on stdout in every output
mode, and the exit code stays 0. Detect It Easy keeps a single error string,
so the line belongs to the next scan that finishes, and an error overwritten
before then is not printed. A ZIP whose entries disagree with their local
headers, overlap or share data is scanned as Binary, as `XZip::isValid`
requires.

The structured formats still differ from `diec` 4.0.0 in two ways. `diec`
nests the record of an overlay, resource or archive-member scan inside its
parent's values (JSON) or in an element named after the child's heading
(XML); cdie lists children flat, with their `archive_member` and
`offsetbasis` fields, in `<file-part>` elements. CSV and TSV write the type of
an unknown result in lower case (`unknown`, as older `diec` versions do) where
`diec` 4.0.0 writes `Unknown`. Text output, the acceptance criterion, matches.

`--recursivescan` is implemented for walking directories on disk and, as in
`diec` 4.0.0, also turns on resource and overlay scanning. Scanning
*inside* archives is a different feature and is covered above.
`-W`/`--firstwrapperonly` and `-F`/`--filetype` are not ported yet: the engine
rejects a first-wrapper-only scan, and no type-name parser exists.
