# Testing

The acceptance criterion for this port is simple: for the same input file and
the same databases, `cdie` must print exactly what `diec` prints.

## Reference comparison

For a repeatable comparison of the locally built console versions, use the
developer harness in `private_tests/compare_console.py`:

```powershell
python private_tests/compare_console.py --cdie F:\ownCloud_build\cdie\build\srct\src\console\cdie.exe --diec F:\ownCloud_build\die\build\srct\src\console\diec.exe --database F:\ownCloud\prepare\qt5\_mylibs\Detect-It-Easy\db --corpus F:\out_iso\out_tmp2\PE32 --sample-per-directory 1 --batch-size 8 --timeout 120 --output private_tests/comparison-results
```

It uses `-b -d -u` for both tools, selects one file per directory recursively,
and explicitly isolates extra/custom databases so only the specified main
database is used. It snapshots both executables and records database hashes,
file sizes, modification times, raw stdout/stderr, exit codes and diffs. Only
CRLF is normalized; whitespace and diagnostic messages remain significant.
Use `--files-from` with a UTF-8 newline-separated manifest for a fixed file
selection, and `--resume` to continue an interrupted run with unchanged inputs.
Timeouts are recorded as failures, never as equal empty outputs.

After rebuilding `cdie`, use a new output directory and
`--reference-results private_tests/comparison-results` to reuse successful
`diec` records. The harness verifies the reference executable, database,
auxiliary database isolation, raw output hashes, and input size/mtime before
reusing a record. Missing reference records are scanned normally. `--resume`
requires both executables to remain unchanged.

`private_tests/test_signature_errors.c` checks malformed signatures, cached
comparisons, searches, and the unmatched quote in the database's ZIP marker.
`private_tests/test_windows_argv.c` compares the Windows UTF-8 argument parser
with the CRT's wide-character parser. Both are private CMake test targets.
The standalone `test_disasm_reference.c` and `test_js_trim_reference.cpp`
compare primitives directly with the reference Capstone and Qt5Script
libraries; those reference dependencies are not part of the production build.

`private_tests/test_file_reads.c` checks the shared file loader with a 97-byte
device that returns one or seven bytes per call. It verifies the complete
payload and terminator, and rejects premature EOF, read errors, invalid sizes,
and transfers claiming more bytes than requested. Failed loads leave the
`DieFile` empty; the borrowed source remains open. Build and run it with:

```powershell
cmake --build F:\ownCloud_build\cdie\build --target cdie_test_file_reads
& F:\ownCloud_build\cdie\build\srct\private_tests\cdie_test_file_reads.exe
```

The loader retries short reads in requests of at most 64 MiB and publishes the
file size only after all declared bytes are present. This matters for files
above 2 GiB: the Windows device caps a single transfer at `0x7fffffff` bytes.
Previously one read silently scanned only that prefix. Two RAR SFX inputs
exposed the problem: `TSData.exe` contains 1351 files and 178 directories, but
its first 2 GiB contain only 283 files and 142 directories; `Game.exe` contains
139 files and 36 directories, versus 98 files and 22 directories in the prefix.
These counts come from seeking between archive headers; compressed payloads
need not be unpacked to establish the expected result.

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
python private_tests/verify_pe_metadata_reference.py --diec F:\ownCloud_build\die\build-release\srct\src\console\diec.exe --cdie F:\ownCloud_build\cdie\build-release\srct\src\console\cdie.exe --output private_tests/pe-metadata-oracle
```

```powershell
param([string[]]$Files)

$root = "C:\path\to\Detect-It-Easy"
$cdie = "C:\path\to\cdie.exe"

$same = 0; $diff = 0
foreach ($f in $Files) {
    $a = (& "$root\diec.exe" $f 2>&1 | Out-String).Trim()
    $b = (& $cdie -D "$root\db" -E "$root\db_extra" -C "$root\db_custom" $f 2>&1 | Out-String).Trim()
    if ($a -eq $b) { $same++ } else { $diff++; "DIFF: $f" }
}
"same=$same diff=$diff"
```

Four things will produce false results if you skip them:

1. **Give `cdie` all three databases.** `diec` picks up `db`, `db_extra` and
   `db_custom` from its own directory; `cdie` needs `-D`, `-E` and `-C`.
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

### Sweep the other output modes too

The default text output is the acceptance criterion, so it is easy to test
only that — and everything else then drifts unnoticed. Loop the same corpus
over the flags as well:

```powershell
foreach ($fl in @("-j","-x","-c","-t","-b","-p")) {
    $a = (& "$root\diec.exe" $fl $f | Out-String).Trim()
    $b = (& $cdie -D "$root\db" -E "$root\db_extra" -C "$root\db_custom" $fl $f | Out-String).Trim()
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
cdie -D db -j -U random.bin
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
cdie -D probedb some_assembly.dll
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
CDIE_TRACE=1 cdie -D db weird.bin 2>&1 | tail -5
```

```powershell
$env:CDIE_TRACE = "1"; cdie.exe -D db weird.bin
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
