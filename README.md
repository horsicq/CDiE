# [C]DiE — Detect It Easy console scanner, pure C port

`cdie` is a C99 re-implementation of the **Detect It Easy**
console scanner (`diec`). It loads the stock DIE signature database, runs the
JavaScript detection rules and prints the same result strings as the original
Qt-based tool.

Everything needed to do that lives in this source tree:

* a **JavaScript (ES5 subset) interpreter** — lexer, parser, tree-walking
  evaluator, standard library and a backtracking regular-expression engine;
* **binary format parsers** — PE/PE+ (sections, imports, exports, resources,
  version info, manifest, debug directory, Rich header and the full .NET
  metadata tables), ELF, Mach-O, DEX, JPEG/EXIF, PNG, APK (ZIP + a
  from-scratch DEFLATE + Android binary XML) and PDF, plain-text detection,
  plus file-type detection;
* the **DIE signature engine** — the `'text'`, `..`, `**`, `%%`, `$$`, `##`,
  `++` signature syntax, memory maps and RVA/VA translation;
* the **script API** (`PE.*`, `Binary.*`, `_setResult`, `includeScript`, …).

No Qt or code generators. The scan engine uses the C11 `cdisasm` x86 decoder
through xxfclib. Both libraries build from source with the scanner, and the
decoder adds no runtime library dependency.

The focused scan engine registers TAR as its only archive writer, preserving
`--createtar` and `--createprecompiledtar` for signature databases. Reading and
decompression of other supported archive formats remain enabled.

On Windows it goes one step further: the runtime layer is Win32 only, and the
native MSVC x64 and ARM64 builds link no C runtime at all. The current Release
console executable imports **50 functions on x64 and 51 on ARM64, all from
`KERNEL32.dll`**; ARM64 adds `IsProcessorFeaturePresent` for its CPU feature
switches. That includes its own
correctly-rounded `strtod`/`dtoa` and its own libm. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#the-runtime-layer).

Before the decoder migration, the scanner matched `diec` on a 348-file corpus
(Windows system DLLs, Visual Studio .NET assemblies, Qt binaries, Python,
scripts, text, images, an APK and a PDF): **348 identical, 0 different**. A
30-file subset also matched byte for byte across `--json`, `--xml`, `--csv`,
`--tsv`, `--plaintext` and `--verbose`. These parity results need to be
rechecked with the `cdisasm` backend. See [docs/TESTING.md](docs/TESTING.md).

---

## Scan expansion

The console exposes the shared engine's additional scan passes:

- `-a` / `--alltypes` scans applicable parent file types.
- `--overlayscan` scans trailing overlays.
- `--resourcesscan` scans PE/ELF resource payloads.
- `--archivesscan` scans the members of ZIP, JAR, APK and IPA archives,
  nested archives included.

Archive scanning follows Detect It Easy 4.0.0. A member, overlay or resource
that is itself an archive is expanded too, down to 20 archive levels. Without
`-g` (`--aggressivescan`) only members that Detect It Easy considers scannable
(executables, DEX, PDF, TAR and archives) are scanned, at most 21 per archive.
Every member reached is CRC-checked, stored members included: a damaged member
is skipped, the result ends with `Last error: Invalid CRC`, and the exit code
stays 0. GZIP, BZIP2, LHA and Mach-O Fat files are scanned as files and not
expanded, as in Detect It Easy; 7z, RAR, CAB and ISO 9660 members are not
expanded yet. As a safety limit that Detect It Easy does not have, one input
scans at most 10,000 archive members (100,000 with `-g`). Directory traversal
remains controlled by `-r` / `--recursivescan`, which, as in Detect It Easy,
also turns on resource and overlay scanning. See
[docs/LIMITATIONS.md](docs/LIMITATIONS.md).

## File Information

`diec --info FILE` (`-i`) reports native file information without a signature
database. `--showstructs` (`-w`) lists available native structures, while
`--struct` (`-S`) selects a structure. The native PE, ELF and thin Mach-O readers
supply DataStructs; unsupported file formats are rejected.

The native GUIs provide **File Information** through the shared
`xxwidgets_file_information_dialog`. A cancellable worker builds a bounded
presentation model, then the browser displays navigation and structure fields.
Read failures and cancellation discard partial models. Closing restores the
scan window. See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the native
DataStructs adapter and ownership rules.

## Entropy

`diec --entropy FILE` (`-e`) calculates entropy without a signature database.
It reads checked 64 KiB chunks and combines 64-bit byte histograms into the
whole-file total. Native memory maps supply physical regions without using
DataStructs. Empty files are valid. The report retains at most 1024 regions,
100 samples and one 256-bin histogram; failures and cancellation discard
incomplete results.

The native GUIs provide **Entropy** on a cancellable worker and present the
completed result through the shared xxwidgets entropy tables, graph and byte
histogram. Parser and file I/O stay outside the widget. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Hash

`diec --hash FILE` (also `-S Hash`) calculates CRC32, MD5, SHA1 and SHA256
without loading a signature database or file parser. Every checked 64 KiB read
feeds all four digest contexts in one pass. Reports support plain text, JSON,
XML, CSV and TSV. Plain output prints
algorithm/value rows. Multiple file results use the existing JSON/XML `files`
wrapper; delimited output repeats the Hash block for each file. All digests
are lowercase hexadecimal. CRC32 is the ISO-HDLC/ZIP checksum with 8 digits.
Empty files return the standard empty-file digests (older DIE 3.21 emits blank
strings). Read errors and cancellation discard the entire file result.

