---
name: torbscript-projects
description: "Sets up and maintains TorbScript projects: `project.trb` and `project.lock.trb`, dependencies (`torb add`, `remove`, `update`, `install`), workspaces, build profiles and native binaries, `torb doc`, and publishing to a registry. Use it when creating a TorbScript project or changing its manifest, dependencies, build or release."
license: MIT
compatibility: "Needs the torb command, which the torbscript skill checks and installs. Adding a package needs network access to its registry."
---

# TorbScript projects and packages

A project is a directory with a `project.trb`, which is TorbScript run against a `Project` value, not a configuration
language. The names of the files say what a project produces: `src/main.trb` is its program, `src/lib.trb` what a
package exports, and every `*.test.trb` a test. The `torbscript` skill has `torb new` and the everyday commands; this
skill has the manifest, the lock file, dependencies, workspaces, builds and publishing. Paths are relative to the
directory of this file.

The manifest is edited by the commands where a command exists: `torb add` and `torb remove` rewrite `project.trb`,
resolve the workspace and write `project.lock.trb` in one step. Commit `project.lock.trb`.

## Adding a dependency

A project may only import a package its own `project.trb` lists first - there is no dependency that becomes reachable
by being imported. `dependencies { }` is where that list lives, and it is the one place capabilities are visible
from, before a single line of the dependency's code runs.

### Steps

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

### Pitfalls

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
  ([project.lock.trb](references/tooling/project-lock-trb.md)).
- **The requirement grammar**: `^1.2.3` (the default, also written `1.2.3`) is `>=1.2.3 <2.0.0`, `^0.2.3` is
  `>=0.2.3 <0.3.0`; `~1.2.3` is `>=1.2.3 <1.3.0`; `=1.2.3` is the one version; comparators joined by spaces all hold
  (`>=1.2.0 <1.5.0`); `*` or no `:` at all is every release. A pre-release is only chosen where the requirement names
  one, and then only pre-releases of that release (`^1.3.0-beta.1`).

### Full example

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

### Related

- [Packages](references/language/modules-and-packages/packages.md) - `owner/name`, `src/lib.trb`, and the coherence rule in full.
- [Set up a workspace](references/how-to/set-up-a-workspace.md) - members that depend on each other without a registry.
- [torb add](references/tooling/torb-add.md) and [project.lock.trb](references/tooling/project-lock-trb.md) - the command and the file.
- use (skill `torbscript-language`: `references/language/modules-and-packages/use.md`) - every form of importing a name once the package is declared.

## Pages

- [Packages](references/language/modules-and-packages/packages.md) - A package is a directory with a project.trb, named owner/name, whose file names say what it produces - src/lib.trb, src/main.trb, *.test.trb - and it can only be reached by a project that lists it as a dependency.
- [Workspaces](references/language/modules-and-packages/workspaces.md) - A workspace is one root project.trb naming several member projects that check, build and test as a group, and resolve their dependencies into one shared project.lock.trb.
- [std/project](references/standard-library/project.md) - The receiver type of project.trb - Project, Dependencies, Program, Profile, Test, Tasks, Workspace, Registry and Source.
- [Build a native binary](references/how-to/build-a-native-binary.md) - Run torb build in a package or point it at a file, look at the generated C with --emit-c first if a C compiler is not on the machine yet, and read what the back end does not lower yet before you debug the program instead.
- [Add a dependency](references/how-to/add-a-dependency.md) - Declare the package in project.trb before importing from it, tell a runtime dependency from a development one, and read what the imports of everything you depend on say it can reach.
- [Set up a workspace](references/how-to/set-up-a-workspace.md) - Name the member directories in the root project.trb, give each one its own project.trb, and depend on a sibling by name alone - the workspace resolves it from source.
- [project.trb](references/tooling/project-trb.md) - The manifest of a project - its name, dependencies, workspace, further programs and profiles. The names of its files say what it produces, and the settings that decide its files are read before anything runs.
- [project.lock.trb](references/tooling/project-lock-trb.md) - The locked manifest - the exact version, tree hash and registry of every package a workspace depends on, and the evaluated settings of each of its packages - written deterministically and read without running anything.
- [torb add](references/tooling/torb-add.md) - torb add writes a dependency into project.trb, resolves the workspace with it, writes project.lock.trb and installs what is new - or changes nothing and explains why there is no solution.
- [torb remove](references/tooling/torb-remove.md) - torb remove takes a dependency out of project.trb, resolves the workspace again, and drops from project.lock.trb every package nothing needs any more.
- [torb update](references/tooling/torb-update.md) - torb update resolves every package of the workspace, or only the named ones, to the highest version project.trb allows, writes project.lock.trb, and refuses an update that gains a capability unless --accept-capabilities says it is wanted.
- [torb install](references/tooling/torb-install.md) - torb install fetches every package project.lock.trb pins that is not in the cache yet, checks its tree hash before a file is written, and never changes the lock.
- [torb publish](references/tooling/torb-publish.md) - torb publish builds and checks a package's archive as a registry receives it, prints its tree hash and capabilities, and writes it into a directory registry or uploads it with a token or through trusted publishing.
- [torb lock](references/tooling/torb-lock.md) - torb lock writes the settings block of every member of a workspace into project.lock.trb from its evaluated manifest and keeps the graph as it is; with --check it writes nothing and fails where the file is not what it would write.
- [torb doc](references/tooling/torb-doc.md) - torb doc turns the public API of a package and its doc comments into a reference - a static site, or one JSON document for an editor and the registry - and runs the examples of the doc comments as doc tests.
