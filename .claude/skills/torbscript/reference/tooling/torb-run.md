---
title: torb run
summary: torb run executes a file directly, or src/main.trb of a project directory, and passes the rest of the command line to the program as Process.arguments().
kind: tooling
status: stable
order: 40
keywords:
  - torb run
  - stage 0
  - arguments
  - entry point
source:
  - bootstrap/README.md
  - bootstrap/crates/torb-cli/src/main.rs
---

Until the compiler compiles itself, `run` is the one command that does not go through `run ../compiler`: it is stage
0 itself, the untyped interpreter of `bootstrap/`, run directly.

## Synopsis

```text
torb run <file> [arguments]   Run a file, or src/main.trb of a project directory, passing the rest as arguments
```

## What it does

### The entry point

Given a file, `run` interprets it directly. Given a directory, it joins `src/main.trb` onto it first, so
`torb run my-project` and `torb run my-project/src/main.trb` reach the same file. A file that nothing imports may
hold top-level code and needs no `fn main`, which is what makes a single script runnable at all.

```console
$ cd bootstrap
$ cargo run --release -q -- run ../examples/tour/src/01-bindings-and-values.trb
1
Hello, World! 1 + 1 is 2
list: [1, 2, 3], first: 1, a: 1
```

### Arguments

Everything after the file is what the program's own `Process.arguments()` sees - `run` itself never reads them - so
`torb run tool.trb --verbose input.txt` hands `["--verbose", "input.txt"]` to `tool.trb`. This is also how `run`
runs the self-hosted compiler on itself: `run ../compiler check ..` interprets `../compiler/src/main.trb`, and that
program reads `["check", ".."]` back out of `Process.arguments()` to dispatch to its own `check`.

### What stage 0 does not do

Stage 0 has no type checker and does not load `std/`: a mistake that `check` would catch is instead found when the
line runs, or not noticed at all, and only the small part of the standard library the compiler itself needs is
implemented natively. A program that uses more of `std/` than that, or whose types stage 0 would need to have caught
a mistake, has to be checked separately - see [torb check](torb-check.md) - before it is trusted. This is a property
of stage 0 alone: once the compiler compiles itself, its own `run` type checks first, the same way `torb build` does
today.

### Exit codes

`0` when the program's top-level code finishes; a non-zero code when it does not - a file it cannot read, a syntax
error, or an import it cannot resolve leaves with `2` before a line of the program has run, and a `panic` or an
uncaught `Fail` at the top level leaves with `1`. This is stage 0's own choice of number and not the language's: a
native binary built by
[torb build](torb-build.md) exits a panic with `101`, the number `CONCEPT.md` specifies, because it runs the real
runtime instead of the bootstrap interpreter.

## Examples

Running a project directory reaches the same file as naming it directly:

```console
$ cargo run --release -q -- run ../compiler check ../examples/tour
14 files, no problems
```

## Related

- [torb check](torb-check.md) - type checking, which stage 0's own `run` does not do.
- [torb build](torb-build.md) - a native binary, whose panic exit code stage 0's `run` does not match.
- [The torb command](the-torb-command.md) - every subcommand in one table.
- [Top-level code](../language/modules-and-packages/top-level-code.md) - what makes a file runnable on its own.
