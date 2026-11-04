# Building CDiE

## Requirements

- A C11 compiler and CMake 3.21+ (3.26+ for MSVC ARM64).
- Windows: Visual Studio C++ build tools; use a Developer Command Prompt.
- macOS: Xcode Command Line Tools.
- Linux: `pkg-config` and GTK 3.16+ development files. On Debian/Ubuntu:
  `sudo apt install build-essential cmake pkg-config libgtk-3-dev`.

Dependencies are loaded from `dep/` or the sibling `_mylibs/` directory:
`xxfclib`, `cdisasm`, `xxwidgets` and `Detect-It-Easy`.
Database updates also need `xxnetlib` and `xxgithub`.
For a Git checkout with submodules, initialize them first:

```sh
git submodule update --init --recursive
```

## Build

Run from the `cdie_source` directory:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

On Windows or Linux, add `-DCDIE_BUILD_GUI=OFF` to build without the GUI.
To use a database elsewhere, add
`-DCDIE_DATABASE_DIR=/path/to/Detect-It-Easy` (the folder containing `db/`).

## Run

- Linux: `build/src/gui/die` or `build/src/console/diec FILE`.
- Windows: `die.exe` and `diec.exe` in the corresponding build folders;
  Visual Studio builds put them under `Release/`.
- macOS: `open build/src/gui/die.app` (GUI only).
