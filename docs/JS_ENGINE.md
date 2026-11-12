# The JavaScript engine

The signature database is JavaScript, so `cdie` ships its own interpreter.
It targets the language actually used by the DIE rules: an ES5 subset with a
few ES2015 conveniences, no modules, no generators, no `async`.

Files:

| File | Role |
| --- | --- |
| `js.h` | public API used by the rest of the program |
| `js_internal.h` | shared internals: values, objects, property maps, context |
| `js_lex.h/.c` | tokeniser |
| `js_ast.h`, `js_parse.c` | recursive-descent parser producing an AST |
| `js_interp.c` | tree-walking evaluator |
| `js_builtins.c` | Object/Function/Array/String/Number/Boolean/Math/JSON |
| `js_regexp.c` | backtracking regular-expression engine + `RegExp` object |
| `js_engine.c` | context creation, teardown, `js_eval` |

---

## Values

```c
typedef enum { JT_UNDEF, JT_NULL, JT_BOOL, JT_NUM, JT_STR, JT_OBJ } JSTag;

typedef struct {
    JSTag tag;
    union { int b; double n; JSStr *s; JSObj *o; } u;
} JSVal;
```

Numbers are IEEE-754 doubles, as in ECMAScript. Strings are **byte strings**
(`JSStr` = length + NUL-terminated buffer); see *Text encoding* below.

Objects carry a class tag (`JCLASS_OBJECT`, `ARRAY`, `FUNCTION`, `NATIVE`,
`REGEXP`, `STRING`, `SCOPE`, …), a prototype pointer and an ordered property
map.

### Property maps

A property map is an append-only array of entries plus an open-addressing hash
index. Keeping insertion order matters: `for…in` and `Object.keys` must
enumerate in the order the DIE scripts expect, and `JSON.stringify` output
depends on it.

Array elements are ordinary properties keyed by their decimal index, and the
array object tracks `nArrayLen` separately. That is why the framework script
can write `aMatch[-1] = n` and `PE.section[".rsrc"] = …` on the same array —
non-index keys simply become normal properties.

### Memory model

Objects, scopes and strings are registered in engine-wide lists and released
in one sweep when the context is destroyed (`js_free`). Reference counters are
maintained but never trigger an eager free, because closures and scopes form
cycles by construction (a function references its defining scope, which
contains the function). A scan creates one context per file, so the arena is
bounded by a single file's script activity.

---

## Supported grammar

**Statements** — `var` / `let` / `const` (all function-scoped), `function`
declarations and expressions, `if`/`else`, `for`, `for…in`, `while`,
`do…while`, `switch` with fall-through, `break`/`continue` with labels,
`return`, `throw`, `try`/`catch`/`finally`, labelled statements, blocks and
the empty statement.

**Expressions** — assignment (`=` and all compound forms), the conditional
operator, comma, `||`, `&&`, bitwise `| ^ &`, equality (`== != === !==`),
relational (`< > <= >=`, `in`, `instanceof`), shifts (`<< >> >>>`), additive,
multiplicative, unary (`! ~ + - typeof void delete`), prefix/postfix
`++`/`--`, `new`, calls, member and index access, array literals, object
literals (identifier, string and numeric keys), regular-expression literals,
`this` and grouping.

**Semantics** — closures, prototype chains, `arguments`, named function
expressions, `var` and function-declaration hoisting, automatic semicolon
insertion, and the sloppy-mode rule that assigning to an undeclared name
creates a global.

Not implemented: `with`, getters/setters in object literals, destructuring,
spread, arrow functions, classes, generators, `async`/`await`, `Symbol`,
`Proxy`, typed arrays, `Date`. None of these appear in the database.

---

## Standard library

| Global | Members |
| --- | --- |
| `Object` | `keys`, `defineProperty`; prototype: `hasOwnProperty`, `toString`, `toLocaleString`, `valueOf` |
| `Function` | prototype: `call`, `apply`, `bind`, `toString` |
| `Array` | `isArray`; prototype: `push`, `pop`, `shift`, `unshift`, `join`, `slice`, `splice`, `concat`, `indexOf`, `lastIndexOf`, `reverse`, `sort`, `forEach`, `map`, `filter`, `every`, `some`, `reduce`, `toString` |
| `String` | `fromCharCode`; prototype: `charAt`, `charCodeAt`, `indexOf`, `lastIndexOf`, `slice`, `substring`, `substr`, `toUpperCase`, `toLowerCase`, `toLocaleUpperCase`, `toLocaleLowerCase`, `trim`, `concat`, `split`, `match`, `search`, `replace`, `toString`, `valueOf`, `length` |
| `Number` | `MAX_SAFE_INTEGER`, `MIN_SAFE_INTEGER`, `MAX_VALUE`, `MIN_VALUE`, `NaN`, `POSITIVE_INFINITY`, `NEGATIVE_INFINITY`; prototype: `toString(radix)`, `valueOf`, `toFixed`, `toPrecision`, `toLocaleString` |
| `Boolean` | prototype: `toString`, `valueOf` |
| `Math` | `abs`, `floor`, `ceil`, `round`, `sqrt`, `log`, `log2`, `log10`, `exp`, `sin`, `cos`, `tan`, `trunc`, `sign`, `min`, `max`, `pow`, `random`, `PI`, `E`, `LN2`, `LN10` |
| `JSON` | `stringify`, `parse` |
| `RegExp` | prototype: `test`, `exec`, `toString`; properties: `source`, `flags`, `global`, `ignoreCase`, `multiline`, `lastIndex` |
| globals | `parseInt`, `parseFloat`, `isNaN`, `isFinite`, `NaN`, `Infinity`, `undefined`, `globalThis`, `Error` |

