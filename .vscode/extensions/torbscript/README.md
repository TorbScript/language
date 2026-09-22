# TorbScript for VS Code

Syntax highlighting for

- `.trb` files
- ` ```trb ` (or ` ```torbscript `) code blocks in Markdown, in the editor **and** in the preview

plus comment toggling, bracket matching, auto-closing pairs, doc comment continuation, and **semantic
highlighting**: fields, locals, parameters, cases, methods, functions and generics each in their own color, driven
by `torb highlight` (see "Semantic highlighting" below) - a TextMate grammar alone cannot tell them apart, since it
never sees the syntax tree.

## Install

The extension lives in `.vscode/extensions/torbscript`, which makes it a _local workspace extension_
(VS Code 1.89+): open the repository folder, go to the Extensions view, and install "TorbScript" from the
**Recommended** section ("Workspace Recommendations"). Reload the window afterwards.

Alternatively link it into your user extensions (works everywhere, also outside of this workspace):

```powershell
New-Item -ItemType Junction -Path "$env:USERPROFILE\.vscode\extensions\torbscript" -Target "$PWD\.vscode\extensions\torbscript"
```

```sh
ln -s "$PWD/.vscode/extensions/torbscript" ~/.vscode/extensions/torbscript
```

## Files

| File                                    | Purpose                                                                      |
|-----------------------------------------|------------------------------------------------------------------------------|
| `syntaxes/trb.tmLanguage.json`          | TextMate grammar for the editor (`source.trb`)                               |
| `syntaxes/trb.markdown.tmLanguage.json` | Injects `source.trb` into fenced code blocks in Markdown                     |
| `extension.js`                          | Highlights `trb` blocks in the Markdown preview (highlight.js classes, not TextMate), and runs `torb highlight` for semantic highlighting in the editor |
| `language-configuration.json`           | Comments, brackets, indentation, doc comment continuation                    |
| `samples/tokens.trb`                    | One file exercising every semantic token kind and modifier - open it to see the palette below in place |

## What the grammar knows

- `//`, `/* */`, `/** */` doc comments
- Strings with `{interpolation}` (nested, full highlighting inside), `\{` escapes, `"""` multi-line strings,
  raw strings (`r"..."`, `r"""..."""`), chars
- Declarations (`fn`, `type`, `trait`, `extend`, `case`, `const`, `var`), modifiers, control flow,
  contextual keywords (`from` only in `use ... from "..."`)
- Types (uppercase names), primitives, `Self`, `Some`/`Ok`/`Fail`/`None`
- Calls, generic calls (`load<Config>(path)`), trailing closures (`.map { }`), labels (`to: "x"`), implicit parameters (`_`, `_2`)
- Command calls at the start of a statement (`route "/health", to: "health"`, `database {`) and on member paths
  (`expect(x).toBe 3`). This is a heuristic: a TextMate grammar has no types, so it cannot know that `port 8080` is a
  property command and not a call. Both are colored as calls.

The grammar follows `CONCEPT.md`. When the syntax changes, change `trb.tmLanguage.json` and the keyword lists in
`extension.js` - both are derived from `compiler/src/syntax/token.trb`'s `TokenKind`, so that is
where to check what changed.

## Semantic highlighting

