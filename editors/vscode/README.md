# TorbScript for Visual Studio Code

[TorbScript](https://torb.dev) is a single-language ecosystem: one language for the program, its configuration, its
build and its tests, with value semantics, run in a VM or compiled to a native binary. This extension brings its
toolchain, `torb`, into VS Code: the language server that is the compiler itself, the tests in the Test Explorer, the
debugger, the formatter, and a walkthrough that installs the toolchain for you.

## Features

- **Problems as you type.** Every error `torb check` reports, while you type, and every finding of `torb lint` as a
  hint with its rule; a binding nothing reads is faded.
- **Quick fixes** (Ctrl+. / Cmd+.): the fix `torb lint --fix` would write, one finding at a time.
- **Hover**: a declaration as the source writes it, with its doc comment; a local or a constant with its type.
- **Go to definition** (F12): locals, functions, types, fields, cases and constants, in any file of the program and in
  the standard library.
- **Completion**: after `value.` its fields and methods, after `Type.` its cases and `static` members, and the names in
  scope everywhere else.
- **Semantic highlighting**: fields, locals, parameters, cases, methods, functions and generics each in their own
  color, and every `var` underlined - from the language server, or from `torb highlight` where it does not run.
- **Tests in the Test Explorer**: every `*.test.trb` of the workspace, with its `group "..."` and `test "..."` calls
  and a run button beside each in the editor. A file, a group or a single test runs with `torb test` - in the VM, or
  natively with the profile "Run Natively" - and a failure shows its message at the line that failed, with the expected
  and the actual value side by side where the failed `assert` compared two. Turn on continuous run (the eye icon) to
  run the chosen tests again on every save.
- **The debugger**: Debug beside Run for every test and group, a CodeLens "Run | Debug" above an entry file, and F5
  for the file in the editor. Breakpoints, stepping (F10, F11, Shift+F11), the call stack, the locals of every frame
  with records, cases, lists and maps to open, values on hover, watch expressions such as `point.x` or `names[2]`, and
  a stop where a panic begins - a failing `assert` too. The program runs in the VM under `torb debug`, and a test's
  result lands in the Testing view as for a run.
- **Format Document** (Shift+Alt+F / Shift+Option+F) with `torb format`: the one layout of the language.
- **Run File**: the play button of the editor runs the file with `torb run`, in a terminal.
- **A walkthrough** (Help > Welcome > Walkthroughs > "Get Started with TorbScript"): install the toolchain, create a
  first program with `torb new`, run it, and find the documentation. It opens by itself the first time the extension
  starts.
- **Syntax highlighting** for `.trb` files and for ` ```trb ` code blocks in Markdown, in the editor and in the preview;
  comment toggling, bracket matching, auto-closing pairs, doc comment continuation, and snippets (`test`, `group`,
  `fn`, `type`, `case`, `trait`, `match`, `use`, `assert`).

## Requirements

The extension runs the TorbScript toolchain, `torb`, for everything but the grammar. **If `torb` is not found, the
extension says so** - once in a notification and in the status bar (`torb missing`) - and offers to install it with the
official installer, in a terminal you can watch. Nothing is installed without your click. To install it yourself:

```sh
curl -fsSL https://torb.dev/install.sh | sh
```

```powershell
irm https://torb.dev/install.ps1 | iex
```

or download an archive from [torb.dev/download](https://torb.dev/download). "TorbScript: Install or Update Toolchain"
runs the installer where there is no `torb`, and `torb upgrade` where there is one.

The extension looks for `torb` in this order: the setting `torbscript.executablePath`; `build/release/torb` below an
open workspace folder (a checkout of the TorbScript repository); `PATH`; and where the installers and package managers
put it (`~/.torb/bin/torb`, `%LOCALAPPDATA%\Programs\torb\bin\torb.exe`, Homebrew, Scoop, winget). A toolchain
installed while VS Code runs is found without a restart. The Test Explorer runs `torb test --report json`; with a
`torb` older than that flag it reads the plain report instead - a single test then runs its whole file, without
durations - and says so once, with `torb upgrade` a click away.

## Getting started

1. Install the extension, and open the walkthrough if it did not open by itself: "TorbScript: Get Started" in the
   command palette.
2. Install the toolchain from the walkthrough's first step; it completes itself once `torb --version` answers.
3. "TorbScript: New Project..." asks for a folder and a name, runs `torb new`, and opens the new package with
   `src/main.trb` in the editor.
4. Press the play button of the editor to run it, and open the Testing view to run `tests/main.test.trb`.

## Settings

| Setting | Default | Meaning |
|---------|---------|---------|
| `torbscript.executablePath` | `""` | The `torb` to run; on Windows `.exe` may be left out. Empty searches the places above, in that order |
| `torbscript.languageServer.enabled` | `true` | Start `torb lsp`. Read when the window loads |
| `torbscript.semanticHighlighting.enabled` | `true` | Semantic colors, from the language server or from `torb highlight`; off leaves the grammar's colors alone |
| `torbscript.toolchain.offerInstall` | `true` | Offer the installer in a notification when `torb` is not found. The status bar item shows either way |

## Commands

| Command | What it does |
|---------|--------------|
| TorbScript: Get Started | Opens the walkthrough |
| TorbScript: Install or Update Toolchain | The official installer in a terminal where there is no `torb`, `torb upgrade` where there is one |
| TorbScript: Run the Toolchain Installer | The official installer, whether or not a `torb` is found |
| TorbScript: Check Toolchain | Looks for `torb` again and says which one it found |
| TorbScript: Open the Download Page | [torb.dev/download](https://torb.dev/download) |
| TorbScript: New Project... | `torb new <name>` in a folder you pick, then opens it |
| TorbScript: Run File | `torb run <file>` in a terminal |
| TorbScript: Debug File | The file under `torb debug`: its tests where it is a `*.test.trb`, the program otherwise |
| TorbScript: Restart Language Server | Starts `torb lsp` again, for example after building a new `torb` |

## Without the language server

Where the language server is turned off, cannot be started, or has crashed three times within three minutes, the
extension says so once in the "TorbScript" output channel (View > Output) and keeps working with less: the TextMate
grammar colors the code, `torb highlight` adds the semantic colors where a `torb` runs at all, and the Test Explorer
lists the test files, each run as a whole. There are no error popups for any of this.

The grammar is a heuristic - regular expressions cannot tell a field from a local or a case from a type - which is why
the semantic colors from the syntax tree are layered over it.

## Colors

The semantic tokens use VS Code's standard types (type, interface, type parameter, enum member, function, method,
parameter, variable, property), so every theme colors them in its own palette, light or dark. The extension adds only
two styles: a `var` is underlined, a `static` member italic. Your `editor.semanticTokenColorCustomizations` win where
they are set.

## Links

- [torb.dev](https://torb.dev) - the language, the download, the documentation.
- [The language server](https://git.torb.dev/torbscript/language/src/branch/main/docs/tooling/torb-lsp.md) - every
  message `torb lsp` answers.
- [The debugger](https://git.torb.dev/torbscript/language/src/branch/main/docs/tooling/torb-debug.md) - what a
  `launch` names, and every request `torb debug` answers.
- [Set up your editor](https://git.torb.dev/torbscript/language/src/branch/main/docs/how-to/set-up-your-editor.md) -
  this extension, and `torb lsp` in Neovim and Helix.
- [Issues](https://git.torb.dev/torbscript/language/issues) - bugs and wishes, of the extension as well.
- [The source of this extension](https://git.torb.dev/torbscript/language/src/branch/main/editors/vscode) - MIT, like
  the rest of TorbScript.
