---
title: project.trb
summary: The manifest of a project - name, dependencies, the workspace it belongs to, and what torb build and torb test read out of it today.
kind: tooling
status: stable
order: 90
keywords:
  - project.trb
  - manifest
  - Project
  - dependencies
  - workspace
source:
  - std/project/src/lib.trb
  - compiler/src/project/manifest.trb
  - CONCEPT.md#projecttrb
---

Every project has one `project.trb` at its root. It is a [receiver script](../language/configuration/receiver-scripts.md)
against [`Project`](../standard-library/project.md), so a setting is a field written with `=`
(`name = "acme/shop"`) and a section is a field configured in place (`dependencies { ... }`).

## Synopsis

```trb fragment
name = "owner/name"
version = "1.0.0"

dependencies {
  runtime "acme/http:^1.2.3"
  development "acme/mock-server:^3.4.5"
}

workspace {
  members "packages/*"
}

build {
  input = "src/main.trb"
}

test {
  input = "tests"
}
```

## What it does

### What the toolchain reads

`torb check`, `build`, `run` and `test` are what read `project.trb`. They read it **statically** first, from its
syntax tree, and evaluate it against `Project` in the sandboxed VM - the way a receiver script runs - only where a
setting they use is computed. Only the following settings are consulted:

| Setting | What it decides |
|---------|------------------|
| `name` | The package's `owner/name`, and what an importing package's `dependencies` has to name |
| `version` | The version the resolver takes a workspace member at, and what [`torb publish`](torb-publish.md) publishes |
| `language` | The oldest language the project needs; written into the lock and the index |
| `description`, `license`, `repository`, `authors` | Metadata; `description` and `license` are required by [`torb publish`](torb-publish.md) |
| `registry "owner", url: "..."` | Where the packages of an owner come from: `https://...`, or `file:` and a directory relative to the project. An owner no line binds comes from `https://packages.torb.dev` |
| `source "owner/name", path: "..."` | A package taken from a directory instead of its registry, listed in the lock as `path:`. `git:` and `archive:` sources type check and are not resolved yet |
| `prelude` | The package whose public names are in scope in every file (default `std/prelude`) |
| `dependencies { runtime "..." }` | What a package may import at all, and what the package manager resolves into [project.lock.trb](project-lock-trb.md) |
| `dependencies { development "..." }` | The same, for a project's own tests and tools |
| `workspace { members "..." }` | Which directories are the projects of a workspace |
| `build { input = "..." }` | The entry file [`torb build`](torb-build.md) compiles when a directory is given |
| `test { input = "..." }` | The directory [`torb test`](torb-test.md) defaults to (`test` itself still has to be told the path) |
| `tasks { workers = 4 }` | How many workers the program's tasks run on, where `TORB_WORKERS` does not say (default: the core count) - an integer from 1 to 1024 |
| `tasks { blocking = 8 }` | How many threads its blocking pool has, where `TORB_BLOCKING` does not say (default: 4) - an integer from 1 to 1024 |

`build { target, output }` and `test { coverageThreshold }` are part of `Project`'s vocabulary
and type check, because the whole file is also checked as an ordinary program against `Project` - but no command
reads them yet.

**What decides the workspace is a literal**: `name`, `prelude`, `dependencies`, `workspace` and the `input` of
`build` and `test` are needed before anything can run, so one of them written as something that would have to run is
an error at its value:

```console
$ torb check shop
error: `name` has to be a plain string
 --> shop/project.trb:3:8
  = The toolchain reads `name` before it can run anything, so it cannot be computed
```

**`version` and `tasks` may be computed.** Where one is - or where the file has a line that is no setting at all, such
as an `if` - the build, `torb run` and `torb test` evaluate the file in the sandboxed VM and read the two from what it
configured. What the evaluation read is kept in `build/manifest-inputs.trb` beside what it answered, each file and each
variable by a hash - a variable never by its value - and the next build evaluates again only where one of them changed.
A file that computes only what no command reads, such as `description`, is never evaluated.

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
that only names a dependency on it.

```trb
name = "acme/shop"
version = "1.4.0"

workspace {
  members "packages/*"
}
```

## Examples

`examples/game-engine/project.trb`, unchanged: `name`, `build { input }` and `test { input }` are read; `authors`,
`build { target, output }` and `test { coverageThreshold }` type check and are not.

```trb fragment
name = "torbscript/example-game-engine"
version = "0.1.0"
authors "Torben Köhn"

const binary = name.substringAfter("/") ?? name   // Read before `build { }`: only the innermost receiver is implicit

build {
  target = "release"
  input = "src/main.trb"
  output = "build/{target}/{binary}"
}
test {
  input = "tests"
  coverageThreshold = 80
}
```

## Related

- [std/project](../standard-library/project.md) - `Project`, `Dependencies`, `Build`, `Test` and `Workspace` as a package.
- [project.lock.trb](project-lock-trb.md) - the file `project.trb`'s dependencies resolve into.
- [Packages](../language/modules-and-packages/packages.md) - `owner/name`, and the rules around publishing one.
- [Workspaces](../language/modules-and-packages/workspaces.md) - one root, several members, one lock file.

