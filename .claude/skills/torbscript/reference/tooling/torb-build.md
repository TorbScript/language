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
  - TORB_MEMORY_LIMIT
source:
  - compiler/src/cli/build.trb
---

`build` is `check` plus the C back end: a program that does not type check is never handed to a C compiler, and a
construct the back end cannot lower yet is reported the same way a type error is, rather than miscompiled.

## Synopsis

```text
torb build [path]         Compile an entry file to a native binary through C
    --profile dev|release   How hard the C compiler optimizes (default: release)
    --release               The same as --profile release
    --emit-c                Write the C and stop, which needs no C compiler at all
    --output <file>          Where the binary goes (the C is written next to it)
    --target <os>-<arch>     What the program is built for (default: this machine), with --emit-c for another one
    --embed-vm               A native binary of the VM that runs the program's bytecode, as torb run does
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

A program may have **more than one entry file**, and `torb test` is the one command that builds one: every `*.test.trb`
of a directory is an entry of one binary, and the generated `main` runs them in the order of their paths with the name
of each file printed in front of its tests. `build` itself always takes exactly one.

### What the back end does not lower yet

Not every declaration `torb check` accepts lowers to C yet; `torb ir --statistics` on the same path answers what
fraction does, as a count rather than a number written here that would only go stale. What is still missing is a
**finding**, not a miscompilation - `build` reports it and refuses, the same way it reports a type error. A variadic
parameter of a function you declare is one of the constructs still missing (`print`'s own variadic call is a
back-end intrinsic and builds regardless):

```console
$ torb build my-project
error: a variadic argument list is not supported by the native back end yet (at my-project/src/main.trb:5:7)
1 problem the native back end cannot compile yet, nothing was built
```

A program that only uses what the back end already lowers - functions, types, control flow, arithmetic, `print`,
string interpolation, `Process.exit` - builds and runs like any other native binary:

```console
$ torb build my-project --output build/dev/my-project
wrote ../build/dev/my-project.exe
```

### `--emit-c`

Writes the generated C next to where the binary would have gone and stops, needing no C compiler on the machine at
all. This is what lets a change to the back end be reviewed as a diff of the C it emits.

### `--target`

What `OperatingSystem.current`, `Architecture.current` and `ByteOrder.current` answer in the program, and so which
arm of a `match` on them is compiled ([Compile-time branches](../language/execution/compile-time-branches.md)): one
of `windows-x64`, `windows-arm64`, `linux-x64`, `linux-arm64`, `macos-x64`, `macos-arm64`, `freebsd-x64`,
`freebsd-arm64`. Without it, a build is for the machine `torb` runs on. A target that is not this machine needs
`--emit-c`, because the C compiler `build` finds compiles for this machine; the C it writes builds on the target.

### `--embed-vm`

Builds a native binary that embeds the VM instead of compiling the program itself to C: the program is checked and
encoded as bytecode as [`torb run`](torb-run.md) does, and the interpreter is compiled with that bytecode inside it. The
binary runs the program as `torb run` would - the same output, exit code and panics - without `torb` or a C compiler
where it runs, which is how a program that loads receiver scripts ships as one executable: they run in their sandbox
([The sandbox](../language/configuration/the-sandbox.md)). The front end is not in the binary, so it loads a script only
by a path the program was compiled with. The interpreter is built from the toolchain's own `compiler/`, found like
`std/` and `runtime/` (or where `TORB_COMPILER` says), which costs a C compile of about half a minute per binary.
`--embed-vm` takes neither `--emit-c` nor `--target` (docs/design/VM.md section 11).

```console
$ torb build --embed-vm examples/config-dsl --output build/config-dsl
$ build/config-dsl
Listening on 0.0.0.0:8443 (tls: true)
```

### `--profile`

A profile is how hard the C compiler works on the one C file: `dev` is `-O1`, `release` is `-O2`. The C is the same
under both, so a profile never changes what a program means - only how long the build takes and how fast the binary
is. `build` builds `release` unless told otherwise, and the default path of the binary is `build/<profile>/<name>`;
`torb test` and `torb run` build `dev`.

The one thing besides speed a profile decides is the **default memory limit**. A `dev` binary stops at the smaller of
8 GiB and half the physical memory, with `panic: out of memory: the limit of ... was reached` and exit code `102`, so
a scratch program or a test suite that allocates without end cannot take the machine down; a `release` binary has no
limit of its own, because what a shipped program may use is its user's decision. `TORB_MEMORY_LIMIT` in the
environment of the running binary overrides both: a number of bytes, or one with `K`, `M`, `G` or `T` (`512M`), and
`0` or `none` for no limit. [`torb run`](torb-run.md) says what the limit does on each system.

### The C compiler

Without `--emit-c`, `build` looks for one in this order: `$TORB_CC`, `clang`, `gcc`, `cc`, then `cl` (MSVC), and
compiles with warnings turned into errors, because a warning in generated code is the emitter's bug and not the
program's. `$TORB_RUNTIME` says where the C runtime (`runtime/`) is; without it, `build` walks up from the entry file,
then from the working directory and then from `torb` itself to a directory that has `runtime/include/torb.h`, which a
checkout of the toolchain has - so `<checkout>/build/release/torb` finds its runtime from anywhere.

A C file of 8 MB or more is compiled while holding one of `$TORB_BUILD_SLOTS` (default 3) machine-wide build slots,
through `tools/build-slot.sh` beside the runtime, so that several builds of that size at once do not run the machine
out of memory; `build` prints one line when it has to wait for one.

### Exit codes

`0` for a binary (or, with `--emit-c`, a C file); `1` when the program does not check or the back end cannot lower
part of it; `3` when no C compiler or no runtime was found; `70` when the C compiler itself fails on generated C,
which is always a bug of `build` and never the program's fault. The binary it writes leaves with `101` on a `panic`
and with `102` when it runs out of memory.

## Examples

Look at the C before trusting it, then build the binary from the same source:

```console
$ torb build examples/tour/src/scratch.trb --emit-c --output build/dev/scratch
wrote ../build/dev/program.c
$ torb build examples/tour/src/scratch.trb --output build/dev/scratch
wrote ../build/dev/scratch.exe
```

## Related

- [torb check](torb-check.md) - what `build` runs before it emits anything.
- [torb run](torb-run.md) - running the same program without a build step.
- [project.trb](project-trb.md) - `build { input, target, output }`, and which of them `build` reads today.
- [The torb command](the-torb-command.md) - every subcommand in one table.

