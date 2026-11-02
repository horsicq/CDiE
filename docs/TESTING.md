# Testing

The acceptance criterion for this port is simple: for the same input file and
the same databases, this port's `diec` must print exactly what the original
Qt `diec` prints. Use separate executable paths when comparing them.

## Reference comparison

### Public compatibility tests

The public suite requires Python 3.8+ and generates its own original PE,
resource, overlay, PNG, ZIP, GZIP and BZIP2 fixtures plus a minimal signature
database. No private samples or installed signature database are needed.

```bash
cmake -S . -B build -DCDIE_BUILD_TESTS=ON -DCDIE_BUILD_LIBRARY=OFF
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

`cdie.compatibility` exercises console defaults, `-a` and clustered aliases,
all four expansion modes, combinations, stored versus decoded archive offsets,
directory recursion, failures and JSON/XML/CSV/TSV/plaintext output. Its
archive cases pin what `diec` 4.0.0 prints (see *Archive scanning tests*
below). It also compiles the real GUI backend without a desktop toolkit and
checks mode changes, separate format groups, parent-before-child tree ordering
and cancellation.

`cdie.database_tar` scans with a source database and its precompiled TAR and
expects the same result. It also checks that `-E`/`--extradatabase` exit 4
(lowercase `-e` is still entropy), that `--showdatabase` names only the main
and custom databases in text, JSON and XML, and that a `db_extra` beside `db`
is never loaded (the same rule in `db_custom` is the positive control).
Its `BufferSizeOptionTests` cover `--buffersize` and `--filebuffersize`:
byte counts and `K`/`M`/`G` suffixes in either case from 1 KiB to 64 MiB leave
scans (JSON, plain text, archive members, nested archives), `--hash`,
`--entropy`, `--strings` and `--info` byte-identical; the top of the range
(1 GiB, or 64 MiB when `diec` is a 32-bit PE or ELF file) is accepted with
`--version`, which opens no file, so no test allocates a 1 GiB buffer; `0`,
an empty or missing value, `1023`, one byte over the top, `2G`, a 20-digit
number, `12X`, `64KB`, `-5`, `1 M` and other
malformed values exit 4 with the option's message and nothing on stdout; the
last of repeated options wins. The analysis modes give the same output
whatever either size, so they show only that a size is accepted, not that it
is applied before them (for `--buffersize`, the `read_buffer` suite below
shows that), and the `G`
multiplier is checked only through the range (on a 64-bit `diec`, `1G` is
accepted, `2G` and 1 GiB + 1 refused). With `TMP`, `TEMP` and `TMPDIR` naming a
missing folder, a deflated 1 MiB PE member is scanned with
`--filebuffersize 2M` or `1025K` (decoded in memory) and, on Windows only,
skipped without a message at the 64 KiB default and at `1M` (no temporary
file); glibc's `tmpfile()` falls back to `/tmp`, so POSIX hosts check only the
first half.

File Information coverage uses independently generated 32/64-bit PE and
little-/big-endian ELF and Mach-O files. It verifies native header values,
structure/field offsets, repeated table entries, inventory counts, catalog
scope, field selectors, argument errors, malformed native tables, output
escaping and multi-file documents. These tests deliberately point at a missing
signature database to prove that native information commands do not load it.
Native DataStructs output has a separate documented schema; the `diec`
differential suite below continues to cover signature-scan output.

When xxwidgets is built, `cdie_test_file_information` compares the GUI model
with console DataStructs for all generated executable fixtures, including
absolute field offsets. Shared-view tests destroy the source model before
navigating the copied widget data, verify failed replacement preserves the
old view, and check typed ENTRY fields. A POSIX PTY test opens the actual shared
dialog, navigates and closes it with Escape. Both pre-cancelled and mid-read
observer cancellation return no partial model. Windows' native smoke test
opens File Information and checks for populated field-table rows; Cocoa's
smoke test exercises the shared form and native model in its actual window.

To compare with a reference executable, configure
`-DCDIE_REFERENCE_EXECUTABLE=/absolute/path/to/diec`. This enables
`cdie.reference`, comparing the same generated corpus and isolated databases
in both default and all-types modes. By default every output mode is checked.
Only CRLF is normalized; spaces, trailing newlines, stderr and exit codes
remain significant. Timeouts fail the test.

Reference versions matter. The local 3.21 executable matches this suite's JSON
cases, but has existing differences in XML element shape, trailing newlines
and unknown-result type casing. `-DCDIE_REFERENCE_FORMATS=j` selects the JSON
subset explicitly; it is not a claim of full output compatibility. The default
`p,j,x,c,t` comparison intentionally exposes those differences. Run it directly:

```bash
python3 tests/test_compatibility.py --cdie build/src/console/diec \
  --reference /absolute/path/to/diec --suite reference
