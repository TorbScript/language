---
title: torb test
summary: torb test runs every *.test.trb file below the paths it is given - one binary for all of them, or one process per file on stage 0 - and prints ok or FAILED for every test call it sees.
kind: tooling
status: stable
order: 60
keywords:
  - torb test
  - test runner
  - jobs
  - assert
source:
  - compiler/src/cli/test.trb
  - compiler/src/cli/build.trb
  - std/test/src/lib.trb
---

A `.test.trb` file is an ordinary script: `test` and `group` are calls, not a keyword, and a test fails when its body
panics - there are no matchers to learn beyond [`assert`](../standard-library/expression.md).

## Synopsis

```text
torb test [path]... [--jobs N]   Run every *.test.trb below the paths (default: tests)
```

## What it does

### Finding the tests

Every file below a `path` whose name ends in `.test.trb` is collected, recursively, sorted by path. `path` defaults to
`tests`, the directory [`project.trb`](project-trb.md)'s `test { input "..." }` names by default, but `test` itself
does not read `project.trb` - it only ever looks at the paths you give it.

**Several paths are one run**, and therefore one binary and one report over all of them, in the order the paths sort
in: `torb test std/path/tests std/linear/tests` builds both packages together. Any test package works the same way -
the standard library's, an example's, your own - because nothing about the command is particular to the compiler's own
suite.

### Running them

Every `test "name" { ... }` call runs its body and prints one line: `  ok      <name>` if it returns without a
`Fail` or a `panic`, `  FAILED  <name>` followed by the message and the site otherwise. `group "name" { ... }` nests:
a `test` inside two `group`s prints as `<outer> > <inner> > <name>`. At the end, one line gives the totals across
every file:

```console
$ cd bootstrap
$ cargo run --release -q -- test ../compiler/tests/calls.test.trb
  ok      Constructors, cases and `copy` > the generated constructor takes the fields in declaration order
  ok      Closures as arguments > a call of a generic function solves its parameters from the arguments (milestone 4.4)

30 passed, 0 failed (1 files)
```

`test` exits with `0` when nothing failed and `1` otherwise, so it composes with a shell's `&&` and with continuous
integration the same way [`check`](torb-check.md) does.

### One binary, or one process per file

The self-hosted compiler builds **one binary for every file it found** and runs it: one C translation unit that holds
every test file plus a generated `main` that calls each file's own top-level code with the file's name printed in
front of it. One binary per file is not an option, because every test file imports its harness and through it whatever
it tests - that would be one C compile of a translation unit that size per file, and the C compiler is where the time
of a build goes. The whole suite together is about the size of one such translation unit.

A test whose body panics is reported and the **next** one runs, in the file it is in and in every file after it: that
is the recovery point of the runtime, and it is what makes one binary behave like one process per file.

Stage 0 runs one process per file instead, because its interpreter is built on reference counts that are not shared
between threads.

### `--jobs`

`--jobs N` belongs to stage 0: it runs that many files at once, each in its own process, and prints every file's output
together once that file is done - so the order of the output never depends on which file finished first. The default is
the number of cores, and `--jobs 1` runs every file one after another in the current process, which is what you want
when the output order of a crash matters, because a process that panics outside of a `test` call takes the rest of that
file's tests down with it.

The self-hosted `test` **accepts `--jobs` and ignores it**: one binary is one process, so there is nothing to spread
over cores that the one C compile in front of it did not already dominate. It is accepted rather than rejected so that
a command line written for stage 0 still runs.

### What the run forwards

The self-hosted `test` runs the binary as a child process **with its own three streams**, so the report is the
binary's own output and nothing on the way out touches it: every line appears while the suite runs rather than after
it, a panic that escaped every recovery point - a file whose *top level* panicked - stays on standard error where the
binary put it, and the exit code is the binary's.

### Coverage

[`project.trb`](project-trb.md)'s `test { coverageThreshold }` is part of the manifest's vocabulary, but no command
reads it yet: `test` runs every file it is given and reports pass and fail counts, nothing more.

## Examples

Everything a `.test.trb` file needs - `assert` is in the prelude, so only `test` itself is imported:

```trb check
use test from "std/test"

test "adds two numbers" { assert(1 + 1 == 2) }
```

`group "Vector2" { ... }` nests any number of `test` calls the same way, one indentation level deeper.

## Related

- [std/test](../standard-library/test.md) - `test` and `group` as a package, and where `assert` comes from.
- [project.trb](project-trb.md) - `test { input, coverageThreshold }`, and which of them `test` reads today.
- [torb check](torb-check.md) - the gate that runs before a test file is worth trusting.
- [The torb command](the-torb-command.md) - every subcommand in one table.
