# Building and testing

## Requirements

A C99 compiler and CMake 3.16+. Nothing else — no Qt, no third-party
libraries, no code generation.

Built and tested with MSVC 19.44 (Visual Studio 2022) on Windows and GCC 11.4
on Linux; both produce identical scan output. Clang and macOS are expected to
work but have not been exercised.

The project compiles as **strict ISO C99** — `CMAKE_C_EXTENSIONS` is `OFF`,
so GCC gets `-std=c99` rather than `-std=gnu99`. That hides every POSIX
declaration behind a feature-test macro, which is why `core/cd_fs.c` defines
`_POSIX_C_SOURCE`, `_DEFAULT_SOURCE` and (on Apple) `_DARWIN_C_SOURCE` before
its first include. Any new file that calls a POSIX function needs the same
preamble.

## Build

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release
cmake --build cdie_build
```

The binary lands in `cdie_build/src/console/cdie` (`.exe` on Windows).

### Windows, MSVC + Ninja

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S cdie_source -B cdie_build
ninja -C cdie_build
```

### The CRT-free Windows build

On 64-bit MSVC the executable links **no C runtime at all**: the option
`CDIE_NO_CRT` defaults to `ON`, the entry point becomes `x_entry_point`
instead of `mainCRTStartup`, and the only import is `KERNEL32.dll`.

```text
> dumpbin /imports cdie.exe
    KERNEL32.dll   23 functions
```

The link flags that produce this live in `src/console/CMakeLists.txt`:
`/NODEFAULTLIB /ENTRY:x_entry_point /SUBSYSTEM:CONSOLE`, plus `/GS-` to drop
the stack cookie (`__security_check_cookie` is a CRT symbol).

Turn it off if you need to link something that expects a runtime:

```bash
cmake -S cdie_source -B cdie_build -DCDIE_NO_CRT=OFF
```

