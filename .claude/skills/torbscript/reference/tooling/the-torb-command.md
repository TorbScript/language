---
title: The torb command
summary: Every subcommand of the toolchain, what it does today, and which of them are still planned.
kind: tooling
status: stable
order: 10
keywords:
  - torb
  - check
  - build
  - run
  - test
  - canon
source:
  - compiler/src/main.trb
  - bootstrap/README.md
  - CONCEPT.md#toolchain
---

`torb` is a single binary: the runtime, the compiler, the package manager, the test runner, the linter, the formatter and
the language server are all in it. That keeps a project's setup to one installation and one manifest.

## Synopsis

```text
torb run [path]        Run src/main.trb of a project, or a single script file
torb check [path]...   Check projects, workspaces or single files
torb build [path]      Compile an entry file to a native binary through C
torb ir <path>...      Print the typed IR the back end lowers
torb parse <path>...   Check the syntax of files or directories
torb tokens <file>     Print the tokens of a file
torb ast <file>        Print the syntax tree of a file
torb docs <command>    Check, index, and derive the documentation
torb canon [path]...   Write the formatter canon over the syntax tree
torb test <path>       Run the *.test.trb files of a directory
```

Until the compiler compiles itself, the self-hosted commands run through stage 0. From `bootstrap/`:

```console
cargo run --release -q -- run ../compiler check ..
cargo run --release -q -- test ../compiler/tests
cargo run --release -q -- canon --check ..
```

The first form runs a command **of the self-hosted toolchain**; `test` and `canon` are commands **of stage 0** and need no
`run ../compiler` in front of them.

## What it does

### `run`

Runs a file directly, or `src/main.trb` of a project directory, passing the rest of the command line to the program
as `Process.arguments()`. There is no build step and no cache to manage: a script may hold top-level code because
nothing imports it. `run` is the one command above that does not go through `run ../compiler`: until the compiler
compiles itself, it is stage 0 itself, which has no type checker and does not load `std/` - see
[torb run](torb-run.md) for what that means for a program that uses more of the standard library than the compiler
itself does.

### `check`

Resolves every module, every import, every name in a type position and the type of every expression, and reports what is
wrong. It answers `<n> files, no problems` when everything holds, and otherwise one block per diagnostic with the line and
a caret under the span.

| Flag | What it adds |
|------|--------------|
| `--statistics` | How many expressions of every module have a type, and how many are deferred to a later milestone |
| `--timings` | The wall time of every pass, in the order they ran |

`check` is the gate: a false positive of it is a bug in the checker, not a reason to change the program.

### `build`

Compiles the typed IR ahead of time. The first back end prints C and hands it to a C compiler, so `torb build` needs one
on the machine; `--emit-c` writes the C and stops, which needs nothing. `--output <file>` says where the binary goes, and
the C is written next to it. Nothing observable differs between a binary and the same program under `torb run`.

### `parse`, `tokens`, `ast` and `ir`

The four windows into the front end. `parse` checks syntax only, recursively over directories. `tokens` and `ast` print the
lexer and parser output of one file in a deterministic format - the same format stage 0 prints, which is what the
differential tests compare. `ir` prints the typed intermediate representation the back end lowers, with `--statistics` for
the counts alone.

### `canon`

Writes the formatter canon over the syntax tree, never with a regular expression: a call becomes a command wherever the
grammar allows it and gets parentheses everywhere else, and a multi-line `"""` string is indented. Every edit is applied on
its own and the file is parsed again; it only stays if the tree is the one from before with every span and call style
erased, so a run cannot change what a program means.

| Flag | What it does |
|------|--------------|
| `--check` | Report the files that are not in the canon, and write nothing |
| `--rule calls` | Only the call form |
| `--rule strings` | Only the indentation of multi-line strings |
| `--rule imported-case-patterns` | `.None` becomes `None`. Has to be asked for |
| `--rule unused-bindings` | A binding of a refutable pattern that nobody reads becomes `_`. Has to be asked for |

`canon` is temporary. Milestone 8's `torb format`, written in TorbScript, enforces the same canon and goes away with the
rest of stage 0.

### `test`

Runs every `*.test.trb` file of a directory, one process per file, as many at a time as the machine has cores. `--jobs 1`
runs them one after another in one process, which is what you want when the output order of a crash matters. The output is
passed on in the order of the files, so it never depends on which finished first.

### `docs`

Four commands over the documentation: `check` is the gate, `index` writes the generated part of every `index.md`, `skill`
derives the Agent Skill, and `bundle` writes `llms.txt` and `llms-full.txt`. See
[the docs commands](../contributing/checks.md).

### What is still planned

These are in the design and not in the binary. A page about one of them carries `status: planned`.

| Command | What it will do |
|---------|-----------------|
| `torb format` | The formatter, taking over from `canon` |
| `torb lint` | The naming and style rules the compiler does not care about |
| `torb doc` | Documentation from the doc comments, including the standard-library pages |
| `torb repl` | An interactive session where every input is a nested scope of the previous one |
| `torb add`, `remove`, `update`, `audit` | The package manager and the advisory database |

## Examples

Check the whole repository and see that every expression has a type:

```console
$ cd bootstrap
$ cargo run --release -q -- run ../compiler check ..
255 files, no problems
$ cargo run --release -q -- run ../compiler check --statistics ..
149982 of 149982 expressions typed (100%), 0 deferred
```

Build a native binary, and look at the C first - see [torb build](torb-build.md) for what the back end does not
lower yet:

```console
$ cargo run --release -q -- run ../compiler build ../examples/tour/src/scratch.trb --emit-c --output ../build/dev/scratch
wrote ../build/dev/program.c
$ cargo run --release -q -- run ../compiler build ../examples/tour/src/scratch.trb --output ../build/dev/scratch
wrote ../build/dev/scratch.exe
```

## Related

- [Verify your work](verifying-your-work.md) - the commands to run before you are done.
- [Run your first program](../guide/installing-and-running.md) - the first use of `run` and `check`.
- [torb check](torb-check.md), [torb run](torb-run.md), [torb build](torb-build.md), [torb test](torb-test.md),
  [torb canon](torb-canon.md) - one page per command, in depth.
- [Command calls](../language/syntax/command-calls.md) - the canon that `canon` enforces.
- [The docs commands](../contributing/checks.md) - the four `docs` subcommands.
