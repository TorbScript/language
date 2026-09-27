---
title: Packages
summary: A package is a directory with a project.trb, named owner/name, whose file names say what it produces - src/lib.trb, src/main.trb, *.test.trb - and it can only be reached by a project that lists it as a dependency.
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
  - docs/design/PROJECT.md
---

Every `"owner/name"` after `from` names a [package](../../glossary.md#package): a directory with a `project.trb`,
whose library is its `src/lib.trb`. What it may be imported by is never a guess, because a project has to say so
first, and what it produces is never a setting, because the names of its files say it.

## Example

```trb
name = "acme/greeter"
version = "0.1.0"

dependencies {
  runtime "acme/greeting-words:^1.0.0"
  development "acme/mock-clock:^2.0.0"
}
```

## Syntax

```text
my-project/
├ src/
├─ main.trb        // The program: what torb run executes; may contain top-level code, never imported
├─ lib.trb          // The library: what other packages import, declarations only
├─ <path>.trb       // A module: "owner/name/<path>"
├ tools/
├─ migrate.trb      // A second program, where project.trb says program "migrate", entry: "tools/migrate.trb"
├ tests/
├─ *.test.trb       // A test, wherever it lies in the package
├ project.trb
└ project.lock.trb
```

## Rules

1. **A name is `owner/name`, and the owner is a verified namespace of a registry.** A project binds an owner to a
   registry once (`registry "acme", url: "https://packages.acme.test"`), so a public package can never take the
   place of a private one under the same bare name.

2. **The names of the files decide what a package produces.** `src/lib.trb` is its library, what `"owner/name"`
   imports, and it holds declarations only; `src/main.trb` is its program, named after the package; a file called
   `*.test.trb` is a test wherever it lies. Nothing in `project.trb` makes a file any of these. A second program is a
   `program` line of [project.trb](../../tooling/project-trb.md#programs), and a program's entry is never importable:
   `"owner/name/main"` and a relative path to an entry are errors at the `use`.

3. **A package's files are every `.trb` file below its directory**, except the ones inside a nested package - a
   directory below it with a `project.trb` of its own - and inside a hidden directory, `build`, `target` or
   `node_modules`. `tools/migrate.trb` is a file of the package, and it may import the package's modules with a
   relative path. A relative path never leaves the package: another package is imported by its name.

4. **A package can only be imported by a project whose `project.trb` lists it as a dependency**, `std/*` excepted -
   the standard library comes with the toolchain and needs no entry.

   ```trb check
   use Int from "std/prelude"

   print Int.tryFrom("42")
   ```

5. **`project.lock.trb` pins the whole graph: the exact version, content hash and registry of every dependency,
   direct or transitive.** `torb run`, `build` and `test` fail if it does not match `project.trb`; only `torb add`,
   `torb remove` and `torb update` write the graph, and `torb lock` and `torb publish` the evaluated settings beside
   it.

6. **Published versions are immutable.** A version can be withdrawn from new resolutions, never replaced or deleted,
   so a lock file always resolves to the same bytes it pinned.

7. **Resolution picks the highest version compatible with every requirement, and one version of a package per major
   version in a graph.** A requirement is written as published, `^1.2.3` by default, so patch and minor upgrades
   happen without editing `project.trb`.

8. **`project.trb` runs in a sandbox without IO, so installing a package never runs code.** There are no install
   scripts and no build scripts; reading a package's metadata is as safe as reading its source.

9. **Capabilities are visible from the imports alone.** There is no reflection, no `eval` and no dynamic import, so
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

```trb skip a second, custom package cannot be added to the workspace this gate type checks a snippet in, so the real message ("The package 'acme/other' is not a dependency of 'acme/app'") cannot be produced here
use Router from "acme/http/routing"
```

A relative path does not reach it either, even where the other package's files lie right beside this one's: a
relative path names a file of the same package.

```trb skip a snippet of this documentation is a package of its own with no neighbour to climb into; the real diagnostic, from inside std/geometry, is: '../../linear/src/vector2' leaves 'std/geometry'
use Vector2 from "../../linear/src/vector2"
```

**A `project.trb` is not a list of what the package produces.** There is no setting that makes a file the program,
the library or a test; the names of the files say it, and the manifest adds only a second program.

## Related

- [use](use.md) - every form of importing a name out of a package.
- [Workspaces](workspaces.md) - several packages sharing one root and one lock file.
- [The prelude](the-prelude.md) - the one package whose names need no `use` at all.
- [project.trb](../../tooling/project-trb.md) - the manifest, its `program` lines and its profiles.
