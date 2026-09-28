# Changelog

The extension carries the version of the TorbScript toolchain it is released with: `0.1.0` of the extension comes with
`torb` 0.1.0. A version is published to the Visual Studio Marketplace and to Open VSX when the toolchain of the same
number is released.

## 0.1.0 - unreleased

The first version in the stores.

- The language server of `torb lsp`: problems as you type, the fixes of `torb lint` as quick fixes, hover, go to
  definition, completion and semantic tokens.
- Tests in the Test Explorer: the `group` and `test` calls of every `*.test.trb`, run with `torb test` in the VM or
  natively, with the failure at the line that failed and continuous run on save.
- Format Document with `torb format`, and "Run File" with `torb run`.
- The walkthrough "Get Started with TorbScript", which installs the toolchain with the official installer in a terminal,
  creates a first program with `torb new`, runs it and points at the documentation; a status bar item and a
  notification while `torb` is missing, and "TorbScript: Install or Update Toolchain".
- The TextMate grammar for `.trb` files and for ` ```trb ` blocks in Markdown, highlighting in the Markdown preview,
  semantic highlighting from `torb highlight` where the language server does not run, and snippets.
