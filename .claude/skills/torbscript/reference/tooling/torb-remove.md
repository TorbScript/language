---
title: torb remove
summary: torb remove takes a dependency out of project.trb, resolves the workspace again, and drops from project.lock.trb every package nothing needs any more.
kind: tooling
status: stable
order: 103
keywords:
  - torb remove
  - dependency
  - package manager
source:
  - compiler/src/cli/packages.trb
  - compiler/src/package/manifest-edit.trb
---

`remove` is the inverse of [torb add](torb-add.md): the requirement goes out of the `dependencies { }` block of
[project.trb](project-trb.md), the workspace is resolved again with the versions the lock has, and
[project.lock.trb](project-lock-trb.md) loses every package nothing needs any more.

## Synopsis

```text
torb remove <package>...     Remove dependencies from the project around the working directory
  --project <directory>      The project to remove from
```

## What it does

The literal of the package is taken out of its line, or the whole line where it was the only one; everything else in
the file stays as it was. A package the project does not depend on is an error that names it, and nothing is written.
The packages that remain keep their locked versions, and what left the lock is printed:

```console
$ torb remove acme/text --project app
removed acme/text from acme/app
  - acme/text 1.0.0
```

The cache keeps the unpacked files: another project may use them, and the cache is content-addressed, so a file of it
is never wrong.

## Pitfalls

**A `use` of the removed package is now an error.** `torb check` says `The package ... is not a dependency` at every
import that is left.

## Examples

```console
$ torb remove acme/text --project app
removed acme/text from acme/app
  - acme/text 1.0.0
$ torb remove acme/text --project app
error: `acme/text` is not a dependency of acme/app
```

## Related

- [torb add](torb-add.md) - the command it undoes.
- [project.lock.trb](project-lock-trb.md) - what it rewrites.

