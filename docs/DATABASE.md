# The signature database

`cdie` reads the stock Detect It Easy database unchanged. No conversion, no
index, no cache.

## Layout

```text
db/
├── _init                      framework script, file type "unknown"
├── _debug                     helper, included by _init
├── _runtime_helpers           String/Array polyfills, included by _init
├── language                   _setLang helper, included by _init
├── read                       byte readers, included by Binary/_init
├── FASM, FPC, Borland, ...    include-only helper scripts
├── PE/
│   ├── _init                  PE framework: PE.section[], PE.resource[], ...
│   ├── _PE.0.sg
│   ├── _linkers.6.sg
│   ├── _Microsoft.6.sg
│   ├── sign_tool_Windows_Authenticode.7.sg
│   └── ...
├── Binary/
├── ELF/
├── MACH/
└── ...
```

A **directory** maps to a file type; files directly in the database root have
file type *unknown* and are only reachable through `includeScript()`.

Recognised directories: `Binary`, `COM`, `Archive`, `ZIP`, `JAR`, `APK`,
`IPA`, `NPM`, `MACHOFAT`, `DEX`, `MSDOS`, `LE`, `LX`, `NE`, `PE`, `ELF`,
`MACH`, `DOS16M`, `DOS4G`, `Amiga`, `AtariST`, `JavaClass`, `PYC`, `PDF`,
`CFBF`, `Image`, `JPEG`, `PNG`, `RAR`, `ISO9660`.

A file is a signature when it is a regular file whose extension is `sg` or
empty. Sub-directories inside a format directory (`PE/dotnet_only`,
`PE/native_only`, `_icons`, `.vscode`) are **not** scanned — the reference
loader is non-recursive, and `cdie` matches that.

## Signature tags

Per-signature tags live in `db/_metadata/<type>.txt`. Each non-empty line has
this form:

```text
<relative-name>#TAG1;TAG2
```

`<type>` is the database format directory and `<relative-name>` is the
signature path within it. The separator is the **last** `#` on the line, so a
signature name such as `protector_CC#3.2.sg` remains valid. Tags are separated
with semicolons. Readers also accept older comma-separated metadata. Metadata
tag names match case-insensitively and exactly; a filter for `build` does not
match `build-tool`.

`--includetags` (alias `--include-tags`) runs a signature when it has any tag
in the requested list. `--excludetags` (alias `--exclude-tags`) skips a
signature when it has any excluded tag, and exclusion takes precedence when a
tag matches both filters. Command-line lists use semicolons, for example
`--includetags "build;protection"`. An empty include list permits every tag;
an empty exclude list excludes none. Framework `_init` scripts are not normal
signature-loop entries and continue to run regardless of these filters.

Metadata mappings remain in the combined database after the main database is
loaded. Signatures appended later from the custom database therefore receive
matching tags from the main metadata index, including entries whose relative
names were not present in the main database itself. (xxfclib still accepts a
deprecated `DB_EXTRA` kind with its own `_metadata` index; no cdie frontend
loads one.)

The rules Detect It Easy used to ship in a separate extra database are part of
the main database, tagged `extra`; they run by default, and
`--excludetags extra` skips them.

## Load order and priority

Two databases are loaded in sequence: main (`-D`) and custom (`-C`). A
`db_extra` folder is never loaded. **Each is sorted on its own and appended
after the previous one.**
The combined list is never re-sorted.

Within one database the sort key is:

1. file type (enumeration order — "unknown" first, then `Binary`, `COM`,
   `MSDOS`, …, `PE`, …);
2. the **priority digit**: for a name with more than one dot, the segment
   before the extension (`_Microsoft.6.sg` → `6`,
   `compiler_EP.MSVC.4.sg` → `4`); otherwise `9`;
3. the file name.

This ordering is load-bearing. Scripts such as `_Microsoft.6.sg` guard their
output with `if (!_isResultPresent("compiler", name))`, so running them in a
different order changes what is printed.

## Execution rules

`_init` scripts never run through the normal loop. Before the loop the driver
evaluates:

1. the database-root `_init` (file type unknown), then
2. the `_init` of the detected file type.

