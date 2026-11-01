# Building and testing

## Requirements

A C11 compiler and CMake 3.16+. The CRT-free MSVC ARM64 build needs CMake
3.26+ for `ASM_MARMASM`. No Qt or code generation is required. The
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
Recheck corpus parity after the decoder migration. The Cocoa GUI
also builds with AppleClang on macOS ARM64; the GUI smoke test exercises a
native Mach-O scan. This does not replace a full corpus parity run.

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

Windows and Linux build the native GUI and console scanner. Their output
names are `src/gui/die` and `src/console/diec`, with `.exe` on Windows.
macOS builds only `src/gui/die.app`. The TUI application is disabled on all
platforms, including when an older build cache enables `CDIE_BUILD_TUI`.

The internal CMake targets remain `cdie_gui` and `cdie`. Select the supported
frontends with `CDIE_BUILD_GUI` and `CDIE_BUILD_CONSOLE`; the console is always
disabled on macOS. The GUI requires CMake 3.21+ and `xxwidgets` (set
`CDIE_XXWIDGETS_DIR` to override its location). Linux additionally requires
`pkg-config` and GTK 3 development files (`gtk+-3.0` >= 3.16), for example:

```bash
sudo apt-get install pkg-config libgtk-3-dev
```

On Windows or Linux, set `CDIE_BUILD_GUI=OFF` for a console-only developer
build. Set `CDIE_BUILD_LIBRARY=OFF` to omit the separate library targets.

### Linux GTK GUI

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release -DCDIE_BUILD_GUI=ON
cmake --build cdie_build --target cdie_gui --parallel
cdie_build/src/gui/die /path/to/target
```

For a headless startup and scan check, run `xvfb-run -a
cdie_build/src/gui/die --smoke-test /path/to/target`. Signature databases
selected by `CDIE_DATABASE_DIR` are staged beside the executable. The Linux
frontend also opens without a database; F3 opens Options on the Scan page,
whose database folders load the main and custom folders. Fonts (F4) edits the
shared role-based font settings. Both are saved with the other Options.

With `CDIE_BUILD_TESTS=ON` and `xvfb-run` installed, `cdie.linux_settings`
exercises the actual GTK dialogs, failed-load rollback and retry, folder chooser
acceptance/cancellation, default paths, font validation and rendered font changes.
It repeats the checks without a database beside the executable to cover recovery.

The build stages `db/` and `db_custom/` from `CDIE_DATABASE_DIR` beside the
console and Linux GUI executables. The macOS GUI carries the same folders
inside its app bundle. Linux installs both `die` and `diec` into `bin/`,
sharing the databases in `bin/db` and `bin/db_custom`. A `db_extra/` in
`CDIE_DATABASE_DIR` is neither staged nor installed (cdie has no extra
database; configure reports `cdie: ignoring .../db_extra`).
`CDIE_DATABASE_DIR=NONE` disables this staging.

With the local thin wrapper on Windows or Linux, build both frontends using:

```bash
cmake -S ~/ownCloud_build/cdie -B ~/ownCloud_build/cdie/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCDIE_BUILD_GUI=ON -DCDIE_BUILD_CONSOLE=ON
cmake --build ~/ownCloud_build/cdie/build --target cdie_gui cdie --parallel
```

The wrapper puts these outputs under `build/cdie/src/gui/` and
`build/cdie/src/console/`, respectively. A macOS build produces only the GUI.

### macOS GUI

Use Xcode Command Line Tools (including the macOS SDK and Objective-C compiler)
and CMake 3.21+ for the xxwidgets Cocoa backend. macOS builds only the GUI:

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release \
  -DCDIE_DATABASE_DIR=/path/to/Detect-It-Easy
cmake --build cdie_build --parallel
open cdie_build/src/gui/die.app
```

`CDIE_DATABASE_DIR` contains `db/` and optionally `db_custom/` (a `db_extra/`
there is ignored). The app copies these into `Contents/Resources` when linked,
so it can be moved and opened from Finder. Without an explicit path it uses
the bundled dependency or the local sibling database, like the console
package. Set the option to `NONE` to omit databases and select external
folders under **Database folders…** (Cmd+Shift+D, Options on the Scan page).
Successful folder selections are saved with the other Options in Detect It
Easy's `biz.ntinfo.die` preferences domain. A build tree that bundled a
`db_extra` before keeps that copy (the post-build step warns about it);
package from a fresh tree.

The GUI provides file picking, file drops, Finder/Dock file opening, a parser
selector, the result tree, full text reports, scan options and optimization
settings. Cmd+O opens a file; Cmd+R, F5 or Return scans it. The Windows Explorer
integration is specific to the Windows front end.

