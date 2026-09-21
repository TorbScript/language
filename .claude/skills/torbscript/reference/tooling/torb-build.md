---
title: torb build
summary: torb build type checks a program, lowers it to C, and hands the C to whatever compiler it finds - one file in, one native binary out, nothing to configure.
kind: tooling
status: stable
order: 50
keywords:
  - torb build
  - native binary
  - C back end
  - emit-c
  - TORB_CC
  - TORB_RUNTIME
source:
  - compiler/src/cli/build.trb
---

`build` is `check` plus the C back end: a program that does not type check is never handed to a C compiler, and a
construct the back end cannot lower yet is reported the same way a type error is, rather than miscompiled.

## Synopsis

```text
torb build [path]         Compile an entry file to a native binary through C
    --emit-c                Write the C and stop, which needs no C compiler at all
    --output <file>          Where the binary goes (the C is written next to it)
```

## What it does

### Finding the entry

`path` (default `.`) is checked exactly like a `check` target. What is compiled is one entry file: either `path`
itself, when it names a file directly, or the single file requested when `path` names a project whose
`build { input "..." }` (or the default `src/main.trb`) matches one of its modules. A path that resolves to more
than one file with none of them the project's build input is refused:

```text
error: `torb build` needs one entry file. Name the file, or a project with a `build` input
```

Only what the entry file reaches is emitted - a function nothing calls from `main` never becomes C.

### What the back end does not lower yet

Not every declaration `torb check` accepts lowers to C yet; `torb ir --statistics` on the same path answers what
fraction does, as a count rather than a number written here that would only go stale. What is still missing is a
**finding**, not a miscompilation - `build` reports it and refuses, the same way it reports a type error. A variadic
parameter of a function you declare is one of the constructs still missing (`print`'s own variadic call is a
back-end intrinsic and builds regardless):

```console
$ cd bootstrap
$ cargo run --release -q -- run ../compiler build ../my-project
error: a variadic argument list is not supported by the native back end yet (at my-project/src/main.trb:5:7)
1 problems the native back end cannot compile yet, nothing was built
```

A program that only uses what the back end already lowers - functions, types, control flow, arithmetic, `print`,
string interpolation, `Process.exit` - builds and runs like any other native binary:

```console
$ cargo run --release -q -- run ../compiler build ../my-project --output ../build/dev/my-project
wrote ../build/dev/my-project.exe
```

### `--emit-c`

Writes the generated C next to where the binary would have gone and stops, needing no C compiler on the machine at
all. This is what lets a change to the back end be reviewed as a diff of the C it emits.

### The C compiler

Without `--emit-c`, `build` looks for one in this order: `$TORB_CC`, `clang`, `gcc`, `cc`, then `cl` (MSVC), and
compiles with warnings turned into errors, because a warning in generated code is the emitter's bug and not the
program's. `$TORB_RUNTIME` says where the C runtime (`runtime/`) is; without it, `build` walks up from the working
directory looking for it, which is only found inside a checkout of the toolchain itself.

### Exit codes

`0` for a binary (or, with `--emit-c`, a C file); `1` when the program does not check or the back end cannot lower
part of it; `3` when no C compiler or no runtime was found; `70` when the C compiler itself fails on generated C,
which is always a bug of `build` and never the program's fault.

## Examples

Look at the C before trusting it, then build the binary from the same source:

```console
$ cd bootstrap
$ cargo run --release -q -- run ../compiler build ../examples/tour/src/scratch.trb --emit-c --output ../build/dev/scratch
wrote ../build/dev/program.c
$ cargo run --release -q -- run ../compiler build ../examples/tour/src/scratch.trb --output ../build/dev/scratch
wrote ../build/dev/scratch.exe
```

## Related

- [torb check](torb-check.md) - what `build` runs before it emits anything.
- [torb run](torb-run.md) - running the same program without a build step.
- [project.trb](project-trb.md) - `build { input, target, output }`, and which of them `build` reads today.
- [The torb command](the-torb-command.md) - every subcommand in one table.
