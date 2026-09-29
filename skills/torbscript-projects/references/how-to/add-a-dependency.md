---
title: Add a dependency
summary: Declare the package in project.trb before importing from it, tell a runtime dependency from a development one, and read what the imports of everything you depend on say it can reach.
kind: how-to
status: stable
order: 70
keywords:
  - dependencies
  - project.trb
  - project.lock.trb
  - runtime
  - development
source:
  - CONCEPT.md#packages-and-the-supply-chain
---

A project may only import a package its own `project.trb` lists first - there is no dependency that becomes reachable
by being imported. `dependencies { }` is where that list lives, and it is the one place capabilities are visible
from, before a single line of the dependency's code runs.

## Steps

1. **Add the package under `runtime` for code your project ships, `development` for tests and tools** -
   `torb add acme/http` writes the line with the newest release as `^` and locks and installs it, and
   `torb add --development acme/mock-server` writes a development line. A `development` dependency is never part of
   what somebody who depends on your project gets.

   ```trb fragment
   dependencies {
     runtime "acme/http:^1.2.3"
     development "acme/mock-server:^3.4.5"
   }
   ```

2. **Write the requirement the way it is published**, `owner/name:^major.minor.patch`. `^` is the default and the
   usual choice: it resolves to the highest version whose major component matches, so a patch or minor upgrade needs
   no edit here.

3. **Import from it by package name**, `"owner/name"` for its `src/lib.trb`, `"owner/name/path"` for `src/path.trb`
   of it. Nothing needs re-declaring at the `use` line - the dependency list is the only place the version is
   written. A relative path never reaches another package, even one whose files lie beside yours: `"../other/src/x"`
   is an error that says it leaves your package.

   ```trb fragment
   use Router from "acme/http/routing"
   ```

4. **Read what a new dependency can reach before you rely on it**, from its own `use` lines and everything it depends
   on in turn. There is no reflection and no dynamic import, so file access, the network, processes, the environment
   and foreign functions are never hidden behind a name that looks pure.

5. **Bind an owner to a registry once, if your project uses more than the default.** `registry "acme", url:
   "https://packages.acme.test"` fixes what `acme/...` resolves against, so a name under that owner can never be
   satisfied by a different registry by accident.

## Pitfalls

- **Importing before declaring is refused, naming the fix.** A `use` of a package `project.trb` does not list fails
  with the exact line to add:

  ```text
  error: The package `acme/http` is not a dependency of `acme/shop`
    = Add it: `dependencies { runtime "acme/http" }` in project.trb
  ```

- **The standard library needs no entry.** Every `std/*` package comes with the toolchain and is exempt from the
  dependency list; only third-party and workspace packages have to be declared.
- **A line written by hand is not resolved yet.** `torb add acme/http` writes the line, resolves the workspace, writes
  `project.lock.trb` and installs the package in one step. A line written by hand is resolved by `torb update`, and
  until then `torb check`, `build`, `run` and `test` refuse with exactly that advice:

  ```text
  error: `acme/shop` depends on `acme/http:^1.2.3`, and project.lock.trb does not pin it: run `torb update`
  ```

- **Commit `project.lock.trb`.** It is what makes two checkouts build the same files; `torb install` fetches what it
  pins into the cache, and only `torb add`, `remove` and `update` ever write its graph - `torb lock` writes the
  evaluated settings beside it, and `torb lock --check` says whether the file is current
  ([project.lock.trb](../tooling/project-lock-trb.md)).
- **The requirement grammar**: `^1.2.3` (the default, also written `1.2.3`) is `>=1.2.3 <2.0.0`, `^0.2.3` is
  `>=0.2.3 <0.3.0`; `~1.2.3` is `>=1.2.3 <1.3.0`; `=1.2.3` is the one version; comparators joined by spaces all hold
  (`>=1.2.0 <1.5.0`); `*` or no `:` at all is every release. A pre-release is only chosen where the requirement names
  one, and then only pre-releases of that release (`^1.3.0-beta.1`).

## Full example

The whole of `project.trb` for a project with one runtime and one development dependency, plus the registry its owner
needs because it is not the default one.

```trb
name = "acme/shop"
version = "1.0.0"

registry "acme", url: "https://packages.acme.test"

dependencies {
  runtime "acme/http:^1.2.3"
  development "acme/mock-server:^3.4.5"
}
```

Importing from `acme/http` afterwards needs nothing else:

```trb fragment
use Router from "acme/http/routing"
```

## Related

- [Packages](../language/modules-and-packages/packages.md) - `owner/name`, `src/lib.trb`, and the coherence rule in full.
- [Set up a workspace](set-up-a-workspace.md) - members that depend on each other without a registry.
- [torb add](../tooling/torb-add.md) and [project.lock.trb](../tooling/project-lock-trb.md) - the command and the file.
- use (skill `torbscript-language`: `references/language/modules-and-packages/use.md`) - every form of importing a name once the package is declared.

