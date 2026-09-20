---
title: project.lock.trb
summary: The file that is specified to pin the exact version, content hash and registry of every dependency, direct or transitive - no command reads or writes it yet.
kind: tooling
status: planned
order: 100
keywords:
  - project.lock.trb
  - lock file
  - dependency resolution
  - content hash
source:
  - CONCEPT.md#packages-and-the-supply-chain
  - CONCEPT.md#projecttrb
---

> **Planned.** This feature is designed but not implemented. Nothing on this page works today.

A [workspace](../language/modules-and-packages/workspaces.md) has one `project.lock.trb`, at its root, next to the
root [`project.trb`](project-trb.md). It exists so that two checkouts of the same commit resolve every dependency to
the exact same package, without asking a registry again.

## Synopsis

```text
project.lock.trb   Written by `torb add`, `torb remove` and `torb update` (none of which exist yet)
```

## What it does

### What is specified

`CONCEPT.md` names three facts about the file. It pins the exact version, content hash and registry of every
package the workspace depends on, direct or transitive, so installing never has to guess. `torb run`, `build` and
`test` read it and never write it, and refuse when it does not match `project.trb` - a dependency that was added to
`project.trb` and not yet resolved is an error, not a silent re-resolution. Only `torb add`, `torb remove` and
`torb update` write it, because resolving a version is a decision, not a side effect of running a program.

`compiler/src/semantics/checker/receiver.trb` quotes `CONCEPT.md`'s own description of the mechanism:
"`project.trb` and `project.lock.trb` are exactly this mechanism with the receiver `Project`" - the same
[receiver script](../language/configuration/receiver-scripts.md) kind, checked against the same
[`Project`](../standard-library/project.md).

### What exists today

Nothing reads or writes `project.lock.trb`. A file by that name next to a `project.trb` is not treated as a receiver
script the way `project.trb` itself is - it is invisible to `torb check` entirely, not even read as an ordinary
module:

```console
$ cd bootstrap
$ cargo run --release -q -- run ../compiler check --statistics ../examples/tour
../examples/tour/project.trb: 6 typed, 0 deferred
14 files, no problems
```

Adding a `project.lock.trb` next to `examples/tour/project.trb` and running the same command reports the same 14
files: the new file is neither counted nor checked. `torb add`, `torb remove` and `torb update` - the only commands
`CONCEPT.md` says may write it - do not exist either; see
[The torb command](the-torb-command.md#what-is-still-planned).

### What is not decided yet

The exact shape of the file - which settings it carries, and whether they are the same names as `project.trb`'s or a
vocabulary of their own for a version, a hash and a registry per package - is an open question of `CONCEPT.md` itself,
not only an implementation gap: no registry protocol exists to resolve against, so the file's fields cannot be pinned
down before that does. This page will show the shape once `CONCEPT.md` fixes it.

## Examples

None: no command produces or consumes this file today, so there is nothing to run.

## Related

- [project.trb](project-trb.md) - the manifest whose dependencies this file resolves.
- [Packages](../language/modules-and-packages/packages.md) - `owner/name`, and the supply-chain rules the lock file
  is part of.
- [Workspaces](../language/modules-and-packages/workspaces.md) - the one lock file several members share.
- [The torb command](the-torb-command.md) - `torb add`, `remove` and `update`, still planned.
