# The library — die_library-compatible C API

The build produces a shared and a static library that expose the same C API as
[horsicq/die_library](https://github.com/horsicq/die_library). The public
header is [`lib/die.h`](../lib/die.h); it declares the same flags and the same
exported functions with the same signatures, so a program written against
die_library — including its own samples — compiles and links against this
library unchanged. The implementation is the pure-C `cdie` engine
([ARCHITECTURE.md](ARCHITECTURE.md)) rather than the Qt one.

The compatibility contract was checked against die_library commit
`fcf005cd19aefcacca0a011b1f529e0ef51c1fe1` (library version 0.1.0), its C,
VB6 and VB.NET samples, and the `elastic/die-python` and `elastic/die-rust`
bindings linked from that repository.

## Artifacts

| Target | Windows | Linux | macOS |
| --- | --- | --- | --- |
| `die_shared` | `die.dll` + `die.lib` (import lib) | `libdie.so` | `libdie.dylib` |
| `die_static` | `die_static.lib` | `libdie.a` | `libdie.a` |

The static archive is named `die_static` on Windows only, because the shared
library's import lib already claims `die.lib`; on Linux and macOS the static
archive and shared library have different extensions.

Both are on by default. Configure with `-DCDIE_BUILD_LIBRARY=OFF` to build only
the console scanner.

## Exported functions

```c
char    *DIE_ScanFileA(char *pszFileName, unsigned int nFlags, char *pszDatabase);
wchar_t *DIE_ScanFileW(wchar_t *pwszFileName, unsigned int nFlags, wchar_t *pwszDatabase);
char    *DIE_ScanMemoryA(char *pMemory, int nMemorySize, unsigned int nFlags, char *pszDatabase);
wchar_t *DIE_ScanMemoryW(char *pMemory, int nMemorySize, unsigned int nFlags, wchar_t *pwszDatabase);
int      DIE_LoadDatabaseA(char *pszDatabase);
int      DIE_LoadDatabaseW(wchar_t *pwszDatabase);
char    *DIE_ScanFileExA(char *pszFileName, unsigned int nFlags);   /* uses the loaded database */
wchar_t *DIE_ScanFileExW(wchar_t *pwszFileName, unsigned int nFlags);
char    *DIE_ScanMemoryExA(char *pMemory, int nMemorySize, unsigned int nFlags);
wchar_t *DIE_ScanMemoryExW(char *pMemory, int nMemorySize, unsigned int nFlags);
void     DIE_FreeMemoryA(char *pszString);
void     DIE_FreeMemoryW(wchar_t *pwszString);
/* Windows only, __stdcall, for VB: */
int __stdcall DIE_VB_ScanFile(wchar_t *pwszFileName, unsigned int nFlags, wchar_t *pwszDatabase, wchar_t *pwszBuffer, int nBufferSize);
int __stdcall DIE_VB_ScanFileCallback(..., DIE_VB_CALLBACK pfnCallback);
```

A returned string is owned by the caller and freed with the matching
`DIE_FreeMemory{A,W}` — never with `free`/`delete`.

The twelve ordinary functions use the platform C calling convention (`__cdecl`
on Windows x86). The two VB functions are `__stdcall`. On x86 their exported
names are `_DIE_VB_ScanFile@20` and `_DIE_VB_ScanFileCallback@24`, as used by
the upstream VB6 sample; on x64 they have the undecorated names shown above.
There is no exported die_library version function. Version 0.1.0 is build/file
metadata, and the Python binding obtains its version strings at compile time.

`DIE_ScanFile*`/`DIE_ScanMemory*` load the database directory named in the call
on every invocation. A null or empty database argument selects `$data/db`, as
it does upstream. `DIE_LoadDatabase*` loads once into the library and returns
`1` on success or `0` on failure; the `*Ex` variants reuse that database.

## Flags

| Flag | Value | Meaning |
| --- | ---: | --- |
| `DIE_DEEPSCAN` | `0x00000001` | deep-scan signatures (`DS.*`, `EP.*`) |
| `DIE_HEURISTICSCAN` | `0x00000002` | heuristic signatures (`HEUR.*`) |
| `DIE_ALLTYPESSCAN` | `0x00000004` | scan all applicable file types |
| `DIE_RECURSIVESCAN` | `0x00000008` | recursive scan option |
| `DIE_VERBOSE` | `0x00000010` | verbose detections |
| `DIE_AGGRESSIVESCAN` | `0x00000020` | aggressive scan |
| `DIE_RESULTASXML` | `0x00010000` | XML result |
| `DIE_RESULTASJSON` | `0x00020000` | JSON result |
| `DIE_RESULTASTSV` | `0x00040000` | currently plain text, matching upstream's omitted TSV assignment |
| `DIE_RESULTASCSV` | `0x00080000` | semicolon-separated result |

Flags are interpreted as `XScanEngine::setScanFlags` does, including its current
omission of `DIE_RESULTASTSV`. Combined result bits use ScanItemModel's exact
precedence: CSV, JSON, TSV, XML, then text. The library adapts cdie's console
XML element names and Unknown capitalization to the ScanItemModel spelling;
text, JSON, XML and CSV have been byte-compared with the Qt library. Results
remain in signature-priority order unless the undocumented sort bit
`0x02000000` is set; the console scanner normally sorts by result priority.

## Database paths and `$data`

A database path beginning with `$data` has that token replaced by the running
executable's directory, matching `XOptions::convertPathName`. So the
die_library sample's `"$data/db"` resolves to `<exe-dir>/db` — put a `db`
folder (and, on Windows, `die.dll`) next to the program.

## Building the sample

The die_library C sample lives verbatim in [samples/C/simple.c](../samples/C/simple.c).

Windows (Developer Command Prompt):

```bat
cl /nologo /MD /c simple.c /I ..\..\lib
link /nologo simple.obj die.lib /SUBSYSTEM:CONSOLE
```

Compile the consumer with `/MD` — the libraries use the dynamic CRT, so a
default (`/MT`) `cl` invocation would warn `LNK4098`.

Linux:

```bash
build_dir=/path/to/cdie_build
gcc simple.c -o simple -I ../../lib -L"$build_dir/lib" -l:libdie.so
# or, static:
gcc simple.c -o simple -I ../../lib "$build_dir/lib/libdie.a" \
    "$build_dir/xxfclib/libdie_engine.a" \
    "$build_dir/xxfclib/cdisasm/libcdisasm.a" -lm
```

For a static consumer, link the engine and cdisasm archives after `libdie.a`.
The CMake `die_static` target carries those dependencies automatically.

## Compatibility notes

- The Windows DLL is `die.dll` on both x86 and x64; no architecture suffix is
  used. Its 14-name x64 export table is identical to the reference DLL built
  from the commit named above.
- `DIE_VB_ScanFileCallback` forwards signature name, zero-based database index
  and database count, and a callback return of zero cancels the scan.
- The ABI and serialization are compatible. Detection content still comes from
  the cdie engine, so a core signature-execution difference documented in
  [LIMITATIONS.md](LIMITATIONS.md) can change which records are serialized.