```

Compatibility tests, native GUI smoke tests, and corpus comparisons run only
locally. The developer source tree keeps tests/ and private_tests/; these
folders are excluded from publication to GitHub. Run the commands above from
a local tree containing tests/. GitHub Actions builds and packages releases
and checks package contents, but does not run application tests.
Large real-world corpus and decoder parity sweeps remain separate local work.

### Archive scanning tests

`--archivesscan` follows `diec` 4.0.0 built from the current Detect It Easy
source, so use that build as the reference. DIE 3.10 has no
`--archivesscan` (its `-r` expands archives), never checks a member's CRC and
scans every fixture ZIP as Binary; where the two differ, 4.0.0 is followed.
`cdie.compatibility` checks:

* what expands: only ZIP, JAR, APK and IPA members; `image.gz` and
  `image.bz2` stay unexpanded Binary, also with `-g`;
* the member gate: without `-g` a member is scanned only when
  `XScanEngine::isScanable` accepts it (an MZ of any length, TAR, ZIP, GZIP,
  a PDF also behind a BOM, DEX), never a PNG, 7z, BZIP2, zlib, XZ or LHA
  stream; an empty member or a directory needs `-g`;
* nesting: an inner ZIP expands, an archive overlay expands with
  `--overlayscan --archivesscan`, and a 600-level ZIP stops 20 archive levels
  below the input (without that bound a few hundred levels overflowed the
  stack);
* the scan limit: four levels of 21 nested ZIPs ask for 204,204 member scans;
  the scan stops at exactly 10,000 with `Last error: Archive member scan limit
  reached (10000)` and exit code 0. The limit is cdie's own; `diec` has none;
* damaged members, in default and aggressive mode: a bad CRC in a stored or
  deflated member, an unscannable PNG included, prints `Last error: Invalid
  CRC` after the detections, on stdout in text, JSON, XML, CSV and TSV, with
  exit code 0. Two damaged members with a good one between them print two
  lines, two adjacent ones print one, as `diec`'s single error string does.
  The CRC covers the whole decoded stream, so a wrong one is reported whether
  the declared size is smaller or larger, and a right one over another size
  is skipped silently. A Deflate stream that does not decode and an encrypted
  member are skipped silently (a Shrink, Reduce or Implode decoding error is
  `Invalid CRC`, as in `diec`); a method XZip does not map prints
  `Last error: Unknown compression method: Unknown`;
* ZIP validity: a damaged local header and 21 entries sharing one blob make
  the file Binary, as `XZip::isValid` decides, and a ZIP cut short is still
  read from its local headers.

Three tests drive the archive walker on fixtures that
`tests/archive_storage/generate_fixtures.py` writes into the build tree.
`archive_storage` checks member bytes, memory versus temporary-file storage
around the buffer threshold, bounded source views, the source cursor and
cleanup. `archive_failures` injects temporary-file open, write, short-write,
seek, size and close faults, a full temporary disk (an injected free-space
value, so no walker case depends on the real free space; the cases that write
hundreds of MiB to the real temporary folder print a `SKIP` line instead when
it has less than 2 GiB free), cancellation, rejected callbacks, damaged
members and explicit size/count limits: a member that cannot have its
temporary file, does not fit on the temporary volume (from 16 MiB on, with
1 GiB left free), or whose temporary file
cannot be written or read back (seek, size: POSIX stdio may write its
buffered tail only then) is skipped without a report (as `diec` skips a
member whose buffer it cannot create) and the walk goes on; a failed close of
the temporary file fails nothing; cancellation fails the walk; damaged
members are skipped, a wrong CRC reported; members over an explicit limit are
skipped without a report. `generate_large_members.py`
adds members over the 256 MiB limit cdie once had (deflated, about 1 MB of
fixtures): one of 256 MiB + 1 byte is scanned through a temporary file, from
the central directory or the local headers, and a wrong CRC is reported; a
stream that decodes past its declared size stops at an explicit limit but
fails its CRC within the default 4 GiB bound; without a temporary file a
15 MiB member is scanned from memory under a 16 MiB file buffer size and a
17 MiB one is skipped undecoded (no `Invalid CRC`); a memory-only scope's
ceiling is the room; a stored member of 300 MiB, written and deleted by the
test, is read in place with no free space at all. Around 16 MiB, where the
1 GiB reserve starts (Detect It Easy's own cut-off between memory and disk):
a 100 KiB member is scanned with 200 KiB, or exactly 100 KiB, free and
skipped with 50 KiB; a member of 16 MiB - 1 needs no more than its size, one
of 16 MiB its size plus 1 GiB, and one of 20 MiB is skipped with 1 GiB free
and scanned with 2 GiB; a GZIP stream, of unknown size to the walker, only
needs room for each write while it is under 16 MiB (the injected free space
shrinks by what the temporary file holds): 100 KiB is scanned with exactly
100 KiB free and skipped with a byte less; it needs the reserve as it
reaches 16 MiB: with 500 MiB free one of 16 MiB - 1 is scanned, while one of
16 MiB or 20 MiB is skipped at that check, its temporary file just under
16 MiB. It also writes a
Zstandard member of 17 MiB built from Raw and RLE blocks (1.6 KB): scanned
from a temporary file or from memory, skipped silently without a temporary
file, without room or with a 256 MiB window, its wrong CRC reported; an XZ
member with one 65 MiB Block behind the x86 BCJ filter, filtered as it
streams; and a ZIP method `diec` does not map, reported only for a member
that can have its storage. The same fixtures check that the Extractor lists
members over 256 MiB and extracts one; reads a stored member in place (also
in a 1 MiB memory-only scope, and when extracting it); keeps a PDF stream of
unknown size in memory below the file buffer size and moves a larger one to
a temporary file; leaves out one that cannot have its temporary file (a
memory-only scope too small for it); and still fails on an explicit limit.
The Extractor's free-space check is injected too, in a copy of its source
built into the test (its temporary files tracked as the walker's are): the
same 100 KiB, 16 MiB - 1, 16 MiB and 20 MiB members, and PDF streams of
20 KiB, 100 KiB, 16 MiB - 1, 16 MiB and 20 MiB, follow the walker's rule (a
100 KiB stream is listed with exactly 100 KiB free and left out with a byte
less, at its second 64 KiB write; streams of 16 MiB and 20 MiB are left out
with 500 MiB free at the check as they reach 16 MiB, one of 16 MiB - 1 is
listed, and 20 MiB is listed with 2 GiB). Its temporary-file write failures
are not injected. In `tests/test_compatibility.py`, `diec --archivesscan` scans a
300 MiB member, reports its bad-CRC copy, reads it without end records and
keeps the 21-member window in order with it third (`--suite reference`
compares the four with a `diec` 4.0.0); both tests are skipped when the
temporary folder has less than 2 GiB free. It also feeds the
head filter that decides whether a member can be scannable in 1-, 4- and
7-byte pieces (a 6-byte XZ header, a 7-byte LHA header, a PDF marker at
offset 900, a 2-byte `MZ`). `archive_integration` scans every fixture through
the real DIE engine and compares the member offsets with extents that
Python's own parsers derive. Every case of these three runs as an aggressive
scan and as a default scan: a ZIP or LHA member that starts with `MZ` is
passed on in both, any other member only by the aggressive scan, and the
default scan still decodes it and verifies its CRC. (The walker passes GZIP
and BZIP2 streams on unfiltered; the engine no longer expands them.)

```bash
ctest --test-dir build -C Debug -R "^archive_|cdie\.compatibility" --output-on-failure
```

### MS-DOS entry tests

`die_engine_callbacks` pins the MS-DOS script map to what `diec` built from
the current Detect It Easy source prints (Formats' `XMSDOS` maps the image at
`0x10000000` and puts the entry point at that address plus CS:IP):
`getEntryPointOffset`, `getAddressOfEntryPoint`, `compareEP`, `getImageBase`,
the overlay helpers and the address conversions for a normal image, a file
whose page count is zero, a header clipped by the file size, a zero header
size and an entry point that wraps at 1 MiB. A second case checks
`compareEP` with a four-byte `#` far pointer: a match and a mismatch, two-
and one-byte pointers, a target at the end of the file and beyond it, a
1 MiB wrap and a truncated pointer field. DIE 3.10 gives the same values
except for the zero-page file, which 4.0 treats as an image filling the whole
file (entry offset 96 and `compareEP` true, where 3.10 gives -1). xxfclib's
own `test_die_msdos_ep` checks the library side.

### Private tests and corpus comparison

`private_tests/test_signature_errors.c` checks malformed signatures, cached
comparisons, searches, and the unmatched quote in the database's ZIP marker.
`private_tests/test_windows_argv.c` compares the Windows UTF-8 argument parser
with the CRT's wide-character parser. Both are private CMake test targets.

The private I/O-buffer suite checks the shared `xx_io` cache with a source that
returns short transfers. It verifies window boundaries, independent cursors,
sticky read errors, premature EOF, and source ownership. Automatic buffering
eagerly copies inputs smaller than the captured capacity, including a trailing
NUL, while equal-sized inputs remain streamed. Eager-load failures leave the
source open and owned by the caller. The same suite checks adaptive whole-file
promotion, borrowed memory, and search estimates with extreme capacities.
Build and run it with:

```powershell
cmake --build F:\ownCloud_build\cdie\build --target cdie_test_io_buffer
ctest --test-dir F:\ownCloud_build\cdie\build -R io_buffer --output-on-failure
```

