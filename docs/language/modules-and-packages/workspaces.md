---
title: Workspaces
summary: A workspace is one root project.trb naming several member projects that check, build and test as a group, and resolve their dependencies into one shared project.lock.trb.
kind: reference
status: stable
order: 50
keywords:
  - workspace
  - member
  - project.lock.trb
source:
  - CONCEPT.md#workspaces
---

A project can consist of several projects. A [workspace](../../glossary.md#workspace) is the root that names them, so
`torb build`, `test` and `check` can work across all of them at once, in the order their dependencies require.

## Example

```trb
name = "acme/shop"
version = "1.4.0"

workspace {
  members "packages/*", "tools/importer"
}
```

## Syntax

```text
shop/
├ packages/
├─ core/            // name = "acme/shop-core"
├─ api/              // name = "acme/shop-api", dependencies { runtime "acme/shop-core" }
├ tools/
├─ importer/
├ project.trb        // the workspace
└ project.lock.trb   // one lock file for all of them
```

## Rules

1. **Every member is an ordinary project, with a `project.trb` of its own.** A workspace root only adds the
   `workspace { members ... }` section; nothing about a member's own file changes because it is part of one.

2. **A pattern is a directory, or `directory/*` for every project directly below it.** `members "packages/*"` picks
   up every package under `packages/`; `members "tools/importer"` names one project exactly.

3. **A dependency whose name is a member of the workspace is that member, from source**, and it needs no version
   inside the workspace: the resolver takes it as a package of one version, its manifest's.

4. **One `project.lock.trb`, at the root, holds the resolution every member shares**, so two members can never end up
   depending on different versions of the same package: `torb add`, `remove` and `update` resolve the requirements of
   every member together, and where two members want what no version satisfies, the explanation names both. See
   [project.lock.trb](../../tooling/project-lock-trb.md).

5. **A member inherits `version`, `authors`, the registries and the `profile` blocks of the root unless it sets its
   own.** Setting them again in a member's `project.trb` overrides the root for that member only; a member without a
   `profile "release"` block builds `release` the way the root's block says.

6. **A cycle between members is an error.** `torb build`, `test` and `check` at the root run on every member in the
   order their dependencies require, which a cycle makes impossible to compute. `torb build` at the root builds every
   program of every member, and `torb build <name>` the one program of that name.

7. **The root may have sources of its own, or be nothing but the list of members.** A workspace root is a project
   like any other; `workspace { ... }` does not require it to have an empty `src/`. A member is a nested package, so
   its files are never files of the root, even though they lie below it.

## What this is not

**A workspace is not a way to skip declaring a dependency.** A member still names every other member it uses in its
own `dependencies { runtime ... }` and imports it by its name; what the workspace changes is where that dependency's
source comes from, not whether it has to be named. A relative path into a sibling - `"../../core/src/cart"` - is an
error that says it leaves the package.

```trb
name = "acme/shop-api"

dependencies {
  runtime "acme/shop-core"
}
```

```trb skip a workspace of several projects cannot be built inside one snippet, so the real diagnostic for an unnamed member dependency cannot be produced here
name = "acme/shop-api"

// No `dependencies` block, but the source still imports `acme/shop-core`
```

## Related

- [project.lock.trb](../../tooling/project-lock-trb.md) - the lock file, and which commands read and write it.
- [Packages](packages.md) - what an ordinary project is, member or not.
- [use](use.md) - importing from a package, workspace member or not.
