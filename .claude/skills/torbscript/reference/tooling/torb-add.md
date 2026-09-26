---
title: torb add
summary: torb add writes a dependency into project.trb, resolves the workspace with it, writes project.lock.trb and installs what is new - or changes nothing and explains why there is no solution.
kind: tooling
status: stable
order: 102
keywords:
  - torb add
  - dependency
  - package manager
  - project.lock.trb
source:
  - compiler/src/cli/packages.trb
  - compiler/src/package/manifest-edit.trb
  - compiler/src/package/resolve.trb
---

`add` is how a dependency gets into a project: one line in the `dependencies { }` block of
[project.trb](project-trb.md), a resolution of the whole workspace with it, the new
[project.lock.trb](project-lock-trb.md), and every new package fetched and verified into the cache. If the
requirements have no solution, nothing is written and the reason is printed.

## Synopsis

```text
torb add <package>[@<requirement>]...   Add runtime dependencies to the project around the working directory
  --development                         A development dependency: tests and tools, never part of what dependents get
  --offline                             No network: indexes from the cache's copies, archives only if unpacked
  --project <directory>                 The project to add to, instead of the one around the working directory
```

## What it does

### The line it writes

`torb add acme/json` asks the registry for the newest release of `acme/json` that may be chosen - no pre-release, no
yanked release, none that needs a newer language than this toolchain - and writes `runtime "acme/json:^1.2.0"`. With a
requirement, `torb add acme/json@~1.2.0` writes that one instead. A workspace member is added without a version.

The edit is made over the syntax tree and touches one line: a requirement of the same package is replaced in place, a
new one goes at the end of the block, indented like the lines above it, and a file without a block gets one at its end.
Comments, blank lines and the file's line endings stay. **A block that computes anything is refused** rather than
rewritten, because only plain `runtime "..."` and `development "..."` lines can be edited without changing what the
file means.

### The resolution

Every member of the workspace, every `source ... path:` package and every registry package any of them reaches is
resolved at once by PubGrub, which answers the highest version every requirement allows and keeps the version the lock
already has for every other package where it still fits. The result is written to `project.lock.trb` and printed, one
line a package, with what each new one touches:

```console
$ torb add acme/json --project app
added acme/json to acme/app
  + acme/json 1.0.0
installed acme/json 1.0.0
1 package installed, 0 of them already in the cache
```

### When there is no solution

Resolution fails before anything is written, and the explanation is the derivation of the failure, one reason a line:

```console
$ torb add acme/json@^9.0.0
Because no version of acme/json matches ^9.0.0 and acme/app depends on acme/json ^9.0.0, version solving failed.
error: nothing was changed: the dependencies of project.trb have no solution
```

## Pitfalls

**The toolchain's packages are never dependencies.** `torb add std/fs` is refused: every `std/*` package comes with
`torb` and needs no line.

**A requirement is `^` by default.** `@1.2.3` means `^1.2.3`, as in Cargo; `@=1.2.3` is the one version.

## Examples

A requirement that a dependency of a dependency rules out is explained through it:

```console
$ torb add acme/json@=1.0.0 --project app
Because every version of acme/text depends on acme/json ^1.1.0 and acme/app depends on acme/json 1.0.0, every version of acme/text is forbidden.
So, because acme/app depends on acme/text ^1.0.0, version solving failed.
error: nothing was changed: the dependencies of project.trb have no solution
```

## Related

- [torb remove](torb-remove.md), [torb update](torb-update.md), [torb install](torb-install.md) - the other package
  commands.
- [project.lock.trb](project-lock-trb.md) - the file `add` writes.
- [Add a dependency](../how-to/add-a-dependency.md) - the steps, with the requirement grammar.