The cache retries short reads in bounded requests and exposes a completed
window or whole-file copy only after all declared bytes are present. This matters for files
above 2 GiB: the Windows device caps a single transfer at `0x7fffffff` bytes.
Previously one read silently scanned only that prefix. Two RAR SFX inputs
exposed the problem: `TSData.exe` contains 1351 files and 178 directories, but
its first 2 GiB contain only 283 files and 142 directories; `Game.exe` contains
139 files and 36 directories, versus 98 files and 22 directories in the prefix.
These counts come from seeking between archive headers; compressed payloads
need not be unpacked to establish the expected result.

The `read_buffer` suite (`tests/test_read_buffer.c`) checks that the read
buffer size (`diec --buffersize`, `Feature/ReadBufferSize`) is the read chunk
of the hash, strings, entropy, checksum and search loops. A 200,003-byte
device that records every read it is asked for runs `cdie_hash_read_device`,
`cdie_strings_read_device`, `xx_md5_device`, `xx_sha1_device`,
`xx_sha256_device`, `xx_blake2sp_calc_device`, `xx_crc_calculate_device` and
`xx_io_find_bytes` at buffer sizes of 7, 1000, 4096, 64 KiB (the default) and
1 MiB. The largest read is the buffer size, or the whole input when that is
less (an 11-byte CRC reads 11 bytes, a 5000-byte search at most 5000), the
hash takes the matching number of reads, and every hash, digest, CRC, string
record and search result equals the one at 64 KiB; at 7 the 8-byte search
pattern takes the prefix-table path. The strings chunk stops at 16 MiB, and
on a 64-bit build a size no allocator can satisfy (2^62) falls back to
64 KiB reads with the same search result. Entropy opens its file itself, so
it reads a 10,000,037-byte temporary file under a progress observer: the
largest step of *Reading blocks* is the buffer size, or the 100,037-byte
block when that is less, and the total and every block value equal those at
64 KiB. Build and run it with:

```powershell
cmake --build F:\ownCloud_build\cdie\build --target cdie_test_read_buffer
ctest --test-dir F:\ownCloud_build\cdie\build -R read_buffer --output-on-failure
```

Outside the suite, the operating system's read count shows the same on
Windows (`GetProcessIoCounters`): `diec --hash` on a 7.4 MB file reads 113
times at the default, 7217 times with `--buffersize 1K` and 8 times with
`1M`, with the same output.

`private_tests/test_pe_metadata.c` exercises 32 small PE fixtures through the
script API: invalid-address sentinels, header entry points, unaligned raw
sections, Latin-1 section names, partial debug directories and invalid raw
pointers, ordinal/named export ordering, zero-RVA resource payloads, and the
reference console's scalar-search endianness. It also verifies reverse
physical mapping for overlapping sections and import descriptor/thunk
continuity, including the legacy partial first descriptor used by Upack.
Build the private
`cdie_test_pe_metadata` target and run its executable. The expected conditions
are independently checked against the native console by
`private_tests/verify_pe_metadata_reference.py`; its `--output` contains a
private probe database, fixtures, raw outputs and `summary.json`. The installed
database remains unchanged. For example:

```powershell
python private_tests/verify_pe_metadata_reference.py --diec F:\ownCloud_build\die\build-release\srct\src\console\diec.exe --cdie F:\ownCloud_build\cdie\build-release\srct\src\console\diec.exe --output private_tests/pe-metadata-oracle
```

A corpus comparison with the shipped Detect It Easy console needs no harness;
run both tools over the same files and compare their output:

```powershell
param([string[]]$Files)

$root = "C:\path\to\Detect-It-Easy"
$cdie = "C:\path\to\this-port\diec.exe"

$same = 0; $diff = 0
foreach ($f in $Files) {
    $a = (& "$root\diec.exe" $f 2>&1 | Out-String).Trim()
    $b = (& $cdie -D "$root\db" -C "$root\db_custom" $f 2>&1 | Out-String).Trim()
    if ($a -eq $b) { $same++ } else { $diff++; "DIFF: $f" }
}
"same=$same diff=$diff"
```

Four things will produce false results if you skip them:

1. **Give `cdie` both databases.** `diec` picks up `db` and `db_custom` from
   its own directory; `cdie` needs `-D` and `-C`. Compare against a Detect It
   Easy whose `db` holds the former extra rules (tagged `extra`); an older
   one whose `diec` still loads a separate `db_extra` reports detections
   `cdie` does not.
2. **Avoid WOW64 redirection.** The shipped `diec.exe` is 32-bit, so it reads
   `C:\Windows\SysWOW64\x.dll` when you ask for
   `C:\Windows\System32\x.dll`. A 64-bit `cdie` reads the real System32 file.
   Compare inside `SysWOW64`, or use any directory that is not redirected.
3. **Copy the binary before a long sweep.** Rebuilding mid-run swaps the
   executable underneath it and the results mean nothing — silently, since
   the sweep just keeps going.
4. **Do not compare a Windows tool's output with a Linux build's directly.**
   `diec.exe` emits CRLF and a POSIX `cdie` emits LF, so every single file
   "differs". Strip `\r` from both, or compare like with like.

## Results

The following console-parity results are the pre-cdisasm baseline. The corpus
and output-mode sweeps have not yet been rerun with the cdisasm decoder.

| Corpus | Files | Identical |
| --- | --- | --- |
| `C:\Windows\SysWOW64\*.dll` | 120 | 120 |
| Visual Studio 2022 `Common7\IDE\*.dll` (mostly .NET assemblies) | 80 | 80 |
| Qt 5.15.2 `msvc2019_64\bin` | 40 | 40 |
| `C:\utils\python3` (`*.exe`, `*.dll`, `*.pyd`) | 60 | 60 |
| Detect It Easy distribution, **every** file — binaries, scripts, text, images, an APK, a PDF | 48 | 48 |
| **total** | **348** | **348** |

Separate format sweeps also match byte for byte: 40 PDFs (classic `xref`,
cross-reference streams, incrementally-updated files), 60 ELF binaries and
shared objects (glibc/GCC detection, `.comment` versions, static and dynamic),
a DEX (`classes.dex`, version + map hash + ProGuard) and a synthetic Mach-O
(dylibs, sections, entry point), plus the APK manifest extraction. ELF, Mach-O
and DEX are not in the Windows corpus above, so they are tested against
`diec.exe` on files pulled from WSL and, for Mach-O, a hand-built sample.

Deliberately point the sweep at a whole directory rather than a filtered list
of executables. Restricting it to `*.dll` and `*.exe` hides every gap that
lives outside PE parsing — that filter is what let plain-text detection, the
PNG detail line and the APK/PDF parsers stay unwritten while the sweep read
322/322.

Rounds of that sweep found two real bugs, both since fixed:

* a phantom trailing `REPRO` debug record on 31 files (see *Robustness*
  below and `parse_debug` in `format/xpe.c`);
* an access violation on an input where a signature script hands the engine
  an offset near `2^63`.

### Archive and MS-DOS parity with `diec` 4.0.0

These were measured in November 2026 against `diec` 4.0.0 built from the
current Detect It Easy source (Qt 5.15.2, x86 Release) and released DIE 3.10.

