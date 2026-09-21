---
title: Packages
summary: A package is a directory with a project.trb and a src/, named owner/name, and it can only be reached by a project that lists it as a dependency.
kind: reference
status: stable
order: 40
keywords:
  - package
  - owner
  - dependency
  - project.trb
  - registry
source:
  - CONCEPT.md#packages-and-the-supply-chain
  - CONCEPT.md#project-layout
---

Every `"owner/name"` after `from` names a [package](../../glossary.md#package): a directory with a `project.trb` and
a `src/`. What it may be imported by is never a guess, because a project has to say so first.

## Example

```trb
name "acme/greeter"
version "0.1.0"

dependencies {
  runtime "acme/greeting-words:^1.0.0"
  development "acme/mock-clock:^2.0.0"
}
```

## Syntax

```text
my-project/
├ src/
├─ main.trb        // What torb run executes; may contain top-level code
├─ lib.trb          // What other packages import: the public surface
├ tests/
├─ *.test.trb
├ project.trb
└ project.lock.trb
```

## Rules

1. **A name is `owner/name`, and the owner is a verified namespace of a registry.** A project binds an owner to a
   registry once (`registry "acme", url: "https://packages.acme.test"`), so a public package can never take the
   place of a private one under the same bare name.

2. **`src/lib.trb` is what other packages import, `src/main.trb` is what `torb run` executes.** A dependency is
   always reached through `src/lib.trb`; nothing outside a package can name its `src/main.trb`.

3. **A package can only be imported by a project whose `project.trb` lists it as a dependency**, `std/*` excepted -
   the standard library comes with the toolchain and needs no entry.

   ```trb check
   use Int from "std/prelude"

   print Int.tryFrom("42")
   ```

4. **`project.lock.trb` pins the whole graph: the exact version, content hash and registry of every dependency,
   direct or transitive.** `torb run`, `build` and `test` fail if it does not match `project.trb`; only `torb add`,
   `torb remove` and `torb update` write it.

5. **Published versions are immutable.** A version can be withdrawn from new resolutions, never replaced or deleted,
   so a lock file always resolves to the same bytes it pinned.

6. **Resolution picks the highest version compatible with every requirement, and one version of a package per major
   version in a graph.** A requirement is written as published, `^1.2.3` by default, so patch and minor upgrades
   happen without editing `project.trb`.

7. **`project.trb` runs in a sandbox without IO, so installing a package never runs code.** There are no install
   scripts and no build scripts; reading a package's metadata is as safe as reading its source.

8. **Capabilities are visible from the imports alone.** There is no reflection, no `eval` and no dynamic import, so
   what a package can touch - the file system, the network, processes, the environment, foreign functions - is
   exactly what its `use` lines and those of everything it depends on say. `torb add` shows it, and gaining one on an
   update needs confirmation.

## What this is not

**A package is not a folder you can reach because it happens to be on disk.** Only a name `project.trb` lists as a
dependency resolves; `std/*` is the one family that needs no entry.

```trb check
use File from "std/fs"

print File.exists("project.trb")
```

```trb skip a second, custom package cannot be added to the workspace this gate type checks a snippet in, so the real message ("The package `acme/other` is not a dependency of `acme/app`") cannot be produced here
use Router from "acme/http/routing"
```

## Related

- [use](use.md) - every form of importing a name out of a package.
- [Workspaces](workspaces.md) - several packages sharing one root and one lock file.
- [The prelude](the-prelude.md) - the one package whose names need no `use` at all.