The native GUIs provide a **Hash** button for the current path, **Ctrl+H** on
Windows and **File → Hash** / **Cmd+Shift+H** on macOS. A cancellable worker
calculates every digest in one pass using a 64 KiB buffer. The reusable
`xxwidgets_hash_*` browser copies only path, size and hash strings. Select a
row to inspect its digest; **Copy selected** copies that value and **Copy all**
copies algorithm/value lines. Close or Escape returns to the scan window.

## Strings

`diec --strings FILE` (also `-S Strings`) extracts string candidates from any
file type without a signature database or DataStructs. The default minimum is
**4 decoded characters**, with ASCII/UTF-8 and automatic UTF-16LE/BE scanning.
`-s` continues to mean database information.

```bash
diec --strings program.exe
diec --strings --strings-min 8 --strings-encoding ascii -j file.bin
diec -S Strings --strings-encoding utf16le -j file.bin
diec --strings -rj samples/
```

`--strings-min` accepts 1–1024; `--strings-encoding` accepts `ascii`, `utf8`,
`utf16le`, `utf16be` or `all`. These options require Strings mode. ASCII means
bytes 0x20–0x7E. UTF-8 decoding is strict; ASCII-only runs retain the `ASCII`
label. UTF-16 checks both byte alignments and decodes surrogate pairs. In
`all` mode a UTF-16 run needs a BOM or a consecutive ASCII run at least as long
as the minimum, reducing accidental wide interpretations of binary data. Use
an explicit UTF-16 encoding for text without a BOM or that ASCII anchor.
Controls, BOMs, malformed sequences and noncharacters delimit candidates;
this uses a scalar acceptance policy rather than a Unicode category database.

Records are sorted by physical file offset, then encoding. They include
`offset`, `byte_size`, `character_length`, `encoding` and decoded UTF-8 `text`.
Sizes exclude terminators/BOMs, and character counts count Unicode scalars.
Different encodings may describe overlapping byte ranges. All five console
report formats are supported. JSON uses `{path,size,strings:[...]}`; XML uses
`<file-strings>` and `<string>` records; CSV/TSV has a path column and one header
across multiple files. Recursive targets use `-r`; multi-file JSON/XML uses
the usual `files` wrapper. DIE 3.21 has no matching Strings command/schema.

The native GUIs provide **Strings**, with **Ctrl+Shift+S** on Windows and
**File → Strings / Cmd+Shift+S** on macOS. Extraction and indexing run on a
cancellable worker. The shared `xxwidgets_strings_source_dialog` displays at
most 128 records at once, with **Previous**, **Next** and a 64-bit page field.
Enter text or column filters and click **Search** (or Ctrl+F) to search the
complete source on a worker with a progress dialog. Cancelling a search keeps
the preceding results and page. **Save CSV** streams every filtered record on
a worker after choosing a native destination. Close/Escape restores the scan
window.

GUI results use a temporary 32-byte metadata record per string, sorted by
physical offset and encoding through a bounded-memory merge. Index creation
can temporarily require twice that disk space; temporary indexes are removed
when the browser closes. Only the visible page is decoded. Text longer than
4 KiB is explicitly marked `[display prefix]` in the browser and its View/Copy
controls; searching and CSV export still process complete decoded text.

The reusable `xxfclib/strings/xx_strings_scan.h` scanner reads 64 KiB chunks,
reports 64-bit counts, and sends borrowed metadata/text to callbacks without
retaining a result array. It includes an SSE2 ASCII classifier and a portable
scalar path. The console report retains its explicit limits: 10,000 results,
8 MiB total UTF-8 text, 1 MiB per decoded string and 32 MiB report output. A
limit, read error or cancellation fails that report without exposing partial
results. Empty files produce an empty list. Strings, Hash, Entropy, File
Information and database operations are separate command modes.

With tests enabled, build `bench_strings_billion` and `cdie_strings_billion`
for reproducible stress examples:

```text
bench_strings_billion --text
bench_strings_billion --text --scalar
cdie_strings_billion
cdie_strings_billion --gui
```

The default generated input contains exactly 1,000,000,000 four-character
ASCII strings separated by NUL (5,000,000,000 bytes). Every byte passes through
the scanner and every callback is verified; `--text` additionally checks each
decoded string. Neither example allocates a billion-row array or stores a 5 GB
input file. The GUI example uses the known generated pattern for lazy pages,
then performs actual full scans for searches and supports cancellation. Use
`--count N` for smaller runs. This measures generated input and scanning; real
files also incur storage I/O and metadata-index costs.

## Hex View

The Windows and macOS GUIs provide **Hex View** for the current file path,
with **Ctrl+Shift+X** on Windows and **File → Hex View / Cmd+Shift+X** on macOS.
It opens any file type read-only, including empty files, without a signature
database or DataStructs. The existing `xxwidgets_hexview_*` control supplies
hex bytes, addresses, decoded symbols, selection and keyboard/mouse navigation.

The new reusable `xxwidgets_hexview_browser_*` adds:

- **Go** to a decimal offset or `0x`-prefixed hexadecimal offset. Ctrl+G/Cmd+G
  focuses the field; Enter loads the position. EOF is a valid empty window.