Archive scanning, on 59 generated archive fixtures with `--archivesscan`,
with and without `-g`: the text output is identical in all 236 cases. In
`-j`, `-x`, `-c` and `-t` all 590 cases agree on exit code, stderr and the
`Last error` lines; the documents keep the shape differences listed in
[LIMITATIONS.md](LIMITATIONS.md). With the full database, 15 of 15 real APKs
scanned with `--archivesscan` print exactly what `diec` 4.0.0 prints (9 of
15 before the CRC and member-gate changes). With the fixture database, a
ZIP of 21 nested ZIPs three levels deep (9,723 member scans) takes `diec`
188 s and cdie 5.5 s, with identical output.

MS-DOS detection, after the `XMSDOS` fix that restored the 0x10000000 module
address in the Qt source:

| Set | DIE 3.10 | `diec` before | `diec` after | cdie |
| --- | --- | --- | --- | --- |
| first `.exe` of each MSDOS packer folder (34) | 29 | 2 | 29 | 29 |
| MS-DOS and COM packer corpora (208) | 164 | 62 | 164 | 164 |
| DOS compilers and tools with an MZ header (350) | 280 | 25 | 280 | 280 |

The per-sample verdicts of the 34-sample set are identical in DIE 3.10, the
fixed `diec` and cdie, and cdie's output is byte-identical to the fixed
`diec` on all 208 packer files and all 350 compiler files. Before its
`#` far-pointer fix cdie detected 246 of the 350 and printed only "EPACK" for
EPACK, without "Borland Pascal". PE, NE and LE files with MZ headers print
the same before and after, in default and in `-a -d -u` mode.

### Sweep the other output modes too

The default text output is the acceptance criterion, so it is easy to test
only that — and everything else then drifts unnoticed. Loop the same corpus
over the flags as well:

```powershell
foreach ($fl in @("-j","-x","-c","-t","-b","-p")) {
    $a = (& "$root\diec.exe" $fl $f | Out-String).Trim()
    $b = (& $cdie -D "$root\db" -C "$root\db_custom" $fl $f | Out-String).Trim()
    if ($a -ne $b) { "DIFF $fl $f" }
}
```

That is what turned up the structured formats having a different shape
entirely from `diec`'s, `--verbose` printing `Windows(6.0)` where `diec`
prints `Windows(Vista)[AMD64, 64-bit]`, and — worst of the three — short
options that silently meant different things in the two tools (`-p` was
profiling here and `--plaintext` there). All are fixed; the point is that
none of them were visible from the default-output sweep.

Include an input with **no** detections, which the structured formats treat
specially: DiE drops the file-part wrapper and emits a single bare record
carrying the file type as its `string`. A few kilobytes of random bytes plus
`--hideunknown` gets you there.

```bash
diec -D db -j -U random.bin
```

```json
{
    "detects": [
        {
            "info": "",
            "name": "",
            "string": "Binary",
            "type": "",
            "version": ""
        }
    ]
}
```


The target case from the task, `python3.dll`:

```text
PE64
    Linker: Microsoft Linker(14.50.35225)
    Compiler: Microsoft Visual C/C++(19.50.35225)[LTCG/C]
    Tool: Microsoft Visual Studio(2026, 18.0-18.3)
    Sign tool: Windows Authenticode(2.0)[PKCS #7]
    Debug data: Records[CodeView, VC Feature, POGO]
```

`--showdatabase` also matches, both the signature counts and the file type
display names:

```text
	Binary: 292     PE: 965      APK: 52       Amiga Hunk: 98
	COM: 247        ELF: 47      DEX: 29       Atari ST: 1
	MSDOS: 351      Mach-O: 12   NPM: 4        DOS/16M: 2
	NE: 13          PDF: 7       Mach-O FAT: 2 DOS/4G: 2
	LE: 3           CFBF: 3      ISO 9660: 23
	LX: 5           Image: 1     Archive: 1
	                JPEG: 5      ZIP: 3
	                PNG: 1       JAR: 2
	                RAR: 1
```

## Cross-checking the .NET metadata

Assembly and module names alone are a strong signal that the table layout is
right: the `Assembly` table sits at index `0x20`, so its file offset depends
on every preceding table's row size being computed correctly. If any coded
index or heap index width were wrong, the name would come back as garbage.

For a stricter check, dump the tables with an independent reader and feed the
names back through a throwaway probe signature:

```js
// probedb/PE/_probe.0.sg
meta("probe", "net");

function detect() {
    if (PE.isNet()) {
        _setResult("probe", "type",   PE.isNetTypePresent("Some.Namespace", "SomeType") ? "yes" : "no", "");
        _setResult("probe", "method", PE.isNetMethodPresent("Some.Namespace", "SomeType", "SomeMethod") ? "yes" : "no", "");
    }
    return result();
}
```

```bash
diec -D probedb some_assembly.dll
```

The probe database needs `_init`, `_debug`, `_runtime_helpers`, `language`
and `PE/_init` copied from the real database — those provide `meta()` and
`result()`.

Checks worth including: a type that exists, a type that does not, a method
in the middle of a type's method range, and a method that does not exist.
Getting all four right exercises the `TypeDef` → `MethodDef` range walk,
which is where an off-by-one would hide.

## Parsing the whole database

Every script in the database should parse. `js_parse_program()` is enough to
check that:

```c
JSNode *pNode = js_parse_program(pCtx, pSource, pPath, &pError);
if (pNode == NULL) { printf("FAIL %s: %s\n", pPath, pError); }
```

Walking the stock database this way gives `ok=2098 fail=26`; all 26 failures
are `.png`, `.txt` and `.json` files inside sub-directories that the loader
ignores anyway (`_icons/`, `.vscode/`, `PE/dotnet_only/`, `PE/native_only/`).

## Testing the runtime primitives

