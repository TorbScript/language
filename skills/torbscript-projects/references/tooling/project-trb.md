---
title: project.trb
summary: The manifest of a project - its name, dependencies, workspace, further programs and profiles. The names of its files say what it produces, and the settings that decide its files are read before anything runs.
kind: tooling
status: stable
order: 90
keywords:
  - project.trb
  - manifest
  - Project
  - dependencies
  - workspace
  - program
  - profile
source:
  - std/project/src/lib.trb
  - compiler/src/project/manifest.trb
  - compiler/src/project/programs.trb
  - compiler/src/project/workspace.trb
  - docs/design/PROJECT.md
  - CONCEPT.md#projecttrb
---

Every project has one `project.trb` at its root. It is a receiver script (skill `torbscript-language`: `references/language/configuration/receiver-scripts.md`)
against [`Project`](../standard-library/project.md), so a setting is a field written with `=`
(`name = "acme/shop"`), a section is a field configured in place (`dependencies { ... }`), and the rest are command
calls (`program "migrate", entry: "tools/migrate.trb"`).

**The names of the files say what a package produces, and the manifest says what no file name can**: who the package
is, what it needs, where that comes from, and the rare second program. `src/main.trb` is the program, `src/lib.trb`
the library and a file called `*.test.trb` a test without a line in `project.trb`.

## Synopsis

```trb fragment
name = "acme/shop"
version = "1.0.0"

dependencies {
  runtime "acme/http:^1.2.3"
  development "acme/mock-server:^3.4.5"
}

program "migrate", entry: "tools/migrate.trb"

profile "release" {
  optimize = 3
}
```

## What it does

### What the files of a package are

**A package's files are every `.trb` file below its directory** that is not inside a nested package - a directory
below it with a `project.trb` of its own - skipping hidden directories, `build`, `target` and `node_modules`.
`project.trb` and `project.lock.trb` are never files of it. So `tools/migrate.trb` is a file of the package as much as
`src/cart.trb` is, and it may import the package's modules.

What a file is follows from its name and from the `program` lines, never from anything else the manifest says:

| The file | What it is | Top-level code | Importable |
|----------|------------|----------------|------------|
| `src/main.trb` | The default program, named after the package's short name: `acme/shop` builds `shop` | yes | no |
| `src/lib.trb` | The library, what `"owner/name"` imports | no | yes |
| `src/<path>.trb` | A module of the package, what `"owner/name/<path>"` imports | only where nothing imports it | yes |
| the `entry` of a `program` line | Another program | yes | no |
| `*.test.trb`, wherever it lies | A test, which `torb test` (skill `torbscript-testing`: `references/tooling/torb-test.md`) finds | yes | no |
| any other `.trb` file | A module that a relative path of the package reaches, or a script (skill `torbscript-language`: `references/language/modules-and-packages/top-level-code.md`) where nothing imports it | only where nothing imports it | yes |

A package without `src/main.trb` and without a `program` line has no program: `torb build` (skill `torbscript`: `references/tooling/torb-build.md`) checks
it and builds nothing. With a `src/lib.trb` it is a library; without one it is a directory of scripts, such as
`examples/tour`, whose files are run by naming them.

### What the toolchain reads

`torb check`, `build`, `run` and `test` are what read `project.trb`. They read it **statically** first, from its
syntax tree, and evaluate it against `Project` in the sandboxed VM - the way a receiver script runs - only where a
setting they use is computed.

