# The library — die_library-compatible C API

The build produces a shared and a static library that expose the same C API as
[horsicq/die_library](https://github.com/horsicq/die_library). The public
header is [`lib/die.h`](../lib/die.h); it declares the same flags and the same
exported functions with the same signatures, so a program written against
die_library — including its own samples — compiles and links against this
library unchanged. The implementation is the pure-C `cdie` engine
([ARCHITECTURE.md](ARCHITECTURE.md)) rather than the Qt one.

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

`DIE_ScanFile*`/`DIE_ScanMemory*` load the database directory named in the call
on every invocation. `DIE_LoadDatabase*` loads it once into the library and the
`*Ex` variants reuse it.

## Flags

| Flag | Meaning |
| --- | --- |
| `DIE_DEEPSCAN` | deep-scan signatures (`DS.*`, `EP.*`) |
| `DIE_HEURISTICSCAN` | heuristic signatures (`HEUR.*`) |
| `DIE_ALLTYPESSCAN` | scan all file types |
| `DIE_RECURSIVESCAN` | recurse into directories |
| `DIE_VERBOSE` | verbose detections |
| `DIE_AGGRESSIVESCAN` | aggressive scan |
| `DIE_RESULTASXML` / `DIE_RESULTASJSON` / `DIE_RESULTASTSV` / `DIE_RESULTASCSV` | structured output instead of plain text |

Flags are interpreted exactly as `XScanEngine::setScanFlags` does, and the
result string is what `ScanItemModel::toString()` produces — which the cdie
formatters reproduce byte for byte. As in die_library, results are left in
signature-priority order unless the (undocumented) sort bit `0x02000000` is
set; the console scanner, by contrast, always sorts by result-type priority.

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

- The `.def` in die_library lists `DIE_LoadDatebase*` (a typo); the real
  exports — and the header — use `DIE_LoadDatabase*`, which is what this
  library exports.
- On x64 the `__stdcall` VB functions export under their plain names (x64 has a
  single calling convention), matching die_library.
- The per-signature progress callback of `DIE_VB_ScanFileCallback` is not
  surfaced by the cdie engine; the scan still runs and fills the buffer exactly
  as `DIE_VB_ScanFile`.