`String.prototype.repeat`, `padStart`, `startsWith`, `endsWith`,
`replaceAll`, `includes` and `Array.prototype.includes` are intentionally
**not** provided: the DIE framework file `_runtime_helpers` polyfills them
behind `if (!String.prototype.x)` guards, and letting those guards fire keeps
`cdie` behaviourally identical to the reference engine.

Number→string conversion reproduces ECMAScript's shortest round-tripping
representation, so `(0.1 + 0.2).toString()` yields
`0.30000000000000004` and `1e21` yields `1e+21`. `toFixed` and `toPrecision`
follow their own spec algorithms rather than `printf`'s `%f`/`%g`, which are
not the same thing: `toPrecision` keeps trailing zeros
(`(1e-7).toPrecision(2)` is `"1.0e-7"`), switches to exponential at
`e < -6` rather than `e < -4`, and writes the exponent unpadded (`1.2e+3`,
not `1.2e+03`).

None of this goes through a C library — the conversions are implemented in
`core/utils_fp.c`; see
[ARCHITECTURE.md](ARCHITECTURE.md#floating-point).

---

## Regular expressions

`js_regexp.c` is a backtracking matcher over a small AST.

Supported: literals, `.`, character classes with ranges, negation and the
`\d \D \w \W \s \S` shorthands, escapes (`\n \r \t \f \v \0 \xHH \uHHHH`),
greedy and lazy quantifiers (`* + ? {n} {n,} {n,m}` and their `?` variants),
capturing groups, non-capturing groups `(?:…)`, named groups `(?<name>…)`
(treated as capturing), alternation, anchors `^ $`, word boundaries
`\b \B`, back references `\1`–`\9`, and look-ahead `(?=…)` / `(?!…)`.
Flags: `g`, `i`, `m`.

Matching uses an **explicit continuation chain** rather than rewriting the
AST, so a quantified group backtracks correctly and captures are restored on
failure:

```c
struct RECont {
    int nKind;        /* 0 = match a node chain, 1 = resume a repetition */
    RENode *pNode;    /* kind 0 */
    RENode *pRepeat;  /* kind 1 */
    int nDone;
    size_t nLastPos;
    RECont *pNext;
};
```

A zero-width iteration terminates the repetition, which prevents the classic
`(a*)*` infinite loop. A step counter caps pathological backtracking.

`String.prototype.replace` supports `$&`, `` $` ``, `$'`, `$$` and `$1`–`$99`
as well as function replacers; `split` accepts a regular expression and keeps
capture groups.

---

## Text encoding

Strings are byte strings. Source files and script literals pass through
unchanged, `read_ansiString` yields Latin-1 bytes and `read_unicodeString`
converts UTF-16 to UTF-8.

The consequence is that `.length`, `charAt` and index access count **bytes**,
not UTF-16 code units, for non-ASCII text. The DIE database compares ASCII
identifiers, version strings and section names, so this is not observable in
practice; it is documented here because it is the one intentional deviation
from ECMAScript semantics.

---

## Errors

Exceptions set a flag plus a value on the context. Every evaluation step
checks the flag and unwinds, which keeps the interpreter free of `longjmp`
and makes native functions safe to call from anywhere. `try`/`catch`/`finally`
save and restore the flag. A syntax error becomes a pending exception whose
message carries the line number.

The **parser** works the same way, and for a second reason. `JSParser` carries
a `bError` flag; once it is set `tok()` reports end-of-input forever, so every
loop in the recursive-descent parser terminates on its own and control returns
through the ordinary call chain. The alternative — `setjmp`/`longjmp` — would
pull in the C runtime, which the Windows build does not link
(see [ARCHITECTURE.md](ARCHITECTURE.md#the-runtime-layer)). Loops that consume
a delimited list (object and array literals, argument and parameter lists)
check for EOF explicitly so a truncated file cannot spin.

The scan driver reports a failing script as
`<script name>: <message>` in `xx_scan_result.ppErrors`, printed with `-m`.
A failing script never aborts the scan — this mirrors `DiE_Script::_handleError`.

---

## Embedding

```c
JSCtx *pCtx = js_new();

js_def_fn(pCtx, "print", my_print, 1, NULL);

if (!js_eval(pCtx, "print('hello ' + (6 * 7));", "<test>", NULL)) {
    fprintf(stderr, "%s\n", js_error(pCtx));
}

js_free(pCtx);
```

A native function has the signature

```c
JSVal fn(JSCtx *pCtx, JSVal thisVal, int nArgc, JSVal *pArgv, void *pUser);
```

and returns an **owned** value. Arguments are borrowed. `js_dup` adds a
reference, `js_release` drops one.
