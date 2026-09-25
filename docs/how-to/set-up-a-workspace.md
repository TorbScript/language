---
title: Set up a workspace
summary: Name the member directories in the root project.trb, give each one its own project.trb, and depend on a sibling by name alone - the workspace resolves it from source.
kind: how-to
status: stable
order: 80
keywords:
  - workspace
  - members
  - project.lock.trb
  - monorepo
source:
  - CONCEPT.md#workspaces
---

A workspace is a root `project.trb` that names several member projects, sharing one dependency resolution between
them. It is also the one way, until a registry exists, to depend on a package that is not `std/*`: the member has to
be there in source, and the workspace is what makes that legal.

## Steps

1. **Lay out one directory per member, each with its own `project.trb`.** A member is an ordinary project; nothing
   about its own file changes because it will be listed by a root.

   ```text
   shop/
   ├ packages/
   ├─ core/            project.trb: name = "acme/shop-core"
   ├─ api/              project.trb: name = "acme/shop-api"
   ├ tools/
   ├─ importer/
   ├ project.trb
   └ project.lock.trb
   ```

2. **Name the members from the root, with a directory or a `directory/*` pattern.** `members "packages/*"` picks up
   every project directly below `packages/`; `members "tools/importer"` names one project exactly.

   ```trb fragment
   workspace {
     members "packages/*", "tools/importer"
   }
   ```

3. **Depend on a sibling the same way as any other package**, by name in `dependencies { }`. Because the name matches
   a member, the workspace resolves it from that member's source instead of anywhere else, and no version is needed.

   ```trb fragment
   dependencies {
     runtime "acme/shop-core"
   }
   ```

4. **Run `check`, `build` and `test` at the root to work across every member at once**, in the order their
   dependencies require. Point the same commands at one member's directory to work on it alone.

5. **Commit one `project.lock.trb`, at the root.** Every member shares that resolution, so two members can never end
   up depending on different versions of the same package.

## Pitfalls

- **A member still has to declare the sibling it uses.** Being in the same workspace changes where a dependency's
  source comes from, not whether `dependencies { }` has to name it - a member that imports another without declaring
  it gets the same diagnostic an unrelated project would.
- **A cycle between members is an error.** The root works through members in the order their dependencies require,
  which a cycle between two members makes impossible to compute; a cycle between ordinary modules of one package is
  fine, because nothing runs when a module is only imported.
- **`version`, `authors` and the registries are inherited from the root**, not copied. A member that sets its own
  overrides the root for itself only, and one that sets nothing changes when the root does.
- **The root does not need an empty `src/`.** `workspace { members ... }` can stand next to a root that also has its
  own sources; the two are independent.

## Full example

```trb
name = "acme/shop"
version = "1.4.0"

workspace {
  members "packages/*", "tools/importer"
}
```

The `project.trb` of one member, depending on a sibling from source:

```trb
name = "acme/shop-api"
version = "1.4.0"

dependencies {
  runtime "acme/shop-core"
}
```

## Related

- [Workspaces](../language/modules-and-packages/workspaces.md) - the member, root and lock-file rules in full.
- [Add a dependency](add-a-dependency.md) - declaring what a project or a member depends on.
- [Packages](../language/modules-and-packages/packages.md) - what an ordinary project is, member or not.