Run the Cocoa smoke test in a logged-in desktop session:

```bash
cdie_build/src/gui/die.app/Contents/MacOS/die --smoke-test /usr/bin/file
```

It checks bundled database loading, scanning, explicit Binary selection, report
contents and missing-file recovery. With `CDIE_BUILD_TESTS=ON` it is also
registered as the `gui_cocoa` CTest test when a database is available.

### Windows, MSVC + Ninja

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S cdie_source -B cdie_build
ninja -C cdie_build
```

### The CRT-free Windows build

On native x64 and ARM64 MSVC the executable links **no C runtime at all**:
the option `CDIE_NO_CRT` defaults to `ON`, the entry point becomes
`x_entry_point` instead of `mainCRTStartup`, and the only import is
`KERNEL32.dll`.

```text
> dumpbin /imports diec.exe
    KERNEL32.dll
```

The link flags that produce this live in `src/console/CMakeLists.txt`:
`/NODEFAULTLIB /ENTRY:x_entry_point /SUBSYSTEM:CONSOLE`, plus `/GS-` to drop
the stack cookie (`__security_check_cookie` is a CRT symbol).

Turn it off if you need to link something that expects a runtime:

```bash
cmake -S cdie_source -B cdie_build -DCDIE_NO_CRT=OFF
```

It is already off for Win32 and ARM64EC MSVC, for non-MSVC compilers and for
every non-Windows target — see [LIMITATIONS.md](LIMITATIONS.md#runtime) for
why.

Only the console `diec` executable is CRT-free. The libraries (below) link the
dynamic CRT so they interoperate with normally-compiled consumers.

Two rules apply when adding code that will be compiled into this build:

* **No standard library calls**, including implicit ones. Go through the `x_`
  stubs in `core/utils.h`.
* The freestanding xxfclib build supplies the compiler helpers needed by each
  supported architecture: a page-by-page `__chkstk` probe on x64 and ARM64,
  plus the ARM64 interlocked operations MSVC emits out of line. Large frames
  therefore retain normal Windows guard-page growth without importing the CRT.

### Windows GUI

The Windows build also produces `src/gui/die.exe`. Its native front end
uses `dep/xxwidgets` when populated, otherwise the sibling
`../_mylibs/xxwidgets` (override `CDIE_XXWIDGETS_DIR` when it is elsewhere).
The reusable `xxwidgets_scan_panel` provides a result tree, Flags
and Databases checkbox combo boxes (Databases holds one item, Custom database),
and Scan and Report buttons. The detected
format is the root, with colored `Type: Name (Version) [Info]` detection rows.
Overlay nodes contain their format, offset, size and nested detections. This
tree layout is shared with macOS and the GTK/TUI widget backends. Report opens
the full formatted engine output and supports copying it.

Flags and the custom database selection take effect on the next scan; the
main database is always used. Options edits the same values and keeps both
combo boxes synchronized. Its Scan page holds the two signature folders, main
and custom; failed reloads and Cancel retain the previously loaded database.
About uses the reusable xxwidgets dialog. Enter or F5 scans, Ctrl+O opens a
file, and Ctrl+Shift+D opens Options on the Scan page.

The GUI uses a hosted C runtime. With `CDIE_NO_CRT=ON`, the GUI and xxwidgets
link the runtime statically, while the console retains its CRT-free entry
point and import behavior.

### The libraries

The build also produces a shared and a static library exposing the
die_library C API — `die.dll`/`die.lib` and `die_static.lib` on Windows,
`libdie.so`/`libdie.a` on Linux, and `libdie.dylib`/`libdie.a` on macOS — from
the same engine sources (minus the console's `core/utils_entry.c`). They are
on by default:

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release
cmake --build cdie_build           # builds cdie, die_shared, die_static
cmake -S cdie_source -B cdie_build -DCDIE_BUILD_LIBRARY=OFF   # frontends only
```

