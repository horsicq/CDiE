# GUI translation

The native CDiE GUI uses the UTF-8 PO reader from xxfclib through
xxwidgets. One catalog translates both CDiE captions and xxwidgets controls.
The scan engine retains its identifiers and report data. Console scanning and
structured reports keep their existing output.

Select a catalog when starting the GUI:

```sh
die --translation /path/to/de.po file.bin
CDIE_TRANSLATION=/path/to/de.po die file.bin
```

On Windows, the same option works with `die.exe`;
Unicode command-line and environment paths are converted to UTF-8. Options may
precede or follow the initial file; `--` ends option recognition. An explicit
`--translation` overrides the environment. Repeated explicit options use the
last path. A missing path is an argument error. With neither option nor a
nonempty environment value, the interface uses English.

The catalog is loaded before controls are created, so initial captions, common
buttons, scan options, status text and modal titles share the selected language.
An unreadable or malformed catalog reports an error at startup. Missing, empty
or fuzzy messages fall back to the original English. Languages are selected at
startup; changing already-created labels requires restarting the GUI.

`translations/de.po` is the German catalog. It includes shared xxwidgets
messages and CDiE captions. Untranslated text remains English. `cdie.catalog`
and `xxwidgets.catalog` load the German catalogs and check sample translations
from the main window, Options and the signature editor. The template
`translations/cdie.pot` combines source messages from CDiE and the configured
xxwidgets source tree. Built frontend directories carry a `translations` folder
beside the executable; a macOS bundle carries it in `Contents/Resources`.
Linux installation puts catalogs in `share/cdie/translations` under the install
prefix. The command-line path is interpreted relative to the current directory;
use an absolute path when launching from other directories.

## Editing catalogs

No gettext runtime or system locale installation is needed. GNU gettext tools
are optional maintenance tools:

```sh
cmake --build build --target cdie_update_translations
msgmerge --update translations/de.po translations/cdie.pot
msgfmt --check -o /dev/null translations/de.po
```

New source captions use `CDIE_TR("English text")`. Static source text can be
marked with `CDIE_N_` and translated when displayed. Captions returned by an
engine (file names, signatures, versions, tags, format names) remain application
data. Only the UI's Automatic sentinel is translated; its numeric value stays
zero. Cocoa menu/accessibility labels use equivalent NSString helpers.

Formatted messages use `CDIE_FMT` or `cdie_translation_snprintf`; the Windows
helper bounds UTF-16 formatting. Preserve the exact ordered printf conversion
specifications, including widths, precisions and length modifiers. Adding,
removing or changing conversions, `%n`, or positional reordering causes the
runtime to use the original English format. UTF-8 truncation removes an
incomplete final character. This validation also works for Cocoa `%@` messages.
The template marks formatted messages so `msgfmt --check` can validate them.

The small helper in `src/app/cdie_translation.*` binds the single interactive
application without owning its catalog. GUI startup calls
`cdie_translation_open` before creating controls and unbinds before destroying
the xxwidgets app. Other engine and library entry points do not acquire this
process-local UI binding.

## Verification

Enable focused regressions without the full engine compatibility suite:

```sh
cmake -S . -B build -DCDIE_BUILD_GUI=ON -DCDIE_BUILD_TRANSLATION_TESTS=ON
cmake --build build --target cdie_test_translation
ctest --test-dir build -R '^cdie.translation$' --output-on-failure
```

Linux GUI tests use a virtual display when `xvfb-run` is available. The regression
checks command-line parsing, shared widget/application messages, failure
preservation, environment precedence, format rejection and UTF-8 truncation.

`cdie.translations` (`tests/test_translations.py`, no gettext tools needed)
checks the shipped catalogs on every platform: the template holds every
translatable literal of `src/` and none that the sources no longer have (a
renamed caption otherwise silently stays English), each catalog holds exactly
the template's messages with a `Language` header naming its file, plural
entries carry `nplurals` forms, and a translation keeps the shape of its
original: the same printf conversions in the same order, the same trailing
newline, surrounding spaces, trailing colon and ellipsis. With the GUI built,
`gui_catalog_probe` also loads every catalog with the real reader. On macOS,
`gui_cocoa_de` runs the whole Cocoa dialog smoke under `de.po`: the driver
matches captions in English or their translation, so a translated caption the
code still compares in English fails there.