- **Previous / Next** to move between 64 KiB window boundaries.
- **Refresh / F5** to reload the displayed bytes, including file size changes.

Keyboard and context-menu copies support selected bytes, offsets and decoded
symbols. Copies of source bytes are bounded to 1 MiB; offset-only copies are
exempt. The shared byte-dump dialog streams selected ranges. Reads preserve the
source cursor and failed reads retain the preceding window. Refresh reloads
the file size and clamps a shrunken file's current position to EOF. The borrowed
file device stays open until the browser closes.

## Disasm

The Windows and macOS GUIs provide **Disasm** for the current file path,
with **Ctrl+Shift+A** on Windows and **File → Disasm / Cmd+Shift+A** on macOS.
It opens files read-only without a signature database or DataStructs. The
existing `cdisasm` decoder supplies instructions; the expanded xxwidgets library
composes its existing `XXWIDGETS_DEBUGGER_DISASM` table into a reusable browser
and modal.

- **Auto (file)** detects x86 16/32/64-bit, ARM, Thumb or AArch64 from native
  PE, ELF and thin Mach-O metadata and starts at a mapped entry point when
  available. Raw files and other architectures need a manual instruction mode.
- **Mode** offers x86 16/32/64-bit and ARM32/Thumb/ARM64 in both instruction
  byte orders. Big-endian ARM files require manual selection because container
  endianness alone does not determine instruction endianness. ARM modes are
  enabled by default; build with `-DCDIE_DISASM_ARM=OFF` for x86 only.
- Normal builds use cdisasm's base instruction profile with MIPS and extra
  opcodes disabled. Optional x86, modern ARM and Apple-specific extensions are
  excluded. The private compatibility corpus uses a separate extended profile.
- **Go** accepts decimal or `0x` hexadecimal physical file offsets, including
  EOF. **Ctrl+G/Cmd+G** focuses that field. Addresses in the table use virtual
  addresses where mapped, with the physical offset shown in each row's comment.
- **Next / Back** preserve decoded instruction boundaries and keep up to 256
  previous page starts. Go or changing mode clears that history.
- **Refresh / F5** reloads bytes and file size. **Copy row** copies the selected
  address, bytes, instruction and comment. **Close / Escape** returns to scanning.

Pages contain at most 512 rows whose starts lie in a 4 KiB range, with 15 bytes
of lookahead to complete the last instruction. Invalid, unsupported or truncated
instructions become single-byte `db` rows. Opening metadata runs on a cancellable
worker; bounded page reads are deferred until after input dispatch. Invalid
navigation or mode changes retain the preceding page and selector.

## Extractor

The native **Extractor** tool (`Ctrl+Shift+T`, or **File → Extractor** on
macOS) uses `xxfclib/src/extractor` through the shared
`xxwidgets_extractor_dialog`. Heuristic chooses structured resources or
archive members when available; Format handles PE resources and ZIP/TAR/PDF
streams, and Raw finds validated embedded formats. **Deep scan** also searches
inside discovered objects. Type selection and nine column filters apply to
the complete result index; the table pages at 128 rows and selection follows
the corresponding offset in the read-only hex preview.

Scans, table saves, and dumps run on cancellable workers. **Dump selected**
and **Dump all** copy the source bytes identified by Offset and Size, including
compressed bytes for compressed members. Output filenames are flattened and
made unique, and each file is published atomically after a successful write.
The core API also supports decoded member extraction with a size limit.
Raw carving validates candidates with native readers. It retains the complete
signed PE extent and rejects malformed streams that Qt may list as Binary.

Disk writes use the shared `xxwidgets_disk_write_run` dialog: destination,
current file, completed files and bytes, speed, elapsed time and remaining
time. It appears after 350 ms and joins the worker before returning. Extractor,
Strings, Memory Map, Hex View and the signature editor share this dialog.
Cancel is checked before atomic publication, preserving an existing destination;
a completed commit remains successful if cancellation arrives afterward.

## Memory Map

Memory Map uses `app/cdie_memory_map.c` to read native PE sections or ELF/thin
Mach-O segments into a detached snapshot. It retains physical, virtual-only,
invisible and overlapping records in native order, checks physical bounds and
address overflow, and rejects maps over 1024 rows. Generic inputs receive an
unmapped Data interval. Readers and the read-only device are destroyed before
the snapshot returns; it contains no borrowed format/map pointers. DataStructs
and the signature engine are not used.

`gui/gui_memory_map.c` loads that snapshot on the established cancellable worker,
escapes executable name bytes and translates native part flags into labels.
`xxwidgets_memory_map_*` copies it into a new MEMORY_MAP table, reusing the
transactional debugger-table storage, cell escaping and shared native/TUI
tabular rendering. It adds metadata, region details, physical/address lookup
and deferred clipboard actions. Virtual-only rows never acquire a physical
offset. Model/metadata replacement rolls back together; lookups use native
record order with half-open intervals. The modal restores owner state on exit.

## Imports

Imports uses `app/cdie_imports.c` to collect a detached snapshot of native
`xx_import_record` values. PE uses its StreamImport cursor for normal/delay
tables; ELF uses its native symbol cursor and limits executable/shared-object
records to DYNSYM, accepting named undefined global/weak symbols. Relocatable
objects can use SYMTAB. Mach-O uses native inspection's undefined external nlist
symbols and resolves ordinary dylib ordinals to library basenames. Unknown
slot offsets/addresses stay unknown. The focused build enables import/symbol
streams; DataStructs remain restricted to the previous three reader families.