The last match in list order wins for each, so the custom database can
override the framework.

A signature runs when all of the following hold:

* `xft_check(signature type, file type)` — a `PE` entry matches `PE32`/`PE64`,
  an `ELF` entry matches `ELF32`/`ELF64`, a `MACH` entry matches
  `MACHO32`/`MACHO64`, otherwise the types must be equal. Note that a
  `Binary` entry does **not** match a `PE64` file;
* the name's first dot-segment, uppercased, is not `DS` or `EP` unless
  `--deepscan` is given;
* that same prefix is not `HEUR` unless `--heuristicscan` is given;
* the name is not `_init`;
* the owning database is enabled.

Each script is evaluated in the global scope, then its `detect()` function is
called with `(bShowType, bShowVersion, bShowInfo)`.

## Signature syntax

`PE.compare()`, `PE.compareEP()`, `PE.findSignature()` and friends take a
signature string. `xb_convert_signature` normalises it first: text between
single quotes becomes hex, `?` becomes `.`, spaces are dropped and hex digits
are lowercased.

| Token | Meaning |
| --- | --- |
| `4d5a` | literal bytes |
| `'MZ'` | ASCII text, converted to bytes |
| `..` | skip one byte (wildcard) |
| `??` | same as `..` |
| `**` | one byte, must not be zero |
| `%%` | one printable ASCII byte |
| `%&` | one ASCII digit |
| `!%` | one non-printable byte |
| `_%` | one byte that is neither printable nor zero |
| `$$$$$$$$` | relative offset: read a signed displacement of *n*/2 bytes and jump |
| `########` | absolute address: read an address of *n*/2 bytes and jump |
| `#[base]##` | as above, with a base address |
| `++` | search forward for the following bytes, window `32 × n` |

Example, the Microsoft linker stub check from `_linkers.6.sg`:

```js
PE.compare("'MZ'90000300000004000000FFFF0000B8000000000000004000...")
```

and a relative jump from `sfx_Zip_SFX.2.sg`:

```js
PE.compareEP("e8$$$$$$$$8bff558bec83ec..a1........8365....8365....5357bf........bb")
```

## Result records

`_setResult(type, name, version, options)` appends a record. The scan driver
sorts records by the type priority:

| Priority | Types |
| --- | --- |
| 10 | operation system, virtual machine |
| 12 | format |
| 14 | platform, DOS extender |
| 20 | linker |
| 30 | compiler |
| 40 | language |
| 50 | library |
| 60 | tool, PE tool, sign tool, APK tool |
| 70 | protector, cryptor, crypter, virus, malware, trojan, corrupted data, personal data, author |
| 80 | .NET/APK/JAR obfuscator |
| 90 | dongle protection, protection |
| 100 | packer, .NET compressor |
| 110 | joiner |
| 120 | SFX, installer |
| 200 | debug data |
| 1000 | anything else |

The sort is stable, so records with equal priority keep the order in which the
scripts produced them.

A leading `~` marks a heuristic type and prints `(Heur)`; a leading `!` marks
an "aggressive heuristic" type and prints `(A-Heur)`.

## Updating the database

