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
source:
  - compiler/src/cli/run.trb
  - compiler/src/cli/build.trb
  - tools/bootstrap.sh
---

`run` is [`build`](torb-build.md) plus starting what came out. There is no second implementation of the language behind
it: what runs is the binary `torb build` would have written, so a program cannot behave one way under `run` and another
way when it is shipped.

## Synopsis

```text
torb run <path> [arguments]   Build a file, or a project's build { input }, and run it
```

## What it does

### The entry point

Given a file, `run` builds that file. Given a directory, it builds the `build { input }` of that directory's
[`project.trb`](project-trb.md) - so `torb run my-project` and `torb run my-project/src/main.trb` reach the same file
when the manifest says `input "src/main.trb"`. A file that nothing imports may hold top-level code and needs no
`fn main`, which is what makes a single script runnable at all.

### The cache

The binary goes into `build/run/<key>/` under the workspace root, and `<key>` is a hash of **every file the front end
read**, with its path and its text, plus the entry that was named. An unchanged program is therefore not rebuilt: the
first run costs a build, every run after it costs a process start.

Keying on every file that was read rather than on the ones the program imports is the same trade
[`check`](torb-check.md) makes when it reads the whole workspace: an edit to a file the program does not import costs
one rebuild, and in exchange "is this binary current" is a question about bytes that were read anyway instead of about
a dependency graph that is only known after the check. The cache is a directory and nothing else - `rm -r build/run`
is how it is cleared, and a toolchain that was upgraded while the program stayed the same is the one case that asks
for it.

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

### Exit codes

The program's own, unchanged. A `panic` leaves with `101`, the number `CONCEPT.md` specifies; an uncaught `Fail` at the
top level leaves with `1`; a program that could not be built leaves with `1` and one that found no C compiler with `3`.

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

