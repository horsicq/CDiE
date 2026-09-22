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

No Qt, no third-party libraries, no code generators. Just a C compiler.

On Windows it goes one step further: the runtime layer is Win32 only, and the
64-bit MSVC build links no C runtime at all — the finished executable imports
**23 functions from `KERNEL32.dll` and nothing else**. That includes its own
correctly-rounded `strtod`/`dtoa` and its own libm. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#the-runtime-layer).

Verified against `diec` on a 348-file corpus (Windows system DLLs, Visual
Studio .NET assemblies, Qt binaries, Python, plus a directory of scripts,
text, images, an APK and a PDF): **348 identical, 0 different**. A 30-file
subset is additionally compared across every output mode — `--json`, `--xml`,
`--csv`, `--tsv`, `--plaintext`, `--verbose` — and matches byte for byte in
all of them. Builds warning-free as strict ISO C99 under both MSVC and GCC,
with
identical output from each. See [docs/TESTING.md](docs/TESTING.md).

---

## Quick start

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release
cmake --build cdie_build
```

A portable Windows package (executable + database + docs, ~1.8 MB zipped):

```bat
packaging\windows\build_portable_windows.cmd x64 win64_msvc2022
```

```bash
cdie -D /path/to/Detect-It-Easy/db /path/to/target.exe
```

Example, matching `diec` byte for byte:

```text
PE64
    Linker: Microsoft Linker(14.50.35225)
    Compiler: Microsoft Visual C/C++(19.50.35225)[LTCG/C]
    Tool: Microsoft Visual Studio(2026, 18.0-18.3)
    Sign tool: Windows Authenticode(2.0)[PKCS #7]
    Debug data: Records[CodeView, VC Feature, POGO]
```

---

## Command line

```text
cdie [options] target
```

The short options are the ones `diec` uses, so existing command lines work
unchanged. Note that several are not the mnemonic you would guess — `-a` is
`--alltypes`, not aggressive scan, and `-p` is `--plaintext`, not profiling.
Where `cdie` previously used a different letter, that spelling is still
accepted as an alias (shown in parentheses) except where it would collide.

| Option | Meaning |
| --- | --- |
| `-h`, `-?`, `--help` | show usage |
| `-v`, `--version` | show the version |
| `-r`, `--recursivescan` | descend into directories |
| `-d`, `--deepscan` | enable deep-scan signatures (`DS.*`, `EP.*`) |
| `-u` (`-he`), `--heuristicscan` | enable heuristic signatures (`HEUR.*`) |
| `-g`, `--aggressivecscan` | aggressive scan (`--aggressivescan` also accepted) |
| `-b` (`-V`), `--verbose` | verbose detections (operation system, language, …) |
| `-a` (`-A`), `--alltypes` | accepted, not implemented (see LIMITATIONS.md) |
| `-f`, `--format` | put a space before `(version)` and `[info]` |
| `-U` (`-hu`), `--hideunknown` | drop `Unknown: Unknown` results |
| `-M` (`-m`), `--messages` | print engine/script messages on stderr |
| `-l`, `--profiling` | enable script profiling flags |
| `-j`, `--json` | JSON output |
| `-x`, `--xml` | XML output |
| `-c`, `--csv` / `-t`, `--tsv` | delimited output |
| `-p` (`-P`), `--plaintext` | plain text output |
| `-D`, `--database <path>` | main database directory |
| `-E`, `--extradatabase <path>` | extra database directory |
| `-C`, `--customdatabase <path>` | custom database directory |
| `-s`, `--showdatabase` | print database paths and signature counts |

`diec`'s `-S`/`--struct`, `-w`/`--showstructs`, `-e`/`--entropy`,
`-i`/`--info`, `--test` and `--createtest` are not ported; see
[docs/LIMITATIONS.md](docs/LIMITATIONS.md).

When a database path is not given, `cdie` looks for `db`, `db_extra` and
`db_custom` next to the executable and then in the working directory.

Exit codes follow the original tool: `0` success, `1` file not found,
`2` cannot open file, `3` database not found, `4` invalid parameter.

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
| [docs/DATABASE.md](docs/DATABASE.md) | database layout, load order, priorities, signature syntax |
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
    └── console/              command line front end
```

## License

MIT — see [LICENSE](LICENSE).
