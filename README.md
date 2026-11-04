# CDiE

Detect It Easy in C: identify file formats, compilers, packers and protectors
using the DIE signature database. Includes a native desktop GUI, a command-line
scanner and a library API. No Qt is required.

- Windows and Linux: `die` GUI and `diec` command line.
- macOS: native `die.app` GUI.
- Binary inspection tools include hashes, entropy, strings, hex view and disassembly.
- Scan reports support plain text, JSON, XML, CSV and TSV.

## Build

Requires a C11 compiler and CMake 3.21+ for the GUI. Dependencies are loaded from
`dep/` or the sibling `_mylibs/` directory: `xxfclib`, `cdisasm`, `xxwidgets`
and `Detect-It-Easy`. Database updates also use `xxnetlib` and `xxgithub`.
Linux GUI builds need `pkg-config` and GTK 3 development files.

Run from this directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

On Windows or Linux, add `-DCDIE_BUILD_GUI=OFF` for a console-only build.
Set `CDIE_DATABASE_DIR` if the signature database is stored elsewhere.

## Use

```sh
diec target.exe
diec --json target.exe
diec -D /path/to/Detect-It-Easy/db target.exe
```

## Updating the signature database

Run `die-update --check` to check for updates, or `die-update` to install them.

See [building](docs/BUILDING.md), [database](docs/DATABASE.md),
[library API](docs/LIBRARY.md), [tests](docs/TESTING.md) and
[limitations](docs/LIMITATIONS.md) for details.

MIT license. See [LICENSE](LICENSE).
