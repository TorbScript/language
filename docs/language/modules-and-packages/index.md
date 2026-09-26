---
title: Modules and packages
summary: How a file brings in names from elsewhere, what a package is, and the two rules - visibility and top-level code - that decide what a module may contain.
kind: index
status: stable
order: 80
---

`use` and every form after `from`, `public` at the top of a file, the prelude, packages and workspaces, what top-level
code may do, and why an import cycle is not an error.

## What belongs here

What does not belong here: the members of a `type` or a `trait`, whose own visibility is in `language/types/` and
`language/traits/`. Every page in this folder is a reference page: an example first, then the syntax, then numbered
rules, then what the construct is not.

<!-- torb:index:begin -->

## Pages

- **[use](use.md)** - use brings names into scope from a package or a file. Everything after from names a module; a path brings in a case of a type or a member another package attaches to it, and a use without names is an error.
- **[Visibility](visibility.md)** - A top-level declaration is private to its file unless marked public, and a public declaration may not expose a type that is private to its own file.
- **[The prelude](the-prelude.md)** - The prelude is the package whose public names are in scope in every file without an import, and it holds only the pure part of the standard library.
- **[Packages](packages.md)** - A package is a directory with a project.trb and a src/, named owner/name, and it can only be reached by a project that lists it as a dependency.
- **[Workspaces](workspaces.md)** - A workspace is one root project.trb naming several member projects that check, build and test as a group, and resolve their dependencies into one shared project.lock.trb.
- **[Top-level code](top-level-code.md)** - A statement outside every declaration is only allowed in an entry file, a script or a test file, and a top-level const of a module has to be known at compile time.
- **[Cyclic imports](cyclic-imports.md)** - Two modules may import each other, because nothing runs when a module is imported and its exports are computed to a fixpoint, but the same cycle between top-level statements is an error.

<!-- torb:index:end -->
