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

### What the toolchain reads today

`torb check`, `build` and `test` are what read `project.trb`, and today they read it **statically**, from its syntax
tree, rather than by evaluating it against `Project` the way a receiver script normally runs. The evaluation exists -
`torb manifest` runs the file in the sandboxed VM and prints the settings it configured - but no command reads its
result yet (see [The sandbox](../language/configuration/the-sandbox.md)). Only the following settings are consulted,
each as a literal string argument:

| Setting | What it decides |
|---------|------------------|
| `name` | The package's `owner/name`, and what an importing package's `dependencies` has to name |
| `version` | Recorded, not yet checked against anything |
| `prelude` | The package whose public names are in scope in every file (default `std/prelude`) |
| `dependencies { runtime "..." }` | What a package may import at all |
| `dependencies { development "..." }` | The same, for a project's own tests and tools |
| `workspace { members "..." }` | Which directories are the projects of a workspace |
| `build { input = "..." }` | The entry file [`torb build`](torb-build.md) compiles when a directory is given |
| `test { input = "..." }` | The directory [`torb test`](torb-test.md) defaults to (`test` itself still has to be told the path) |

`authors`, `registry`, `build { target, output }` and `test { coverageThreshold }` are part of `Project`'s vocabulary
and type check, because the whole file is also checked as an ordinary program against `Project` - but no command
reads them yet. A computed setting, such as `build { output = "build/{target}/{binary}" }`
(see [`CONCEPT.md`](../../CONCEPT.md#projecttrb)), type checks the same way and is silently not read either: only a
literal string argument with no `{...}` in it is.

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
