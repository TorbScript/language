---
title: torb canon
summary: torb canon rewrites sources into the formatter canon over the syntax tree, checks every edit against a second parse, and drops anything that would change what the program means.
kind: tooling
status: stable
order: 70
keywords:
  - torb canon
  - formatter canon
  - safety net
  - imported-case-patterns
source:
  - bootstrap/README.md
  - bootstrap/crates/torb-cli/src/canon/mod.rs
---

`canon` is stage 0's own tool, not the self-hosted compiler's: it exists because milestone 8's `torb format` does not
exist yet, and every file in the repository still has to be in the canon in the meantime.

## Synopsis

```text
torb canon [--check] [--rule <rule>]... <path>...

Rules (calls and strings run by default):
  calls                    A call becomes a command where the grammar allows it, parentheses everywhere else
  strings                  A multi-line """ string is indented one level deeper than the line it starts on
  imported-case-patterns   .None becomes None for a case a use imported (off by default)

  --check   Write nothing, list what would change, leave with a non-zero code
```

## What it does

### Rewriting, never generating

`canon` moves parentheses and indentation over the existing syntax tree; it never reprints a file, so a comment, a
blank line or a line ending it does not touch stays exactly as it was.

### The safety net

Every edit is applied on its own and the file is parsed again. The edit only stays if the tree that comes back is the
tree from before with every span and every call style erased - so an edit that would change what the program means is
dropped and reported instead of applied. A file that does not parse to begin with is skipped whole.

```console
$ cd bootstrap
$ cargo run --release -q -- canon --check ../examples/tour/src/scratch.trb
../examples/tour/src/scratch.trb

1 of 1 files would change: 2 calls became commands, 0 got parentheses, 0 strings were indented, 0 case patterns
```

```console
$ cargo run --release -q -- canon ../examples/tour/src/scratch.trb
../examples/tour/src/scratch.trb

1 of 1 files changed: 2 calls became commands, 0 got parentheses, 0 strings were indented, 0 case patterns
$ cargo run --release -q -- canon --check ../examples/tour/src/scratch.trb
0 of 1 files would change: 0 calls became commands, 0 got parentheses, 0 strings were indented, 0 case patterns
```

A second run over an already-canonical file changes nothing, which is what makes `--check` a gate: a repository where
it reports `0 of <n> files would change` is a repository `canon` agrees with.

### `imported-case-patterns`

Off by default, because it changes the syntax tree on purpose (`.None` in a pattern becomes `None`) instead of only
its formatting, so it is exempt from the safety net's tree comparison and is checked by its own tests instead. Only a
payload-less case that a `use` brought into the file counts, which today is `None` from the prelude and any case a
file explicitly imports the same way.

```console
$ cargo run --release -q -- canon --check --rule imported-case-patterns ../examples/tour/src/scratch.trb
../examples/tour/src/scratch.trb

1 of 1 files would change: 0 calls became commands, 0 got parentheses, 0 strings were indented, 1 case patterns
```

### Exit codes

`0` when nothing needs to change, or after writing; `--check` exits `1` when a file would change, so it composes with
a shell's `&&` and with continuous integration the same way [`check`](torb-check.md) does. An argument `canon` does
not recognize prints its usage and exits `2`.

## Examples

The three commands `compiler/CONTRIBUTING.md` runs before every commit, over the whole repository:

```console
$ cd bootstrap
$ cargo run --release -q -- canon --check ..
0 of 282 files would change: 0 calls became commands, 0 got parentheses, 0 strings were indented, 0 case patterns
```

## Related

- [The formatter canon](the-formatter-canon.md) - every rule of the canon in one place.
- [Command calls](../language/syntax/command-calls.md) - the rule `calls` enforces.
- [Multi-line strings](../language/syntax/multi-line-strings.md) - the rule `strings` enforces.
- [Verify your work](verifying-your-work.md) - where `canon --check` sits among `check` and `test`.
