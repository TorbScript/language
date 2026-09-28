---
title: Set up your editor
summary: Install the VS Code extension of the repository, which starts torb lsp for diagnostics, hover, go to definition, completion, semantic tokens and quick fixes - or point any editor with a language server client at torb lsp.
kind: how-to
status: stable
order: 5
keywords:
  - editor
  - VS Code
  - language server
  - torb lsp
  - Neovim
  - Helix
source:
  - .vscode/extensions/torbscript/package.json
  - .vscode/extensions/torbscript/lsp-client.js
  - compiler/src/language-server/command.trb
---

An editor understands TorbScript through [`torb lsp`](../tooling/torb-lsp.md), the language server that is the compiler
itself: the diagnostics are `torb check`'s, the types in a hover are the checker's, and a quick fix is what `torb lint
--fix` writes. VS Code gets it from the extension in `.vscode/extensions/torbscript`; every other editor with a client
of the Language Server Protocol starts `torb lsp` itself.

## Steps

1. **Have a `torb` that knows `lsp`.** `sh tools/bootstrap.sh` writes one to `build/release/torb`; an installed
   toolchain has it on `PATH`. `torb lsp --help` says whether it does.

2. **In VS Code, install the extension of the repository.** Open the repository folder, go to the Extensions view, and
   install "TorbScript" from **Recommended** ("Workspace Recommendations"), then reload the window. Outside of this
   repository, link the folder into your extensions once:

   ```console
   ln -s "$PWD/.vscode/extensions/torbscript" ~/.vscode/extensions/torbscript
   ```

   On Windows, `New-Item -ItemType Junction -Path "$env:USERPROFILE\.vscode\extensions\torbscript" -Target
   "$PWD\.vscode\extensions\torbscript"` does the same.

3. **Open a `.trb` file.** The extension starts `torb lsp` from `torbscript.executablePath`, or from
   `build/release/torb` below the workspace folder, or from `torb` on `PATH`, in that order. The "TorbScript" output
   channel (View > Output) shows one line per run of the checker, and why the server did not start where it did not.

4. **Use it.** Problems appear as you type; a hint with a lightbulb is a finding of `torb lint` whose fix is one click
   (Ctrl+. or Cmd+.); hover a name for its signature and doc comment; F12 goes to its declaration, in the standard
   library as well; `.` offers the members of what stands in front of it. "TorbScript: Restart Language Server" in the
   command palette starts it again after a new `torb` was built.

5. **In another editor, register `torb lsp` for `.trb` files.** It needs no argument (`--stdio` is accepted), speaks
   over standard input and output, and takes the root of the workspace from `initialize`. The file type is `trb`.

## Settings

| Setting | Default | Meaning |
|---------|---------|---------|
| `torbscript.executablePath` | `""` | The `torb` to start. Empty searches `build/release/torb[.exe]` under every workspace folder, then `torb` on `PATH` |
| `torbscript.languageServer.enabled` | `true` | Whether `torb lsp` is started at all, read when the window loads |
| `torbscript.semanticHighlighting.enabled` | `true` | Semantic tokens: from the language server while it runs, from `torb highlight` where it does not |

Where the language server does not run - turned off, not found, or crashed three times within three minutes - the
TextMate grammar colors the code and `torb highlight` adds the semantic tokens, as before the language server existed.

## Pitfalls

- **A `torb` that is older than the extension answers nothing.** `torb lsp` is a subcommand since milestone 8; an older
  binary exits with its usage at once, which the output channel shows. Build the compiler again.
- **The workspace folder decides what the server reads.** It reads the workspace below the folder VS Code has open, the
  way `torb check` reads it; a file outside of it is checked as a script against the toolchain's own standard library.
- **A file changed on disk by something else is read again only when the editor says so.** An open document is the
  editor's text; any other file changed by a `git checkout` or a formatter is read again once the client reports it
  (`workspace/didChangeWatchedFiles`), which the extension does for every `.trb` of the workspace. A client of another
  editor that does not watch files should restart the server after such a change.

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
- [torb lint](../tooling/torb-lint.md) - the rules whose fixes are the quick fixes.
- [The language server](../design/LANGUAGE-SERVER.md) - the design record.
