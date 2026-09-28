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
  - shard
  - assert
  - memory limit
source:
  - compiler/src/cli/test.trb
  - compiler/src/cli/build.trb
  - std/test/src/lib.trb
  - runtime/memory.c
  - runtime/test.c
---

A `.test.trb` file is an ordinary script: `test` and `group` are calls, not a keyword, and a test fails when its body
panics - there are no matchers to learn beyond [`assert`](../standard-library/expression.md).

## Synopsis

```text
torb test [path]... [--jobs N]   Run every *.test.trb below the paths (default: this package) in the bytecode VM
    --native                        Build them into one binary and run it instead
    --profile dev|release           Build natively, this hard does the C compiler optimize (default: dev)
    --release                       The same as --profile release
    --shard k/n                     Only the k-th of every n files, so n processes run the suite between them
    --vm                            The default, accepted
```

## What it does

### Finding the tests

Every file below a `path` whose name ends in `.test.trb` is collected, recursively, sorted by path. **A file called
`*.test.trb` is a test wherever it lies in the package** - `tests/` is a convention, not a setting - so nothing in
[`project.trb`](project-trb.md) says where the tests are. Without a path, `test` tests **the package the working
directory is in**: every `*.test.trb` below the innermost directory above it that has a `project.trb`, or below the
working directory itself where there is none. So `torb test` in `shop/src/` runs the tests of `shop`.

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

### One program for every file

`test` runs **one program for every file it found**, in the bytecode VM inside `torb` unless `--native` or a profile
says to build it: the same program - every test file an entry, its name printed in front of its tests - is lowered to
the typed IR a binary is compiled from and run by the VM on the same runtime, and the report is the one the binary
writes, line for line, and so is the exit code. The gates of the repository hold the test packages of `std/` and
`examples/` to that in both back ends. The compiler's own suite runs natively (`torb test --native compiler/tests`):
each of its tests checks a whole program, and interpreted that takes many times as long as the one C compile.

With `--native`, `test` builds **one binary for every file it found** and runs it: one C translation unit that holds
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

### `--jobs`

`test` **accepts `--jobs` and ignores it**: one binary is one process, so there is nothing to spread over cores that
the one C compile in front of it did not already dominate.

### `--shard`

`--shard k/n` runs **a part of the files**: the k-th of every n, counted from 1 in the order the files come, so n
processes that each run another shard run the whole suite between them at the same time. The report of a shard names
only its own files, and its summary says which shard it was:

```console
$ torb test --native compiler/tests --shard 2/4
...
515 passed, 0 failed (20 files, shard 2 of 4)
```

The runtime chooses the files, in the binary and in the VM alike: it reads `--shard` off the command line of the
process - `test` hands it to the binary, and the VM runs inside `torb`, whose command line has it already - so a test
binary started by hand takes it too: `tests/build/dev/tests --shard 2/4`. A file of another shard prints nothing and
none of its tests run; only its top-level code does, which in a test file declares constants. A native shard is built
into `<first path>/build/<profile>/shard-<k>-of-<n>/`, so shards that are built at the same time never write one file;
their C is the same, and the object cache compiles and links it once for all of them while the others wait for it.

The files are dealt out in turn, not by how long they take, so the shard that holds the slowest file takes at least as
long as that file alone.

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
- [project.trb](project-trb.md) - what the names of the files decide, and `test { coverageThreshold }`, which nothing
  reads yet.
- [torb check](torb-check.md) - the gate that runs before a test file is worth trusting.
- [The torb command](the-torb-command.md) - every subcommand in one table.
