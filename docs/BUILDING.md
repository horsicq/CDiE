# Building and testing

## Requirements

A C11 compiler and CMake 3.16+. No Qt or code generation is required. The
xxfclib engine uses `cdisasm` for x86 instruction decoding and Intel text
formatting. CMake builds `cdisasm` from source with xxfclib and links it
statically, so it adds no DLL dependency. In the local source layout,
`cdisasm` lives at `_mylibs/cdisasm`; initialize the bundled dependencies in a
Git checkout before building. Set `-DXXFC_CDISASM_DIR=<path>` if the sources
are elsewhere. Its license and generated-data notices are in the `cdisasm`
source tree.

The earlier scanner build was tested with MSVC 19.44 (Visual Studio 2022) on
Windows and GCC 11.4 on Linux, with identical scan output. The `cdisasm`
version builds with Clang 21 on Linux and passes the two available CDiE tests.
Recheck corpus parity after the decoder migration. Windows and macOS builds
have not yet been exercised with this decoder.

The CDiE sources compile as **strict ISO C99** — `CMAKE_C_EXTENSIONS` is `OFF`,
so GCC gets `-std=c99` rather than `-std=gnu99` for those sources. The xxfclib
and cdisasm dependencies use C11. Strict C99 hides POSIX declarations behind
feature-test macros, which is why `src/app/cdie_app.c` defines
`_POSIX_C_SOURCE` before its first include. A new CDiE source file that calls
POSIX functions needs the same preamble.

## Build

For a Git checkout, initialize the bundled dependencies first:

```bash
git submodule update --init --recursive
```

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

### Windows GUI

The Windows build also produces `src/gui/cdie_gui.exe`. Its native front end
uses `dep/xxwidgets` when populated, otherwise the sibling
`../_mylibs/xxwidgets` (override `CDIE_XXWIDGETS_DIR` when it is elsewhere).
The reusable `xxwidgets_scan_panel` provides a result tree, Flags
and Databases checkbox combo boxes, and Scan and Report buttons. Each file
contains detection nodes with Type, Name, Version, and Info details. Report
opens the full formatted engine output and supports copying it.

Flags and optional database selections take effect on the next scan; the main
database is always used. Options edits the same values and keeps both combo
boxes synchronized. The Database paths button edits signature folders;
failed reloads and Cancel retain the previously loaded database. About uses
the reusable xxwidgets dialog. Enter or F5 scans, Ctrl+O opens a file, and
Ctrl+Shift+D opens database paths.

The GUI uses a hosted C runtime. With `CDIE_NO_CRT=ON`, the GUI and xxwidgets
link the runtime statically, while the console retains its CRT-free entry
point and import behavior.

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
binary, plus cdisasm's license and generated-data notices under
`licenses/cdisasm/` and the signature database when one was found (see below).

## Packaging

### GitHub Beta packages

Run **Build and publish Beta** from the GitHub Actions tab on `main`. The
workflow runs only when started manually. It builds portable ZIP packages on
Ubuntu 24.04 and Windows 2022, then publishes both to the `Beta` prerelease
after checking that `main` still points at the packaged commit.

Both builds use the same resolved revisions of `xxfclib`, `cdisasm`, and the
Detect It Easy signature database; Windows also uses `xxwidgets`. These are
checked out from the `horsicq` GitHub repositories into `dep/`. If any of those
repositories are private, add a `CDIE_DEPS_TOKEN` Actions secret with read
access to them. Public repositories work with the workflow's default token.

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
`cdie` builds xxfclib and its cdisasm decoder from source, so the platform and
the package suffix are the only arguments.

The default generator is Ninja. It builds the ~3400 xxfclib sources several
times faster than a Visual Studio generator. Run the script from any prompt:
when `cl.exe` is not already on `PATH` it finds Visual Studio with `vswhere`
and calls `vcvarsall` for the requested platform (`x64`, `x64_x86` for `Win32`,
`x64_arm64` for `ARM64`). From a Developer Command Prompt it uses that
environment, but stops if the prompt targets a different architecture.

Following the repo convention, build trees and CPack staging live under
`%TEMP%` and only finished artefacts land in `release\`:

```text
release\
├── cdie_win64_msvc2022_portable_1.0.0\   ready-to-run folder
│   ├── cdie.exe
│   ├── db\  db_extra\  db_custom\
│   ├── docs\
│   ├── licenses\cdisasm\
│   └── README.md  LICENSE  changelog.txt
└── cdie_win_x64_portable_1.0.0.zip       CPack ZIP
```

Environment overrides:

| Variable | Effect |
| --- | --- |
| `CMAKE_GENERATOR_NAME` | generator, default `Ninja`; a Visual Studio generator (e.g. `Visual Studio 17 2022`) also works but builds much more slowly |
| `CDIE_DATABASE_DIR` | directory holding `db`, `db_extra`, `db_custom`; `NONE` packages the binary alone |

### Bundling the signature database

A scanner without signatures is not useful, so the install rules bundle a
database when they can find one. The CMake cache variable
`CDIE_DATABASE_DIR` controls this:

* unset — use `dep/Detect-It-Easy` if it has a `db` subdirectory, otherwise
  use the sibling `../_mylibs/Detect-It-Easy` if available;
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
