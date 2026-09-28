---
title: Set up your editor
summary: Install the VS Code extension from the Marketplace, Open VSX or a .vsix - it installs a missing toolchain and brings the language server, the Test Explorer and the formatter - or point any language server client at torb lsp.
kind: how-to
status: stable
order: 5
keywords:
  - editor
  - VS Code
  - extension
  - Marketplace
  - Open VSX
  - vsix
  - language server
  - torb lsp
  - Test Explorer
  - walkthrough
  - Neovim
  - Helix
source:
  - editors/vscode/package.json
  - editors/vscode/toolchain.js
  - editors/vscode/testing.js
  - editors/vscode/lsp-client.js
  - compiler/src/language-server/command.trb
---

An editor understands TorbScript through [`torb lsp`](../tooling/torb-lsp.md), the language server that is the compiler
itself: the diagnostics are `torb check`'s, the types in a hover are the checker's, and a quick fix is what `torb lint
--fix` writes. VS Code gets it from the TorbScript extension, `torbscript.torbscript`, which every release publishes to
the Visual Studio Marketplace and to Open VSX; every other editor with a client of the Language Server Protocol starts
`torb lsp` itself.

## Steps

1. **Install the extension.** In VS Code: the Extensions view (Ctrl+Shift+X), search "TorbScript", Install - or from a
   terminal, `code --install-extension torbscript.torbscript`. VSCodium, Cursor and the other editors built on VS
   Code's open source install the same extension from [Open VSX](https://open-vsx.org/extension/torbscript/torbscript).
   Without a store, download `torbscript-<version>.vsix` from a release at
   [torb.dev/download](https://torb.dev/download) and run `code --install-extension torbscript-<version>.vsix`, or use
   "Extensions: Install from VSIX..." in the command palette; a nightly's `torbscript-nightly-<date>.vsix` is a
   pre-release of `main` that no store has.

2. **Let it find or install the toolchain.** The walkthrough "Get Started with TorbScript" opens the first time the
   extension starts (Help > Welcome > Walkthroughs brings it back). Its first step completes itself once `torb
   --version` answers. The extension looks for `torb` in `torbscript.executablePath`, then in `build/release/torb`
   below the workspace folder (a checkout of this repository), then on `PATH`, then where the installers put it
   (`~/.torb/bin/torb`, `%LOCALAPPDATA%\Programs\torb\bin\torb.exe`, Homebrew, Scoop, winget). Where it finds none, the
   status bar says `torb missing` and a notification offers **Install TorbScript**, which runs the official installer
   in a terminal you can watch - `curl -fsSL https://torb.dev/install.sh | sh`, on Windows
   `irm https://torb.dev/install.ps1 | iex`. Nothing is installed without that click. A toolchain installed while VS Code
   runs is found without a restart, and "TorbScript: Install or Update Toolchain" runs `torb upgrade` once there is one.

3. **Create a program, or open one.** "TorbScript: New Project..." asks for a folder and a name, runs
   [`torb new`](../tooling/torb-new.md) there and opens the new package with `src/main.trb` in the editor. The play
   button of the editor ("TorbScript: Run File") runs the file with `torb run` in a terminal.

4. **Use the language server.** Problems appear as you type; a hint with a lightbulb is a finding of `torb lint`
   whose fix is one click (Ctrl+. or Cmd+.); hover a name for its signature and doc comment; F12 goes to its
   declaration, in the standard library as well; `.` offers the members of what stands in front of it. The
   "TorbScript" output channel (View > Output) shows one line per run of the checker, and why the server did not start
   where it did not. "TorbScript: Restart Language Server" starts it again after a new `torb` was built.

5. **Run the tests from the Testing view.** Every `*.test.trb` of the workspace is listed with its `group` and `test`
   calls, which the language server finds (`torbscript/tests`), and each has a run button in the editor's gutter. A
   file, a group or a single test runs with [`torb test --report json`](../tooling/torb-test.md) and
   `--filter "<group> > <test>"` - in the VM, or natively with the profile "Run Natively". A failure shows its message
   at the line that failed, with the expected and the actual value side by side where the `assert` compared two with
   `==`; continuous run (the eye icon) runs the chosen tests again on every save.

6. **Format with Shift+Alt+F** (Shift+Option+F): "Format Document" runs [`torb format`](../tooling/torb-format.md)
   over the text in the editor.

7. **In another editor, register `torb lsp` for `.trb` files.** It needs no argument (`--stdio` is accepted), speaks
   over standard input and output, and takes the root of the workspace from `initialize`. The file type is `trb`.

## Settings

| Setting | Default | Meaning |
|---------|---------|---------|
| `torbscript.executablePath` | `""` | The `torb` to run. Empty searches the places of step 2, in that order |
| `torbscript.languageServer.enabled` | `true` | Whether `torb lsp` is started at all, read when the window loads |
| `torbscript.semanticHighlighting.enabled` | `true` | Semantic tokens: from the language server while it runs, from `torb highlight` where it does not |
| `torbscript.toolchain.offerInstall` | `true` | The notification that offers the installer when `torb` is not found; the status bar item shows either way |

Where the language server does not run - turned off, not found, or crashed three times within three minutes - the
TextMate grammar colors the code, `torb highlight` adds the semantic tokens, and the Test Explorer runs each test file
as a whole.

## Pitfalls

- **A `torb` that is older than the extension answers less.** A `torb` without `lsp` exits with its usage at once, which
  the output channel shows. One without `torb test --report json` still runs the tests: the extension notices once,
  reads the plain report instead - a single test runs its whole file, and there are no durations - and offers
  `torb upgrade`. In a checkout of this repository, build the compiler again.
- **The workspace folder decides what the server reads.** It reads the workspace below the folder VS Code has open, the
  way `torb check` reads it; a file outside of it is checked as a script against the toolchain's own standard library.
- **A file changed on disk by something else is read again only when the editor says so.** An open document is the
  editor's text; any other file changed by a `git checkout` or a formatter is read again once the client reports it
  (`workspace/didChangeWatchedFiles`), which the extension does for every `.trb` of the workspace. A client of another
  editor that does not watch files should restart the server after such a change.
- **The installer extends `PATH` for new terminals, not for VS Code.** The extension finds the new `torb` in the
  installer's own directory anyway; a terminal VS Code opened before the install needs to be opened again for `torb`
  to be a command there.
- **In this repository**, `.vscode/extensions.json` recommends the published extension, and it runs the compiler of
  the checkout (`build/release/torb`). To work on the extension itself, "Run and Debug -> TorbScript extension" loads
  `editors/vscode` in an Extension Development Host; `editors/vscode/CONTRIBUTING.md` has the rest.

## Full example

The settings of VS Code that use a `torb` of another checkout, in `.vscode/settings.json`:

```json
{
  "torbscript.executablePath": "/home/ada/torbscript/build/release/torb",
  "torbscript.languageServer.enabled": true
}
```

The same server in Neovim, with its built-in client, in `init.lua`:

```text
vim.filetype.add({ extension = { trb = "trb" } })
vim.api.nvim_create_autocmd("FileType", {
  pattern = "trb",
  callback = function(event)
    vim.lsp.start({
      name = "torb",
      cmd = { "torb", "lsp" },
      root_dir = vim.fs.root(event.buf, { "project.trb", ".git" }),
    })
  end,
})
```

And in Helix, in `languages.toml`:

```text
[language-server.torb]
command = "torb"
args = ["lsp"]

[[language]]
name = "trb"
scope = "source.trb"
file-types = ["trb"]
roots = ["project.trb"]
language-servers = ["torb"]
```

## Related

- [torb lsp](../tooling/torb-lsp.md) - every message the server answers, and what a keystroke costs.
- [torb test](../tooling/torb-test.md) - the report the Test Explorer reads, and `--filter`.
- [torb lint](../tooling/torb-lint.md) - the rules whose fixes are the quick fixes.
- [The language server](../design/LANGUAGE-SERVER.md) - the design record, and why the extension is published as it is.