The Windows build supplies its own `strtod`, `dtoa`, `printf` and libm
(see [ARCHITECTURE.md](ARCHITECTURE.md#the-runtime-layer)), so those are worth
testing directly rather than only through a scan. They are also where a
regression is least obvious: a broken `x_strtod` does not crash, it silently
turns `Microsoft Linker(14.50)` into `Microsoft Linker(14.32)`.

`tests/test_runtime.c` does that. It links only `core/utils*.c`, with the same
flags the scanner uses — so building it is itself a check that the CRT-free
link still works — and exits with the number of failures:

```bash
cmake -S cdie_source -B cdie_build -DCDIE_BUILD_TESTS=ON
cmake --build cdie_build
cdie_build/tests/cdie_test_runtime
```

```text
--- x_strtod ---
ok   5e-324                             5e-324
...
63 checks, 0 failures
```

Cases that actually catch things:

| Input | Why |
| --- | --- |
| `0.1 + 0.2` → `0.30000000000000004` | shortest round-trip, the classic |
| `5e-324` | smallest subnormal |
| `1.7976931348623157e+308` | largest finite; catches exponent overflow in the bignum |
| `1e21`, `1e-7` | the two thresholds where `String(number)` switches to exponential |
| `(0.5).toFixed(0)` → `"1"` | the value rounds up into a place it has no digit for |
| `(13.75).toFixed(1)` → `"13.8"` | round-half-up at the boundary |
| `(1234.5678).toPrecision(2)` → `"1.2e+3"` | ECMAScript does not zero-pad the exponent; `%g` does |
| `x_strtod` round-tripping each of the above | the two directions fail independently |
| `x_strcmp("B", "a") < 0` | byte ordering, not locale ordering |

Both directions matter and they fail independently. During development
`x_dtoa_shortest` printed `5e-324` correctly while `x_strtod` read it back as
`2.2250738585072014e-308` — the smallest *normal* — because the assembly step
picked its encoding from the exponent instead of from the mantissa. Round-trip
tests catch that; testing either direction alone does not.

Equally, `x_dtoa_shortest` passed all eighteen of its cases while `x_strtod`
was returning `32` for `"50"`, a normalisation bug in the long division that a
scan could only ever reveal as a wrong version number.

## Tracing a scan

Set `CDIE_TRACE` to print each script name to stderr just before it runs.
This is how you find the rule responsible when a scan misbehaves on an
unusual input:

```bash
CDIE_TRACE=1 diec -D db weird.bin 2>&1 | tail -5
```

```powershell
$env:CDIE_TRACE = "1"; diec.exe -D db weird.bin
```

## Robustness

Signature scripts are third-party code and some of them are buggy. One rule in
the stock database (`Binary/format_bin.Hermes.1.sg`) does

```js
Binary.compare("C61FBC03C103191F", Binary.read_uint64(0, _BE))
```

— passing a 64-bit big-endian *value* where an offset is expected. The engine
therefore receives an offset near `2^63`, and `nOffset + nSize` overflows.
Every range check in `format/xb.c` is written overflow-safe
(`nSize <= nFileSize - nOffset`, never `nOffset + nSize <= nFileSize`), and
`xb_clamp()` rejects out-of-range starts outright.

When adding new API functions, keep that property: never add an offset and a
size before comparing them against the file size.

Two more guards exist for the same reason: the JavaScript call depth is
capped (`JSCtx.nMaxCallDepth`) and the regular-expression matcher limits both
backtracking steps and recursion depth (`RE_MAX_STEPS`, `RE_MAX_DEPTH`), so a
runaway rule fails its own detection instead of taking the process down.

## Speed

Scanning a single PE with the full database, including loading ~2100 scripts
from disk:

| Tool | Time |
| --- | --- |
| `diec` (Qt + QtScript) | ~1340 ms |
| `cdie` | ~790 ms |

Entropy regressions run as `cdie.entropy` and optional `cdie.entropy_reference`.
They cover zero/one/eight bits per byte, empty files, every generated executable
variant, region boundaries, byte histograms, block partition remainders,
all output formats, multiple/recursive targets and invalid combinations. Linux
also analyzes a 128 MiB file under a 64 MiB address-space limit. The shared
widget probe verifies snapshot ownership, transactional rejection, 64-bit ranges,
NaN/bounds/gap validation, returning to owned-byte mode and SVG export.
Cancellation before and during I/O returns no partial model. A POSIX PTY opens
the actual worker/progress/result modal and dismisses it with Escape. Windows'
native smoke sequence also opens Entropy and requires populated region rows.
The optional DIE check compares seven PE/binary cases at 1e-12 tolerance.

Hash regressions run as `cdie.hash` and optional `cdie.hash_reference`.
They compare CRC32/MD5/SHA1/SHA256 with Python zlib/hashlib for known vectors,
empty inputs, hash-block padding boundaries and 64 KiB I/O boundaries. Generated
PE/ELF/Mach-O/PNG/ZIP files verify format-independent hashing without a database.
Coverage also includes selectors, all output formats, multiple/recursive targets,
invalid modes and algorithms. A borrowed fake device forces 17-byte short reads,
mid-stream EOF/errors, unknown sizes and failed seeks; failures return no model.
The GUI probe checks result equality, ownership, invalid model replacement,
selection, copy-text export/short buffers, clearing and cancellation. A POSIX
PTY opens the actual worker/progress/result modal and closes it with Escape.
Linux hashes a 128 MiB file under a 64 MiB address-space limit. Native Windows
and Cocoa smoke sequences check populated hash rows when run on those platforms.
The optional DIE reference checks 21 selected MD5/SHA1/SHA256 results. Older
DIE 3.21 returns blank strings for empty files; that known difference is explicitly
accepted only for an empty reference value and CDIE must match hashlib instead.

Strings regressions run as `cdie.strings`. ASCII extraction is compared with
independent byte-run matching and optionally the installed native `strings`
command. Strict Python decoders verify every emitted byte range, decoded text
and scalar count. Fixtures cover UTF-8, both UTF-16 byte orders/alignments,
surrogate pairs, partial/malformed sequences, BOMs, automatic detection,
minimum lengths, empty files and 64 KiB boundaries. Console coverage includes
all report formats, quoting, recursion, missing databases, invalid options and
record/string/aggregate limits with no partial output.

Borrowed fake devices force 17-byte short reads, mid-stream EOF/read failures,
unknown sizes and failed seeks. Worker cancellation before/during I/O exposes
no model. The GUI probe verifies console equality, copied ownership, filtering,
selection, invalid replacement rollback, CSV export/short buffers and clearing.
The CSV host callback is checked with a Unicode destination and a failed open.
A POSIX PTY opens the real worker/progress/result modal, activates full-text
View with Enter, closes it with Escape, then closes the restored parent browser.
Linux also scans a sparse 128 MiB file under a 64 MiB address-space limit.
Native Windows/Cocoa smoke code checks populated shared Strings controls using
a small synthetic record, avoiding extraction limits in the GUI executable itself.
Native file-chooser interaction and desktop runtime validation require those hosts.

Hex View regressions run as `cdie.hex_view` using the actual shared browser.
Six methods cover empty/tiny files, Unicode paths, read-only open/unchanged
input, missing files, cross-window selection export, fake-device short reads,
transactional replacement/read-failure rollback, decimal/hex/invalid offsets,
page buttons, refresh/file shrink, encoding/row changes, borrowed-device lifetime
and oversized-copy rejection without source reads. The virtual source uses a
byte pattern varying above 32 bits, so address truncation cannot pass by wrapping
a repeating pattern. Linux opens a sparse file over 4 GiB under a 64 MiB address
limit, verifies the displayed last byte against a known marker, and preserves
its size/content. A PTY opens the real CDIE adapter/modal and exercises Ctrl+G,
Go/Enter, F5 and Escape. Native Windows/Cocoa smoke code checks populated file
hex controls. Desktop runtime, native clipboard and chooser interaction remain
host-specific checks; local cross-compilation covers changed Windows sources.

Disasm regressions run as `cdie.disasm`. Eleven methods cover x86 16/32/64-bit
golden instructions; ARM, Thumb and AArch64 in both instruction byte orders;
native PE/ELF/Mach-O mode detection, mapped entry points and virtual addresses;
manual raw-file modes, empty files, missing files, cancellation, invalid/truncated
opcodes and instruction boundaries across the 4 KiB page limit. ARM cases skip
when those decoders are explicitly disabled. Linux reads a known instruction
beyond 4 GiB under a 64 MiB address-space limit.

The shared browser probe verifies copied row ownership after native page
replacement, manual mode selection, Next/Back history, Go/EOF and rollback
after failed providers or invalid pages. A PTY opens the real worker/modal,
exercises Ctrl+G, decimal Go, F5 and Escape, and verifies unchanged source bytes.
Native Windows/Cocoa smoke code requires populated shared Disasm controls;
desktop runtime and clipboard checks require those hosts. Targeted MinGW
compilation covers the changed Windows GUI, adapter, decoder and widget sources.

Memory Map regressions run as `cdie.memory_map`. Ten methods verify PE32/64
headers, sections, overlays, virtual tails and absent entry points; ELF32/64
segments and zero fill in both byte orders; and Mach-O32/64 segment maps and
LC_MAIN entries in both byte orders. Flat Data, empty files, malformed-header
fallback, missing files, cancellation before/during snapshot copying and the
1024-record bound are covered. Linux maps a sparse file beyond 4 GiB under a
64 MiB address-space limit without reading the whole file.

The actual shared browser is tested for copied string ownership, transactional
rejection of invalid ranges/flags/UTF-8/counts, clearing, 64-bit offset/address
lookup, half-open boundaries, virtual/invisible rows, first-match overlap,
escaped names, text export and short output buffers. A PTY opens the real
worker/modal, uses Ctrl+G and Enter to locate a section, closes with Escape and
checks unchanged source bytes. Native Windows/Cocoa smoke sequences require a
populated Memory Map table. Changed Windows GUI, adapter, reader, table, modal
and Win32/TUI backend sources compile with MinGW; native desktop runtime and
clipboard validation require those hosts. Cocoa/GTK sources were not compiled
locally. Existing Disasm and other analysis suites also exercise the expanded
shared table engine.

Imports regressions run as `cdie.imports`. Ten methods verify independently
generated PE32/64 normal/delay, named/ordinal, hint, physical IAT offset and
virtual IAT address values. PE32 legacy VA-based delay descriptors and absent
OriginalFirstThunk fallback are covered. ELF32/64 fixtures exercise both byte
orders and ET_REL/ET_DYN undefined global/weak filtering. Mach-O32/64 fixtures
in both byte orders verify dylib ordinals, special unresolved ordinals and
exclusion of common/local/debug/defined symbols. Empty tables, unsupported and
missing inputs, malformed thunk reads, pre/during cancellation, name escaping
and record/name/aggregate bounds require no partial result.

The real shared browser is checked for copied ownership after snapshot release,
case-insensitive filtering, visible versus total counts, no matches, reset,
invalid UTF-8/offsets/counts, rollback, clearing, escaped TSV, full 64-bit values
and short buffers. A POSIX PTY opens the actual worker/modal, focuses Filter
with Ctrl+F, types a symbol substring, observes one of four imports, closes with
Escape and verifies unchanged source bytes. Linux reads a sparse PE beyond
4 GiB under a 64 MiB address-space limit. Native Windows/Cocoa smoke sequences
require populated Imports controls. The changed Windows GUI, adapter, reader,
browser and modal compile with MinGW; native desktop runtime, clipboard and
Cocoa compilation require their respective hosts. The full scan/analysis suite
is rerun after enabling native import/symbol stream support.

Exports regressions run as `cdie.exports`. Ten methods verify generated PE32/64
named, ordinal-only, forwarded, aliased and empty function slots, with exact
physical offsets and virtual addresses. ELF32/64 fixtures in both byte orders
exercise DYNSYM/ET_REL, visibility/binding/type filtering, mapped data/functions
and absolute values with no physical offset. Mach-O32/64 fixtures in both byte
orders cover public/private/local/debug/undefined nlist symbols, indirect aliases,
absolute exports, export tries and dylib reexports. Empty/unsupported/missing
inputs, cancellation before/during reads, malformed targets/tries, escaped name/
forwarder bytes and record/name/aggregate limits require no partial model.

The actual shared browser is checked for native-adapter equality, copied source
ownership, name/forwarder filtering, visible/total counts, empty matches, reset,
invalid UTF-8/offsets/counts, rollback, clearing, escaped TSV, unknown ordinal
display, full 64-bit values, exact NUL-inclusive export sizes and short buffers.
A POSIX PTY opens the real worker/modal, focuses Filter with Ctrl+F, types a
forwarder/name substring, observes one of three exports, closes with Escape and
checks unchanged input. Linux reads a sparse PE beyond 4 GiB under a 64 MiB
address-space limit. Native Windows/Cocoa smoke code checks populated shared
Exports controls using a synthetic row because GUI executables may export no
symbols. Changed Windows GUI, adapter, reader, browser/modal and shared export
table sources compile with MinGW. Native desktop runtime/clipboard and Cocoa
compilation require those hosts. The full suite is rerun after enabling export
streams and expanding the shared export table.

Resources regressions run as `cdie.resources`. Eleven methods verify generated
PE32/64 numeric IDs, multiple languages, code pages, byte ranges, Unicode names
including a surrogate pair and zero-byte resources. ELF32/64 fixtures in both
byte orders cover bundle/leaf ranges, sorting, compressed stored bytes, empty
leaves and invalid UTF-8 key escaping. When available, `glib-compile-resources`
generates an independent bundle and the reported leaf bytes must match its
original input. This optional method skips when GLib's compiler is absent.

Tests cover empty/unsupported/missing inputs, cancellation before/during
collection, invalid PE payload ranges/directory cycles, malformed GVDB roots
and native skipped cyclic leaf-name graphs. Exactly 10,000 resources load;
record/name/aggregate overflow returns no model. Linux reads a sparse PE beyond
4 GiB under a 64 MiB address-space limit.

The shared browser probe checks every native field against the adapter, copied
ownership of all three strings, filtering/counts, empty matches, reset, invalid
UTF-8/offsets/full ranges/counts, transactional rollback, clearing, escaped TSV,
numeric IDs/code pages, exact NUL-inclusive output length and short buffers.
A POSIX PTY opens the actual worker/modal, focuses Filter with Ctrl+F, filters
by a language ID to one of two resources, closes with Escape and checks unchanged
input. Native Windows smoke opens its own PE resource table; Cocoa smoke uses
a synthetic record because Mach-O has no native resource provider. Changed
Windows GUI, adapter, collector, shared browser/modal compile with MinGW.
Native desktop runtime/clipboard and Cocoa compilation require those hosts.

Symbols regressions run as `cdie.symbols`. Ten methods cover PE32/64 COFF
primary/auxiliary records, file names, function sizes, undefined/debug/absolute
symbols, Unicode names and physical/virtual targets. ELF32/64 in both byte
orders exercises ET_REL/ET_DYN symbol tables, local/global/weak bindings,
hidden/undefined/file/section records and absolute values. Mach-O32/64 in both
byte orders covers public/private/local/debug/undefined nlist symbols, indirect
aliases, absolute symbols and trie-only entries with unknown nlist locations.

Tests cover missing/unsupported/empty inputs, cancellation before/during reads,
invalid tables, byte escaping and record/name/aggregate limits. Exactly 10,000
symbols load; a larger model fails without partial results. Linux reads symbols
from a sparse PE beyond 4 GiB under a 64 MiB address-space limit.

The real shared browser is compared against every native field, then checked
for copied ownership, name/filename filtering, visible totals, reset, invalid
offsets/record offsets/enums/UTF-8/counts, rollback, clearing, TSV escaping,
flags, full 64-bit values, EOF target offsets, exact export size and short
buffers. A direct table replacement with an oversized filename is safely
rejected by TSV export. A POSIX PTY opens the worker/modal, uses Ctrl+F to
filter to one of six symbols, closes with Escape and verifies unchanged source
bytes. Native Windows/Cocoa smoke uses a synthetic symbol because GUI binaries
may be stripped. Changed Windows GUI, adapter, reader, browser/modal compile with
MinGW; native desktop runtime/clipboard and Cocoa compilation require those hosts.


## Dialog smoke tests

Every dialog of the three GUIs is opened, checked and closed by a smoke run:
`gui_win32` (`die --smoke-test`, Windows), `cdie.linux_dialogs` and
`cdie.linux_settings` (GTK probes under `xvfb-run`; skipped with code 77
without it) and `gui_cocoa` (macOS, needs a logged-in session). The shipped
GTK `die` itself runs as `cdie.gui_gtk_smoke` (`xvfb-run -a die --smoke-test
die`: a hidden window scans its own executable), also skipped with 77 without
xvfb-run, as are `cdie.catalog`, `xxwidgets.catalog` and `cdie.translation`
there. Unset
`CDIE_TRANSLATION`: all matching is against English captions. On Windows,
`CDIE_GUI_SMOKE_TRACE=1` prints each step and why a check was skipped;
`CDIE_GTK_DRIVE_DUMP=1` prints every GTK dialog's widgets.

The Windows run checks focus only while die.exe holds the foreground. If
another program takes it during a step, the checks are skipped; if the
foreground leaves when the step closes its dialog, the step fails, because
that is a broken focus restore. A work area smaller than a dialog's layout
(for example Options on a 768-pixel-high screen) is reported as a known
issue, not failed. The scan-cancel step grows its zero-filled file up to
64 MiB until the progress dialog shows (after one second), and reports the
step as skipped if the scan is faster still. The panel's Databases combo
must select the custom database alone, and Options > Scan must show 17 checkboxes,
the Main database and Custom database folder rows, and no caption naming an
extra database.

xxwidgets has `xxwidgets_dialog_keys_gtk` (Enter with a focused button,
dialog window type) and, in `xxwidgets_win32`, checks that a dialog closed
while still modal returns activation to its owner and that a minimum larger
than the screen is held to the work area. Known open issue: the GTK
Extractor finds no object in the `overlay.exe` fixture even with All types;
the probe reports it.

## Portable packaging and Options persistence

`cdie.settings` runs the Options store against Detect It Easy's own files. A
die.ini written the way Qt 5's QSettings writes it (CRLF, `\x` escapes, lists,
`@ByteArray`, Qt 6 font strings, `#aarrggbb` colors) loads into the expected
values, including Detect It Easy's recent files; its stale
`Die\Database\Extra` keys take no database slot (`$data/db` and
`$data\db_custom` are the defaults). A save without changes
leaves it byte-identical, and one change rewrites one line. A follow-up
checks three things:
- A save refused by the memory-only I/O policy and then undone leaves the
  file unchanged.
- Fonts are written with a family Qt resolves: `monospace` with style hint 7,
  and a monospace default for the table and text views.
- A `.po` language goes only to `cdie/Lang`, and `System` reaches `View/Lang`
  too.

Buffer sizes take effect within the *Optimization* page's range (1 GiB, or
64 MiB in a 32-bit build, at most): `Feature/ReadBufferSize` and
`Feature/FileBufferSize` of 0, 2147483648, 4294967295, 4294967296,
99999999999 or 18446744073709551615 count as the 64 KiB default (Detect It
Easy reads each as its default), 2147483647, 1610612736 and 1073741825 as
the maximum, 512, 1 or 1023 as 1 KiB; 1024, 1500, 16 MiB, 64 MiB and 1 GiB
are used as stored (above 64 MiB only in a 64-bit build), and a number past
64 bits is ignored. A save that changes another option, after Options' OK
applied the sizes as loaded, keeps both lines byte-identical, also for typed
QWORD values; a file buffer size changed as Options' OK applies it rewrites
its own line only.

