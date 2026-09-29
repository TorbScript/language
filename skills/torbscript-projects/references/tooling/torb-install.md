---
title: torb install
summary: torb install fetches every package project.lock.trb pins that is not in the cache yet, checks its tree hash before a file is written, and never changes the lock.
kind: tooling
status: stable
order: 105
keywords:
  - torb install
  - cache
  - offline
  - tree hash
source:
  - compiler/src/cli/packages.trb
  - compiler/src/package/fetch.trb
  - compiler/src/package/store.trb
---

`install` is what a fresh checkout runs before its first build: it reads [project.lock.trb](project-lock-trb.md),
fetches every registry package it pins that is not unpacked in the cache yet, checks each archive against the tree
hash the lock pins, and unpacks it. It resolves nothing and **never writes the lock**; installing never runs code.

## Synopsis

```text
torb install                 The packages of the lock of the workspace around the working directory
  --offline                  No network: fail naming the first package that would have to be fetched
  --project <directory>      The project whose workspace to install
```

## What it does

### The cache

The cache is `$TORB_CACHE`, or `~/.torb/cache` on Linux, macOS and FreeBSD and `%LOCALAPPDATA%\torb\cache` on Windows.
It is content-addressed and shared by every project of the machine:

```text
<cache>/packages/<hex of the tree hash>/    one package, unpacked, with a .complete written last
<cache>/index/<registry>/<owner>/<name>.trb the last copy of an index file from a registry on the network
```

### Where a package is fetched from

From the registry the lock names, as the project binds the owner: a registry on the network over HTTPS, or a `file:`
registry, a directory of the same files. The network is `curl`, else `wget`; `TORB_FETCH=curl` or `wget` chooses
one.

### What is refused

An archive whose tree hash is not the pinned one, a path that is absolute or climbs out of the package, a link, and an
archive of more than 10 MiB or of more than 64 MiB unpacked - each before anything is written:

```console
$ torb install --project app
error: acme/json 1.2.0: the tree hash is sha256:3f0a..., and sha256:9d41... was pinned: the archive is not the one that was published
```

A lock that does not match `project.trb` is refused with the command that fixes it; `install` does not resolve.

## Examples

A fresh checkout, with an empty cache:

```console
$ torb install --project app
installed acme/json 1.2.0
1 package installed, 0 of them already in the cache
$ torb check app
2 files, no problems
```

## Related

- [project.lock.trb](project-lock-trb.md) - what `install` reads.
- [torb update](torb-update.md) - the command that writes a lock where there is none.