It is already off for 32-bit MSVC, for non-MSVC compilers and for every
non-Windows target — see [LIMITATIONS.md](LIMITATIONS.md#runtime) for why.

Only the console `cdie` executable is CRT-free. The libraries (below) link the
dynamic CRT so they interoperate with normally-compiled consumers.

Two rules apply when adding code that will be compiled into this build:

* **No standard library calls**, including implicit ones. Go through the `x_`
  stubs in `core/utils.h`.
* **No stack frame over 4 KB.** Stack probes come from the CRT, so a large
  frame walks past the guard page. `/Gs` is deliberately *not* used to raise
  the probe threshold: without it an oversized frame fails at link time with
  an unresolved `__chkstk`, which is a much better failure than a stack
  overflow at run time.

### The libraries

The build also produces a shared and a static library exposing the
die_library C API — `die.dll`/`die.lib` and `die_static.lib` on Windows,
`libdie.so`/`libdie.a` on Unix — from the same engine sources (minus the
console's `core/utils_entry.c`). They are on by default:

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release
cmake --build cdie_build           # builds cdie, die_shared, die_static
cmake -S cdie_source -B cdie_build -DCDIE_BUILD_LIBRARY=OFF   # console only
```

See [LIBRARY.md](LIBRARY.md) for the API, the flag mapping and how to compile a
consumer (use `/MD` on MSVC to match the libraries' dynamic CRT).

### Install

```bash
cmake --install cdie_build --prefix /where/you/want
```

On Linux the binary goes to `bin/`; elsewhere to the prefix root. The install
also places `README.md`, `LICENSE`, `changelog.txt` and `docs/` next to the
binary, plus the signature database when one was found (see below).

## Packaging

### Windows portable package

```bat
packaging\windows\build_portable_windows.cmd <cmake-platform> <package-suffix>
```

```bat
packaging\windows\build_portable_windows.cmd x64   win64_msvc2022
packaging\windows\build_portable_windows.cmd Win32 win32_msvc2022
packaging\windows\build_portable_windows.cmd ARM64 winarm64_msvc2022
```

Unlike the Qt projects in this repository the script takes **no Qt root** —
`cdie` depends only on xxfclib, which is built from source alongside it, so the platform and the package suffix
are the only arguments.

Following the repo convention, build trees and CPack staging live under
`%TEMP%` and only finished artefacts land in `release\`:

```text
release\
├── cdie_win64_msvc2022_portable_1.0.0\   ready-to-run folder
│   ├── cdie.exe
│   ├── db\  db_extra\  db_custom\
│   ├── docs\
│   └── README.md  LICENSE  changelog.txt
└── cdie_win_x64_portable_1.0.0.zip       CPack ZIP, ~1.8 MB
```

Environment overrides:

| Variable | Effect |
| --- | --- |
| `CMAKE_GENERATOR_NAME` | generator, default `Visual Studio 17 2022` |
| `CDIE_DATABASE_DIR` | directory holding `db`, `db_extra`, `db_custom`; `NONE` packages the binary alone |

### Bundling the signature database

A scanner without signatures is not useful, so the install rules bundle a
database when they can find one. The CMake cache variable
`CDIE_DATABASE_DIR` controls this:

* unset — look for `../_mylibs/Detect-It-Easy` next to the source tree and
  use it if it has a `db` subdirectory;
* a path — use that directory's `db`, `db_extra` and `db_custom`;
* `NONE` — package the executable only.

```bash
cmake -S cdie_source -B cdie_build -DCDIE_DATABASE_DIR=/opt/detect-it-easy
cmake -S cdie_source -B cdie_build -DCDIE_DATABASE_DIR=NONE
```

Because `cdie` looks for `db`, `db_extra` and `db_custom` next to its own
executable, an extracted package runs with no arguments at all:

```text
> cdie.exe C:\utils\python3\python.exe
PE64
    Linker: Microsoft Linker(14.50.35225)
    Compiler: Microsoft Visual C/C++(19.50.35225)[POGO_O_C]
    ...
```

### Other platforms

There is no `.deb`/`.pkg` script yet. `cpack -G TGZ` (or `DEB`) against a
configured build tree works, since the CPack metadata is set up in the root
`CMakeLists.txt`:

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release
cmake --build cdie_build
cpack --config cdie_build/CPackConfig.cmake -G TGZ
```

## Running

```bash
cdie -D /path/to/Detect-It-Easy/db \
     -E /path/to/Detect-It-Easy/db_extra \
     -C /path/to/Detect-It-Easy/db_custom \
     target.exe
```

Without `-D`/`-E`/`-C` the tool looks for `db`, `db_extra` and `db_custom`
next to the executable, then in the working directory.

## Tests

```bash
cmake -S cdie_source -B cdie_build -DCDIE_BUILD_TESTS=ON
cmake --build cdie_build
cdie_build/tests/cdie_test_runtime
```

`tests/test_runtime.c` covers the runtime layer — the formatter, both
directions of the `double` ↔ text conversions, the math subset and the string
and memory stubs. It exits with the number of failures, so `ctest` or a shell
script can use it directly. See
[TESTING.md](TESTING.md#testing-the-runtime-primitives).

## Comparing against the reference

The point of the port is byte-identical output, so the natural test is a diff
against `diec`:

```powershell
$root = "C:\path\to\Detect-It-Easy"
$a = & "$root\diec.exe" $target | Out-String
$b = & cdie.exe -D "$root\db" -E "$root\db_extra" -C "$root\db_custom" $target | Out-String
if ($a.Trim() -eq $b.Trim()) { "IDENTICAL" } else { "DIFFERENT" }
```

```bash
diff <(diec "$target") <(cdie -D "$root/db" -E "$root/db_extra" -C "$root/db_custom" "$target")
```

### Two traps when comparing on Windows

* **Pass the same databases.** `diec` loads `db`, `db_extra` and `db_custom`
  from its own directory. If you only give `cdie` the main database, extra
  detections such as `Microsoft Visual C/C++(…, by EP)` will be missing.
* **Watch WOW64 redirection.** The stock `diec.exe` is a 32-bit binary, so
  `C:\Windows\System32\foo.dll` silently resolves to `SysWOW64\foo.dll` for
  it but not for a 64-bit `cdie`. Compare inside `SysWOW64` (or any
  non-redirected directory) to avoid diffing two different files.

## Checking the database parses

A quick way to validate the JavaScript front end against a whole database is
to parse every file and report syntax errors. The engine exposes
`js_parse_program()` for exactly this; a ~40 line harness that walks a
directory is enough. Against the stock database every one of the 2098
signature scripts parses; the only failures are the `.png`, `.txt` and
`.json` files that live in sub-directories the loader ignores anyway.

## Layout of the build

`src/CMakeLists.txt` collects every source file into `CDIE_SOURCES` and
`src/console/CMakeLists.txt` links them into the executable. Adding a new
format parser means dropping the files into `src/format/` and adding them to
`CDIE_SOURCES`; adding script API functions means one enum value, one
`switch` case and one table row in `src/engine/api.c`.