| Setting | What it decides |
|---------|------------------|
| `name` | The package's `owner/name`, what an importing package's `dependencies` has to name, and the name of the default program (the part after the `/`) |
| `version` | The version the resolver takes a workspace member at, and what [`torb publish`](torb-publish.md) publishes |
| `language` | The oldest language the project needs; written into the lock and the index |
| `description`, `license`, `repository`, `authors` | Metadata; `description` and `license` are required by [`torb publish`](torb-publish.md) |
| `registry "owner", url: "..."` | Where the packages of an owner come from: `https://...`, or `file:` and a directory relative to the project. An owner no line binds comes from `https://packages.torb.dev` |
| `source "owner/name", path: "..."` | A package taken from a directory instead of its registry, listed in the lock as `path:`. `git:` and `archive:` sources type check and are not resolved yet |
| `prelude` | The package whose public names are in scope in every file (default `std/prelude`) |
| `dependencies { runtime "..." }` | What a package may import at all, and what the package manager resolves into [project.lock.trb](project-lock-trb.md) |
| `dependencies { development "..." }` | The same, for a project's own tests and tools |
| `workspace { members "..." }` | Which directories are the projects of a workspace |
| `program "name", entry: "...", output: "..."` | A program beyond `src/main.trb`, or with no `entry` a new name for that one; see [Programs](#programs) |
| `profile "dev" { ... }`, `profile "release" { ... }` | How hard the C compiler optimizes and whether the binary carries debug information; see [Profiles](#profiles) |
| `tasks { workers = 4 }` | How many workers the program's tasks run on, where `TORB_WORKERS` does not say (default: the core count) - an integer from 1 to 1024 |
| `tasks { blocking = 8 }` | How many threads its blocking pool has, where `TORB_BLOCKING` does not say (default: 4) - an integer from 1 to 1024 |

`test { coverageThreshold = 80 }` is part of `Project`'s vocabulary and type checks, because the whole file is also
checked as an ordinary program against `Project` - but no command reads it yet.

**There is no `build { }` and no `test { input }`.** They said what the names of the files say, and a manifest that
still writes one does not check - `Project` has no `build` and `Test` no `input` - with a note that names what took its
place:

```console
$ torb check lib
error: Cannot find `build` here
 --> lib/project.trb:3:1
  |
3 | build {
  | ^^^^^
  = `build { }` is gone: a file called `src/main.trb` is the program and `src/lib.trb` the library, another program is a line - `program "migrate", entry: "tools/migrate.trb"` - and `output:` says where it goes
```

### Programs

**A program is `src/main.trb`, or a `program` line.** A line with an `entry` adds a program; a line without one
renames the default program and adds nothing:

```trb fragment
program "migrate", entry: "tools/migrate.trb"
program "importer", entry: "tools/importer.trb", output: "dist/importer"
program "shop-server"
```

- **`entry`** is the file whose top-level code the program is, relative to the project. It may lie anywhere in the
  package except `src/lib.trb`.
- **`output`** is where the binary goes, relative to the project and taken literally: `dist/importer` under every
  profile and every target. Without it the binary goes to `build/<profile>/<name>`.
- **The name** is what `torb run` (skill `torbscript`: `references/tooling/torb-run.md`) and `torb build` (skill `torbscript`: `references/tooling/torb-build.md`) are given and what the binary is
  called. It is named like a package - lowercase letters, digits and `-` - so `torb run <name>` is never a path.

`torb build` builds `src/main.trb` first and then the `program` lines in the order they are written. The toolchain
reads the lines before it can run anything, because the `entry` decides which files may hold top-level code, and
`torb check` reports what is wrong with one at its line:

| The line | The error |
|----------|-----------|
| two programs of one name, the default program's among them | ``There are two programs called `migrate` `` |
| two lines without an `entry` | `` `torb` and `cli` both rename the default program`` |
| a line without an `entry` and no `src/main.trb` | `` `torb` renames the default program, and there is no `src/main.trb` ``, with the note ``A program with an entry of its own names it: `program "torb", entry: "tools/torb.trb"` `` |
| an `entry` that is no file | ``The entry `tools/nothing.trb` of the program `migrate` is not a file`` |
| an `entry` or an `output` outside the project | ``The entry `../tools/migrate.trb` of the program `migrate` is outside `acme/shop` `` |
| `entry: "src/lib.trb"` | `` `src/lib.trb` is the library of `acme/shop`, so it cannot be the entry of a program`` |
| a name that is no program name | `` `Migrate` is no program name`` |

**An entry is never importable.** A file that may hold top-level code has no initialization order to protect only
because nothing imports it, so a `use` of one is an error at the `use`, and so is `"acme/shop/main"`:

```console
$ torb check shop
error: `../tools/migrate` is the entry of the program `migrate`, so nothing can import it
 --> src/report.trb:1:1
  |
1 | use greeting from "../tools/migrate"
  | ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  = A file that may hold top-level code is never importable. What two programs share belongs in a module of the package, which both import
```

### Profiles

**There are two profiles, `dev` and `release`, and a `profile` block sets the few knobs each one has.** The command
line chooses the profile - `torb build` (skill `torbscript`: `references/tooling/torb-build.md`) builds `release`, `torb run --native` and `torb test` build
`dev`, and `--profile` or `--release` says otherwise - and the block says how that profile builds:

```trb fragment
profile "dev" {
  debugInformation = true
}
profile "release" {
  optimize = 3
}
```

| Setting | Default | What it does |
|---------|---------|--------------|
| `optimize` | `1` in `dev`, `2` in `release` | How hard the C compiler optimizes, 0 to 3: `-O<optimize>`, and `/Od`, `/O1` or `/O2` with MSVC |
| `debugInformation` | `false` | `-g`: the binary carries debug information for a debugger |
| `panicFrames` | `true` in `dev`, `false` in `release` | Read and ignored for now: a `dev` binary prints the frames of a panic and a `release` binary does not, whatever the block says |

A member of a workspace without a block of that name takes its workspace root's. A block may be computed; then the
toolchain evaluates the file, as it does for a computed `version`. `torb check` reports a block named anything else
(`` `fast` is no profile: the profiles are `dev` and `release` ``), a second block of one name
(``There are two `profile "dev"` blocks``) and a level outside the range
(`` `optimize = 4` of `profile "dev"` is outside 0 to 3``).

### What has to be a literal

**What decides the workspace and its files is a literal**: `language`, `name`, `prelude`, `dependencies`, `source`,
`registry`, `workspace` and `program` are needed before anything can run, so one of them written as something that
would have to run is an error at its value:

```console
$ torb check shop
error: `name` has to be a plain string
 --> shop/project.trb:3:8
  = The toolchain reads `name` before it can run anything, so it cannot be computed
```

A `program` line says it for its three arguments together: ``The name, the `entry` and the `output` of a `program`
have to be plain strings``, with the note `The toolchain reads them before it can run anything: the entry decides which
files may hold top-level code`.

**`version`, `tasks` and the `profile` blocks may be computed.** Where one is - or where the file has a line that is no
setting at all, such as an `if` - the build, `torb run` and `torb test` evaluate the file in the sandboxed VM and read
them from what it configured. What the evaluation read is kept in `build/manifest-inputs.trb` beside what it answered,
each file and each variable by a hash - a variable never by its value - and the next build evaluates again only where
one of them changed. A file that computes only what no command reads, such as `description`, is never evaluated.

### What a project file may do

A project file may import `std/fs`, `std/text` and `std/os/environment`, and nothing else - `torb check` reports any other
`use` at its line. When it is evaluated, it may read files below its own directory (a relative path is read against
that directory) and every variable of the environment, and it runs at most 1 000 000 steps, 16 MB of allocations and
two seconds. Anything outside that stops the evaluation with an error at the line that asked for it:

```console
$ torb manifest shop
error: The script may not read `../VERSION`: it is not inside `C:/work/shop`
 --> shop/project.trb:4
  |
4 | version = File.readText("../VERSION").ok() ?? "0.0.0"
```

### Workspaces

A `workspace { members "..." }` names every project below it - see
[Workspace](../standard-library/project.md#workspace) for what a pattern matches. `check`, `build` and `test` at the
workspace root all work on every member this way, which is how the toolchain finds `std/` when it checks a package
that only names a dependency on it. A member is a nested package, so its files are not files of the root.

```trb fragment
name = "acme/shop"
version = "1.4.0"

workspace {
  members "packages/*"
}
```

## Pitfalls

**A directory named without a slash is a program name.** `torb build compiler` builds the program called `compiler`;
the directory is `torb build ./compiler`.

**What two programs share goes into a module.** An entry cannot be imported, so a helper that `src/main.trb` and
`tools/migrate.trb` both need lives in `src/<something>.trb`, and both import it.

**A relative path stays inside its package.** `use Vector2 from "../../linear/src/vector2"` from inside
`std/geometry` is ``error: `../../linear/src/vector2` leaves `std/geometry` ``: another package is imported by its
name, `"std/linear/vector2"`, and it has to be a dependency.

## Examples

`compiler/project.trb`, the whole of it: the compiler's program is its `src/main.trb`, and the one line renames it so
that the binary is called `torb` rather than `compiler`. Its tests are the `*.test.trb` files of `compiler/tests/`,
found by their names.

```trb fragment
name = "torbscript/compiler"
version = "0.1.0"

// `src/main.trb` is the program, and its binary is called `torb` rather than `compiler`
program "torb"
```

A shop with a second program that writes its binary to a fixed place, a library of its own that both programs import,
and a faster `release` profile:

```text
shop/
├ src/
├─ main.trb            the program `shop`
├─ lib.trb             the library `acme/shop`
├─ cart.trb            a module: "acme/shop/cart", or "./cart" from beside it
├ tools/
├─ migrate.trb         the program `migrate`: use Cart from "../src/cart"
├ tests/
├─ cart.test.trb       a test, found by its name
└ project.trb
```

```trb fragment
name = "acme/shop"
version = "1.4.0"
license = "MIT"
description = "A shop"

program "migrate", entry: "tools/migrate.trb", output: "dist/migrate"

profile "release" {
  optimize = 3
}
```

`torb build` in `shop/` writes `build/release/shop` and `dist/migrate`, `torb run migrate` runs the second one, and
`torb test` runs `tests/cart.test.trb`.

## Related

- [std/project](../standard-library/project.md) - `Project`, `Program`, `Profile`, `Dependencies` and the rest as a
  package.
- [project.lock.trb](project-lock-trb.md) - the file `project.trb`'s dependencies resolve into.
- torb build (skill `torbscript`: `references/tooling/torb-build.md`) and torb run (skill `torbscript`: `references/tooling/torb-run.md`) - what a program name and a path mean on the command line.
- [Packages](../language/modules-and-packages/packages.md) - `owner/name`, and the rules around publishing one.
- [Workspaces](../language/modules-and-packages/workspaces.md) - one root, several members, one lock file.

