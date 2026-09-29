---
title: Verify your work
summary: The commands that decide whether TorbScript you wrote is correct and in the layout of the formatter, in the order to run them.
kind: tooling
status: stable
order: 20
keywords:
  - check
  - format
  - lint
  - test
  - verify
source:
  - compiler/src/main.trb
  - compiler/src/cli/test.trb
---

Never hand over TorbScript you have not run through the compiler. The language has a type checker, an exhaustiveness
check, a dead-change check and a formatter, and all four of them answer in seconds. A snippet that looks right and
was not checked is the most expensive thing you can produce.

## Synopsis

All of these take a file or a directory, and default to the current directory: run them in the directory of the
project, the one with `project.trb`, or name a single file.

```text
torb check <path>                   Type check: "no problems", or a line and a caret
torb parse <path>                   Syntax only, recursively
torb format --check <path>          Is it in the layout of the formatter?
torb format <path>                  ...write it
torb lint <path>                    The rules of style the checker leaves alone
torb test <path>                    Every *.test.trb file below the path
```

## What it does

### The order to run them in

1. **`check`** first. It resolves every name and types every expression, so it finds the mistakes that matter: an
   undeclared name, a wrong type, a non-exhaustive `match`, a change that cannot be seen, a `var` that is missing.
2. **`format --check`** second. It reports every file whose call form, indentation, spaces, blank lines or line breaks
   are not in the layout. Run plain `format` to write it rather than fixing it by hand - it edits over the syntax tree
   and re-parses, so it cannot change what a program means.
3. **`test`** last, when there are tests. One program runs every test file, and a test that fails is reported and the
   next one runs.

A change is done when all three are green **and** `check` still answers `no problems` over the whole project. A false
positive of the checker is a bug in the checker.

### What each one catches

| Command | Catches |
|---------|---------|
| `parse` | A semicolon, an unclosed brace, a command call in the wrong position, a `{` where a block was meant |
| `check` | An undeclared name, a wrong type, a missing `var`, a non-exhaustive `match`, an unreachable arm, a dead change, a discarded result, a visibility violation |
| `format --check` | A parenthesized call that should be a command, a command that should have parentheses, a multi-line string that is not indented, a line indented wrong, a missing or extra space, a second blank line, a line over 120 columns that a line break makes fit, a broken call that fits on one line again |
| `lint` | The own name of a type instead of `Self`, a `Bool` field named as a question, an unread binding, an unlabeled literal option |
| `test` | Everything a test asserts, including the exact text of a diagnostic |

### Reading a diagnostic

A diagnostic points at a span and says one thing:

```text
error: Comparisons do not chain. Use `&&`: `a < b && b < c`
 --> src/main.trb:12:19
  |
12 | const chained = a < b > c
  |                       ^
```

One root cause, one message. When a message offers a fix, take it: the diagnostics of this language are written to
name the correct line rather than to describe the rule.

### Checking a snippet that is not a file yet

Write it to a file and check the file. A `.trb` file that nothing imports is a script, so it may hold top-level code and
needs no `fn main`. A loose file, anywhere, is checked against the standard library of the `torb` that checks it and
built with that toolchain's C runtime; `TORB_STD=<path to std>` and `TORB_RUNTIME=<path to runtime>` point it at
others:

```console
$ torb check scratch.trb
1 file, no problems
$ torb run scratch.trb
```

`run` runs it in the VM at once; `run --native` builds it natively first, so its first run costs a C compile.

## Examples

A clean run over a project:

```console
$ torb check .
4 files, no problems
$ torb format --check .
0 of 4 files would change
$ torb test
tests/main.test.trb
  ok      greets by name

1 passed, 0 failed (1 file)
```

A run that found something:

```console
$ torb check scratch
error: `append` needs a `var`. Did you mean `appended`?
 --> scratch/src/main.trb:3:7
  |
3 | fixed.append 3
  |       ^^^^^^

1 problems in 1 of 1 files
```

## Related

- [The torb command](the-torb-command.md) - every subcommand and its flags.
- [Command calls](../language/syntax/command-calls.md) - the rule of the canon `format --check` decides first.
- [What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md) - what to look for before
  running anything.
- [The toolchain](index.md) - the other pages about the commands.
