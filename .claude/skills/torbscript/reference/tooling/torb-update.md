---
title: torb update
summary: torb update resolves every package of the workspace, or only the named ones, to the highest version project.trb allows, writes project.lock.trb, and refuses an update that gains a capability unless --accept-capabilities says it is wanted.
kind: tooling
status: stable
order: 104
keywords:
  - torb update
  - project.lock.trb
  - capabilities
  - package manager
source:
  - compiler/src/cli/packages.trb
  - compiler/src/package/resolve.trb
---

`update` is the one command that moves locked versions forward. Without arguments every package of
[project.lock.trb](project-lock-trb.md) may move; with names, only those move and everything else stays where the
lock has it. It is also how a lock is written the first time for a `project.trb` whose dependencies were written by
hand.

## Synopsis

```text
torb update [<package>...]   Resolve again: every package, or the named ones
  --accept-capabilities      Take versions that touch more than the locked ones did
  --offline                  No network: indexes from the cache's copies
  --project <directory>      The project whose workspace to update
```

## What it does

The changes are printed one line a package, `~` for a new version, `+` and `-` for a package that joins or leaves the
graph, and every package that is not in the cache is installed:

```console
$ torb update --project app
  ~ acme/json 1.0.0 -> 1.1.0
installed acme/json 1.1.0
1 package installed, 0 of them already in the cache
```

### An update that gains a capability

Every release in the index says what it touches by its own imports (docs/design/RELEASE.md section 7.8). **An update
that takes a version touching more than the locked one did is refused**, and says what it would gain, until
`--accept-capabilities` says it is wanted:

```console
$ torb update --project app
  acme/json 1.1.0 -> 1.2.0 gains files
error: the update gains capabilities: nothing was changed. `--accept-capabilities` takes them
```

## Pitfalls

**A requirement is a ceiling `update` never crosses.** `^1.2.0` never becomes 2.0.0; a new major is a new requirement,
written with [torb add](torb-add.md).

## Examples

The gain taken on purpose, for one package, with everything else left where the lock has it:

```console
$ torb update acme/json --accept-capabilities --project app
  ~ acme/json 1.1.0 -> 1.2.0, touches files
installed acme/json 1.2.0
2 packages installed, 1 of them already in the cache
```

## Related

- [torb add](torb-add.md) - a new dependency, or a new requirement for one.
- [torb install](torb-install.md) - the packages of a lock, without resolving anything.
- [project.lock.trb](project-lock-trb.md) - what `update` writes.

