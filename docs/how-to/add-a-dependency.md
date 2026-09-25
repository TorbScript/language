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

1. **Add the package under `runtime` for code your project ships, `development` for tests and tools.** A
   `development` dependency is never part of what somebody who depends on your project gets.

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
   written.

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
- **A dependency still has to be found somewhere the toolchain reads packages from.** `torb add`, a registry client
  and `project.lock.trb` are designed in `CONCEPT.md` but not built yet: today a name in `dependencies { }` satisfies
  the check above only, and the package itself still has to exist as a member of the same workspace for `use` to
  resolve it, the same way [Workspaces](set-up-a-workspace.md) sets one up. A name that is neither `std/*` nor a
  workspace member is refused as `There is no package \`<name>\``, whatever `dependencies { }` says.
- **`torb run`, `build` and `test` are meant to fail if `project.lock.trb` does not match `project.trb`, and only
  `torb add`, `remove` and `update` are meant to write it.** Until those commands exist, nothing enforces this, so a
  project with dependencies has no lock file to commit yet.

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
- [Set up a workspace](set-up-a-workspace.md) - what makes a dependency resolvable without a registry today.
- [use](../language/modules-and-packages/use.md) - every form of importing a name once the package is declared.
