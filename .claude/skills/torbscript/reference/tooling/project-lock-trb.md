---
title: project.lock.trb
summary: The locked manifest - the exact version, tree hash and registry of every package a workspace depends on, and the evaluated settings of a published package - written deterministically and read without running anything.
kind: tooling
status: stable
order: 100
keywords:
  - project.lock.trb
  - lock file
  - dependency resolution
  - tree hash
  - content hash
source:
  - compiler/src/package/lock.trb
  - compiler/src/project/workspace.trb
  - docs/design/PROJECT.md#the-locked-manifest-installing-still-never-runs-code
---

A [workspace](../language/modules-and-packages/workspaces.md) has one `project.lock.trb`, at its root, next to the root
[`project.trb`](project-trb.md). It exists so that two checkouts of the same commit build every dependency from the
exact same files, without asking a registry again. It is a receiver script of literals in the vocabulary of
`project.trb`, and **the toolchain reads it statically and never evaluates it**: installing never runs code.

## Synopsis

```trb fragment
// project.lock.trb - written by `torb add`, `torb remove`, `torb update` and `torb publish`. Do not edit.

language = "0.1.0"

settings "acme/app" {
  version = "0.1.0"
  license = "MIT"
}

graph {
  package "acme/json", version: "1.2.0", registry: "https://packages.torb.dev", hash: "sha256:9f2c..." {
    capabilities "files"
  }
  package "acme/local", path: "../local"
}
```

## What it does

### What each part pins

| Part | What it says |
|------|--------------|
| `language` | The `language` of the root project, where it names one |
| `settings "<package>" { }` | The evaluated settings of one package of the workspace, printed back as literals: what a consumer of the published package reads instead of its `project.trb`. Written by [`torb publish`](torb-publish.md) only |
| `graph { }` | One `package` line per package the workspace depends on, direct or transitive |
| `package "<name>", version:, registry:, hash:` | A registry package: the exact version, the registry that answered (the URL the owner is bound to, `https://packages.torb.dev` for an unbound owner), and the **tree hash** of its archive |
| `package "<name>", path:` | A `source ... path:` package, listed rather than pinned: it is source the project owns, and a hash of it would change on every edit |
| `capabilities "..."` | What the package touches by its own imports (`files`, `network`, `processes`, `environment`, ...), as its release in the index says |

**The tree hash** is `sha256:` over a listing of every file of the package - the SHA-256 of its bytes and its path,
one line each, sorted by path. It is not the hash of the archive's bytes, so a mirror may recompress an archive without
changing a lock. It is checked every time a package is installed, before anything is written into the cache.

### Who writes it, who reads it

| Command | The lock |
|---------|----------|
| [`torb add`](torb-add.md), [`torb remove`](torb-remove.md), [`torb update`](torb-update.md) | Resolve `project.trb` and write `graph` |
| [`torb publish`](torb-publish.md) | Writes the `settings` block of the package it publishes |
| [`torb install`](torb-install.md) | Reads it, fetches what is not in the cache yet, verifies every tree hash; never writes it |
| `torb check`, `build`, `run`, `test` | Read `graph` and never write it; refuse where it does not match `project.trb` |

The build takes a locked registry package from the package manager's cache, where [`torb install`](torb-install.md)
unpacked it, and a `path:` package from its directory - each is then a package of the workspace like a member, so
`use X from "acme/json"` resolves to it. **It refuses, with the command that fixes it**, where a member depends on a
package the lock does not pin, pins at a version the requirement does not allow, or pins from another registry than
the one the owner is bound to, and where a locked package is not installed:

```console
$ torb check app
error: `acme/app` depends on `acme/json:^2.0.0`, and project.lock.trb pins acme/json 1.2.0: run `torb update acme/json`
```

### Written the same way everywhere

The same lock is the same bytes on every machine, so it can be reviewed in a diff and compared byte for byte: the parts
in a fixed order, the `settings` blocks sorted by package and the lines inside one in the order of the vocabulary, the
packages of `graph` sorted by name, nothing about the machine - no time, no absolute path, no tool build, a `path:`
relative to the lock - hashes as `sha256:` and lowercase hexadecimal, UTF-8, LF, and exactly one trailing newline.

## Pitfalls

**Do not edit it by hand.** A hand-edited version or hash is refused by the next install; `torb update` writes the
file again from `project.trb`.

**A `path:` package is not reproducible.** The lock names its directory and nothing about its content, which is the
honest half of what a lock can promise about a directory somebody is editing - and why a package with a `path:`
dependency cannot be published.

## Examples

`tools/packages.sh` runs the whole cycle against a `file:` registry and compares every file it writes with
`tests/packages/transcript.expected`.

## Related

- [project.trb](project-trb.md) - the manifest whose dependencies this file resolves.
- [torb add](torb-add.md), [torb update](torb-update.md), [torb install](torb-install.md) - the commands that write
  and read it.
- [Workspaces](../language/modules-and-packages/workspaces.md) - the one lock file several members share.
- [The torb command](the-torb-command.md) - every subcommand.

