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
| `lsp-client.js` | The client of `torb lsp`: the Language Server Protocol over the server's standard input and output, with no dependency - diagnostics, hover, definition, references, rename, completion, signature help, symbols, formatting for `formatting.js`, semantic tokens, quick fixes |
| `toolchain.js` | Finding `torb` (`torbscript.executablePath`, `build/release`, `PATH`, the installers' places), `torb --version`, the context key `torbscript.toolchainFound`, the status bar item, the installer as a task |
| `testing.js` | The Test Explorer: discovery through the request `torbscript/tests`, runs through `torb test --report json`, and the Debug profile |
| `debugging.js` | The debugger: `torb debug` as the debug adapter, a configuration for F5 without a `launch.json`, "Debug File", the CodeLens "Run \| Debug" of an entry file |
| `formatting.js` | Format Document and Format Selection: through the language server where it runs, and through `torb format` over a temporary copy of the whole text where it does not |
| `test/debugging.js`, `test/language-server.js` | What `sh tools/vscode-test.sh [debugging \| language-server]` runs in an Extension Development Host: the debugger, and every feature of the language server through VS Code's own commands |
| `syntaxes/trb.tmLanguage.json` | The TextMate grammar for the editor (`source.trb`) |
| `syntaxes/trb.markdown.tmLanguage.json` | Injects `source.trb` into fenced code blocks in Markdown |
| `language-configuration.json` | Comments, brackets, indentation, doc comment continuation |
| `snippets/trb.json` | The snippets |
| `walkthrough/*-light.svg`, `walkthrough/*-dark.svg` | The images of the walkthrough's steps, one per theme - see "The brand" |
| `images/icon.png`, `images/icon.svg` | The icon of the stores and of the Extensions view - see "The brand" |
| `images/file-icon-light.svg`, `images/file-icon-dark.svg` | The icon of a `.trb` file in the explorer and on tabs - see "The brand" |
| `samples/tokens.trb` | Every semantic token kind in one file, to see the palette in place; checked and formatted by the gates like any other file |
| `test/toolchain.js` | Where `toolchain.js` finds `torb`, against a fake file system and without VS Code: `node editors/vscode/test/toolchain.js`, which the forge's extension job runs |

## The brand

Every image of the extension is TorbScript's brand (`docs/design/BRAND.md`, section 11 "VS Code"), taken from `brand/`
or drawn with its tokens. None is edited by hand: a change starts in BRAND.md or `brand/tokens.json`, and the images
are generated again, like the files of `brand/` themselves, by the brand's script outside the repository (it needs
Node, the Chivo fonts and a rasteriser).

- **The icon.** `images/icon.png` is `brand/icons/app-icon-256.png` - the Orb in its depth gradient, 256 x 256 - and
  is what `package.json`'s `icon` names, because the stores take a PNG of at least 128 x 128 only. `images/icon.svg` is
  `brand/icons/app-icon.svg`, kept beside it as the source and left out of the `.vsix` (`.vscodeignore`). Copy both
  again when the brand changes; nothing else points at them.
- **The gallery banner.** `galleryBanner` in `package.json` is the band behind the icon on the store page: ink
  (`#14110F`, the dark ground of the brand) with `"theme": "dark"`, so the store writes the name and the buttons in
  light text. Never Torb Red: a red band would put the mark on red, which the brand rules out.
- **The file icon.** `contributes.languages[].icon` names `images/file-icon-light.svg` and `images/file-icon-dark.svg`:
  `brand/icons/file-icon.svg` - a page with a straight corner cut, a hairline and the flat mark - drawn on the 16 px grid
  VS Code shows it at, once per ground. On light the page is paper with a `#A9A3A1` hairline, on dark it is `#292422`
  with a `#857E7C` hairline; the mark is Torb Red on both. VS Code shows it for `.trb` files wherever the file icon
  theme has no icon of its own for the language.
- **The walkthrough.** Every step's `media.image` is an object of four paths - `light`, `dark`, `hc` and `hcLight`, all
  four required by VS Code - and each image exists as `<name>-light.svg` and `<name>-dark.svg`; the high-contrast
  themes take the dark and the light one. `install-windows` and `install-posix` belong to the two steps that install
  the toolchain, `create`, `run` and `next` to the other three. Each is 640 x 400, flat, split by 1 px hairlines, with
  its text outlined (the webview has neither Chivo nor Geist Mono, and an SVG image loads no font), the syntax colours
  of BRAND.md section 9, and at most one red: the flat mark in `install-*`, the error in `run` (tint, leading border,
  icon and the word, BRAND.md section 4.6), the top rule of torb.dev in `next`, none in `create`. No screenshot of a
  particular theme, and no red button: the walkthrough's buttons are VS Code's own.
- **The colours of the code.** The extension pins no colour (see "Semantic tokens"); the two colour themes of
  BRAND.md section 11, "TorbScript Light" and "TorbScript Dark", are still to come.

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

## The debugger

`torb debug` is the debug adapter (docs/tooling/torb-debug.md): `debugging.js` registers it for the type `torbscript`
with the `torb` of `toolchain.js`, and fills in a configuration for F5 without a `launch.json` - the file in the
editor, its tests where it is a test file. The Test Explorer's profile "Debug" starts one session per file with
`test`, the chosen names as `filter` and `report: "json"`; the adapter hands the report's lines on as
`torbscript/testReport` events, and a `DebugAdapterTracker` maps them onto the run's items with the same code the
Run profile reads a report with. The session's `configuration` carries `torbscriptTestRun`, which is how the tracker
finds its run.

## Trying it in a real VS Code

`sh tools/vscode-test.sh language-server` starts an Extension Development Host over the package of
`tests/lsp/navigation/` and a file with a problem beside it, and runs `test/language-server.js`: a hover, signature
help, the references, the symbols, a completion and its documentation, the diagnostics of the file nobody opened, a
keystroke until its diagnostics arrive, formatting and a rename - each through the command VS Code's editors run
(`vscode.executeHoverProvider`, `vscode.executeDocumentRenameProvider`, ...). `sh tools/vscode-test.sh` runs the
debugger's suite. Both open a window for as long as they run, so neither is a gate; run them after a change of the
extension or of the server, with `VSCODE_TEST_TORB` naming another `torb`.

## Tasks

`.vscode/tasks.json` of the repository has two tasks (Terminal > Run Task...) that run `build/release/torb`:
`torb: check workspace` (`torb check .`, with the `$torb` problem matcher of this extension, so every diagnostic
becomes an entry of the Problems panel) and `torb: test compiler`. The matcher reads the two lines of a diagnostic of
`torb check` (`compiler/src/cli/render.trb`): `error: <message>`, then ` --> <file>:<line>:<column>`.

## Releasing

The version is the toolchain's: `tools/package-extension.sh` refuses a `package.json` whose `version` is not the one of
`project.trb`, and the release workflow publishes the `.vsix` of a release to the Visual Studio Marketplace and to Open
VSX (`docs/contributing/releasing.md`). A release adds its section to `CHANGELOG.md`.
