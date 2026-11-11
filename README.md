# CDiE

Detect It Easy in C: identify file formats, compilers, packers and protectors
using the DIE signature database. Includes a native desktop GUI, a command-line
scanner and a library API. No Qt is required.

- Windows and Linux: `die` GUI and `diec` command line.
- macOS: native `die.app` GUI.
- Binary inspection tools include hashes, entropy, strings, hex view and disassembly.
- Scan reports support plain text, JSON, XML, CSV and TSV.

## Build

See [build instructions](docs/BUILD.md).

## Use

```sh
diec target.exe
diec --json target.exe
diec -D /path/to/Detect-It-Easy/db target.exe
```

## Updating the signature database

Run `die-update --check` to check for updates, or `die-update` to install them.

## Microsoft Store

DevX-Cipher has my permission to publish CDiE on the Microsoft Store. Issues with that package can be reported here

MIT license. See [LICENSE](LICENSE).
