# Changelog

The extension carries the version of the TorbScript toolchain it is released with: `0.1.0` of the extension comes with
`torb` 0.1.0. A version is published to the Visual Studio Marketplace and to Open VSX when the toolchain of the same
number is released.

## 0.1.0 - unreleased

The first version in the stores.

- The language server of `torb lsp`: problems as you type - a keystroke inside of a function checks that function -
  and the problems of the files you have not opened, the fixes of `torb lint` as quick fixes, hover with the
  documentation of `torb doc`, go to definition, Find All References, a checked Rename Symbol, completion with the
  documentation of each item, signature help, the Outline and "Go to Symbol in Workspace", Format Document and Format
  Selection through the server, and semantic tokens. Every folder of a multi-root workspace is a project of its own.
- Tests in the Test Explorer: the `group` and `test` calls of every `*.test.trb`, run with `torb test` in the VM or
  natively, with the failure at the line that failed and continuous run on save.
- Format Document with `torb format` - through the language server, or over a copy of the text where it does not
  run - and "Run File" with `torb run`.
- The debugger of `torb debug`: breakpoints, stepping, the call stack, locals, hover and watch, a stop at a panic;
  Debug beside Run for every test in the Test Explorer, "Debug File" and a CodeLens "Run | Debug" on entry files.
- The walkthrough "Get Started with TorbScript", which installs the toolchain with the official installer in a terminal,
  creates a first program with `torb new`, runs it and points at the documentation; a status bar item and a
  notification while `torb` is missing, and "TorbScript: Install or Update Toolchain".
- TorbScript's brand: the Orb as the extension's icon, the file icon of `.trb` files in a light and a dark version,
  and the walkthrough's images drawn for light and dark themes.
- The TextMate grammar for `.trb` files and for ` ```trb ` blocks in Markdown, highlighting in the Markdown preview,
  semantic highlighting from `torb highlight` where the language server does not run, and snippets.
