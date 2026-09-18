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

- `//`, nested `/* */`, `/** */` doc comments
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
