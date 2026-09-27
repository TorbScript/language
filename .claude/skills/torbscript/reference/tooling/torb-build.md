---
title: torb build
summary: torb build type checks every program below a path, or the one named, lowers each to C and hands the C to whatever compiler it finds - a library is checked and builds nothing.
kind: tooling
status: stable
order: 50
keywords:
  - torb build
  - program
  - profile
  - native binary
  - C back end
  - emit-c
  - TORB_CC
  - TORB_RUNTIME
  - TORB_MEMORY_LIMIT
source:
  - compiler/src/cli/build.trb
  - compiler/src/project/programs.trb
  - docs/design/PROJECT.md
---

`build` is `check` plus the C back end: a program that does not type check is never handed to a C compiler, and a
construct the back end cannot lower yet is reported the same way a type error is, rather than miscompiled.

## Synopsis

```text
torb build [path | name]    Compile every program below the path, or the one named, through C
    --profile dev|release   How hard the C compiler optimizes (default: release)
    --release               The same as --profile release
    --emit-c                Write the C and stop, which needs no C compiler at all (one program)
    --output <file>          Where the binary goes, the C next to it (one program)
    --target <os>-<arch>     What the program is built for (default: this machine), with --emit-c for another one
    --embed-vm               A native binary of the VM that runs the program's bytecode, as torb run does
    --timings                The wall time of every step on standard error, the passes of the checker first
```

## What it does

### A path or a name

**An argument that contains `/` or `\`, ends in `.trb`, or is `.` or `..` is a path; anything else is the name of a
program.** A program is named like a package - lowercase letters, digits and `-` - so the two readings never overlap,
and nothing has to be asked of the disk to tell them apart. `torb build compiler` looks for a program called
`compiler`; the directory is `torb build ./compiler`.

### What is built

Every path (default `.`) is checked exactly like a `check` target, and then:

- **A directory builds every program whose entry lies below it**: per package its `src/main.trb` first, named after
  the package's short name (`acme/shop` builds `shop`) or renamed by a `program` line, then its `program` lines in the
  order [`project.trb`](project-trb.md) writes them. At a workspace root that is every program of every member.
- **A name builds the one program of that name** below the paths, the working directory where none is given. A name
  nothing declares is refused with the names that exist:

  ```text
  error: There is no program `nothing` here
    = the programs are: shop, migrate
    = a directory or a file is written as a path: `./nothing`
  ```

- **A file named directly is the one program**, whatever it is: a script, or the entry of a program, which then keeps
  that program's name and `output`.

**A package without a program builds nothing, and that is not an error.** There is no artifact a library produces on
its own, so `build` checks it, says so and leaves with `0`: `acme/lib is a library: checked, nothing to build` where
the package has a `src/lib.trb`, `acme/tour has no program: checked, nothing to build` for a package of scripts with
neither `src/main.trb` nor `src/lib.trb`, and `no package below <path> has a program: checked, nothing to build` for a
directory of several.

**`--output` and `--emit-c` are for one program.** Where the path has several, `build` names them instead of guessing:

```text
error: acme/shop has 2 programs. Name one: `torb build shop`
  = shop, migrate
  `--output` is for one program
```

Two programs whose binaries would land on one path are refused before either is built:
``error: `migrate` and `importer` both build to `dist/tool` ``.

Only what a program's entry reaches is emitted - a function nothing calls from its top-level code never becomes C. The
same goes for checking: every body of what the command names is checked and reported as `check` does, and a module of
another package - `std` included - has its bodies checked the first time the lowering reaches it, so a program that
prints one line does not check every body of every package its workspace holds. A program the back end refuses is
checked whole and lowered again before anything is reported, so what it is told is the same either way.

### Where the binary goes

`<package>/build/<profile>/<program>`, and `<package>/build/<target>/<profile>/<program>` when `--target` names
another machine, so that two targets never write over each other. The `output` of a `program` line replaces the whole
path and is taken literally, relative to the project: `output: "dist/migrate"` is `dist/migrate` under every profile
and every target. `--output <file>` overrides both, for one program and one invocation. The C of a program is written
beside its binary as `program.c`.

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
$ torb build ./my-project
error: a variadic argument list is not supported by the native back end yet (at my-project/src/main.trb:5:7)
1 problem the native back end cannot compile yet, nothing was built
```

A program that only uses what the back end already lowers - functions, types, control flow, arithmetic, `print`,
string interpolation, `Process.exit` - builds and runs like any other native binary:

```console
$ torb build ./my-project --output build/dev/my-project
wrote ../build/dev/my-project.exe
```

### `--emit-c`

Writes the generated C next to where the binary would have gone and stops, needing no C compiler on the machine at
all. This is what lets a change to the back end be reviewed as a diff of the C it emits.

What it writes is `program.c`, the whole program as one translation unit, and `program.hash`, the hash of that file.
A program of 4 MB of C or more gets its units beside them as well - `program.h`, which every unit includes, and
`program-01.c` onwards, the first of which holds the static data and every other a share of the functions - the same
C cut apart so that the units compile in parallel. `program.c` alone builds the same program: `cc -I<runtime>/include program.c <runtime>/*.c <runtime>/os/*.c`.

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

