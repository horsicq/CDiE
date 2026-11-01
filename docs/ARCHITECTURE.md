# Architecture

`cdie` is a straight port of the Detect It Easy console scanner. The original
is C++/Qt and splits into `XBinary`/`XPE` (formats), `XScanEngine` (database
and results), `DiE_Script`/`DiE_ScriptEngine` (script driving) and
`QtScript`/`QJSEngine` (JavaScript). This port keeps the same separation but
implements every layer in C99.

The shared implementation lives in xxfclib. `die_engine_init()` installs its
signature scanner as an `xx_scan_engine`, borrowing the loaded database.
Native clients call `xx_scan_file`, `xx_scan_device`, or `xx_scan_memory` and
release returned results through `xx_scan_free_result()` before engine
cleanup. The console, GUI, and DIE library retain the `die_engine_scan_*`
compatibility wrappers, which transfer results into caller-owned
`xx_scan_result` storage released with `xx_scan_result_free()`.

Every layer reaches the runtime only through `core/utils.h` (the `x_` stubs).
On Windows those stubs are implemented on Win32 alone, so the shipped
executable imports nothing but `KERNEL32.dll` — see
[The runtime layer](#the-runtime-layer).

File Information uses `app/cdie_file_info.c` as a shared synchronous native
DataStructs walker. Its visitor API is used by the console formatters and by
`gui/gui_file_information.c`, which copies a bounded presentation model on a
worker thread. It has no signature-database dependency and only opens the PE,
ELF and thin Mach-O native readers. Progress observation copies worker state
under a mutex; observer stop requests are retained as sticky cancellation.
Partial or cancelled models are discarded before any widgets are updated.

The GUI passes the completed model to `xxwidgets_file_information_dialog`.
The reusable form composes `xxwidgets_data_struct_view` and its field table,
copies its input, handles navigation and resize events, and restores the owner
on close. Parser operations stay in CDIE; xxwidgets remains engine-independent.
CDIE builds with the matching xxwidgets source changes for this component.

Entropy uses `app/cdie_entropy.c` for both console and GUI. It reads checked
64 KiB chunks, builds 64-bit byte histograms and combines block histograms into
the whole-file total. Native memory maps supply physical regions without using
DataStructs. It retains at most 1024 regions, 100 samples and one 256-bin
histogram; incomplete and cancelled results are discarded. Empty files are valid.
`gui/gui_entropy.c` runs it on a cancellable worker, publishes mutex-protected
progress snapshots and adapts the result to `xxwidgets_entropy_statistics`.
`xxwidgets_entropy_set_statistics` validates/copies that snapshot and installs
it transactionally. Its modal uses the existing entropy tables, graph and byte
histogram, keeping all parser and file I/O outside xxwidgets.

Hash uses `app/cdie_hash.c` and the public CRC32, MD5, SHA1 and SHA256
contexts from xxfclib. It fans each checked 64 KiB read into all four contexts
and emits a result only after successful finalization; cancellation is sticky.
The borrowed-device entry point makes short reads and failures independently
testable. It never loads parsers, DataStructs, signature databases or file bytes
into a GUI control. `gui/gui_hash.c` uses the same worker/progress snapshot
pattern as Entropy and adapts results to generic `xxwidgets_hash_entry` rows.
The shared hash browser uses ordinary tree, label and clipboard controls,
validates and copies a bounded model, and offers a resizable modal.

Strings uses `app/cdie_strings.c`, independent of file parsers and databases.
A checked 64 KiB stream feeds one byte/UTF-8 decoder and up to four UTF-16
decoders (both byte orders/alignments). Decoder state survives chunks, including
partial codepoints and surrogate pairs. It records physical ranges and counts
scalars while retaining bounded decoded UTF-8. Automatic wide detection needs
a BOM or a minimum-length ASCII streak; explicit modes can omit that heuristic.
The final model is sorted only after successful completion, with no partial
model on read failures, cancellation or resource limits. All model allocations
use xx_rt and are released with `cdie_strings_free`.

`gui/gui_strings.c` runs this same reader on the established cancellable worker
and adapts results to existing `xxwidgets_string_record` values. The expanded
`xxwidgets_strings_dialog` composes the existing string browser with metadata,
Close/Esc, a full-text View and deferred CSV destination/export actions. The
host owns file writes; the widget stays parser/engine-independent. A text dialog
may now be owned by the current modal, restoring that modal and default action
on every exit. TUI archive rows activate with Enter, matching native behavior.

Hex View uses `gui/gui_hex_view.c` to open an `rb` file device independently
of the scanner/parser. The application owns and closes it after the borrowed
`xxwidgets_hexview_dialog` returns. `xxwidgets_hexview_browser_*` composes the
existing file-backed HEXVIEW and toolbar controls, attaching a 64 KiB device
window. Metadata/device replacement is transactional. Reads preserve the source
cursor and failed reads retain the previous window. Device size changes are
reloaded on Refresh; a shrunken file clamps an out-of-range window to EOF.

The browser queues navigation, display settings, clipboard and dump actions
from callbacks; `process` runs them after polling. Keyboard/context copies use
the same override, bounded to 1 MiB of source bytes (offset-only copies are
exempt). The existing shared byte-dump dialog streams selected ranges. The
modal restores callbacks, default action, owner enabled state and focus; its
borrowed device remains open. No format engine, database or DataStructs is used.

Disasm uses `app/cdie_disasm.c` for read-only device ownership, native PE/ELF/
thin Mach-O detection and address mapping, and the existing cdisasm decoder and
formatter. It never requests DataStructs or a signature database. A page reads
at most 4 KiB plus 15 bytes of lookahead and emits at most 512 rows. Instructions
are clipped at physical mapping boundaries; undecodable bytes advance by one.
The previous native page survives read/allocation failures. Metadata/maps are
cached until close; each successful page read refreshes the file size.

`gui/gui_disasm.c` opens that context on the same cancellable worker/progress
pattern as the other analysis views, then lends a page-provider callback to
`xxwidgets_disasm_browser_*`. The browser validates contiguous physical ranges
and immediately copies rows into the existing `XXWIDGETS_DEBUGGER_DISASM` model.
It owns presentation metadata, page history and toolbar state; device/parser/
decoder ownership stays in CDIE. Reads and clipboard actions run outside input
dispatch. Page replacement is transactional, and failed mode changes restore
the previous selector. `xxwidgets_disasm_dialog` adds resizing, Close/Esc, F5 and
Ctrl/Meta+G, restoring the owner's state before CDIE closes the context.

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

```text
                     ┌──────────────────────────────┐
   command line  ──▶ │ console/main_console.c        │
                     └───────────────┬──────────────┘
                                     │ xx_scan_options
                     ┌───────────────▼──────────────┐
                     │ die_engine/                  │  compatibility entry,
                     │   xx_die_engine_legacy.c     │  caller-owned results
                     └──┬──────────┬────────────┬────┘
                        │          │            │
        ┌───────────────▼──┐  ┌────▼───────┐  ┌─▼──────────────┐
        │ scan/            │  │ die_engine/│  │ scan/          │
        │ xx_scan_db.c     │  │ engine.c   │  │ xx_scan_result.c│
        │ database         │  │ callbacks  │  │ formatting     │
        └──────────────────┘  └────┬───────┘  └────────────────┘
                                   │
                    ┌──────────────▼───────────────┐
                    │ xx_die_engine_scan.c          │
                    │ xx_die_engine_api.c           │
                    │ _init + detect, script API    │
                    └──────────────┬───────────────┘
                                   │
                    ┌──────────────▼───────────────┐
                    │ js/  JavaScript engine        │
                    │  js_lex → js_parse → js_interp│
                    │  js_builtins, js_regexp       │
                    └──────────────┬───────────────┘
                                   │
                    ┌──────────────▼───────────────┐
                    │ format/ xb, xpe, xjpeg, xft,  │
                    │         xdisasm               │
                    └──────────────┬───────────────┘
                                   │
                    ┌──────────────▼───────────────┐
                    │ core/ utils (x_ stubs), cd_*  │
                    │  utils.c   strings, memory,   │
                    │            printf, I/O        │
                    │  utils_fp.c  double <-> text  │
                    │  utils_math.c  libm           │
                    │  utils_entry.c startup, argv  │
                    └──────┬────────────────┬──────┘
                           │                │
              ┌────────────▼─────┐   ┌──────▼─────────────┐
              │ Win32 (KERNEL32) │   │ C standard library │
              │ _WIN32           │   │ everywhere else    │
              └──────────────────┘   └────────────────────┘
```

## Modules

### `core/`

* `utils.[ch]` — **the only contact point with the runtime.** Every standard
  function is reachable through an `x_` prefixed stub: `x_strlen`,
  `x_memcpy`, `x_malloc`, `x_snprintf`, `x_floor`, and so on. `utils.c`,
  `utils_fp.c`, `utils_math.c` and `utils_entry.c` are the only translation
  units that see a platform header; no other file does.

  Three things cannot be functions because they are macros that must expand
  in the caller's frame, so they are exposed as macros instead:

  | Macro | Wraps |
  | --- | --- |
  | `X_VA_LIST`, `X_VA_START`, `X_VA_END`, `X_VA_COPY` | `<stdarg.h>` |
  | `X_OFFSETOF` | `offsetof` |

  `<stdarg.h>` and `<stddef.h>` are compiler headers, not library ones — they
  declare no functions and need nothing linked, so a freestanding build may
  still use them. `<setjmp.h>` deliberately has no wrapper: `setjmp`/`longjmp`
  live in the C runtime, so the parser reports errors with a flag instead of a
  non-local jump.

  Values that are macros in libc become functions: `x_nan()`, `x_inf()`,
  `x_isnan()`. `FILE *` never appears outside `utils.c` — streams are opaque
  `void *` handles from `x_fopen`, `x_stdout()` and `x_stderr()`.

  **Rule for new code: never call a standard function directly.** The single
  exception is `cd_fs.c`, which additionally talks to the platform directory
  APIs (`<windows.h>` / `<dirent.h>`) — those are not standard C and have no
  portable stub.

  The payoff is that retargeting the runtime is a change to a few files rather
  than a sweep over 19 — which is exactly how the Win32-only build below came
  about.

* `utils_fp.c` — `double` ↔ decimal text. See
  [Floating point](#floating-point).
* `utils_math.c` — the `<math.h>` subset the script API needs, computed
  in-house rather than linked.
* `utils_entry.c` — program startup: the linker entry point, `argv`
  construction and the three compiler support routines.
* `cd_common.[ch]` — checked allocation (`cd_malloc` aborts on OOM, and
  routes through `x_malloc`), a growable byte buffer (`CDBuf`), a pointer
  vector (`CDVec`), FNV-1a hashing and ASCII case helpers. Includes
  `utils.h`, which is where `size_t` and `NULL` come from.
* `cd_fs.[ch]` — file reading, directory listing (Win32 `FindFirstFile` and
  POSIX `dirent`), path splitting/joining, executable directory lookup and
  recursive file collection.

### `format/`

* `xb.[ch]` — the binary substrate:
  * `XBFile`: the whole file in memory plus bounds-checked readers
    (`xb_u8` … `xb_f64`, ANSI/UTF-16/UTF-8 string readers).
  * `XBMemoryMap`: a list of regions (`header`, `section`, `overlay`) with
    `offset → address` and `address → offset` translation and `raw size`
    computation (which is what defines the overlay).
  * The **signature engine**: `xb_convert_signature` (DIE syntax → canonical
    hex), `xb_signature_parse` (into `XSigRecord`s) and
    `xb_signature_compare` / `xb_find_signature`.
  * Checksums: entropy, MD5, CRC-32, CRC-16, Adler-32 and the DIE
    "custom CRC-32" used by the import hashes.
* `xpe.[ch]` — the PE parser. Fills one `XPE` struct with the DOS/NT headers,
  data directories, sections, imports (with per-library position hashes),
  export names, the flattened resource tree, the parsed `VS_VERSIONINFO`
  block, the manifest, the debug directory and the Rich header.
  It also parses the **CLI metadata**: the `#Strings`, `#US`, `#Blob` and
  `#GUID` heaps plus the `#~` table stream — heap index widths, coded-index
  widths, per-table row sizes and per-table file offsets — which is what
  `isNetTypePresent`, `isNetMethodPresent`, `isNetFieldPresent`,
  `getNetModuleName` and `getNetAssemblyName` are built on.
* `xjpeg.[ch]` — the JPEG segment walker: JFIF version, `COM` comments, the
  `DQT` hash, `APP*` marker presence, and a small TIFF/IFD reader that
  recovers the EXIF camera make and model.
* `xpng.[ch]` — the PNG chunk walker. Reads `IHDR` (dimensions, bit depth,
  colour type), `pHYs` and `bKGD`, and assembles the description string
  `XPNG::getInfo` produces — `723x464, 8 bits, RGB, pHYs: 3780x3780 meter`.
  Everything else is skipped by chunk length.
* `xapk.[ch]` + `inflate.[ch]` — the APK reader. `xapk` scans the ZIP central
  directory for `AndroidManifest.xml`, and `inflate.c` (a self-contained
  RFC 1951 DEFLATE decoder — no zlib) decompresses it. The manifest is
  Android binary XML, which `xapk` decodes into attribute text so
  `getAndroidManifestRecord("package")` and `("android:versionName")` resolve.
* `xpdf.[ch]` — the PDF reader. It reduces each object to the flat token list
  `XPDF` produces (`handleXpart`), finding objects through classic `xref`
  tables, the deep scan a cross-reference stream needs, and incremental-update
  chains, then answers the key/value queries the database makes: `/Filter`,
  `/Creator`, `/Producer`, `/Author`, the version and the header comment.
  Stream contents are not decompressed — nothing the database asks for lives
  there.
* `xelf.[ch]` — the ELF reader. Header, section table (names resolved through
  `.shstrtab`), program headers, the dynamic library list (`DT_NEEDED` via the
  mapped string table), the `TYPE MACHINE-BITS` general-options string, the
  entry-point file offset (virtual address translated through `PT_LOAD`) and a
  raw-size/overlay estimate. Backs the ELF compiler and library detections.
* `xmach.[ch]` — the Mach-O reader. Walks the load commands for
  `LC_LOAD_DYLIB` libraries (matched by basename), sections (by `sectname`)
  and the `LC_MAIN` entry point.
* `xdex.[ch]` — the Dalvik reader. The version from the magic, the CRC-32 hash
  of the map-item type sequence (the format detail), and the string_ids /
  type_ids tables the obfuscator signatures scan.
* `xft.[ch]` — magic-based file type detection and `xft_check`, the
  equivalent of `XBinary::checkFileType` (so a `PE` database entry matches a
  `PE32`/`PE64` file).
* Disassembly for script rules lives in xxfclib's `die_engine` module. Its
  `xx_die_engine_xdisasm.c` adapter decodes x86 instructions in 16-, 32- and
  64-bit modes and formats Intel text through cdisasm.

### `js/`

See [JS_ENGINE.md](JS_ENGINE.md).

### xxfclib `scan/` and `die_engine/`

* `scan/xx_scan_db.c` — walks the database directory tree, maps each subdirectory to a file
  type, reads every `*.sg` (or extension-less) file and sorts each database by
  `(file type, priority digit, name)`.
* `die_engine/xx_die_engine_engine.c` — installs the native `xx_scan_engine`
  callbacks through `die_engine_init()`. It borrows the database, bounds each
  selected input range, maps native file types to signature types, and owns
  callback results until their engine free callback is called.
* `scan/xx_scan_engine.c` — common detection, callback dispatch, parent-format
  passes, resource, archive and overlay child scans, and result merging for native
  `xx_scan_*` callers. Child scans have distinct scan IDs, retain their parent's
  ID, identify their origin with `scan_part`, and merge offsets into the input.
* `scan/xx_scan_resources.c` — enumerates nonempty PE/.NET resources and ELF
  GResources through the existing format streams, bounded by the selected
  input range. `resources_scan` scans every stored payload once, before the
  overlay pass. Child scans clear all optional traversal flags. Compressed
  GResources are scanned as stored; other file types have no resource pass.
  Resource type names or numeric type IDs reach signature scripts through
  `getScanID()`, and `isResource()` identifies these child scans.
  Malformed ranges, cancellation, allocation failure and more than 65,536
  resource leaves fail the scan and discard partial results.
* `scan/xx_scan_archives.c` — walks archive members for the engine. With
  `archives_scan` the engine expands only ZIP-family inputs (ZIP, JAR, APK,
  IPA), as `XScanEngine` expands ZIP, 7Z, RAR, CAB and ISO 9660 containers;
  GZIP, BZIP2, LHA and Mach-O Fat inputs are scanned unexpanded. (The walker
  still visits LHA members and concatenated GZIP/BZIP2 streams for other
  callers.) Like `XScanEngine`, it decodes or checksums every member it
  reaches, Store members included. A member that fails its CRC, uses a ZIP
  method XZip does not map, does not decode, is encrypted or exceeds a limit
  is skipped; the first two report "Invalid CRC" or "Unknown compression
  method: Unknown", which `xx_scan_engine.c` adds as a "Last error" record to
  the result of the next scan that finishes, as `XScanEngine`'s single error
  string does. Only storage faults, allocation failures and cancellation fail
  the walk; an unreadable later record ends it after the members before it.
  Without `aggressive_scan` a member is passed on only when its first 1,029
  bytes can satisfy `XScanEngine::isScanable` (executables, DEX, PDF,
  archives, TAR); any other member is decoded into a discarding sink for its
  CRC alone, with no buffer, temporary file or type detection. Empty members
  and directories therefore reach only an aggressive scan. A ZIP without end
  records is read from its local headers (Store and Deflate members), and a
  ZIP whose offsets count from the start of leading data is read as XZip
  reads it. Compressed output uses memory only when its decoded size is less
  than `xx_get_file_buffer_size()`. Equality or a larger size uses
  `xx_io_temp_open()`; temporary storage is cleaned up after scanning and
  honors an active memory-only policy. ZIP Store members use bounded
  `xx_io_sub` source views after their CRC is verified. Archive child scans
  use the selected device directly, without copying small inputs into memory
  again. No archive-derived output paths are opened. This storage threshold
  does not bound decoder internal buffers. Defaults are 256 MiB per member,
  no total limit and 100,000 entries; the `archive_max_member_size`,
  `archive_max_total_size` and `archive_max_members` options override these
  when nonzero. Native decoder limits still apply.
  The engine scans at most 21 members per archive (100,000 when aggressive).
  Members keep `archives_scan` and clear the other traversal flags, so nested
  archives expand, down to 20 archive levels (`XX_SCAN_ARCHIVE_MAX_DEPTH`).
  As a deliberate cdie safety limit that `XScanEngine` does not have, one
  input scans at most 10,000 archive members, all levels together (100,000
  when aggressive); reaching it stops the expansion with "Last error: Archive
  member scan limit reached (10000)".
  Records retain the member name, packed extent and compression state.
  Compressed child offsets refer to decoded data; stored child offsets refer
  to the original input. GUI, text and JSON output identify these separately.
* `die_engine/xx_die_engine_legacy.c` — compatibility wrappers for existing
  callers. Default automatic scans preserve the Binary/COM fallback passes;
  explicit file types run one interpretation. Parent-format, resource, archive or overlay flags
  route through the common scanner. The device wrapper restores the borrowed
  cursor, and memory scanning borrows the input until the synchronous call
  returns. Directory recursion remains the console's responsibility.
* `die_engine/xx_die_engine_api.c` — installs the script API. A single dispatcher function keyed by an
  enum implements ~200 methods on the `Binary`/`PE`/`MSDOS` object plus the
  global helpers (`_setResult`, `_isResultPresent`, `_removeResult`,
  `includeScript`, …). It also carries the two lookup tables behind the
  `--verbose` "Operation system" line: the Windows version names and the
  `IMAGE_FILE_HEADER` machine names.
* `die_engine/xx_die_engine_scan.c` — the signature pass driver, mirroring
  `DiE_Script::processDetect`. It connects progress and cooperative cancellation
  to script boundaries and the `_isStop()` script helper.
* `scan/xx_scan_result.c` — type priorities, the type-name table and the
  text/JSON/XML/CSV/TSV formatters. Each reproduces the corresponding DiE
  output exactly (child scans in the structured formats excepted; see
  [LIMITATIONS.md](LIMITATIONS.md)), which for the structured ones means matching quirks of the
  Qt classes that produced them — `QJsonObject` orders keys alphabetically,
  and `QXmlStreamWriter` opens with a blank line.
  `xx_scan_print_text` reuses the same record formatter while writing colored
  spans to the terminal. Category colors come from `scan/xx_scan_color.c`;
  `terminal/xx_terminal_color.c` ports XOptions' nearest-color, ANSI and native
  Windows console mappings. The console flushes buffered headers before direct
  writes and restores attributes/modes before diagnostics or the next file.
  Disabled colors, redirected streams and structured formats remain plain.

### `updater/` and the database update

* `updater/cdie_updater.[ch]` — the update engine, shared by `die-update` and
  the GUIs. `cdie_update_config_load_settings` reads Detect It Easy's
  settings (`die.ini` in a portable installation, else the native `NTInfo/die`
  store). `cdie_update_check` resolves the target folder (`db` only; cdie has
  no extra database), asks the source —
  a GitHub release through xxgithub, or a plain zip URL through xxnetlib —
  and compares it with the `db/die-update.ini` stamp. `cdie_update_install`
  then locks, stages, downloads, checks size and MD5, extracts through
  xxfclib's ZIP reader, loads the new database once with `db_load`, writes
  the stamp and swaps the folder by rename with rollback (see
  [DATABASE.md](DATABASE.md#updating-the-database)). Progress and
  cancellation go through an `xx_pd_struct`, and the result codes are
  `die-update`'s exit codes. The header names xxfclib types only, and
  xxgithub/xxnetlib are linked `PRIVATE`, so their C11 requirement stays on
  this static library.
* `updater/main_updater.c` — `die-update`, the command line over the engine:
  options and stored settings, text and JSON reports, a progress line on
  stderr, and Ctrl+C (SIGINT/SIGTERM on POSIX) turned into a cooperative
  stop. It reuses cdie's entry point but is a hosted program: the network
  backends need the C runtime.
* `gui/gui_update.[ch]` — **Update database** for the Windows, GTK and Cocoa
  frontends: `cdie_update_install` on a worker thread behind xxwidgets'
  cancellable process dialog, then a reload of the backend's databases.
  Default folders are resolved again (the update may have created the
  per-user copy); folders chosen in the GUI are updated and reloaded where
  they are. The URL is the GUI's own `Scan/Die/Database/Main/UpdateUrl`.
* `app/cdie_paths.[ch]` — where an unconfigured database lives, shared by
  `diec`, the GUIs and `die-update` so that an update lands where the
  scanners look: the per-user copy (never in a portable installation), beside
  the executable, the macOS bundle's `Contents/Resources`, the current
  directory. `cdie_user_data_dir()` names the per-user folder, which
  `CDIE_USER_DATA_DIR` overrides. CRT-free safe: xx_rt plus
  `GetEnvironmentVariableW`.
* `app/cdie_spawn.[ch]` — `diec` only. `--checkupdate` and `--update` run
  `die-update` (from `diec`'s folder, or `CDIE_UPDATER`) in the foreground
  with `diec`'s console, standard handles, working directory and
  environment, and return its exit code, or 127 when it cannot be started.
  On Windows the three standard handles are duplicated as inheritable and a
  `PROC_THREAD_ATTRIBUTE_HANDLE_LIST` keeps every other handle out of the
  child; a console control handler keeps `diec` waiting while Ctrl+C stops
  the child cleanly. POSIX uses `posix_spawn` and, like `system()`, ignores
  SIGINT and SIGQUIT until the child exits.

**Why `diec` starts a separate program.** `diec` is CRT-free and imports
only `KERNEL32.dll` ([The runtime layer](#the-runtime-layer)). An HTTPS
client cannot be: WinHTTP is a DLL of its own, libcurl is loaded at run time,
and xxnetlib and xxgithub are hosted C11 libraries. Keeping the network code
in `die-update` costs `diec` only the KERNEL32 process functions in the
import table below, and leaves a scanner that opens no connection unless an
update is asked for.

**How the network libraries link.** xxnetlib links `xxfclib::xxfclib`
unless its cache variable `XXNETLIB_XXFCLIB_TARGET` names another target.
cdie sets it to `xxfclib::die_engine`, the archive it already builds, which
holds every module xxnetlib uses, so `die-update` and the GUIs do not link a
second archive compiled from the same sources. xxnetlib and xxgithub are
added `EXCLUDE_FROM_ALL` with their tests, tools and install rules off
(xxnetlib's local test server is kept when cdie's tests are built). With the
CRT-free console, `die_engine` uses the static runtime, and the network
libraries and updater targets are switched to it too. All of this needs
`CDIE_BUILD_UPDATER` (ON by default), CMake 3.20 and the xxnetlib and
xxgithub sources (`dep/<lib>`, then `../_mylibs/<lib>`, or
`CDIE_XXNETLIB_DIR`/`CDIE_XXGITHUB_DIR`); without them CMake warns and
builds neither `die-update` nor **Update database**.

## The runtime layer

Because every call site already goes through `utils.h`, the implementation
behind it is free to change. It does, on one axis: `#if defined(_WIN32)`.

**On Windows every stub is Win32.** Not "mostly Win32" — the C standard
library is not called at all, and with the default MSVC 64-bit build it is not
even linked. `x_strlen` is `lstrlenA`, `x_malloc` is `HeapAlloc` on
`GetProcessHeap()`, `x_fopen` is `CreateFileW` and the returned `void *` is a
`HANDLE`, `x_printf` formats into a buffer and hands it to `WriteFile` on
`GetStdHandle(STD_OUTPUT_HANDLE)`. The current Release console image imports 71
`KERNEL32.dll` symbols on x64; the ARM64 image adds
`IsProcessorFeaturePresent` for CPU capability detection. The import set is:

| Area | Functions |
| --- | --- |
| Memory | `GetProcessHeap`, `HeapAlloc`, `HeapReAlloc`, `HeapFree` |
| Files and metadata | `CreateFileW`, `ReadFile`, `SetFilePointerEx`, `SetEndOfFile`, `CloseHandle`, `GetFileAttributesW`, `DeleteFileW`, `GetFileInformationByHandle`, `GetFileSizeEx`, `GetFileType`, `LocalFileTimeToFileTime`, `SetFileAttributesW`, `SetFileTime`, `DosDateTimeToFileTime`, `MoveFileExW` |
| Console | `GetStdHandle`, `WriteFile`, `FlushFileBuffers`, `GetConsoleMode`, `SetConsoleMode`, `GetConsoleScreenBufferInfo`, `SetConsoleTextAttribute`, `SetConsoleCtrlHandler` |
| Directories and paths | `FindFirstFileW`, `FindNextFileW`, `FindClose`, `GetModuleFileNameW`, `CreateDirectoryW`, `GetFullPathNameW`, `RemoveDirectoryW`, `GetTempPathW` |
| Process, timing, errors and threads | `GetCommandLineW`, `GetEnvironmentVariableA`, `GetEnvironmentVariableW`, `ExitProcess`, `GetTickCount`, `QueryPerformanceCounter`, `QueryPerformanceFrequency`, `GetLastError`, `SetLastError`, `FormatMessageW`, `GetCurrentThreadId` |
| Running `die-update` | `CreateProcessW`, `GetCurrentProcess`, `DuplicateHandle`, `InitializeProcThreadAttributeList`, `UpdateProcThreadAttribute`, `DeleteProcThreadAttributeList`, `WaitForSingleObject`, `GetExitCodeProcess` |
| Synchronization | `InitializeCriticalSectionAndSpinCount`, `EnterCriticalSection`, `LeaveCriticalSection`, `DeleteCriticalSection` |
| Thread-local storage | `TlsAlloc`, `TlsGetValue`, `TlsSetValue`, `TlsFree` |
| Dynamic loading | `LoadLibraryW`, `GetModuleHandleW`, `GetProcAddress`, `FreeLibrary` |
| Strings | `lstrlenA`, `MultiByteToWideChar`, `WideCharToMultiByte`, `CompareStringOrdinal`, `LCMapStringEx` |
| ARM64 CPU features | `IsProcessorFeaturePresent` |

`app/cdie_spawn.c` (`--checkupdate`, `--update`) accounts for the
**Running `die-update`** row plus `SetConsoleCtrlHandler` and
`FormatMessageW`; `GetEnvironmentVariableW` serves it and
`app/cdie_paths.c`, which read path-valued variables without the ANSI code
page's losses.

The file and directory calls are the **wide** forms, and the program is driven
by `GetCommandLineW`, because a `CreateFileA` build cannot even name a file
outside the process ANSI code page — a Cyrillic path arrives as a row of `?`
and the open fails. Paths are UTF-8 everywhere inside the program;
`MultiByteToWideChar` / `WideCharToMultiByte` convert at the Win32 boundary.
The command line is parsed as UTF-16 and each argument converted to UTF-8
before `x_main` sees it.

`lstrcmpA` is conspicuously absent from that list, and on purpose: it is
locale-aware. It would reorder the signature database and change the result of
every JavaScript relational operator on strings. `x_strcmp` is a byte-exact
loop instead.

Everywhere else the same stubs forward to `<string.h>`, `<stdlib.h>`,
`<stdio.h>` and `<math.h>`, exactly as before.

### Entry point

`main` does not exist. `console/main_console.c` defines `x_main`, and what
reaches it depends on the build:

* **CRT-free** (`CDIE_NO_CRT`, the MSVC x64/ARM64 default) — the linker entry is
  `x_entry_point` in `utils_entry.c`. It calls `x_startup`, which splits
  `GetCommandLineW()` into `argv` using the documented Microsoft quoting
  rules, converts each argument to UTF-8, calls `x_main` and passes the result
  to `ExitProcess`.

  The split between the two functions is not cosmetic. On x64 the call into
  `x_startup` establishes the call-relative stack alignment expected by
  compiled code; on ARM64 it preserves the ABI's 16-byte SP alignment.

* **Hosted** — a one-line `main` forwards to `x_main`.

The CRT-free entry object supplies `memset`, `memcpy`, `memmove`, `memcmp`,
`strlen`, `abs`, and `labs`, plus the `_fltused` marker MSVC references from
every object file that touches floating point. Some are emitted by the
compiler for structure initialisation and large copies; the rest cover the few
standard-library calls retained in dependency code, including Debug builds.

They are compiled with `#pragma optimize("", off)`. `#pragma function` stops
the compiler substituting an intrinsic for an explicit call, but it does *not*
stop loop-idiom recognition: at `/O2` a plain byte loop that fills memory is
itself rewritten into `call memset`. Inside the definition of `memset` that is
infinite recursion, and it presents as a stack overflow on the first call.

The freestanding xxfclib build supplies the architecture-specific compiler
helpers that would otherwise come from the runtime. Its x64 and ARM64
`__chkstk` implementations touch each page before a large stack allocation;
the ARM64 helper library also supplies the 32-bit interlocked operations MSVC
emits as out-of-line calls. `x_startup` still heap-allocates `argv`, and the
floating point bignums remain compact, to avoid unnecessary stack pressure.

### Floating point

The database compares version numbers, and the script API exposes
`Number.prototype.toFixed`, so `double` ↔ text has to be correct, not
approximate. Rounding `14.50` to `14.32` is a wrong answer, not a rounding
artefact. `utils_fp.c` therefore carries a small bignum (48 limbs) and uses
it for exact arithmetic:

* `x_strtod` — decimal → `double`. Builds the exact rational the literal
  denotes, normalises it so the denominator sits just below the numerator,
  then extracts 54 bits by long division and rounds to nearest-even with a
  sticky bit. Correctly rounded for every input.
* `x_dtoa_shortest` — `double` → the shortest decimal that reads back
  identically (Steele & White / Dragon4, with the boundary asymmetry at
  powers of two handled). This is what `String(number)` uses.
* `x_dtoa_fixed`, `x_dtoa_precision` — the counted-digit variants behind
  `toFixed` and `toPrecision`, including the case where the value rounds up
  into a place it has no digits for, which is what makes `(0.5).toFixed(0)`
  come out as `"1"`.

`x_snprintf` has **no floating-point conversions at all** — no `%f`, no `%g`.
Nothing in the port needs them, and leaving them out keeps the formatter free
of the bignum. Numbers reach text through the three functions above.

`utils_math.c` supplies the rest. `x_sqrt` uses the hardware wherever the
compiler exposes it without a library call — an SSE2 intrinsic on 64-bit
MSVC, `__builtin_sqrt` on GCC and Clang — and falls back to Newton-Raphson
elsewhere. That fallback ends with one step carried out in double-double
precision, using Dekker's splitting to compute the residual `v - g²` exactly:
plain Newton converges to a fixed point that can sit 1 ULP off, because every
iteration rounds.

`x_log` and `x_exp` use the fdlibm minimax polynomials and the trigonometric
functions are argument reduction on top of the fdlibm kernels. Accuracy there
is within about 1 ULP rather than correctly rounded;
see [LIMITATIONS.md](LIMITATIONS.md#runtime).

## Scan pipeline

1. **Open** the input with `xx_io` and prepare a bounded read-only view of the
   selected range. Small files are buffered; larger files use bounded windows.
2. **Detect** native file types with `xx_scan_detect_type()` and dispatch the
   preferred type through the engine's callbacks. The compatibility default
   path keeps the reference's Binary/COM fallback order. A .NET PE appears as
   `Binary`, `MSDOS`, `PE`, its actual `PE32`/`PE64` width, and `DOTNET`.
   The DIE callback retains that PE width and enables the CLI-assembly flag
   (`XFT_CLI_ASSEMBLY`) for the DOTNET binding below. A ZIP is typed ZIP only
   when it passes a port of `XZip::isValid` (every central entry agrees with
   its local header and owns its bytes); otherwise it is scanned as Binary.
3. **Parse** the format with xxfclib's native inspection readers and build the
   memory map. Other inputs get a single flat `Data` region. An MS-DOS image
   is mapped as `XMSDOS` maps it: module base `0x10000000`, entry point at that
   base plus CS:IP (wrapped at 1 MiB), so `getAddressOfEntryPoint()` returns
   `0x10000020` for CS:IP 0000:0020, as DIE 3.10 does.
4. **Create** a JavaScript context and install the API. The format object is
   bound as `Binary`, the format name (`PE`, `ELF`, …) and — for a .NET PE —
   `DOTNET`.
5. **Run** the global `_init` script (file type "unknown"), then the file-type
   `_init` script, then (for a .NET PE) the `PE/DOTNET/_init`. These define
   `meta()`, `result()`, the `String.prototype` helpers and the `PE.section` /
   `PE.resource` arrays.
6. **Iterate** the signature list in database order. A signature runs when
   * its file type matches (`xft_check`) — or, for a `PE/DOTNET`
     (`XFT_CLI_ASSEMBLY`) script, the file is a .NET PE, and
   * it is not a `DS.*`/`EP.*` script while deep scan is off, and
   * it is not a `HEUR.*` script while heuristic scan is off, and
   * it is not named `_init`, and
   * its database is enabled.

   Each script is evaluated in the global scope and then its `detect()`
   function is called with `(show_type, show_version, show_info)`.
7. **Collect** results through `_setResult`. Duplicates suppressed by
   `_removeResult` are remembered in a blacklist, exactly as
   `DiE_ScriptEngine::_setResultSlot` does.
8. **Sort** the results by type priority with a stable sort, so equal
   priorities keep insertion order.
9. **Format** and print.

## Why the output matches

Three details decide whether the printed lines are identical to `diec`:

* **Signature order.** Each database (main, custom) is sorted
  independently and appended after the previous one — the reference engine
  never re-sorts the combined list. Scripts that check
  `_isResultPresent(...)` before adding a record therefore see the same state.
* **Result order.** `XScanEngine::sortRecords` sorts only by priority; for the
  small lists a single scan produces this behaves like a stable sort, which is
  what `cdie` implements explicitly.
* **String construction.** `createResultStringEx` is reproduced verbatim:
  optional `(Heur)`/`(A-Heur)` prefix, `Type: `, name, `(version)`,
  `[info]`, with the optional space controlled by `--format`.
