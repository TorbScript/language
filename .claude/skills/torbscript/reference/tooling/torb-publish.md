---
title: torb publish
summary: torb publish builds and checks the archive of a package exactly as a registry receives it, prints its tree hash and capabilities, and writes it into a registry that is a directory; publishing over the network comes later.
kind: tooling
status: stable
order: 106
keywords:
  - torb publish
  - registry
  - archive
  - capabilities
  - dry run
source:
  - compiler/src/cli/packages.trb
  - compiler/src/package/archive.trb
  - compiler/src/package/files.trb
---

`publish` turns a package into the archive a registry holds (docs/design/RELEASE.md section 7.2) and checks everything
a registry would refuse before anything leaves the machine. `--dry-run` stops once the archive is built. Without it,
the release is written into a `file:` registry; **publishing to a registry on the network is not built yet** - the
registry's write service is a later round - and says so.

## Synopsis

```text
torb publish                 Build, check and publish the package around the working directory
  --dry-run                  Build and check the archive, and upload nothing
  --project <directory>      The package to publish
```

## What it does

In order, stopping at the first step that refuses:

1. **The settings.** `project.trb` is evaluated in the sandbox where the VM can, and read statically otherwise.
   `name` has to be `owner/name` and not `std/...`, `version` a version, `description` and `license` set, and no runtime
   dependency may come from a `path:` source, which nobody who installs the package has.
2. **The check.** The package is checked like `torb check` of its directory; a package that does not check is refused.
3. **The files.** Every `.trb` below the package directory that is not inside a nested package, skipping hidden
   directories, `build`, `target` and `node_modules`, plus `README.md` and the license files. There is no include or
   exclude list. The archive's `project.lock.trb` is the package's `settings` block and the workspace's `graph`.
4. **The archive.** A deterministic `tar` inside gzip, at most 10 000 files and 10 MiB, and its tree hash.
5. **The summary**, and with `--dry-run` nothing more:

```console
$ torb publish --dry-run --project sources/json-1.2.0
acme/json 1.2.0
  checked:       2 files, no problems
  capabilities:  files (src/lib.trb)
  files:         3, 1180 bytes, archive 842 bytes
  hash:          sha256:7689cf8f7a49b8349e13f3cccb395192ecb20b839f1e277c366e20560206fe33
nothing was uploaded (--dry-run)
```

6. **The upload.** For a `file:` registry, the archive goes to `archives/<owner>/<name>/<version>.tar.gz` and the release
   is appended to `index/<owner>/<name>.trb`; the `settings` block of the package is written into the workspace's
   `project.lock.trb`. A version that is published already is refused: a published version is never replaced.

## Pitfalls

**The capability summary is what the package's own imports reach**: `files` for `std/fs`, `network` for `std/http`,
`std/network` and `std/tls`, `processes`, `environment`, `the operating system`, `clock`, `scripts`, and `foreign
functions` for a `foreign` block. What its dependencies reach is theirs.

## Examples

A package that is published already is refused after the archive is built, because a published version is never
replaced:

```console
$ torb publish --project sources/json-1.0.0
acme/json 1.0.0
  checked:       2 files, no problems
  capabilities:  none - the package imports nothing that reaches the machine
  files:         3, 698 bytes, archive 569 bytes
  hash:          sha256:76ebb106f4c8fad658f40ddec9a4f2edfe7724f89a5951283121724e454bb37e
error: acme/json 1.0.0 is published already, and a published version is never replaced
```

## Related

- [project.lock.trb](project-lock-trb.md) - the `settings` block `publish` writes.
- [project.trb](project-trb.md) - `description`, `license` and the other settings it requires.
- [torb add](torb-add.md) - the other side: a package from a registry into a project.

