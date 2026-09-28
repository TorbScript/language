# Working on the VS Code extension

`editors/vscode/` is the TorbScript extension for VS Code as the stores publish it: plain JavaScript, no dependencies,
no build step - the folder is the extension. `README.md` is its page in the stores; this file is for working on it and
is not packed.

## Using it in this repository

**Decision: the repository recommends the published extension, and a contributor who changes the extension runs the
working copy.** `.vscode/extensions.json` recommends `torbscript.torbscript`, so VS Code offers it from the
Marketplace or Open VSX when the repository is opened. It finds `build/release/torb` below the workspace folder before
anything else, so it runs the compiler of this checkout.

- **Working on the extension**: Run and Debug -> "TorbScript extension" (`.vscode/launch.json`) starts an Extension
  Development Host with `editors/vscode` loaded over whatever is installed; "Developer: Reload Window" in that window
  picks up a change of a `.js` or `.json` file.
- **The working copy as the everyday extension**: pack it and install the `.vsix`, which replaces the installed one
  until the next release updates it:

  ```console
  $ sh tools/package-extension.sh
  wrote build/extension/torbscript-0.1.0.vsix
  $ code --install-extension build/extension/torbscript-0.1.0.vsix
  ```

  `tools/package-extension.sh` needs Node.js 20 or newer (it runs `@vscode/vsce` through `npx`, pinned).

## Files

| File | Purpose |
|------|---------|
| `package.json` | The manifest: the language, the grammars, the commands, the walkthrough, the settings, the palette |
| `extension.js` | Activation; highlights `trb` blocks in the Markdown preview (highlight.js classes); the semantic tokens of `torb highlight` where the language server does not run; "New Project..." and "Run File" |
| `lsp-client.js` | The client of `torb lsp`: the Language Server Protocol over the server's standard input and output, with no dependency |
| `toolchain.js` | Finding `torb` (`torbscript.executablePath`, `build/release`, `PATH`, the installers' places), `torb --version`, the context key `torbscript.toolchainFound`, the status bar item, the installer as a task |
| `testing.js` | The Test Explorer: discovery through the request `torbscript/tests`, runs through `torb test --report json` |
| `formatting.js` | Format Document through `torb format` over a temporary copy |
| `syntaxes/trb.tmLanguage.json` | The TextMate grammar for the editor (`source.trb`) |
| `syntaxes/trb.markdown.tmLanguage.json` | Injects `source.trb` into fenced code blocks in Markdown |
| `language-configuration.json` | Comments, brackets, indentation, doc comment continuation |
| `snippets/trb.json` | The snippets |
| `walkthrough/*.svg` | The images of the walkthrough's steps - **placeholders**, see "The brand" |
| `images/icon.png`, `images/icon.svg` | The icon - **a placeholder**, see "The brand" |
| `samples/tokens.trb` | Every semantic token kind in one file, to see the palette in place; checked and formatted by the gates like any other file |

## The brand

The icon, the gallery banner and the walkthrough's images are placeholders until the brand round delivers them:

- `images/icon.svg` is the source and `images/icon.png` the 256 x 256 PNG that `package.json`'s `icon` names (the
  stores take a PNG only). Replace both.
- `galleryBanner` in `package.json` (`color`, `theme`) is the band behind the icon on the store page.
- `walkthrough/install.svg`, `create.svg`, `run.svg` and `next.svg` are the `media.image` of the four steps; a step
  takes `{ "light": ..., "dark": ..., "hc": ... }` in place of one path where the image needs a version per theme.

## The grammar

The grammar follows the language reference. When the syntax changes, change `syntaxes/trb.tmLanguage.json` and the
keyword lists of `extension.js` - both follow `compiler/src/syntax/token.trb`'s `TokenKind`, which is where to check
what changed. What it knows: comments and doc comments; strings with `{interpolation}`, `\{`, `"""` and raw strings;
declarations, modifiers, control flow and contextual keywords (`from` only in `use ... from "..."`); types, `Self`,
`Some`/`Ok`/`Fail`/`None`; calls, generic calls, trailing closures, labels and implicit parameters; command calls at
the start of a statement and on member paths - a heuristic, since a grammar has no types and colors
`database { ... }` as a call whether it configures a field or calls a method.

## Semantic tokens

The legend (`TOKEN_TYPES` and `TOKEN_MODIFIERS` of `extension.js`) is the language server's, in the same order, so
the integers of `textDocument/semanticTokens/full` are handed to VS Code as they are. Where the server does not run,
`torb highlight --stdin` (`compiler/src/highlight/`) answers one JSON document per request, from the syntax tree
alone: it assumes `value.field` is a field and `value.method()` a method, guesses what a single-segment
`use Name from "..."` names from its first letter, and gives a name it cannot place no token at all, so the grammar's
color shows. `mutable` is the one modifier that is not standard (declared in `semanticTokenModifiers`); `readonly`,
`static`, `declaration` and `defaultLibrary` are set where they apply. The colors are the theme's: `configurationDefaults` ->
`editor.semanticTokenColorCustomizations` sets no color, because a pinned palette cannot hold on light and dark
themes alike, only the two styles of the modifiers. To underline nothing and
italicize a `var` instead, change `"*.mutable:trb": { "underline": true }` to `{ "fontStyle": "italic" }`.

## Tests

The Test Explorer lists every `*.test.trb` of the workspace and asks the language server for the `group` and `test`
calls of each (`torbscript/tests`, `docs/tooling/torb-lsp.md`) - also for a file that is not open, and again half a
second after the last change of an open one. A run is `torb test --report json` in the workspace folder, with
`--native` for the profile "Run Natively" and a `--filter "<group> > <test>"` per chosen group or test; whole files of
one folder run in one process, a file with chosen groups or tests in one of its own
(`docs/tooling/torb-test.md` has the report's events). The id of an item is `<file URI>#<full name>`, and the full name
is what the report and `--filter` both use. The site of a failure is written as the compiler names the file - the
package's name in front of the path inside the package (`acme/shop/tests/cart.test.trb`) - so the extension takes the
name off one segment at a time and looks for the rest below the test file's package, then below the workspace folder.

A `torb` older than `--report json` takes the flag for a path and answers `error: --report: The file does not exist`.
The extension reads that answer instead of showing it, remembers it for that binary (its path, version and time of
writing, so a rebuilt `torb` is asked again), runs the files again as `torb test <file>...` and reads the plain report
(`  ok      <name>`, `  FAILED  <name>` and the failure's lines indented by ten spaces), and says once that `torb
upgrade` brings the rest: without `--filter` a single test runs its whole file, and the plain report has no durations.

## Tasks

`.vscode/tasks.json` of the repository has two tasks (Terminal > Run Task...) that run `build/release/torb`:
`torb: check workspace` (`torb check .`, with the `$torb` problem matcher of this extension, so every diagnostic
becomes an entry of the Problems panel) and `torb: test compiler`. The matcher reads the two lines of a diagnostic of
`torb check` (`compiler/src/cli/render.trb`): `error: <message>`, then ` --> <file>:<line>:<column>`.

## Releasing

The version is the toolchain's: `tools/package-extension.sh` refuses a `package.json` whose `version` is not the one of
`project.trb`, and the release workflow publishes the `.vsix` of a release to the Visual Studio Marketplace and to Open
VSX (`docs/contributing/releasing.md`). A release adds its section to `CHANGELOG.md`.
