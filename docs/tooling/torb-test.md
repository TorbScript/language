---
title: torb test
summary: torb test runs every *.test.trb file below the paths it is given - one binary for all of them - and prints ok or FAILED for every test call it sees, or JSON Lines for an editor, of every test or of the ones a filter names.
kind: tooling
status: stable
order: 60
keywords:
  - torb test
  - test runner
  - jobs
  - shard
  - filter
  - report
  - JSON Lines
  - assert
  - memory limit
source:
  - compiler/src/cli/test.trb
  - compiler/src/cli/build.trb
  - compiler/src/main.trb
  - std/test/src/lib.trb
  - runtime/memory.c
  - runtime/test.c
  - tools/test-shards.sh
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
    --shards n                      With --native: build once, and run all n shards at the same time
    --filter <name>                 Only the test of this full name, or every test of the group of this name; repeatable
    --report json                   JSON Lines on standard output instead of the report a person reads
    --color auto|always|never       Colour `ok`, `FAILED` and the summary (default: where the output is a terminal)
    --vm                            The default, accepted
torb test --help                 Every flag, and what a path is
```

Any other argument that starts with `--` is refused with `error: unknown flag <argument>` before anything runs, rather
than looked for as a file; a path that starts with `--` is written `./--name`.

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

`--shards n` does the whole of it in one command: it builds the binary once and runs all n shards of it at the same
time, each a process of its own (`tools/test-shards.sh`), and once the last one has ended it prints their reports one
after the other and one summary over all of them. That is what saves time: n commands of `--shard` each check and lower
the whole suite before its C is known to be the same, which costs the front end n times.

```console
$ torb test --native compiler/tests --shards 4
...
2288 passed, 0 failed (80 files, 4 shards)
```

The report comes at the end rather than while the suite runs, and the exit code is the highest a shard left with.

### `--filter`

`--filter <name>` runs **the tests of one name**: `<name>` is a full name as the report writes it, the names of the
groups and of the test joined with ` > `. A test runs when its full name is the name, or starts with the name followed
by ` > ` - then the name is a group's, and every test below that group runs. The flag can be given any number of times,
and a test runs when it matches any of them:

```console
$ torb test --filter "Vector2 > adds component-wise" --filter "Matrix" tests
```

A group whose full name is on the way to none of the names is passed over whole - no name is it, lies below it, or
names a group it lies in - and its body does not run, as a file of another shard does not. Every file still prints its
line, and the counts and the summary count the tests that ran. The name is compared byte for byte, so a name outside ASCII
is written as it is (`--filter "Größe > zählt Äpfel"`): the runtime reads the command line as UTF-8 on every system.
Like `--shard`, it reaches the tests through the command line of the process - `test` hands it to the binary, and the
VM reads `torb`'s own - so a binary started by hand takes it too, and the name is always an argument of its own:
`--filter=...` is refused.

### `--color`

The report a person reads is coloured where standard output is a terminal: `ok` green, `FAILED` bold red, and the
summary green when nothing failed and bold when something did. `--color always` and `--color never` say otherwise, and
`NO_COLOR`, `FORCE_COLOR` and `CLICOLOR_FORCE` decide `auto` as they do for every command
([the torb command](the-torb-command.md#--color)). The runtime writes the report, so the flag reaches it like the others:
the VM reads `torb`'s own command line, and a native binary is handed `--color always` or `--color never` - with
`auto` it looks at its streams itself. The JSON report is never coloured, and a report without colour is byte for byte
what it was before.

### `--report json`

`--report json` writes **JSON Lines** on standard output in place of the report a person reads: one JSON object per
line, UTF-8, each ending in `\n` and flushed as it is written, so an editor that reads the output sees every event
while the suite runs. A test's own output - what it `print`s - stays on standard output as plain lines between the
events: a line that does not start with `{"event":` is output of the test that is running. The exit code is the same
as without the flag, and it combines with `--filter` and `--shard`; `--shards` is refused, because the report is one
stream of one process.

| Event | When | Keys |
|-------|------|------|
| `file` | Before the tests of a file | `path`: the file as the human report prints its line |
| `start` | Before a test's body runs | `file`; `groups`, the names of its groups outermost first; `name`, the test's own; `fullName`, all of them joined with ` > ` |
| `test` | After the body and the tasks it started have ended | the keys of `start`, then `outcome` (`passed` or `failed`) and `duration` (milliseconds, a number with a fraction) |
| `summary` | At the end | `passed`, `failed` and `files`, the counts of the summary line; `shard` with `index` and `count` where `--shard` was given |

A `test` event of a failure has three more keys: `message`, the message of the panic as it is, line breaks included;
`location` with `path`, `line` and `column` where the site is known - the path as the `at` line of the human report
names it, the name of the package in front of the path inside it; and `frames`, the frames a binary of the `dev`
profile prints below the site, where there are any (the VM has none). A key that has nothing to say is left out rather
than `null`. Strings are escaped as JSON escapes them - `"`, `\` and every control character - and every other
character is written as it is.

A file of a package `shop` with a group, a test that passes and one that prints a line and fails:

```console
$ torb test --report json tests/cart.test.trb
{"event":"file","path":"tests/cart.test.trb"}
{"event":"start","file":"tests/cart.test.trb","groups":["Cart"],"name":"adds","fullName":"Cart > adds"}
{"event":"test","file":"tests/cart.test.trb","groups":["Cart"],"name":"adds","fullName":"Cart > adds","outcome":"passed","duration":0.026}
{"event":"start","file":"tests/cart.test.trb","groups":["Cart"],"name":"sums","fullName":"Cart > sums"}
a line the test prints
{"event":"test","file":"tests/cart.test.trb","groups":["Cart"],"name":"sums","fullName":"Cart > sums","outcome":"failed","duration":0.107,"message":"Assertion failed: 7 / 2 == 4","location":{"path":"shop/tests/cart.test.trb","line":10,"column":5}}
{"event":"summary","passed":1,"failed":1,"files":1}
```

The same run with `--native` adds `"frames":"  in a closure in a closure in the top level\n..."` to the failure.
`tools/test-report.sh` holds the reports of both back ends to one expected output.

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
