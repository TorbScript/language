---
title: torb run
summary: torb run runs a file, the only program below a directory or the program named in the bytecode VM, or builds and runs it natively with --native, passing the rest of the command line, the three streams and the exit code through.
kind: tooling
status: stable
order: 40
keywords:
  - torb run
  - program
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

`run` runs a program at once, in the bytecode VM inside `torb`, which needs no C compiler. `--native` is
[`build`](torb-build.md) plus starting what came out. The two are one language: the conformance suite holds every
program to the same output, exit code and panics in both, so a program cannot behave one way under `run` and another
way when it is shipped.

## Synopsis

```text
torb run [--vm | --native] [--profile dev|release] [path | name] [arguments]   Run a file or a program
    --native                In front of the path: build it natively and run the binary
    --profile dev|release   In front of the path: build it natively, this hard does the C compiler optimize;
                            with --vm, run it in the VM in that profile
    --release               The same as --profile release
    --vm                    The default, accepted: run it in the bytecode VM, whatever profile is named
    --timings               In front of the path: what every step up to the first instruction took, on standard error
```

## What it does

### What it runs

**An argument that contains `/` or `\`, ends in `.trb`, or is `.` or `..` is a path; anything else is the name of a
program** ([project.trb](project-trb.md#programs)). So:

- **A file** is run as it is: a script, or the entry of a program. A file that nothing imports may hold top-level code
  and needs no `fn main`, which is what makes a single script runnable at all.
- **A directory**, and the working directory where nothing is named, runs **the only program below it**: a package's
  `src/main.trb`, or the `entry` of one of its `program` lines. `torb run ./my-project` and
  `torb run my-project/src/main.trb` reach the same file. Where there are several, `run` names them instead of
  guessing:

  ```text
  error: acme/shop has 2 programs. Name one: `torb run shop`
    = shop, migrate
  ```

- **A name** runs the program of that name below the working directory - `torb run migrate`. A name nothing declares
  answers ``error: There is no program `migrate` here`` with the names that exist and the reminder that a directory is
  written as a path, `./migrate`.

A package without a program has nothing to run: ``error: acme/lib is a library: there is nothing to run``, or
``error: acme/tour has no program: there is nothing to run`` for a package of scripts with neither `src/main.trb` nor
`src/lib.trb` - its files are run by naming them. All of this is the same in the VM and with `--native`.

### The cache of `--native`

The binary goes into `build/run/<profile>/<key>/` under the workspace root, and `<key>` is a hash of **every file the
front end read**, with its path and its text, plus the path and the program name that were given and **the toolchain
that builds it**: the hash of the compiler's own C that `torb build` wrote beside the running `torb` (`program.hash`,
or `program.c` where only that is there) and the text of every C file and header of the runtime it links. The profile
is `dev` unless the command line says `--profile release` or `--release` in front of the path. An unchanged program is
therefore not rebuilt: the first run costs a build, every run after it costs a process start.

Keying on every file that was read rather than on the ones the program imports is the same trade
[`check`](torb-check.md) makes when it reads the whole workspace: an edit to a file the program does not import costs
one rebuild, and in exchange "is this binary current" is a question about bytes that were read anyway instead of about
a dependency graph that is only known after the check. A `torb` that was built again, or a runtime that was edited,
is another key, so a binary an older compiler built is never the one that starts. The cache is a directory and nothing
else - `rm -r build/run` is how it is cleared. A `torb` copied somewhere without the `program.c` it was built from is
known by its path alone, which is the one case that asks for it.

### Arguments

Everything after the path or the name is what the program's own `Process.arguments()` sees - `run` itself reads none
of it - so `torb run tool.trb --verbose input.txt` hands `["--verbose", "input.txt"]` to `tool.trb`, and a word that
looks like a flag of `run` is passed on rather than read.

### What `run` puts on the output

Nothing of its own. A build that succeeds says nothing, the program is started with the three streams `torb` itself
holds rather than with a pipe, and the exit code is the program's. So a program's output arrives while it is produced,
its standard output and standard error stay apart and in order, a program that reads standard input reads the one the
user is typing into, and a gate that compares what a program wrote compares the program and not the driver.

A build that **fails** says so, on standard error, in the form [`check`](torb-check.md) uses, and `run` leaves with
`1` without starting anything.

### The VM, the default

`torb run <path>` builds nothing: the program is checked and lowered to the same typed IR `build` compiles, encoded
as bytecode, and run by the VM inside `torb` itself, on the same runtime a native binary links. Its output, its exit
code and its panics are the native binary's, which the conformance suite of the toolchain checks for every program. A
program that runs long wants `--native`: the VM interprets, and a loop of arithmetic is
a few dozen times slower than the binary. Its tasks run on the workers of `torb`'s own pool - as many as `TORB_WORKERS` says, the
core count by default - under the rules of a native binary's. A program that uses what the VM does not run yet is
refused before anything runs, with a message that names what is missing. Two things answer differently because the
program runs inside `torb`: `Process.executablePath()` is the path of the entry file, which is what executes, and a
recursion reaches the `stack overflow` panic at a depth of its own.

**It runs in the `dev` profile**, as `--native` builds, and the profile decides one thing in the VM: the return trace
of `?`, which the report of a top-level `?` prints under the error, one `  at` line per `?` the failure went through
([Errors at the top level](../language/errors/top-level-errors.md)). `torb run --vm --release` runs it without the
trace, as a release binary runs - which is how the conformance suite compares a program with its release build.

**It starts at once**, because it does only what the program needs: the files of the workspace are listed and only the
ones the program reaches are read and parsed - the packages its entry imports, depends on and names as its prelude -
and the bodies of the entry are checked, while the bodies of a module of `std` are checked when the lowering first
reaches that module. A program that prints one line is running about a quarter of a second after the command was
typed; `--timings` prints where that time went, step by step, on standard error. A program the lowering refuses is
checked in full before the refusal is printed, so the message is the one `torb build` gives.

### The memory limit

A program `run` runs - in the VM, or built with the `dev` profile - **stops at a memory limit**: the smaller of 8 GiB
and half the physical memory. A native binary hands it to the operating system before the program starts (a job object
on Windows, `RLIMIT_DATA` on Linux, its own count of what it allocates where the system has nothing that fits); in the
VM the kernel counts what the program allocates. A program that
allocates without end ends with `panic: out of memory: the limit of 8 GiB was reached (...)` and exit code `102`
instead of paging the machine to a standstill. A child process the program starts is not held to its limit.

`TORB_MEMORY_LIMIT` sets another one - a number of bytes, or one with `K`, `M`, `G` or `T` (`512M`, `16G`) - and `0` or
`none` sets none; a value that is neither makes the program refuse to start, with exit code `2`. With `--release` there
is no limit unless the variable asks for one. In the VM the program runs inside `torb`, and the limit - the same
default, or the variable - is the program's alone: the VM's kernel counts what the program allocates and ends it with
the same message and exit code, and `torb` checks and lowers it unlimited by the variable. `TORB_MEMORY_LIMIT=64M
torb run big.trb` gives the program 64 MiB.

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

In a package with a second program, `run` wants a name, and everything behind the name is the program's:

```console
$ torb run
error: acme/shop has 2 programs. Name one: `torb run shop`
  = shop, migrate
$ torb run migrate --dry-run
3 tables to migrate
```

Built natively, the second run of an unchanged program starts immediately, because the key of its sources did not
change:

```console
$ torb run --native examples/tour/src/01-bindings-and-values.trb
1
Hello, World! 1 + 1 is 2
list: [1, 2, 3], first: 1, a: 1
```

## Related

- [torb build](torb-build.md) - the build `run` is made of, and where the binary goes when you ask for one.
- [torb check](torb-check.md) - the same front end without the back end, for when only the diagnostics are wanted.
- [project.trb](project-trb.md) - the `program` lines a name is looked up in.
- [The torb command](the-torb-command.md) - every subcommand in one table.
- [Top-level code](../language/modules-and-packages/top-level-code.md) - what makes a file runnable on its own.