See [LIBRARY.md](LIBRARY.md) for the API, the flag mapping and how to compile a
consumer (use `/MD` on MSVC to match the libraries' dynamic CRT).

### Install

```bash
cmake --install cdie_build --prefix /where/you/want
```

The programs (`die`, `diec` and `die-update`) and the signature database go
to one directory, because every program looks for `db/` and `db_custom/`
beside its own executable. `CDIE_INSTALL_LAYOUT` decides where
that is:

| `CDIE_INSTALL_LAYOUT` | Programs and databases | Documents | Use |
| --- | --- | --- | --- |
| `PORTABLE` (default) | `bin/` on Linux, the prefix root elsewhere | prefix root | portable ZIPs, a private folder |
| `FHS` (Linux only) | `lib/cdie/`, with relative symlinks `bin/die`, `bin/diec`, `bin/die-update` | `share/doc/cdie/` | `.deb`, AppImage, `/usr` or `/usr/local` |

`PORTABLE` is what every earlier build installed. In a portable tree
`README.md`, `LICENSE`, `changelog.txt`, `docs/` and cdisasm's license and
generated-data notices (`licenses/cdisasm/`) sit at the prefix root, and on
Windows an empty `PORTABLE` marker sits beside the programs.

`FHS` keeps the programs in a private directory (`CDIE_INSTALL_PKGLIBDIR`,
default `lib/cdie`; not `CMAKE_INSTALL_LIBDIR`, which is the multiarch library
directory under `/usr` on Debian). `bin/` holds relative links such as
`die -> ../lib/cdie/die`, so they resolve inside `DESTDIR`, CPack staging and
an AppDir alike. The programs resolve `/proc/self/exe`, so started through a
link they still find the databases in `lib/cdie/`. The layout also installs
the documents and `copyright` into `share/doc/cdie/`, the translations into
`lib/cdie/translations/`, the desktop entry
`share/applications/io.github.horsicq.cdie.desktop`, the AppStream data
`share/metainfo/io.github.horsicq.cdie.metainfo.xml` and the icon in 48, 128
and 256 pixel sizes under `share/icons/hicolor/`. The package manager owns
`lib/cdie/`, so an `FHS` build compiles the updater with `CDIE_SYSTEM_INSTALL`
(in its build tree too): `die-update` never rewrites the database there and
installs updates into the per-user folder instead (see
[Database updates in packages](#database-updates-in-packages)).

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release \
  -DCDIE_INSTALL_LAYOUT=FHS -DCMAKE_INSTALL_PREFIX=/usr/local -DCDIE_BUILD_LIBRARY=OFF
cmake --build cdie_build --parallel
sudo cmake --install cdie_build
```

The icons in `packaging/linux/icons/` are generated from
`src/gui/images/main.ico` by `packaging/linux/extract_icons.py` (standard
library only); run it again after the icon changes.

## Packaging

Every package carries `die-update`, the signature database updater, when the
build has it. It needs the `xxnetlib` and `xxgithub` sources: CMake looks in
`dep/xxnetlib` and `dep/xxgithub`, then `../_mylibs/`, or takes
`CDIE_XXNETLIB_DIR` and `CDIE_XXGITHUB_DIR`. Without them it prints a warning
and builds every package without `die-update` (and without the GUI's
**Update database**); the packaging scripts then warn instead of failing.

| Package | Script | Programs | Shipped database |
| --- | --- | --- | --- |
| Windows ZIP | `packaging\windows\build_portable_windows.cmd` | `die.exe`, `diec.exe`, `die-update.exe` | `db\` beside them |
| Linux ZIP | CI (`cpack -G ZIP`, `PORTABLE` layout) | `bin/die`, `bin/diec`, `bin/die-update` | `bin/db/` |
| `.deb` | `packaging/debian/build_deb.sh` | `/usr/lib/cdie/`, linked from `/usr/bin` | `/usr/lib/cdie/db/` |
| AppImage | `packaging/appimage/build_appimage.sh` | `usr/lib/cdie/` in the image | `usr/lib/cdie/db/` in the image |
| macOS ZIP | `packaging/macos/build_macos.sh` | `die.app` (`die-update` in `Contents/MacOS`) | `die.app/Contents/Resources/db/` |
| macOS `.pkg` | `packaging/macos/build_macos.sh` | `/Applications/die.app` | `die.app/Contents/Resources/db/` |

### Database updates in packages

`die-update` (and **Update database** in the GUI) replaces the database it
finds the way the scanners do. Where the shipped folder is writable — the
Windows ZIP, whose `PORTABLE` marker sits beside the programs, and a Linux
ZIP extracted into the user's own folder — it is updated in place.

The installations below are read-only: `die-update` never rewrites the
database they ship, even where it could, and goes straight to the user's
data folder (see the table) instead:

* the `.deb` and any other `CDIE_INSTALL_LAYOUT=FHS` install
  (`/usr/lib/cdie`), whose updater the build compiles with
  `CDIE_SYSTEM_INSTALL`. Even `root` gets its own per-user folder
  (`/root/.local/share/NTInfo/cdie`) and never touches files the package
  manager owns: a package upgrade would put those back and leave a mixed
  database that the stored update record still reports as up to date;
* the AppImage: `AppRun` sets `CDIE_READONLY_INSTALL=1`, which also covers
  an image the runtime extracts into a temporary folder and deletes on exit
  (`APPIMAGE_EXTRACT_AND_RUN=1` or `--appimage-extract-and-run`);
* `die.app` on macOS: a database inside an application bundle is never
  updated in place, wherever the bundle is.

Setting the environment variable `CDIE_READONLY_INSTALL` to any non-empty
value gives any other installation without a `PORTABLE` marker the same
treatment (a shared copy on a network drive, say). The rule concerns the
default database folder only; a folder named with `-D` or a stored database
path is updated where it is. Otherwise, when the default database folder
cannot be written and no `PORTABLE` marker sits beside the program,
`die-update` also installs the update into the user's data folder:

| System | Per-user database folder |
| --- | --- |
| Linux | `$XDG_DATA_HOME/NTInfo/cdie` (default `~/.local/share/NTInfo/cdie`) |
| macOS | `~/Library/Application Support/NTInfo/cdie` |
| Windows | `%LOCALAPPDATA%\NTInfo\cdie` |

`die`, `diec` and `die-update` prefer a `db/` there over the shipped one
unless a `PORTABLE` marker sits beside them, so the update takes effect for
that user only and a package upgrade never overwrites it. Delete that folder to return
to the shipped database. `CDIE_USER_DATA_DIR` overrides its location (tests
use it).

On Linux the updater downloads with libcurl, which it loads at run time
(`libcurl.so.4` or `libcurl-gnutls.so.4`); without it only plain `http://`
sources work. The `.deb` depends on it; with the ZIP or the AppImage install
the distribution's libcurl package (`libcurl4`/`libcurl4t64` on Debian and
Ubuntu, `libcurl` on Fedora). Windows uses WinHTTP, and macOS the libcurl
that is part of the system.

### GitHub Beta packages

Run **Build and publish Beta** from the GitHub Actions tab on `master`. The
workflow runs only when started manually. It builds portable ZIP packages on
Ubuntu 24.04 and Windows 2022, the `.deb` and the AppImage on Ubuntu 22.04
(the oldest glibc they support), and on one macOS 15 Apple silicon runner a
portable ZIP, an app ZIP and a `.pkg` for arm64 and for x64 (x86_64 is
cross-compiled there; its programs are run only when the runner has
Rosetta). It publishes all ten files to the `Beta` prerelease after checking
that `master` still points at the packaged commit.

The macOS packages need no signing secrets: without them `die.app` is sealed
ad hoc, the `.pkg` is unsigned and nothing is notarized. Optional Actions
secrets switch signing on: `CDIE_MACOS_CERTIFICATES_P12` (a base64 `.p12`
holding the Developer ID Application and Installer identities) with
`CDIE_MACOS_CERTIFICATES_PASSWORD`, which the job imports into a temporary
keychain, `CDIE_MACOS_CERT_APP` and `CDIE_MACOS_CERT_INSTALL` (the identity
names), and `CDIE_MACOS_NOTARIZE_LOGIN`, `CDIE_MACOS_NOTARIZE_PWD` and
`CDIE_MACOS_NOTARIZE_TEAMID` for notarization. Any of them missing or
unusable only drops that part, as described for `build_macos.sh` below; a
secret that is set but cannot be used also shows up as a warning on the run.
The x64 build needs an `xxfclib` that has `cmake/xxfclib_target_arch.cmake`
(publish `xxfclib` before `cdie`); the job stops at Configure with an error
otherwise.

The builds use the same resolved revisions of `xxfclib`, `cdisasm`, and the
Detect It Easy signature database; every native GUI also uses `xxwidgets`.
These are checked out from the `horsicq` GitHub repositories into `dep/`. If any of those
repositories are private, add a `CDIE_DEPS_TOKEN` Actions secret with read
access to them. Public repositories work with the workflow's default token.
`xxnetlib` and `xxgithub` are optional: when the token cannot see them the
workflow logs a warning and publishes the packages without `die-update`.

### Windows portable package

Windows packaging defaults to a CPack ZIP archive. Its root contains an empty
`PORTABLE` file beside `die.exe`, `diec.exe` and `die-update.exe` (the
updater is checked for whenever the build produced it; without the `xxnetlib`
and `xxgithub` sources the script warns and packages the other two). Being
portable, `die-update.exe` updates the `db\` beside it in place and reads its
settings from `die.ini` there. The package contains no TUI
application. The extracted GUI saves Options in that same `die.ini`, the file
Detect It Easy itself uses when portable, and copies a `cdie.ini` left by an
older version into it once (marking both files `cdie/Migrated`). The native
settings backend (`NTInfo/die`) is used when the marker is absent. The xxfclib
`xx_settings_is_portable()` function checks this executable-local marker;
legacy lowercase `portable` remains supported.

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

The default generator is Ninja. It builds the focused `die_engine` archive
and cdisasm decoder faster than a Visual Studio generator. Run the script from
any prompt:
when `cl.exe` is not already on `PATH` it finds Visual Studio with `vswhere`
and calls `vcvarsall` for the requested platform (`x64`, `x64_x86` for `Win32`,
`x64_arm64` for `ARM64`). From a Developer Command Prompt it uses that
environment, but stops if the prompt targets a different architecture.

Following the repo convention, build trees and CPack staging live under
`%TEMP%` and only finished artefacts land in `release\`:

```text
release\
├── cdie_win64_msvc2022_portable_1.0.0\   ready-to-run folder
│   ├── die.exe  diec.exe  die-update.exe
│   ├── PORTABLE                       empty portable-mode marker
│   ├── db\  db_custom\  info\
│   ├── translations\
│   ├── docs\
│   ├── licenses\cdisasm\
│   └── README.md  LICENSE  changelog.txt
└── cdie_win_x64_portable_1.0.0.zip       CPack ZIP
```

Environment overrides:

| Variable | Effect |
| --- | --- |
| `CMAKE_GENERATOR_NAME` | generator, default `Ninja`; a Visual Studio generator (e.g. `Visual Studio 17 2022`) also works but builds much more slowly |
| `CDIE_DATABASE_DIR` | directory holding `db` and `db_custom`; `NONE` packages the binary alone |

### Bundling the signature database

A scanner without signatures is not useful, so the install rules bundle a
database when they can find one. The CMake cache variable
`CDIE_DATABASE_DIR` controls this:

* unset — use `dep/Detect-It-Easy` if it has a `db` subdirectory, otherwise
  use the sibling `../_mylibs/Detect-It-Easy` if available;
* a path — use that directory's `db` and `db_custom`;
* `NONE` — package the executable only.

cdie has no extra database. A `db_extra` in that directory is never installed
or bundled, and configure says so with `cdie: ignoring <dir>/db_extra (cdie
uses only db and db_custom)`. Its rules are not folded into `db`: Detect It
Easy's own `db` already carries them, tagged `extra`.

```bash
cmake -S cdie_source -B cdie_build -DCDIE_DATABASE_DIR=/opt/detect-it-easy
cmake -S cdie_source -B cdie_build -DCDIE_DATABASE_DIR=NONE
```

Because `diec` looks for `db` and `db_custom` next to its own executable, an
extracted package runs with no arguments at all:

```text
> diec.exe C:\utils\python3\python.exe
PE64
    Linker: Microsoft Linker(14.50.35225)
    Compiler: Microsoft Visual C/C++(19.50.35225)[POGO_O_C]
    ...
```

### Debian package

```bash
CDIE_DATABASE_DIR=/path/to/Detect-It-Easy packaging/debian/build_deb.sh [extra cmake arguments]
```

The script configures a fresh Release build under `$TMPDIR/cdie_deb` with
`CDIE_INSTALL_LAYOUT=FHS`, `CMAKE_INSTALL_PREFIX=/usr` and
`CDIE_BUILD_LIBRARY=OFF`, builds it, runs `cpack -G DEB`, checks the layout
and copies `cdie_<version>_<arch>.deb` to `release/`. It needs `cmake`,
`dpkg-dev` (`dpkg-shlibdeps`, `dpkg-deb`) and `file` (CPack uses it to find
the binaries for `dpkg-shlibdeps`; minimal containers lack it) besides the
build requirements, and checks for them before it builds:

```bash
sudo apt-get install cmake dpkg-dev file
```

Extra arguments go to the CMake configure step, for example
`-DXXFCLIB_DIR=...`. `CDIE_DATABASE_DIR` works as for the other packages; left
unset, CMake picks up `dep/` or `../_mylibs/` and warns that the database
revision is not recorded.

| Variable | Effect |
| --- | --- |
| `CDIE_DATABASE_DIR` | directory holding `db` and `db_custom`; `NONE` packages the programs alone |
| `CMAKE_GENERATOR_NAME` | generator for a new build tree |
| `CDIE_BUILD_DIR` | build there instead of a fresh temporary tree, and keep it (CI shares one tree between the `.deb` and the AppImage) |
| `CDIE_RELEASE_DIR` | where the package goes, default `release/` |
| `KEEP_WORK_ROOT=1` | keep the temporary build tree and CPack staging |

The package `cdie` (section `devel`) installs the FHS layout under `/usr`:
the programs and the database in `/usr/lib/cdie`, links in `/usr/bin`,
desktop entry, icons and AppStream data under `/usr/share`, and documents
with a `copyright` file in `/usr/share/doc/cdie`. `dpkg-shlibdeps` computes
the library dependencies (glibc, GTK 3); libcurl is added by hand as
`libcurl4t64 | libcurl4 | libcurl3t64-gnutls | libcurl3-gnutls`, because
`die-update` loads it with `dlopen()` where `dpkg-shlibdeps` cannot see it.
The package conflicts with and replaces `detectiteasy`, the Qt Detect It Easy
package, which also installs `/usr/bin/die` and `/usr/bin/diec`. Build it on
the oldest release it has to install on: the dependency versions come from
the build system. `cpack -G DEB` refuses a build tree in the `PORTABLE`
layout.

### AppImage

```bash
CDIE_DATABASE_DIR=/path/to/Detect-It-Easy packaging/appimage/build_appimage.sh [--appdir-only] [extra cmake arguments]
```

The script builds the FHS layout like the `.deb`, installs it into an AppDir
(`usr/` with the prefix `/usr`), puts `AppRun`, the desktop entry and the
icon at the AppDir root and runs `appimagetool` with
`APPIMAGE_EXTRACT_AND_RUN=1`, producing `release/cdie_<version>_<arch>.AppImage`.
`appimagetool` is taken from `APPIMAGETOOL`, else from `PATH`, else the
continuous x86_64 build is downloaded from
`github.com/AppImage/appimagetool`. `--appdir-only` stops after the AppDir
has been built and checked and keeps it under `$TMPDIR/cdie_appimage/AppDir`.
The other variables are those of `build_deb.sh`.

`AppRun` (`packaging/appimage/AppRun`) chooses the program:

```bash
./cdie_4.0.0_x86_64.AppImage [file]                  # the GUI (die)
./cdie_4.0.0_x86_64.AppImage diec [options] <file>   # the command-line scanner
./cdie_4.0.0_x86_64.AppImage die-update [options]    # the database updater
```

A symlink to the AppImage named `diec` or `die-update` starts that program.
The image bundles no libraries: GTK 3 and libcurl come from the host, and
the glibc of the build system is the oldest the image runs on, which is why
CI builds it on Ubuntu 22.04. The shipped database inside the image is
read-only: `AppRun` sets `CDIE_READONLY_INSTALL=1`, so `die-update` installs
updates into the per-user folder however the image runs, extracted or
mounted. `AppRun` runs the programs beside itself, never from an `APPDIR`
inherited from another AppImage.

### macOS ZIP and installer

```bash
CDIE_DATABASE_DIR=/path/to/Detect-It-Easy packaging/macos/build_macos.sh [extra cmake arguments]
```

For each architecture the script builds `die.app` (which carries
`die-update` in `Contents/MacOS` and the database in `Contents/Resources`),
installs it into a staging folder, checks with `lipo -archs` that `die` and
`die-update` hold exactly that architecture, signs or seals it, and writes
two files to `release/`:

* `cdie_mac_<arch>_<version>.zip` — `die.app` zipped with
  `ditto -c -k --sequesterRsrc --keepParent`, without `__MACOSX/` entries;
* `cdie_mac_<arch>_<version>.pkg` — a product archive that installs
  `die.app` into `/Applications`, built with `pkgbuild --analyze`,
  `BundleIsRelocatable=false` (so the installer never "upgrades" a copy of
  the app elsewhere on the disk instead), `pkgbuild --component-plist` and
  `productbuild`.

#### Architectures and the oldest supported macOS

By default the script makes `cdie_mac_arm64_*` and `cdie_mac_x64_*`: two
separate single-architecture builds in two build trees. Either kind of Mac
builds both, since Xcode's SDK cross-compiles (x86_64 on Apple silicon,
arm64 on Intel). `MACOS_ARCHITECTURES` (or `CMAKE_OSX_ARCHITECTURES`)
chooses the packages, separated by spaces or commas:

```bash
packaging/macos/build_macos.sh                                      # arm64 and x64
MACOS_ARCHITECTURES=arm64 packaging/macos/build_macos.sh            # cdie_mac_arm64_* only
MACOS_ARCHITECTURES=x86_64 packaging/macos/build_macos.sh           # cdie_mac_x64_* only
MACOS_ARCHITECTURES='arm64;x86_64' packaging/macos/build_macos.sh   # one cdie_mac_universal_*
```

`universal` is the same as `arm64;x86_64`. Pass architectures only this
way, never as `-DCMAKE_OSX_ARCHITECTURES` (the script refuses that).
`CDIE_BUILD_DIR=<dir>` keeps the trees in `<dir>/arm64`, `<dir>/x64` and
`<dir>/universal`; the script refuses a `<dir>` (or a `CDIE_RELEASE_DIR`)
inside `$TMPDIR/cdie_mac`, which every run deletes.

Without a deployment target clang targets the SDK's macOS, so the app would
not start on anything older than the build machine. The script therefore
always sets one: 11.0 for arm64 (the first macOS on Apple silicon) and 10.14
for x86_64 and universal (the compiler raises the arm64 half to 11.0).
10.14 is the oldest macOS the code runs on: the xxwidgets Cocoa backend
calls macOS 10.14 API without `@available` (`NSColor separatorColor`,
`unemphasizedSelectedTextBackgroundColor`, `NSAppearance
bestMatchFromAppearancesWithNames:`, `NSAppearanceNameDarkAqua`), next to
10.13's `NSPasteboardTypeFileURL` and the `clock_gettime` of macOS 10.12 in
xxfclib and xxwidgets; newer API is behind `@available`.
`CMAKE_OSX_DEPLOYMENT_TARGET` (or `MACOSX_DEPLOYMENT_TARGET`) sets one value
for every package, `MACOS_DEPLOYMENT_TARGET_ARM64` and
`MACOS_DEPLOYMENT_TARGET_X86_64` set one architecture (a universal package
uses the x86_64 one).

`die.app`'s `Info.plist` names the same version as `LSMinimumSystemVersion`,
so Finder reports an older macOS instead of the app failing to start. It
comes from `CMAKE_OSX_DEPLOYMENT_TARGET`; a direct CMake build without one
(and without `MACOSX_DEPLOYMENT_TARGET`) gets the script's defaults (11.0 for
arm64 alone, else 10.14) as its deployment target, set before `project()`, so
the compiler targets the same macOS the plist names.

The per-source instruction-set flags of xxfclib (`-mavx2`/`-msse2` for the
x86 SIMD sources, `-march=armv8-a+crc...` for the ARM CRC, Dot Product and
crypto sources) follow the target architectures in
`xxfclib/cmake/xxfclib_target_arch.cmake`, not the host processor, which on
macOS stays the build machine's whatever `CMAKE_OSX_ARCHITECTURES` says. A
universal build passes each flag to its own architecture with
`-Xarch_<arch>`.

#### Signing and notarization

No certificate or password is needed: without them the packages are built
all the same, and whatever is missing, incomplete or unusable is skipped with
a `notice: skipping ...` line instead of an error.

| Variable | Use | When it is missing or unusable |
| --- | --- | --- |
| `X_PRIVATE_CERT_APP` | Developer ID Application identity (its SHA-1, name, or part of the name): signs `die.app` with the hardened runtime and a timestamp | not set, `-`, not listed by `security find-identity -v -p codesigning`, or `codesign` fails with it: no Developer ID signature, `die.app` gets the `MACOS_ADHOC_SIGN` treatment |
| `X_PRIVATE_APP_ENTITLEMENTS` | entitlements for that signature | a path that does not exist is ignored |
| `X_PRIVATE_CERT_INSTALL` | Developer ID Installer identity for the `.pkg`, given the same way | not set, `-`, not listed by `security find-identity -v -p basic`, or `pkgbuild`/`productbuild` fail with it: the `.pkg` is unsigned |
| `X_PRIVATE_KEYCHAIN_PATH` | keychain holding the identities: a path, the path without its `-db` suffix, or a name in `~/Library/Keychains` | none of these exists: neither identity is used |
| `X_PRIVATE_NOTARIZE_KEYCHAIN_PROFILE`, or all of `X_PRIVATE_NOTARIZE_LOGIN`, `X_PRIVATE_NOTARIZE_PWD`, `X_PRIVATE_NOTARIZE_TEAMID` | `xcrun notarytool submit --wait`, then `xcrun stapler staple` | missing or incomplete (the notice names the missing variables, never a value), or no `xcrun notarytool`: nothing is notarized |

The identity variables select an identity from that listing: the one with
that SHA-1 or exactly that name, else among the names containing the text the
first `Developer ID Application:` (or `Developer ID Installer:`) one, else
the first. `codesign` then gets that identity's SHA-1 and
`pkgbuild`/`productbuild` its full name, so the identity checked is the one
used, and an expired certificate of the same name left behind by a renewal
cannot make the name ambiguous.

The zip is notarized (and `die.app` stapled, in both packages) only when
`die.app` was signed with a Developer ID Application identity, since Apple
notarizes nothing else, and the `.pkg` only when it was also signed with a
Developer ID Installer identity. With complete credentials a submission that
fails or that Apple does not answer with `Accepted` (the `status` of
`notarytool --output-format json`) is an error, and the submission's notary
log is printed; a staple that fails is retried three times, 30 seconds apart,
since a new ticket can take a moment to propagate. The `X_PRIVATE_*` names
are the ones the Qt Detect It Easy build scripts use, so a partial set left
in a shell only produces notices. Under GitHub Actions a credential that is
set but cannot be used also produces a `::warning::`, because the run still
succeeds with packages that are not signed or notarized.

Without a Developer ID, `MACOS_ADHOC_SIGN` decides what `die.app` gets:

* `1` (the default): an ad hoc seal of the whole bundle (`codesign --sign -`),
  which needs no certificate. The bundle verifies with `codesign --verify
  --deep --strict` (CI checks that), and a downloaded copy gets Gatekeeper's
  usual "Open Anyway" path. Without a valid seal Gatekeeper may call a
  downloaded app damaged, with no way to open it from Finder.
* `0`: no `codesign` run on the bundle. The script only checks that every
  program in `Contents/MacOS` kept the signature the linker gave it (Apple
  silicon runs no unsigned arm64 code; x86_64 code may be unsigned) and signs
  ad hoc just a program that `cmake --install` left without a valid one. An
  installed `.pkg` or a locally built copy runs; a downloaded zip may be
  reported as damaged.

`CDIE_RELEASE_DIR`, `CMAKE_GENERATOR_NAME` and `KEEP_WORK_ROOT` work as for
the Linux scripts. The script runs with the bash 3.2 that macOS includes.

The CI portable macOS ZIPs (`cpack -G ZIP`, `cdie_mac_arm64_portable_*` and
`cdie_mac_x64_portable_*`) are unchanged in content: `die.app` with the
documents and a copy of the database beside it, and no program at the top
level. CI seals their `die.app` ad hoc too (`codesign --sign -`, as the
`MACOS_ADHOC_SIGN=1` default does) and zips the folder again with `ditto`,
then checks the seal with `codesign --verify --deep --strict`. A configured build tree names its CPack package after
`CMAKE_OSX_ARCHITECTURES` (`x64`, `arm64`, or `universal` for several).

### Other generators

`cpack -G TGZ` against a configured build tree works too, since the CPack
metadata is set up in the root `CMakeLists.txt`:

```bash
cmake -S cdie_source -B cdie_build -DCMAKE_BUILD_TYPE=Release
cmake --build cdie_build
cpack --config cdie_build/CPackConfig.cmake -G TGZ
```

## Running

```bash
diec -D /path/to/Detect-It-Easy/db \
     -C /path/to/Detect-It-Easy/db_custom \
     target.exe
```

Without `-D`/`-C` the tool looks for `db` and `db_custom` next to the
executable, then in the working directory. There is no extra database: `-E`
and `--extradatabase` exit with code 4, and a `db_extra` folder is never
loaded.

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
against the original Qt `diec` (use separate paths because both console
executables are named `diec`):

```powershell
$root = "C:\path\to\Detect-It-Easy"
$a = & "$root\diec.exe" $target | Out-String
$b = & ".\cdie_build\src\console\diec.exe" -D "$root\db" -C "$root\db_custom" $target | Out-String
if ($a.Trim() -eq $b.Trim()) { "IDENTICAL" } else { "DIFFERENT" }
```

```bash
diff <(diec "$target") <(./cdie_build/src/console/diec -D "$root/db" -C "$root/db_custom" "$target")
```

### Two traps when comparing on Windows

* **Pass the same databases.** `diec` loads `db` and `db_custom` from its own
  directory; give this port both with `-D` and `-C`. Compare against a
  Detect It Easy whose `db` holds the former extra rules (tagged `extra`); an
  older one whose `diec` still loads a separate `db_extra` reports detections
  this port does not.
* **Watch WOW64 redirection.** The stock `diec.exe` is a 32-bit binary, so
  `C:\Windows\System32\foo.dll` silently resolves to `SysWOW64\foo.dll` for
  it but not for a 64-bit build of this port. Compare inside `SysWOW64` (or any
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
