---
title: torb publish
summary: torb publish builds and checks a package's archive as a registry receives it, prints its tree hash and capabilities, and writes it into a directory registry or uploads it with a token or through trusted publishing.
kind: tooling
status: stable
order: 106
keywords:
  - torb publish
  - registry
  - archive
  - capabilities
  - dry run
  - TORB_TOKEN
  - trusted publishing
source:
  - compiler/src/cli/packages.trb
  - compiler/src/package/archive.trb
  - compiler/src/package/files.trb
  - compiler/src/package/publish.trb
  - compiler/src/package/upload.trb
  - tools/registry
---

`publish` turns a package into the archive a registry holds (docs/design/RELEASE.md section 7.2) and checks everything
a registry would refuse before anything leaves the machine. `--dry-run` stops once the archive is built. Without it,
the release is written into a `file:` registry, or sent to the write service of a registry on the network
(`tools/registry`, the service behind packages.torb.dev), which checks it once more and refuses in words this command
prints.

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
   `project.lock.trb`. A version that is published already is refused: a published version is never replaced, and a
   `file:` registry that signs its index - its `index/config.trb` lists keys - is refused too, because this command
   cannot sign the record. For a registry on the network (`https://...`), the archive is sent with
   `PUT <registry>/api/1/packages/<owner>/<name>/<version>` and its tree hash in `Torb-Tree-Hash`, through `curl`
   (else `wget`, or the one `TORB_FETCH` names). The registry unpacks it, reads the settings of its lock, refuses what
   this command refuses, and writes the archive and the index entry, signed with its key; its answer is printed, and
   the `settings` block goes into the workspace's lock as for `file:`.

### The token

A registry on the network needs to know who publishes:

- **`TORB_TOKEN`**: a token of the registry whose actions include `publish` and whose scopes include the package. It
  is sent in a header that `curl` reads from a file, never on a command line.
- **Trusted publishing**, where `TORB_TOKEN` is not set and the command runs in a CI job that may ask for an OpenID
  Connect token - a GitHub Actions job with `permissions: id-token: write`, or a Forgejo Actions job with
  `enable-openid-connect: true` on a forge the registry trusts (git.torb.dev): the job's token is requested with the
  registry's host as its audience (`TORB_AUDIENCE` names another) and exchanged at `/api/1/trusted-publishing` for a
  token that lives fifteen minutes and publishes this one package. The package's owners configure which repository
  (`acme/http` on GitHub, `git.torb.dev/acme/http` on the forge), workflow file, ref and - on GitHub - environment may
  do that, and no secret is stored anywhere.

Without either, nothing is sent:

```console
$ torb publish
error: publishing to https://packages.torb.dev needs a token: set TORB_TOKEN to a token of the registry that may publish acme/json, or publish from a CI job that may ask for an identity token (GitHub Actions: `permissions: id-token: write`, Forgejo Actions: `enable-openid-connect: true`) and a trusted publisher the owners of acme/json configured
```

## Pitfalls

**The registry trusts nothing the client computed.** It recomputes the tree hash, reads the settings from the archive's
own lock - never by evaluating a `project.trb` - and derives the capabilities again, so a refusal on the server is the
refusal `--dry-run` shows locally. It does not *check* the package: its toolchain may be another patch release.

**The capability summary is what the package's own imports reach**: `files` for `std/fs`, `network` for `std/http`,
`std/network` and `std/tls`, `processes`, `environment`, `entropy` for `Entropy`, `the operating system` for the rest
of `std/os`, `clock` for `Clock` and `sleep`, `scripts`, and `foreign functions` for a `foreign` block. What its
dependencies reach is theirs.

**Every imported name decides, not its package.** A capability is reading something, and a type is no reading:
`use Timestamp from "std/time"` puts a point in time into a signature and reaches nothing, while
`use Clock from "std/time"` reaches `clock`. The values a capability package answers in are pure - `Duration`,
`Instant` and `Timestamp` of `std/time`; `OsError`, `SystemVersion`, `EnvironmentVariables` and the target constants
of `std/os` - and a name `std/os` re-exports reaches what its own module does, so `Entropy` is `entropy` from `std/os`
as from `std/os/entropy`. A `use * as time from "std/time"` can reach every name, so it reaches the package's
capability.

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