### `--timings`

Prints the wall time of every step on standard error, in the order they ran: the passes of the checker as
[`torb check --timings`](torb-check.md) prints them, indented, then checking, lowering (which includes the bodies it
checks on the way), ownership, verifying, emitting and writing the C, and compiling and linking it. It is what says
where the time of a build goes before anybody guesses:

```console
$ torb build --timings --emit-c tests/conformance/adts.trb --output build/adts
  finding the workspace: 63 ms
  lexing, parsing and the module graph: 1738 ms
  name resolution: 130 ms
  bodies: 3 ms
checking: 2082 ms
lowering: 48 ms
ownership: 3 ms
verifying: 2 ms
emitting the C: 88 ms
writing the C: 2 ms
wrote build/program.c
```

### `--profile`

A profile is how hard the C compiler works on the one C file: `dev` is `-O1`, `release` is `-O2`. The C is the same
under both, so a profile never changes what a program means - only how long the build takes and how fast the binary
is. `build` builds `release` unless told otherwise, and the default path of the binary is `build/<profile>/<name>`;
`torb test` and `torb run --native` build `dev`.

A `profile` block of [`project.trb`](project-trb.md) sets how its profile builds: `optimize` is the level, 0 to 3
(`-O<optimize>`, and `/Od`, `/O1` or `/O2` with MSVC), and `debugInformation = true` adds `-g`. A member of a
workspace without a block of that name takes its workspace root's:

```trb fragment
profile "release" {
  optimize = 3
}
```

One of the two things besides speed a profile decides is the **default memory limit**. A `dev` binary stops at the
smaller of 8 GiB and half the physical memory, with `panic: out of memory: the limit of ... was reached` and exit code `102`, so
a scratch program or a test suite that allocates without end cannot take the machine down; a `release` binary has no
limit of its own, because what a shipped program may use is its user's decision. `TORB_MEMORY_LIMIT` in the
environment of the running binary overrides both: a number of bytes, or one with `K`, `M`, `G` or `T` (`512M`), and
`0` or `none` for no limit. [`torb run`](torb-run.md) says what the limit does on each system.

The other is **the frames of a panic**. A `dev` binary keeps a frame per call on the stack of the function, and a panic
prints them below its site, innermost first - `  in pick, at src/main.trb:12:5` - so a panic inside the standard
library or a closure still names the line of the program that led there ([panic](../language/errors/panic.md)). A
`release` binary keeps none and prints the site alone. The C is the same under both: the two macros that make a frame
compile to nothing outside of `dev`.

### The C compiler

Without `--emit-c`, `build` looks for one in this order: `$TORB_CC`, `clang`, `gcc`, `cc`, then `cl` (MSVC), and
compiles with warnings turned into errors, because a warning in generated code is the emitter's bug and not the
program's. `$TORB_RUNTIME` says where the C runtime (`runtime/`) is; without it, `build` walks up from the entry file,
then from the working directory and then from `torb` itself to a directory that has `runtime/include/torb.h`, which a
checkout of the toolchain has - so `<checkout>/build/release/torb` finds its runtime from anywhere.

Every C file of a build - the program's and the runtime's - is compiled into an object of its own, as many at a time as
the machine has processors (`$TORB_BUILD_JOBS` says otherwise), and the objects are linked. A program of 4 MB of C or
more is written as several units besides `program.c`: `program.h` and `program-01.c` up to at most `program-17.c` in
the output directory, which compile in parallel and, with gcc, are optimized across each other when they are linked. Every object is kept in `build/objects/` beside the runtime, named
after the hash of everything that decides it, so a file that did not change is not compiled again - an unchanged program
only links, and the runtime is compiled once and not with every program. `$TORB_OBJECT_CACHE_MB` (default 2048) bounds
that directory. Without `sh` (`tools/build-units.sh` runs the compiles) or with MSVC, `program.c` and the runtime are
compiled in one call of the C compiler instead.

When what is compiled is 8 MB or more, the whole batch holds one of `$TORB_BUILD_SLOTS` (default 3) machine-wide build
slots, through `tools/build-slot.sh` beside the runtime, so that several builds of that size at once do not run the
machine out of memory; `build` prints one line when it has to wait for one.

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

A package with `src/main.trb` and the line `program "migrate", entry: "tools/migrate.trb"` has two programs, and
`build` in its directory builds both, the default program first; a name builds one:

```console
$ torb build
wrote C:/work/shop/build/release/shop.exe
wrote C:/work/shop/build/release/migrate.exe
$ torb build migrate --profile dev
wrote C:/work/shop/build/dev/migrate.exe
```

## Related

- [torb check](torb-check.md) - what `build` runs before it emits anything.
- [torb run](torb-run.md) - running the same program without a build step.
- [project.trb](project-trb.md) - the `program` lines and `profile` blocks `build` reads, and what the names of the
  files decide.
- [The torb command](the-torb-command.md) - every subcommand in one table.

