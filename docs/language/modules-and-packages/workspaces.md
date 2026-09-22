---
title: Workspaces
summary: A workspace is one root project.trb naming several member projects that check, build and test as a group; a shared project.lock.trb is designed for them but not read or written yet.
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
name "acme/shop"
version "1.4.0"

workspace {
  members "packages/*", "tools/importer"
}
```

## Syntax

```text
shop/
├ packages/
├─ core/            // name "acme/shop-core"
├─ api/              // name "acme/shop-api", dependencies { runtime "acme/shop-core" }
├ tools/
├─ importer/
├ project.trb        // the workspace
└ project.lock.trb   // one lock file for all of them (planned - nothing reads or writes it yet)
```

## Rules

1. **Every member is an ordinary project, with a `project.trb` of its own.** A workspace root only adds the
   `workspace { members ... }` section; nothing about a member's own file changes because it is part of one.

2. **A pattern is a directory, or `directory/*` for every project directly below it.** `members "packages/*"` picks
   up every package under `packages/`; `members "tools/importer"` names one project exactly.

3. **A dependency whose name is a member of the workspace is that member, from source**, and it needs no version
   inside the workspace. Publishing a member is designed to write the current versions of its siblings into what is
   published, but no `torb publish` exists yet to do it.

4. **One `project.lock.trb`, at the root, is designed so every member shares the same resolution** and two members
   can never end up depending on different versions of the same package - but this is planned, not built: nothing
   reads or writes `project.lock.trb` today. See [project.lock.trb](../../tooling/project-lock-trb.md).

5. **A member inherits `version`, `authors` and the registries of the root unless it sets its own.** Setting them
   again in a member's `project.trb` overrides the root for that member only.

6. **A cycle between members is an error.** `torb build`, `test` and `check` at the root run on every member in the
   order their dependencies require, which a cycle makes impossible to compute.

7. **The root may have sources of its own, or be nothing but the list of members.** A workspace root is a project
   like any other; `workspace { ... }` does not require it to have an empty `src/`.

## What this is not

**A workspace is not a way to skip declaring a dependency.** A member still names every other member it uses in its
own `dependencies { runtime ... }`; what the workspace changes is where that dependency's source comes from, not
whether it has to be named.

```trb
name "acme/shop-api"

dependencies {
  runtime "acme/shop-core"
}
```

```trb skip a workspace of several projects cannot be built inside one snippet, so the real diagnostic for an unnamed member dependency cannot be produced here
name "acme/shop-api"

// No `dependencies` block, but the source still imports `acme/shop-core`
```

## Related

- [project.lock.trb](../../tooling/project-lock-trb.md) - the planned lock file, and what still reads or writes
  nothing.
- [Packages](packages.md) - what an ordinary project is, member or not.
- [use](use.md) - importing from a package, workspace member or not.