Names are copied through a bounded byte-escaping adapter; the snapshot owns all
records/strings and no reader/device pointer. The `cdie_imports_free` destructor
releases partial state on errors and complete snapshots after GUI use. Limits
apply before publication. `gui/gui_imports.c` uses the existing worker/progress
pattern and passes a completed snapshot to `xxwidgets_imports_*`.

The reusable imports browser wraps `XXWIDGET_IMPORTSVIEWER` and its copied native
table cells. It owns full source records for filtering and a separate copied
visible table, swapping metadata/filter/buttons/table transactionally. Filter
and clipboard work is queued after polling. Text export includes complete
escaped cells, physical offsets and hints, independently of clipped display
columns. The modal adds resize handling, Close/Esc and Ctrl/Meta+F, restoring
the owner's callbacks/default/enabled state. It links no native parser.

## Exports

Exports uses `app/cdie_exports.c` for a detached `xx_export_record` snapshot.
The focused build enables StreamExport for PE; the native cursor preserves
ordinals, forwarders and slot order, skipping empty slots and choosing the
first name for aliased slots. ELF uses the native symbol cursor, retains
defined global/weak symbols in the applicable symbol tables, and reads the
validated st_other byte for default/protected visibility. Absolute ELF values
are normalized to unknown physical offsets. Mach-O's native inspector provides
exported nlist/trie metadata, aliases, reexports and absolute addresses; regular
addresses are translated through physical segments when possible.

Name/forwarder byte escaping, limits, cancellation and ownership follow the
Imports snapshot pattern. ELF/Mach-O ordinals use UINT64_MAX for unknown.
`gui/gui_exports.c` publishes the completed worker result to the reusable
`xxwidgets_exports_*` browser/modal. This composes the existing EXPORTSVIEWER
table, whose ordinal cell now supports the unknown sentinel, with copied
metadata, name/forwarder filtering, details and deferred TSV copying. The
browser owns source and visible records; native devices/readers are closed
before UI presentation and no DataStructs are requested.

## Resources

Resources uses `app/cdie_resources.c` to collect a detached native
`xx_resource_record` snapshot through StreamResources for PE or ELF. The focused
build already includes both providers for resource scanning. PE streams resource
leaves with numeric/named type, name and language IDs, code pages and ranges;
ELF provides recognized GResource bundles and supported leaves. No reader,
device, cursor or payload is retained after loading.

The adapter validates complete physical ranges and caps records/text before
publication. Valid UTF-8 names are preserved; invalid UTF-8/control bytes escape
individually, retaining PE Unicode names without trusting arbitrary GResource
key bytes. Empty resources can have an unknown offset. Cursor failures and
cancellation discard the complete snapshot. ELF's native provider can skip
unsupported values/invalid leaf-name graphs while retaining a bundle record.

`gui/gui_resources.c` follows the shared worker/progress pattern and passes the
completed snapshot to `xxwidgets_resources_*`. The browser composes the existing
RESOURCESVIEWER with copied metadata, source/visible counts, type/name/language
filtering, numeric-ID/code-page details and deferred TSV copying. It owns no
parser/device and performs no I/O. Source and visible rows are separate owned
copies; metadata/filter/buttons/table replacement is transactional. The modal
adds resize handling, Close/Esc and Ctrl/Meta+F and restores the owner on exit.

## Symbols

Symbols uses `app/cdie_symbols.c` for a detached `xx_symbol_record` snapshot.
PE and ELF use native StreamSymbols cursors; PE returns primary COFF entries
with auxiliary metadata, and ELF traverses native symbol tables. Mach-O's
native inspector supplies nlist entries plus trie-only exports. The adapter
preserves native values, table/record locations, type, binding, section,
auxiliary count and status flags; it maps Mach-O target addresses to physical
segments when possible. Absolute/debug targets are not treated as file offsets.
Trie-only entries use unknown table index/record offset.

Names and source filenames use bounded UTF-8 validation/escaping, with all
snapshot strings owned independently of cursors. Bounds, cancellation and
native errors discard the partial model. `gui/gui_symbols.c` publishes a
completed worker result to the reusable `xxwidgets_symbols_*` browser/modal.
It composes SYMBOLSVIEWER with copied metadata, name/filename filtering,
selection details and deferred TSV copying. Full typed fields are exported
even when table columns are clipped; the widget performs no file I/O and
retains no parser/device. No DataStructs are requested.

## Quick start

For a Git checkout, initialize the bundled dependencies first:

