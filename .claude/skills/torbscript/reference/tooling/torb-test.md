---
title: torb test
summary: torb test runs every *.test.trb file below the paths it is given - one binary for all of them - and prints ok or FAILED for every test call it sees.
kind: tooling
status: stable
order: 60
keywords:
  - torb test
  - test runner
  - jobs
  - assert
  - memory limit
source:
  - compiler/src/cli/test.trb
  - compiler/src/cli/build.trb
  - std/test/src/lib.trb
  - runtime/memory.c
---

A `.test.trb` file is an ordinary script: `test` and `group` are calls, not a keyword, and a test fails when its body
panics - there are no matchers to learn beyond [`assert`](../standard-library/expression.md).

## Synopsis

```text
torb test [path]... [--jobs N]   Run every *.test.trb below the paths (default: tests)
    --profile dev|release           How hard the C compiler optimizes (default: dev)
    --release                       The same as --profile release
    --vm                            Run them in the bytecode VM instead of building a binary
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
$ torb test compiler/tests/calls.test.trb
  ok      Constructors, cases and `copy` > the generated constructor takes the fields in declaration order
  ok      Closures as arguments > a call of a generic function solves its parameters from the arguments (milestone 4.4)

30 passed, 0 failed (1 files)
```

`test` exits with `0` when nothing failed and `1` otherwise, so it composes with a shell's `&&` and with continuous
integration the same way [`check`](torb-check.md) does.

### One binary for every file

`test` builds **one binary for every file it found** and runs it: one C translation unit that holds
every test file plus a generated `main` that calls each file's own top-level code with the file's name printed in
front of it. One binary per file is not an option, because every test file imports its harness and through it whatever
it tests - that would be one C compile of a translation unit that size per file, and the C compiler is where the time
of a build goes. The whole suite together is about the size of one such translation unit.

A test whose body panics is reported and the **next** one runs, in the file it is in and in every file after it: that
is the recovery point of the runtime, and it is what makes one binary behave like one process per file.

### `--profile`

`test` builds the `dev` profile unless told otherwise: the C compiler runs with `-O1` instead of `-O2`, which is the
fastest suite from end to end, because a test binary is built to be run once. The binary goes to
`<first path>/build/<profile>/tests`. [`torb build`](torb-build.md) says what a profile is.

### The memory limit

A test binary of the `dev` profile **stops at a memory limit** instead of taking the machine down with it: the smaller
of 8 GiB and half the physical memory, which the runtime hands to the operating system before the first test runs (a
job object on Windows, `RLIMIT_DATA` on Linux). A suite that allocates without end - a test that loops, a bug that
doubles a list forever - ends with one line and exit code `102`, which no recovery point catches, because the next test
would start at the limit too:

```console
$ torb test compiler/tests
panic: out of memory: the limit of 8 GiB was reached (the default of a dev build; TORB_MEMORY_LIMIT sets another, 0 none)
```

`TORB_MEMORY_LIMIT` in the environment sets another limit - bytes, or a number with `K`, `M`, `G` or `T` (`512M`,
`16G`) - and `0` or `none` sets none. A `--release` binary has no limit unless the variable asks for one. The variable
reaches every TorbScript program that runs with it, `torb` itself included, so a limit meant for the suite alone is
set on the binary: `TORB_MEMORY_LIMIT=2G tests/build/dev/tests`.

### `--vm`: the bytecode VM

`torb test --vm` builds nothing: the same program - every test file an entry, its name printed in front of its tests -
is lowered to the typed IR a binary is compiled from, encoded as bytecode and run by the VM inside `torb`, on the same
runtime. The report is the one the binary writes, line for line, and so is the exit code; tier B of the repository's
gates holds the test packages of `std/` and `examples/` to that ([torb run](torb-run.md) says what the VM is).

### `--jobs`

`test` **accepts `--jobs` and ignores it**: one binary is one process, so there is nothing to spread over cores that
the one C compile in front of it did not already dominate.

### What the run forwards

`test` runs the binary as a child process **with its own three streams**, so the report is the
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

