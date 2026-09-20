---
title: torb check
summary: torb check resolves every module, import and name in a type position, types every expression, and reports one block per diagnostic - the gate every other command trusts.
kind: tooling
status: stable
order: 30
keywords:
  - torb check
  - type checker
  - diagnostics
  - statistics
  - timings
source:
  - compiler/src/main.trb
  - compiler/src/project/read.trb
  - compiler/src/project/workspace.trb
---

`check` is the front end alone, with nothing built and nothing run: it is what `torb build` and the self-hosted
`torb run` call before they do anything else, and it is safe to run on its own as often as you like.

## Synopsis

```text
torb check [path]...     Check projects, workspaces or single files (default: the current directory)
    --statistics          How many expressions of every file have a type, and how many are deferred
    --timings             The wall time of every pass, in the order they ran
```

## What it does

### Finding what to check

A path on the command line can be a single file or a directory. `check` walks up from it looking for a `project.trb`;
the outermost one it finds is the workspace root, and every `.trb` file below that root is read, because that is
where the standard library the path depends on lives. A file with no `project.trb` above it at all is checked alone,
against the default prelude.

Passing several paths checks several targets in one run; each is resolved this way independently.

### What is checked

Every module, every `use`, the visibility of every name, every name in a type position, and the type of every
expression. A clean run answers `<n> files, no problems`, where `<n>` counts only the files the given paths asked
for - the files of the standard library and of other workspace members that had to be read along the way are not
counted, even though they were type checked too.

```console
$ cd bootstrap
$ cargo run --release -q -- run ../compiler check ../examples/tour
14 files, no problems
```

A problem is one block per diagnostic: the file, the line and column, a caret under the span, and one message, the
same rendering `torb build` uses for a program that does not check.

```console
$ cargo run --release -q -- run ../compiler check ../scratch.trb
error: Expected `Int64`, found `String`
 --> ../scratch.trb:1:20
  |
1 | const total: Int = "not a number"
  |                    ^^^^^^^^^^^^^^

1 problems in 1 of 1 files
```

`check` exits with `1` when it reports a problem and `0` when it does not, so it composes with a shell's `&&` and
with a continuous-integration step that only continues on success.

### `--statistics`

Prints how many expressions of every requested file have a type and how many are `deferred` - waiting for a later
milestone of the checker - then the total across every requested file.

```console
$ cargo run --release -q -- run ../compiler check --statistics ../examples/tour
../examples/tour/src/01-bindings-and-values.trb: 84 typed, 0 deferred
../examples/tour/src/02-functions.trb: 260 typed, 0 deferred
../examples/tour/project.trb: 6 typed, 0 deferred
3139 of 3139 expressions typed (100%), 0 deferred
14 files, no problems
```

`0 deferred` over the whole repository is what the checker's own milestone is measured by.

### `--timings`

Prints the wall time of every pass of the checker, in the order it ran, then the total. The first pass - finding the
workspace, lexing and parsing every file the target depends on, and building the module graph - reads and parses the
whole workspace the target belongs to, not only the files the run reports on, which is why it dominates the total for
a target that is a small part of a large workspace.

```console
$ cargo run --release -q -- run ../compiler check --timings ..
finding the workspace: 53 ms
name resolution: 415 ms
names in type positions: 453 ms
bodies: 14796 ms
all passes: 41069 ms
255 files, no problems
```

### A false positive is a bug in the checker

`check` is the one command whose answer is never argued with: a program that should type check and does not, or the
reverse, is reported as a defect of the checker rather than worked around. See
[What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md) for the mistakes that
most often produce a diagnostic worth reading twice before assuming it is wrong.

## Examples

The whole repository, workspace and all, checked in one run:

```console
$ cd bootstrap
$ cargo run --release -q -- run ../compiler check ..
255 files, no problems
```

## Related

- [The torb command](the-torb-command.md) - every subcommand in one table.
- [Verify your work](verifying-your-work.md) - where `check` sits among `canon` and `test`.
- [torb build](torb-build.md) - what runs after `check` succeeds.
- [torb run](torb-run.md) - running a program instead of only checking it.