`--native-migration` (an isolated HKCU key or `XDG_CONFIG_HOME`) copies an
older `NTInfo/cdie` store once and marks both stores `cdie/Migrated`. It keeps
Qt's registry types, and clearing the die store does not bring the old
settings back. The old store's `Scan/Die/Database/Extra/Path` is neither
migrated nor removed. Malformed values and wrong types are ignored, and an
`Extra/Path` or `Extra/Enabled` value of earlier versions changes nothing.
A `REG_DWORD` `Feature/ReadBufferSize` of 0x80000000 (a negative number to
Qt) takes effect as the default and a `REG_QWORD` `Feature/FileBufferSize` of
1.5 GiB as the maximum; both keep their type and value through a save.

`cdie.settings_policy` runs from an isolated Unicode installation folder with
an empty uppercase `PORTABLE` marker and a different working directory. It
tests every exposed Options value in portable INI and an isolated native
registry namespace, complementary value profiles, saved Defaults,
preservation of unrelated keys, unsupported CPU feature masking, and a
write/verify pair in separate processes. Nine buffer sizes in the portable
die.ini, one process each, take effect as above (the maximum read from the
probe's PE or ELF header) and keep their lines through a save. It also
migrates an older `cdie.ini` into an existing die.ini without changing
Detect It Easy's lines; its buffer sizes (0 and 4294967296) are copied as
stored, and its `Scan/Die/Database/Extra/*` keys are not carried over. The old file
gains only `[cdie] Migrated`. Neither removing a migrated key nor deleting
die.ini makes the copy run again. Signature folders are real
minimal fixture databases. Two explicitly stored empty database paths (main
and custom) restore the executable-local default folders even after other
folders were loaded in the same process; a stale `Extra/Path` beside them is
no third slot.

On Windows, `cdie.windows_settings` drives the actual Options window through
Scan, Fonts, Hex and Optimization edits. It verifies OK writes only the
changed options into `die.ini`, in Qt's plain forms,
fresh processes restore every applied value, Cancel leaves the existing INI
bytes and active values unchanged, and Default followed by OK saves defaults
that survive another restart. The dedicated GUI smoke mode requires portable
storage, rejects environment overrides, and omits the Explorer integration
page so tests never register a shell context menu. File-page integration is
itself persisted by Windows rather than an INI preference.

`cdie.windows_portable_package` invokes the default CPack generator, checks
that it produced a ZIP with an empty `PORTABLE` beside the main GUI executable,
checks bundled signatures when enabled, extracts it, and verifies that the
real settings opener selects the adjacent INI. It rejects accidentally
packaged personal `die.ini` or `cdie.ini` files and any `db_extra` folder
(configure with a `CDIE_DATABASE_DIR` that holds one to exercise that; CMake
then prints `cdie: ignoring .../db_extra`). The focused xxfclib
`tests/settings` suite also checks the public portable-status function,
uppercase/lowercase markers, invalid paths, working-directory isolation,
typed INI/native save guards, and QSettings sharing: line-by-line patches of
Qt-written files, Qt lists read as string lists, a quote left open at the end
of a file, `#` keys, a v1 header below a comment, and stricter reading
(unterminated section, literal commas) for stores that are not
Qt-compatible.

## Database updates

With `-DCDIE_BUILD_TESTS=ON`, `cdie.updater` is registered whenever
`die-update` is built. It runs `tests/test_updater.py`, 59 cases (two of them
Windows only) that drive
`die-update` (and, when `diec` is built, `diec --checkupdate`/`--update`)
against `tests/updater_server.py`: an
offline HTTP server on 127.0.0.1 that answers like GitHub's release API, its
release download redirects and its CDN, and also serves plain zip URLs.
Nothing reaches the network. Every run passes `--noproxy` and either
`--nosettings`, `--settings` or a portable copy; databases live in temporary
folders; and `CDIE_USER_DATA_DIR` points the per-user folder at another one.
The user's own database, settings and per-user copy are never read or
written. The cases cover:

* installation: a fresh install with check/update/check, a newer release
  replacing `db` (a merge instead of a swap fails; the archive's `db_extra/`
  is never installed), an update that touches nothing when current,
  `--force`, `-E`/`--extradatabase` refused with exit code 4 and no request,
  the JSON fields, text and `--quiet` output;
* verification, each keeping the old database: MD5 and listed-size
  mismatches, a truncated download, corrupt archives, an archive without
  `db/` or without signatures, members that leave their folder (a
  `db_extra/..` member is skipped, never written), absolute member names and
  links inside the archive;
* sources: a release without an `.md5` (asset identity), the rate-limited
  API falling back to download URLs with and without an MD5, a missing
  release or asset, `/releases/latest` with the API base derived from the
  URL, a release download URL, plain zip URLs with and without `<url>.md5`,
  a catch-all page in place of the `.md5`, and a listed `.md5` asset that
  holds none;
* settings: a portable `die.ini` (also with a stale `Extra/Path`, ignored and
  kept), `--settings`, empty and legacy URLs meaning the default,
  `--nosettings`, and a `die.ini` that the strict parser rejects;
* arguments: invalid combinations, targets and sources, help and version; a
  folder that is not a database, protected folders and a `db_extra` folder
  refused as targets;
* locking and recovery: a held lock (exit code 7), a failed rename of the old
  `db` and a failed rename of the new one, each rolled back (Windows only),
  crash recovery before a failed update and before a successful one, a
  legacy stage holding `old-db_extra` restored, and a stale stage removed;
* the per-user folder: a read-only default database is updated in
  `CDIE_USER_DATA_DIR`, with no `db_extra` created there and the shipped one
  untouched, and a portable installation never uses it. These skip when a
  folder cannot be made read-only (as root, or without `icacls` on Windows);
* `diec` forwarding: `--checkupdate`/`--update`, the exit code passed
  through, `127` for a missing updater, the updater found beside `diec`, and
  `-E`/`--extradatabase` refused by `diec` without starting `die-update`.

`cdie.gui_update` is registered when the GUI is built as well. It runs
`tests/test_gui_update.py`, which copies the GUI into a temporary portable
installation with a fixture database, serves a zip from the same offline
server, and runs `die --smoke-update <expected>` with `CDIE_UPDATE_URL`,
`CDIE_UPDATE_NOPROXY` and a temporary `CDIE_USER_DATA_DIR`. It checks an
install followed by "current", that a portable GUI leaves the per-user
folder alone, that the installation's `db_extra` from an earlier version stays
byte-identical (no `die-update.ini` written into it) and no other `db_extra`
appears, and that a failed (MD5 mismatch) update keeps the database and
leaves no stage behind. On Windows and macOS it also drives the dialogs the
update shows its owner: `--smoke-update box-current` (the "No updates" box),
`ask-no` and `ask-yes` (the question before an install). On Linux without a
display it runs under `xvfb-run` when CMake found it, and skips otherwise.

```bash
ctest --test-dir build -C Debug -R "cdie\.(updater|gui_update)" --output-on-failure
python3 tests/test_updater.py --updater build/src/updater/die-update --diec build/src/console/diec
python3 tests/test_gui_update.py --gui build/src/gui/die
```

Both scripts accept unittest test names after the options, for example
`InstallTests.test_force_reinstalls_a_current_database`. `die-update` also
has options meant for these tests and left out of its help: `--api-base
<url>` (the GitHub API address), `--noproxy`, `--nouserfolder` (never fall
back to the per-user folder) and `--timeout <ms>`.

### The GUI smoke switch by hand

`die --smoke-update installed|current|failed` presses **Update database**
once without a progress dialog, prints `smoke-update: <result> <status text>`
on stderr and exits 0 only when the result is the expected one. It updates
the database the GUI would use and reads the update URL from the GUI's own
settings, so run it on a portable copy with a test database and point it at
a local source. `db.zip` must hold `db/` at its top (other folders, such as
`db_extra/`, are ignored); a `db.zip.md5` beside it is optional:

```bash
mkdir -p /tmp/cdie-gui && cp build/src/gui/die /tmp/cdie-gui/ && touch /tmp/cdie-gui/PORTABLE
cp -r /path/to/test/db /tmp/cdie-gui/
(cd /path/to/release && python3 -m http.server 8000 --bind 127.0.0.1) &
export CDIE_UPDATE_URL=http://127.0.0.1:8000/db.zip CDIE_UPDATE_NOPROXY=1 CDIE_USER_DATA_DIR=/tmp/cdie-user
/tmp/cdie-gui/die --smoke-update installed    # then: --smoke-update current
```

On macOS run `die.app/Contents/MacOS/die` from a copy of the bundle with
`PORTABLE` beside that executable. On Windows `die.exe` is a GUI program, so
let PowerShell wait for it and capture stderr:

```powershell
$env:CDIE_UPDATE_URL = "http://127.0.0.1:8000/db.zip"; $env:CDIE_UPDATE_NOPROXY = "1"
$env:CDIE_USER_DATA_DIR = "$env:TEMP\cdie-user"
$p = Start-Process C:\tmp\cdie-gui\die.exe -ArgumentList "--smoke-update","installed" -Wait -PassThru -RedirectStandardError smoke.txt
$p.ExitCode; Get-Content smoke.txt
```

### Library tests

The library code added for the updater has its own tests, built in each
library's own tree (cdie adds the libraries without their tests):

* xxfclib `test_fs_tree`: `xx_fs_make_dir` (exclusive, no parents),
  `xx_fs_rename` (never replaces a file or folder), `xx_fs_remove_tree`
  (read-only entries, a link or junction inside the tree or as the root
  removed without touching its target, missing paths) and
  `xx_fs_current_dir`;
* xxfclib `test_zip_unpack_path`: extracting a zip into a folder the way
  `die-update` unpacks `db.zip` (every member written byte for byte, empty
  folders, no stray files, no overwriting on a second pass). It guards the
  fix for extraction to a path, which never read the local header and so
  failed every file member silently;
* xxgithub `test_github` (CTest name `xxgithub`): the release URL parser
  (tag, download, latest and GitHub Enterprise URLs, percent-decoding, and
  the malformed forms it must refuse), `xx_github_release_download_url` and
  `xx_github_release_find_asset`.

```bash
cmake -S ../_mylibs/xxfclib -B build-xxfclib -DXXFC_BUILD_FS_TESTS=ON
cmake --build build-xxfclib --target test_fs_tree test_zip_unpack_path
ctest --test-dir build-xxfclib -R "test_fs_tree|test_zip_unpack_path" --output-on-failure

cmake -S ../_mylibs/xxgithub -B build-xxgithub
cmake --build build-xxgithub
ctest --test-dir build-xxgithub -R xxgithub --output-on-failure
```
