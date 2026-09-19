# TorbScript for VS Code

Syntax highlighting for

- `.trb` files
- ` ```trb ` (or ` ```torbscript `) code blocks in Markdown, in the editor **and** in the preview

plus comment toggling, bracket matching, auto-closing pairs and doc comment continuation.

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
| `extension.js`                          | Highlights `trb` blocks in the Markdown preview (the preview uses highlight.js classes, not TextMate) |
| `language-configuration.json`           | Comments, brackets, indentation, doc comment continuation                    |

## What the grammar knows

- `//`, `/* */`, `/** */` doc comments
- Strings with `{interpolation}` (nested, full highlighting inside), `\{` escapes, `"""` multi-line strings,
  raw strings (`r"..."`, `r"""..."""`), chars
- Declarations (`fn`, `type`, `trait`, `extend`, `case`, `const`, `var`), modifiers, control flow,
  contextual keywords (`from` only in `use ... from "..."`)
- Types (uppercase names), primitives, `Self`, `Some`/`Ok`/`Error`/`None`
- Calls, generic calls (`load<Config>(path)`), trailing closures (`.map { }`), labels (`to: "x"`), implicit parameters (`_`, `_2`)
- Command calls at the start of a statement (`route "/health", to: "health"`, `database {`) and on member paths
  (`expect(x).toBe 3`). This is a heuristic: a TextMate grammar has no types, so it cannot know that `port 8080` is a
  property command and not a call. Both are colored as calls.

The grammar follows `CONCEPT.md`. When the syntax changes, change `trb.tmLanguage.json` and the keyword lists in
`extension.js`.

## Tasks

`.vscode/tasks.json` has two tasks (Terminal > Run Task...), both run from `bootstrap/` on stage 0:

| Task                       | Runs                                                | Problem matcher |
|-----------------------------|-----------------------------------------------------|------------------|
| `torb: check workspace`    | `cargo run --release -q -- run ../compiler check ..` | `$torb`, so every diagnostic becomes an entry in the Problems panel with a click-through location |
| `torb: test compiler`      | `cargo run --release -q -- test ../compiler/tests`   | none - the pass/fail counts are read from the terminal |

The `torb` problem matcher (contributed by this extension) reads the two-line diagnostics of `torb check`/`parse`
(`compiler/src/cli/render.trb`):

```text
error: Comparisons do not chain. Use `&&`: `a < b && b < c`
 --> ../compiler/src/example.trb:12:19
  |
12 | const chained = a < b > c
  |                       ^
```

The first line is the message, the second the location; `torb`'s diagnostics show the path relative to where the
toolchain was started (here `bootstrap/`, which is why the task's problem matcher resolves file locations against
`bootstrap/` rather than the workspace root).

There is no language server yet (milestone 8): these tasks are the whole IDE integration for now, and the reason
`torb check`/`torb test` do not need their own terminal habits memorized.
