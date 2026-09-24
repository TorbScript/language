---
title: torb run
summary: torb run builds a file or a project into a cache and executes it, passing the rest of the command line, the three streams and the exit code through.
kind: tooling
status: stable
order: 40
keywords:
  - torb run
  - cache
  - arguments
  - entry point
  - memory limit
  - TORB_MEMORY_LIMIT
source:
  - compiler/src/cli/run.trb
  - compiler/src/vm/run.trb
  - compiler/src/cli/build.trb
  - tools/bootstrap.sh
  - runtime/memory.c
---

`run` is [`build`](torb-build.md) plus starting what came out. There is no second implementation of the language behind
it: what runs is the binary `torb build` would have written, so a program cannot behave one way under `run` and another
way when it is shipped.

## Synopsis

```text
torb run [--profile dev|release] <path> [arguments]   Build a file, or a project's build { input }, and run it
    --profile dev|release   In front of the path: how hard the C compiler optimizes (default: dev)
    --release               The same as --profile release
    --vm                    In front of the path: run it in the bytecode VM instead of building it
```

## What it does

### The entry point

Given a file, `run` builds that file. Given a directory, it builds the `build { input }` of that directory's
[`project.trb`](project-trb.md) - so `torb run my-project` and `torb run my-project/src/main.trb` reach the same file
when the manifest says `input "src/main.trb"`. A file that nothing imports may hold top-level code and needs no
`fn main`, which is what makes a single script runnable at all.

### The cache

The binary goes into `build/run/<profile>/<key>/` under the workspace root, and `<key>` is a hash of **every file the
front end read**, with its path and its text, plus the entry that was named and **the toolchain that builds it**: the
hash of the compiler's own C that `torb build` wrote beside the running `torb` (`program.hash`, or `program.c` where
only that is there) and the text of every C file and header of the runtime it links. The profile is `dev` unless the command
line says `--profile release` or `--release` in front of the path. An unchanged program is therefore not rebuilt: the
first run costs a build, every run after it costs a process start.

Keying on every file that was read rather than on the ones the program imports is the same trade
[`check`](torb-check.md) makes when it reads the whole workspace: an edit to a file the program does not import costs
one rebuild, and in exchange "is this binary current" is a question about bytes that were read anyway instead of about
a dependency graph that is only known after the check. A `torb` that was built again, or a runtime that was edited,
is another key, so a binary an older compiler built is never the one that starts. The cache is a directory and nothing
else - `rm -r build/run` is how it is cleared. A `torb` copied somewhere without the `program.c` it was built from is
known by its path alone, which is the one case that asks for it.

### Arguments

Everything after the path is what the program's own `Process.arguments()` sees - `run` itself reads none of it - so
`torb run tool.trb --verbose input.txt` hands `["--verbose", "input.txt"]` to `tool.trb`, and a word that looks like a
flag of `run` is passed on rather than read.

### What `run` puts on the output

Nothing of its own. A build that succeeds says nothing, the program is started with the three streams `torb` itself
holds rather than with a pipe, and the exit code is the program's. So a program's output arrives while it is produced,
its standard output and standard error stay apart and in order, a program that reads standard input reads the one the
user is typing into, and a gate that compares what a program wrote compares the program and not the driver.

A build that **fails** says so, on standard error, in the form [`check`](torb-check.md) uses, and `run` leaves with
`1` without starting anything.

### `--vm`: the bytecode VM

`torb run --vm <path>` builds nothing: the program is checked and lowered to the same typed IR `build` compiles, encoded
as bytecode, and run by the VM inside `torb` itself, on the same runtime a native binary links. Its output, its exit
code and its panics are the native binary's, which the conformance suite checks for every program it lists
(`tools/conformance.sh --vm`). Its tasks run on the workers of `torb`'s own pool - as many as `TORB_WORKERS` says, the
core count by default - under the rules of a native binary's. A program that uses what the VM does not run yet is
refused before anything runs, with a message that names what is missing. Three things answer differently because the
program runs inside `torb`: `Process.executablePath()` is the path of `torb`, a recursion reaches the `stack overflow`
panic at a depth of its own, and a `TORB_MEMORY_LIMIT` limits `torb` as a whole.

### The memory limit

A program built for `run` - the `dev` profile - **stops at a memory limit**: the smaller of 8 GiB and half the physical
memory, which the runtime hands to the operating system before the program starts (a job object on Windows,
`RLIMIT_DATA` on Linux, its own count of what it allocates where the system has nothing that fits). A program that
allocates without end ends with `panic: out of memory: the limit of 8 GiB was reached (...)` and exit code `102`
instead of paging the machine to a standstill. A child process the program starts is not held to its limit.

`TORB_MEMORY_LIMIT` sets another one - a number of bytes, or one with `K`, `M`, `G` or `T` (`512M`, `16G`) - and `0` or
`none` sets none; a value that is neither makes the program refuse to start, with exit code `2`. With `--release` there
is no limit unless the variable asks for one, and neither is there for `--vm`, whose program runs inside `torb`. The
variable reaches every TorbScript program that runs with it, `torb` itself included, which also has to compile the
program within it: `TORB_MEMORY_LIMIT=64M torb run big.trb` limits the compile as well, so a tight limit goes on the
binary that [`torb build`](torb-build.md) wrote.

### Exit codes

The program's own, unchanged. A `panic` leaves with `101`, the number `CONCEPT.md` specifies, and running out of memory
with `102`; an uncaught `Fail` at the top level leaves with `1`; a program that could not be built leaves with `1` and
one that found no C compiler with `3`.

## Examples

A script, with the program's own arguments behind it:

```console
$ torb run tools/report.trb --since 2026-09-01
14 commits, 3 authors
```

The second run of an unchanged program starts immediately, because the key of its sources did not change:

```console
$ torb run examples/tour/src/01-bindings-and-values.trb
1
Hello, World! 1 + 1 is 2
list: [1, 2, 3], first: 1, a: 1
```

## Related

- [torb build](torb-build.md) - the build `run` is made of, and where the binary goes when you ask for one.
- [torb check](torb-check.md) - the same front end without the back end, for when only the diagnostics are wanted.
- [The torb command](the-torb-command.md) - every subcommand in one table.
- [Top-level code](../language/modules-and-packages/top-level-code.md) - what makes a file runnable on its own.