```bash
git submodule update --init --recursive
```

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release
cmake --build cdie_build
```

A portable Windows package contains `die.exe`, `diec.exe`, the database
updater `die-update.exe`, an empty `PORTABLE` marker beside them, the
database, docs, and decoder license notices:

```bat
packaging\windows\build_portable_windows.cmd x64 win64_msvc2022
```

The macOS packages come in pairs per architecture, `cdie_mac_arm64_*` and
`cdie_mac_x64_*`: a `.zip` holding `die.app` and a `.pkg` that installs it
into `/Applications` (`MACOS_ARCHITECTURES='arm64;x86_64'` makes one
universal pair instead). No signing certificate or password is needed;
without them `die.app` is sealed ad hoc and nothing is notarized. See
[docs/BUILDING.md](docs/BUILDING.md#macos-zip-and-installer):

```bash
CDIE_DATABASE_DIR=/path/to/Detect-It-Easy packaging/macos/build_macos.sh
```

```bash
diec -D /path/to/Detect-It-Easy/db /path/to/target.exe
```

Example, matching the original Qt `diec` byte for byte:

```text
PE64
    Linker: Microsoft Linker(14.50.35225)
    Compiler: Microsoft Visual C/C++(19.50.35225)[LTCG/C]
    Tool: Microsoft Visual Studio(2026, 18.0-18.3)
    Sign tool: Windows Authenticode(2.0)[PKCS #7]
    Debug data: Records[CodeView, VC Feature, POGO]
```

## Frontends

Windows and Linux builds provide the native GUI `die`, the console scanner
`diec` and the signature database updater `die-update` (`die.exe`, `diec.exe`
and `die-update.exe` on Windows). macOS builds provide only the `die.app` GUI,
which carries `die-update` in `Contents/MacOS`. The TUI application is
disabled on every platform. `die-update` and the GUIs' **Update database**
need the `xxnetlib` and `xxgithub` sources; see
[docs/BUILDING.md](docs/BUILDING.md#packaging).

Every GUI has an **Update database** button (Ctrl+U; Cmd+U and an app-menu
item on macOS) that checks for and installs a newer signature database; see
[Updating the signature database](#updating-the-signature-database).

The CMake target names remain `cdie_gui` and `cdie` for build-script compatibility.
Linux GUI builds require the GTK 3 development package.

## macOS GUI

The macOS build produces only `src/gui/die.app`. Open the app in Finder,
choose a file with **Open** (Cmd+O), or drop a file onto its window or Dock icon.
Window drops use the shared xxwidgets file-drop event on macOS and Windows,
including over the result tree and path field. A drop starts scanning the first
file; if several files are dropped together, the remaining files are ignored.
Press **Scan** (Cmd+R or F5), select another file type to rescan, or open
**Report** for the complete engine output. The native Cocoa front end reuses
the xxwidgets scan panel, result tree, scan options and optimization dialogs.

The result tree starts with the detected format and shows one compact
`Type: Name (Version) [Info]` row per detection. Heuristic markers and category
colors are preserved. Overlay scans appear as expandable children with their
format, hexadecimal offset and size, followed by their own detections. Windows
uses the same tree model; xxwidgets also renders it in GTK and terminal UIs.
Category colors come from the C port of XScanEngine in
`xxfclib/src/scan/xx_scan_color.c`, with its light/dark defaults, protection
aliases and malware background colors.

The app bundles the main and custom signature databases (`db` and
`db_custom`) from `CDIE_DATABASE_DIR` and finds them independently of the
working directory. The database folders on the **Options > Scan** page can
load other signature folders; successful choices are remembered for later runs.
See [docs/BUILDING.md](docs/BUILDING.md#macos-gui) for build and smoke-test commands.

## Windows GUI

`die.exe` uses the reusable xxwidgets scan panel and options dialogs.
Choose or drop a file, then select a file type to scan it again with that
parser. **Scan options** edits the flags and the custom database; **Fonts**
chooses fonts for controls, tables, result trees and text. OK applies these
choices for the current run; Cancel preserves the previous values.

**Optimization** sets the general memory buffer and file I/O buffer sizes
through value comboboxes, including 1 KiB, 2 KiB, 4 KiB and 8 KiB. It also
controls SSE2, SSE4.2, AVX2, AES-NI, SHA-NI, VPCLMULQDQ, Arm NEON, Arm CRC32,
Arm AES/SHA crypto and Arm Dot Product acceleration preferences.
SHA-NI applies to SHA-1 and SHA-256; VPCLMULQDQ applies to CRC64 when AVX2
is also enabled. On Arm builds the six x86 controls are hidden, leaving only
the four Arm controls. On x86 and x64 builds the four Arm controls are hidden,
leaving only the six x86 controls. A visible switch is disabled when the build,
CPU or OS does not support it. The four Arm switches expose hardware-gated runtime preferences;
scan algorithms do not consume them yet. **Defaults** restores both buffers to
64 KiB and enables every acceleration option supported by the current CPU and
operating system. OK updates xxfclib's runtime settings for subsequent scans;
Cancel keeps the settings that were active before the dialog opened.

**Context menu** enables or removes the current-user Explorer entry
**Scan with cdie**, following XOptions' quoted executable and file-argument
registration. Explorer settings remain registered until disabled. A file
argument also loads and scans directly:

```bat
die.exe "C:\path with spaces\sample.exe"
```

---

## Command line

```text
diec [options] target
```

The common xxfclib terminal options enable or disable acceleration preferences
before scanning. X86 builds expose SSE2, SSE4.2, AVX2, AES-NI, SHA-NI, and
VPCLMULQDQ. Arm builds expose NEON, CRC32, AES/SHA crypto, and dot-product.
Options for the other architecture are hidden and rejected. Supported features
are on by default; switches for a CPU or OS feature unavailable on the current
machine are accepted and ignored. The last switch for each feature wins.

Disable every x86 preference with:

```bash
diec --nosse2 --nosse42 --noavx2 --noaes --nosha --novpclmulqdq -D /path/to/Detect-It-Easy/db /path/to/target.exe
```

Disable every Arm preference with:

```bash
diec --noneon --noarmcrc32 --noarmcrypto --noarmdotprod --noarmpmull -D /path/to/Detect-It-Easy/db /path/to/target.exe
```

Hyphenated `--no-*` spellings are accepted. Arm features may be enabled with
`--arm-crc32`, `--arm-crypto`, `--arm-dotprod`, and `--arm-pmull`; SSE4.2 also accepts
`--sse4.2`, `--nosse4.2`, and `--no-sse4.2`.

Use `--` before a target whose name starts with a dash. Normal terminal output
colors each detection using XScanEngine's category rules and console palette.
`--nocolor` disables colors; `--color` enables them again. Redirected output,
`--plaintext`, JSON, XML, CSV and TSV remain plain. ANSI terminals and native
Windows consoles are supported, and colors are restored after every row.

The short options are the ones `diec` uses, so existing command lines work
unchanged. Note that several are not the mnemonic you would guess — `-a` is
`--alltypes`, not aggressive scan, and `-p` is `--plaintext`, not profiling.
Where `cdie` previously used a different letter, that spelling is still
accepted as an alias (shown in parentheses) except where it would collide.

| Option | Meaning |
| --- | --- |
| `-h`, `-?`, `--help` | show usage |
| `-v`, `--version` | show the version |
| `-r`, `--recursivescan` | descend into directories; as in Detect It Easy, also scans resources and overlays |
| `-O`, `--overlayscan` / `-R`, `--resourcesscan` / `-A`, `--archivesscan` | scan overlays / resources / archive members |
| `-d`, `--deepscan` | enable deep-scan signatures (`DS.*`, `EP.*`) |
| `-u` (`-he`), `--heuristicscan` | enable heuristic signatures (`HEUR.*`) |
| `-g`, `--aggressivecscan` | aggressive scan (`--aggressivescan` also accepted) |
| `-b` (`-V`), `--verbose` | verbose detections (operation system, language, …) |
| `-a`, `--alltypes` | scan applicable parent file types |
| `-f`, `--format` | put a space before `(version)` and `[info]` |
| `-U` (`-hu`), `--hideunknown` | drop `Unknown: Unknown` results |
| `-M` (`-m`), `--messages` | print engine/script messages on stderr |
| `-l`, `--profiling` | enable script profiling flags |
| `-j`, `--json` | JSON output |
| `-x`, `--xml` | XML output |
| `-c`, `--csv` / `-t`, `--tsv` | delimited output |
| `-p` (`-P`), `--plaintext` | plain text output |
| `--format <layout>` | `text`, `json`, `xml`, `csv` or `tsv`, as in Detect It Easy (a bare `--format` still means `-f`) |
| `-D`, `--database <path>` | main database directory |
| `-C`, `--customdatabase <path>` | custom database directory |
| `-s`, `--showdatabase` | print database paths and signature counts |
| `--checkupdate` | check for a database update by running `die-update --check` (long form only: `-u` is `--heuristicscan`) |
| `--update` | download and install a database update by running `die-update` |

`-E` and `--extradatabase` were removed: cdie uses only the main (`-D`) and
custom (`-C`) databases. Either option is refused with exit code `4`; its path
argument is consumed and nothing is scanned.

Output formats are mutually exclusive and may be selected only once. JSON and
XML remain single documents when several files are scanned; the multi-file
form records each path with its scan result. They also cover version/help,
database reports and database TAR creation without mixing plain text into
standard output.

`--test` and `--createtest` are not ported; see
[docs/LIMITATIONS.md](docs/LIMITATIONS.md).

When a database path is not given, `diec` looks for each of `db` and
`db_custom` first in the per-user folder that `die-update` fills when the
shipped database is read-only (never in a portable installation), then next
to the executable (a folder or `<name>.tar`), and then in the working
directory. A `db_extra` folder is never loaded.

Exit codes follow the original tool: `0` success, `1` file not found,
`2` cannot open file, `3` database not found, `4` invalid parameter. This port
also returns `5` when writing or flushing standard output fails. With
`--checkupdate` or `--update`, `diec` returns `die-update`'s exit code (see
[below](#updating-the-signature-database)), and `127` when `die-update` cannot
be found or started.

## Updating the signature database

```text
die-update [options]            install a newer database when the source has one
die-update --check [options]    only report whether there is one
diec --checkupdate | --update   the same, through diec
```

`diec --checkupdate` and `diec --update` start `die-update` from `diec`'s own
folder (the `CDIE_UPDATER` environment variable can name another executable),
pass on `-D` and `-j`, and cannot be combined with a target or another mode.
With `-E` or `--extradatabase`, `diec` exits with code `4` and does not start
`die-update`.

| Option | Meaning |
| --- | --- |
| `-h`, `--help` / `-v`, `--version` | show usage / the version |
| `-c`, `--check` | only check; exit code `1` when an update is available |
| `-u`, `--update` | install an available update (the default) |
| `-f`, `--force` | install even when the database is up to date |
| `-D`, `--database <path>` | the `db` folder to update |
| `-U`, `--url <url>` | the update source |
| `-s`, `--settings <ini>` | read the settings from this INI file |
| `--nosettings` | ignore the stored settings |
| `-j`, `--json` | result as JSON |
| `-q`, `--quiet` | print errors only |

`-E` and `--extradatabase` were removed; they exit with code `4` before any
settings, lock or network access.

| Exit code | Meaning |
| --- | --- |
| `0` | up to date, or the update was installed |
| `1` | an update is available (`--check`) |
| `2` | the source could not be reached or read |
| `3` | the database folder cannot be written or replaced |
| `4` | invalid parameter, URL or settings |
| `5` | verification failed (size, MD5, zip or database) |
| `6` | cancelled; Ctrl+C stops the update cleanly |
| `7` | another update is running |

**Settings.** `die-update` reads Detect It Easy's own settings, the store the
cdie GUI uses too: `die.ini` beside the executable when a `PORTABLE` (or
`portable`) file sits there, otherwise the native `NTInfo/die` store (the
registry key `HKCU\Software\NTInfo\die` on Windows,
`~/.config/NTInfo/die.conf` on Linux, the `biz.ntinfo.die` preferences domain
on macOS, which is what Qt names it after Detect It Easy's organization
domain `ntinfo.biz`). It uses two keys, and the command line wins over them:

| Key | Meaning |
| --- | --- |
| `Scan/Die/Database/Main/Path` | the `db` folder; a leading `$data` is the executable's folder |
| `Scan/Die/Database/Main/UpdateUrl` | the update source |

```ini
[Scan]
Die\Database\Main\Path=$data/db
```

A stored `Scan/Die/Database/Extra/Path` (written by Detect It Easy or an
earlier cdie) is ignored and left in place. A `die.ini` that the settings
parser rejects is still searched for these two keys.

**Source.** The default is
`https://github.com/horsicq/Detect-It-Easy/releases/tag/current-database`:
its `db.zip` asset holds `db/` (other folders in it, such as `db_extra/`, are
ignored), and `db.zip.md5` its checksum. An empty URL and the old default
`.../releases/tag/db` mean the default. `-U` also takes a GitHub or GitHub
Enterprise release page
(`/<owner>/<repo>/releases/tag/<tag>` or `/releases/latest`), a release
download URL, or any `http(s)` URL of a zip, with an optional `<url>.md5`
beside it. Release details come from the GitHub API; when it is rate limited
(60 requests an hour without a token), a tagged release falls back to its
direct download addresses. Release assets are always downloaded from those
browser addresses, which do not count against the limit.

**Detecting an update.** Each installation records its archive in
`db/die-update.ini`. The remote archive is compared with that record by MD5
when the source publishes one, otherwise by the GitHub asset's id, size and
upload time; a database without the record always has an update available.
See [docs/DATABASE.md](docs/DATABASE.md#updating-the-database).

**Where the database goes.** Without `-D` or a stored path, `die-update`
updates the folder the scanners use: an existing per-user copy, else `db`
beside the executable, else (macOS) the app bundle's `Contents/Resources`,
else `db` in the working directory. `db_custom` and any `db_extra` folder are
never touched. The default database of a read-only installation -- an
installed package (`/usr`, `/usr/local`), an
AppImage, a macOS app bundle -- is never updated in place, even by root: the
update goes to the per-user folder, which `diec` and the GUIs search first.
So does an update of any other default folder that turns out not to be
writable, unless the installation is portable. The per-user folder is
`%LOCALAPPDATA%\NTInfo\cdie` on Windows,
`~/Library/Application Support/NTInfo/cdie` on macOS,
`$XDG_DATA_HOME/NTInfo/cdie` (default `~/.local/share/NTInfo/cdie`)
elsewhere; `CDIE_USER_DATA_DIR` overrides it (ignored for root, whose home
folder must belong to root). An existing folder is replaced only when it
looks like a signature database (it holds `_init`, `*.sg`, the engine's
file-type folders or die-update's own `die-update.ini`), and never when it is
a drive root, the home folder, the program's folder, the current folder, or a
folder named `db_extra`. On Linux and macOS HTTPS needs the system libcurl,
which is loaded at run time.

**In the GUI.** **Update database** runs the same check and installation on
a worker thread. A progress dialog with Cancel appears after a second; the
databases are reloaded afterwards and the result is shown in the status bar.
The source is the setting `Scan/Die/Database/Main/UpdateUrl`
(Options > Scan), whose default is the `current-database` release above.
A main database folder chosen in the GUI is updated where it is; the custom
database folder is never touched.

**GUI settings.** The GUI keeps its Options in the same store as
`die-update` and Detect It Easy itself (`die.ini` when portable, else native
`NTInfo/die`), under Detect It Easy's key names and in the forms its Qt
settings use: `true`/`false`, decimal numbers, `QFont` strings and
`#rrggbb|#rrggbb` colors, with `$data/db` and `$data/db_custom` for the
default database folders. Options > Scan has two database folders, main and
custom, and one database switch, the custom database (on by default; the
main database is always used). Detect It Easy's
`Scan/Die/Database/Extra/Path` and `Scan/Die/Database/Extra/Enabled` are
neither read nor written, and are never deleted. Only options changed in
cdie are written, so values Detect It Easy stored keep their form; cdie never
writes `View/Style`, `View/Qss`, `File/Context` or Detect It Easy's
`RecentFiles` list (its own history is `cdie/RecentFiles`; Detect It Easy's
list is shown while cdie has none), and a scan engine it lacks (`nfd`, `peid`,
`yara`) counts as `auto` without being overwritten. The language is
`cdie/Lang`: Detect It Easy's `View/Lang` names a `.qm` file, so cdie changes
it only to `System` or English (`""`), never to a `.po` path. Fonts always
carry a family Qt can resolve, and cdie's colors are opaque (`#aarrggbb`
values are read blended over white); Detect It Easy's one color per scan
category is cdie's light-theme color, the dark theme's stay in
`Scan/Color/Dark`. On the first start, settings an older cdie saved in
`cdie.ini` or `NTInfo/cdie` fill whatever the store does not hold yet, once:
both stores get `cdie/Migrated`, so resetting Detect It Easy's settings does
not bring them back, and the old settings are otherwise left as they were.
`CDIE_SETTINGS=<file.ini>` names another INI file instead, which is never
migrated into.

**Upgrading from an earlier version.** cdie no longer has an extra database;
like Detect It Easy it uses the main and custom databases only. A `db_extra`
folder from an earlier version (beside the executable, in the per-user folder,
or named in the settings) is no longer used: it is neither loaded, updated nor
deleted. Delete it, or move your own `.sg` files to `db_custom`. The former
extra rules are in `db`, tagged `extra` (skip them with `--excludetags extra`).
Until Detect It Easy publishes that merged `db`, the `current-database`
`db.zip` still carries them under `db_extra/`, which `die-update` does not
install. `diec`, `die-update` and `cdie_tui` refuse `-E` and `--extradatabase`
with exit code `4`. An interrupted update from an earlier version that had
moved `db_extra` aside is still put back by the next run.

---

## Library

The build also produces a **shared and a static library** exposing the same C
API as [horsicq/die_library](https://github.com/horsicq/die_library) — the same
`die.h`, the same flags, the same exported functions — so a program written
against die_library (its samples included) builds against this one unchanged.

```text
die_shared -> die.dll  + die.lib      (Windows)   /  libdie.so  (Unix)
die_static -> die_static.lib          (Windows)   /  libdie.a   (Unix)
public header: lib/die.h
```

```c
#include "die.h"
char *r = DIE_ScanFileA("target.exe", DIE_DEEPSCAN | DIE_HEURISTICSCAN, "db");
printf("%s", r);
DIE_FreeMemoryA(r);
```

Exported: `DIE_ScanFile{A,W}`, `DIE_ScanMemory{A,W}`, `DIE_LoadDatabase{A,W}`,
`DIE_ScanFileEx{A,W}`, `DIE_ScanMemoryEx{A,W}`, `DIE_FreeMemory{A,W}` and (on
Windows) `DIE_VB_ScanFile[Callback]`. A worked sample is in
[samples/C](samples/C); see [docs/LIBRARY.md](docs/LIBRARY.md) and
[docs/BUILDING.md](docs/BUILDING.md). Turn it off with `-DCDIE_BUILD_LIBRARY=OFF`.

---

## Documentation

| Document | Contents |
| --- | --- |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | module map and the scan pipeline |
| [docs/JS_ENGINE.md](docs/JS_ENGINE.md) | the JavaScript engine: grammar, objects, memory model, regexps |
| [docs/SCRIPT_API.md](docs/SCRIPT_API.md) | every function the signature scripts can call |
| [docs/DATABASE.md](docs/DATABASE.md) | database layout, load order, priorities, signature syntax, updates |
| [docs/BUILDING.md](docs/BUILDING.md) | building and installing |
| [docs/LIBRARY.md](docs/LIBRARY.md) | the die_library-compatible shared/static library API |
| [docs/TESTING.md](docs/TESTING.md) | comparing against `diec`, tracing, robustness notes |
| [docs/LIMITATIONS.md](docs/LIMITATIONS.md) | what is deliberately simplified |

---

## Layout

```text
cdie_source/
├── CMakeLists.txt
├── release_version.txt
├── README.md
├── LICENSE
├── changelog.txt
├── docs/
├── packaging/
│   └── windows/
│       └── build_portable_windows.cmd
├── run/                      convenience launchers (cmd + sh)
├── tests/                    runtime tests (-DCDIE_BUILD_TESTS=ON)
├── lib/                      die_library-compatible library (die.h + die.c)
├── samples/                  die_library C sample, builds against lib/
├── release/                  built packages (git-ignored)
└── src/
    ├── CMakeLists.txt
    ├── global.h              application constants
    ├── core/                 utils*.c (the only runtime contact point:
    │                         strings, memory, printf, dtoa, libm, startup),
    │                         buffers, vectors, filesystem
    ├── js/                   the JavaScript engine
    ├── format/               binary access, signatures, PE, file types, disasm
    ├── engine/               database, script API, scan driver, output
    ├── console/              command line front end
    └── updater/              die-update and the update engine the GUIs share
```

## License

MIT — see [LICENSE](LICENSE).

ARM64 builds also expose `--armpmull` / `--noarmpmull` (aliases
`--arm-pmull` / `--no-arm-pmull`) and **Use Arm PMULL** in Optimization.
PMULL is enabled when detected. The accelerated CRC32/CRC32C paths require
both Arm CRC32 and PMULL enabled, and apply to buffers of at least 3072 bytes.
Disabling either feature retains the existing fallback paths.
