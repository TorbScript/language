---
title: torb test
summary: torb test runs every *.test.trb file below a directory, one process per file by default, and prints ok or FAILED for every test call it sees.
kind: tooling
status: stable
order: 60
keywords:
  - torb test
  - test runner
  - jobs
  - assert
source:
  - bootstrap/crates/torb-cli/src/main.rs
  - std/test/src/lib.trb
---

A `.test.trb` file is an ordinary script: `test` and `group` are calls, not a keyword, and a test fails when its body
panics - there are no matchers to learn beyond [`assert`](../standard-library/expression.md).

## Synopsis

```text
torb test [path] [--jobs N]   Run every *.test.trb below path (default: tests), N files at a time
```

## What it does

### Finding the tests

Every file below `path` whose name ends in `.test.trb` is collected, recursively, sorted by path. `path` defaults to
`tests`, the directory [`project.trb`](project-trb.md)'s `test { input "..." }` names by default, but `test` itself
does not read `project.trb` - it only ever looks at the path you give it.

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

### `--jobs`

By default, `test` runs as many files at once as the machine has cores, each in its own process, and prints every
file's output together once that file is done - so the order of the output never depends on which file finished
first. `--jobs 1` runs every file one after another in the current process instead, which is what you want when the
output order of a crash matters, because a process that panics outside of a `test` call takes the rest of that file's
tests down with it.

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