A TextMate grammar is regular expressions over the raw text: it cannot know whether `x` is a field, a local or a
parameter, whether `Circle` is a case or a type, or whether `describe` is a method or a free function - all of that
needs the syntax tree. `torb highlight` (`compiler/src/highlight/`, documented in
[docs/tooling/the-torb-command](../../../docs/tooling/the-torb-command.md)) is the answer: it parses the file with the
compiler's own front end and prints one JSON document of semantic tokens, which `extension.js` turns into a
`vscode.SemanticTokensBuilder` result through a `DocumentSemanticTokensProvider` registered for the `trb` language.
VS Code layers semantic tokens on top of the TextMate grammar, so a name this provider does not color (an unresolved
import, an arbitrary receiver's field) just keeps its TextMate color instead of going blank.

**What it can tell apart** (see the module comment of `compiler/src/highlight/resolve.trb` for the exact rules):
types, traits, generic parameters (including `const` ones), cases (`case Circle`, `.Circle`, `Shape.Circle`, and the
prelude's `Some`/`None`/`Ok`/`Fail` bare), namespaces (`use * as name`), functions vs. methods (a dotted call is a
method, a bare one a function - in both command style and call style, which always color the same), parameters vs.
locals vs. fields (a bare field inside one of its type's own methods included, e.g. `sent = sent + 1`), and `const`
vs. `var` (a modifier, not a color - see the palette below).

**What it cannot tell**, because it answers from the syntax tree alone: the real type of an arbitrary receiver
(`value.field` is *assumed* to be a field, `value.method()` a method, purely because of the call after the dot -
that is also all a real type checker's answer would look like from here, so this rarely shows), and what a
single-segment `use Name from "..."` actually names in the file it comes from (guessed from the first letter's
case). A name this tool cannot place gets no token at all, never a wrong one - the TextMate grammar's guess (usually
reasonable) is what shows.

### Settings

| Setting                                       | Default | Meaning |
|------------------------------------------------|---------|---------|
| `torbscript.executablePath`                    | `""`    | Path to `torb`. Empty searches `build/release/torb[.exe]` under every open workspace folder, then falls back to `torb` on `PATH`. |
| `torbscript.semanticHighlighting.enabled`      | `true`  | Turn semantic highlighting off entirely (only the TextMate grammar's colors show). Also what happens automatically if `torb` cannot be found or run - never an error popup, only one line in the "TorbScript" output channel (View > Output). |

### The legend

Token types: `type`, `interface`, `typeParameter`, `enumMember`, `namespace`, `function`, `method`, `parameter`,
`variable`, `property` - all of them standard VS Code semantic token types. One modifier is not standard, though:

- `mutable` (custom, declared in `package.json`'s `semanticTokenModifiers`): a `var` binding, a `var` field, a
  `var` parameter, or a `var fn` - at its declaration and at every call of it. This is deliberately the *only*
  difference from a
  `const`/non-`var` name of the same kind - too many colors was the complaint this design started from, so `var`
  vs. `const` is an underline, not a hue.
- The standard modifiers `declaration`, `readonly` (a `const`/non-`var` name), `static` (a `static fn` and a
  `static` value, where they are declared and where they are used) and `defaultLibrary`
  (`Some`/`None`/`Ok`/`Fail`, and the primitive types) are also set where they apply. `static` is italic
  (`"*.static:trb"` in `package.json`), so `Point.origin` reads differently from the field `point.x` without a
  second hue; delete that rule to turn it off.

### The palette

Chosen in the spirit of Visual Studio / VS Code Dark+ (the owner's preference), applied through
`package.json`'s `configurationDefaults` &rarr; `editor.semanticTokenColorCustomizations`, so it works in any theme
without editing settings - a theme's own semantic colors, or the user's own `editor.semanticTokenColorCustomizations`,
still win if set (VS Code merges `configurationDefaults` per key; the most specific scope wins). Every color below
was checked against the editor background `#1E1E1E` and clears WCAG AA (4.5:1) by a wide margin - the tightest is
`#569CD6` at 5.65:1 (a keyword, colored by the theme itself, not by this extension).

| Kind            | Color     | Note |
|------------------|-----------|------|
| `type`           | `#4EC9B0` | Dark+'s own default; pinned so it never depends on the theme |
| `interface` (trait) | `#B8D7A3` | Dark+'s own default |
| `typeParameter` (generics) | `#5DBCD2` | A distinct muted teal - not `type`'s green, not `interface`'s sage |
| `enumMember` (cases) | `#4FC1FF` | Dark+'s own default enum member blue |
| `function`       | `#DCDCAA` | Dark+'s own default |
| `method`         | `#E2C08D` | A warmer, paler yellow than `function`, so the two are never confused |
| `parameter`      | `#9CDCFE` | Dark+'s own default |
| `variable` (locals) | `#C8CCD4` | A neutral, desaturated grey-blue - distinct from `parameter`'s saturated blue |
| `property` (fields) | `#E8E8E8` | Near-white, as Visual Studio itself colors fields - distinct from `variable` and `parameter` |
| keyword (TextMate `keyword.trb`) | `#569CD6` | Dark+'s own default; not a semantic token, the grammar's scope alone gets this from the theme |
| control-flow keyword (`keyword.control.trb`) | `#C586C0` | Dark+'s own default, same reasoning |
| string / number / comment | `#CE9178` / `#B5CEA8` / `#6A9955` | Dark+'s own defaults, untouched |

Several of these already match VS Code's own Dark+ theme; they are pinned explicitly anyway so the palette holds on
every theme, dark or light, not only Dark+. **Light themes**: VS Code's theme-scoped settings
(`"[Theme Name]": {...}`) only match an *exact* theme name, with no wildcard for "any dark theme" - there is no way
to scope `configurationDefaults` to "dark themes in general" without hard-coding a theme name list that would go
stale. Rather than ship a fragile list, this extension applies one palette everywhere; it reads fine on Dark+ and
similar dark themes (the target, per the owner) and is merely less ideal - not broken - on a light one, where
`torbscript.semanticHighlighting.enabled: false` or a project-local override of
`editor.semanticTokenColorCustomizations` is the escape hatch.

**To flip `var`/`const` from underline to italic**, edit one rule in `package.json`: change
`"*.mutable:trb": { "underline": true }` to `{ "fontStyle": "italic" }` (underline was chosen because italic is
already used for comments in most themes, including this grammar's).

## Reloading after a change

- **`.tmLanguage.json` / `language-configuration.json`**: command palette &rarr; "Developer: Reload Window". VS
  Code caches compiled grammars per window.
- **`extension.js` / `package.json`**: also "Developer: Reload Window" (this is a workspace extension, not running
  under the Extension Development Host, so there is no separate debug window to restart).
- **The `torb` binary**: no reload needed - the semantic tokens provider spawns a fresh process for every request
  (VS Code already debounces those while typing), so a rebuilt `build/release/torb` takes effect on the next
  keystroke or file switch. If nothing changes, check the "TorbScript" output channel (View > Output) first:
  the provider never pops up an error, only logs one line there.

## Tasks

`.vscode/tasks.json` has two tasks (Terminal > Run Task...), both run from the workspace root with
`build/release/torb`:

| Task                       | Runs                        | Problem matcher |
|-----------------------------|-----------------------------|------------------|
| `torb: check workspace`    | `torb check .`              | `$torb`, so every diagnostic becomes an entry in the Problems panel with a click-through location |
| `torb: test compiler`      | `torb test compiler/tests`  | none - the pass/fail counts are read from the terminal |

The `torb` problem matcher (contributed by this extension) reads the two-line diagnostics of `torb check`/`parse`
(`compiler/src/cli/render.trb`):

```text
error: Comparisons do not chain. Use `&&`: `a < b && b < c`
 --> compiler/src/example.trb:12:19
  |
12 | const chained = a < b > c
  |                       ^
```

The first line is the message, the second the location; `torb`'s diagnostics show the path relative to where the
toolchain was started, which is the workspace root, and that is what the task's problem matcher resolves them
against.

There is no language server yet (milestone 8): these tasks are the whole IDE integration for now, and the reason
`torb check`/`torb test` do not need their own terminal habits memorized.