`die-update` replaces the main database folder (`db`) with the `db/` folder of
a release archive; `diec --checkupdate`/`--update` run it, and the GUIs'
**Update database** runs the same code in-process. The command line, the
settings and the sources it accepts are described in the
[README](../README.md#updating-the-signature-database).

### The archive

The default source is the `current-database` release of
`horsicq/Detect-It-Easy`, whose assets are:

```text
db.zip                 the archive
├── db/                the main database, laid out as above
└── (other folders)    ignored (the published archive still carries db_extra/)
db.zip.md5             optional: "<32 hex digits>", which may be followed
                       by "  db.zip" or " *db.zip"
```

Only members under `db/` are extracted; every other member (another top
folder such as `db_extra/` or `db_custom/`, or an absolute name) is skipped. A
member under `db/` whose name would leave it (`..`, a device name) fails the
whole update, and so does an archive without `db/`. Existing `db_extra` and
`db_custom` folders are never touched.

Until Detect It Easy publishes the merged `db/`, whose rules carry the `extra`
tag, the `current-database` archive still holds the former extra rules only
under `db_extra/`, so an installation that `die-update` fills lacks them.

### The stamp file

Each installation writes `db/die-update.ini`:

```ini
; Written by die-update. Identifies the archive this database came from.
[Update]
Source=https://github.com/horsicq/Detect-It-Easy/releases/tag/current-database
Archive=https://github.com/horsicq/Detect-It-Easy/releases/download/current-database/db.zip
Release=current-database
Md5=<MD5 of the archive>
Asset=<asset id>:<size>:<upload time, Unix seconds>
Installed=2026-11-01T09:00:00Z
```

`Release` and `Asset` are present only for a GitHub release asset. The file is
not a signature: only files with the extension `sg` or none are loaded.

An update is available when:

1. there is no database folder; or
2. the source publishes an MD5 (`db.zip.md5`, or `<url>.md5` beside a plain
   zip URL) and it differs from `Md5`; or
3. without a published MD5, the GitHub asset's `<id>:<size>:<upload time>`
   differs from `Asset`; or
4. neither can be compared: no stamp, or a plain zip URL with no `.md5`.

In the last case `--check` always reports an update. Without a published
MD5, an update downloads the archive and replaces nothing when its MD5 equals
the installed one (unless `--force` is given); a re-uploaded asset with the
same bytes only has its new `Asset` recorded.

### Installing

0. **Check the target.** An existing folder is replaced only when it looks
   like a signature database (`_init`, `*.sg`, an engine file-type folder,
   or `die-update.ini`), and never when it is a drive root, the home folder,
   the program's folder, the current folder, or a folder named `db_extra`
   (cdie no longer uses one, and it may hold the user's own rules). The
   default database of a read-only installation (an AppImage, a `/usr`
   package, a macOS app bundle) is not tried in place at all: the update
   goes straight to the per-user folder.
1. **Stage.** A fresh `.die-update-<id>` folder in `<parent>`, the folder
   that holds `db`, so every rename stays on one volume. Creating it is also
   the test for a writable folder: when the *default* folder cannot be
   written and the installation is not portable, the update moves to the
   per-user folder instead.
2. **Lock.** `<parent>/.die-update.lock`, one per parent folder. A second
   update, from the console or a GUI, fails with exit code 7 instead of
   racing the first; the operating system releases the lock when a process
   dies. With the lock held, stages of earlier runs that were killed are
   cleaned up (see below).
3. **Download and verify.** The archive (at most 256 MB) is downloaded into
   the stage and its size (as listed by the release) and MD5 checked.
4. **Extract** `db/` into the stage, checking each member's
   CRC (members up to 64 MB, 1 GB and 200000 members in all). On Linux and
   macOS the extracted tree gets ordinary modes: folders 0755, files 0644.
5. **Prove.** The new `db` is loaded once and must yield at least one
   signature.
6. **Record.** `<stage>/targets.ini` names where the folder about to be
   moved out comes from (`old-db=<path>`).
7. **Swap.** The old `db` is renamed into the stage and the new one into
   place. If the second rename fails, the first is undone. A stop request
   (Ctrl+C, Cancel) is honoured up to this step and ignored during it.
8. **Stamp.** `die-update.ini` is written into the new `db` last, through a
   temporary file: a run that dies before this leaves a database without a
   stamp, which the next check reports as an update.
9. **Clean up.** The stage is removed, unless it still holds an old folder
   that a rollback could not put back.

A later run cleans up a killed run's stages: an old folder whose recorded
place is empty is moved back; one whose place is taken (the swap had
finished) is deleted with its stage; and a stage holding an old folder it
cannot place -- no record, or the move back fails -- is kept untouched, so
the only copy of a database is never deleted.

Stages left by an earlier version, which also replaced `db_extra`, are
handled the same way: an `old-db_extra` recorded in `targets.ini` goes back
to its place when that is empty. Only stages beside `db` are visited; one
left beside a `db_extra` in another folder is never visited, so it is never
deleted either.
