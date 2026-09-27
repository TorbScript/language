---
title: std/project
summary: The receiver type of project.trb - Project, Dependencies, Program, Profile, Test, Tasks, Workspace, Registry and Source.
kind: package
status: stable
order: 200
keywords:
  - std/project
  - project.trb
  - Project
  - Program
  - Profile
  - manifest
source:
  - std/project/src/lib.trb
---

`std/project` is the vocabulary of `project.trb`. A project file is a receiver script against `Project`, so everything
a project can say about itself is a `var` field or a method of the types here, and nothing else is needed to write one:
a setting is written `name = "acme/shop"`, a section `tasks { ... }` and a program `program "migrate", entry:
"tools/migrate.trb"`. The types are ordinary data, not `native` - a project file is deterministic and has no IO, so
evaluating it is nothing but running these members.

What a package produces is not in this vocabulary at all: the names of its files say it (`src/main.trb`, `src/lib.trb`,
`*.test.trb`), and [project.trb](../tooling/project-trb.md) has the table.

## Import

`project.trb` is loaded as a receiver script against `Project`, so it never writes a `use` for these types itself; a
tool that reads a manifest programmatically imports them like anything else.

```trb fragment
use Project, Dependencies, Program, Profile, Test, Tasks, Workspace, Registry, Source from "std/project"
```

```trb check
use Project from "std/project"

var project = Project()
project.name = "acme/shop"
print project.name
```

## Declarations

### Project

```trb fragment
public type Project {
  var name: String = ""
  var version: String = ""
  var language: String = ""
  var description: String = ""
  var license: String = ""
  var repository: String = ""
  var prelude: String = "std/prelude"
  var dependencies: Dependencies = Dependencies()
  var test: Test = Test()
  var tasks: Tasks = Tasks()
  var workspace: Workspace = Workspace()

  var fn authors(...names: String)
  var fn registry(owner: String, url: String)
  var fn source(package: String, path: String = "", git: String = "", revision: String = "", archive: String = "", hash: String = "")
  var fn program(name: String, entry: String = "", output: String = "")
  var fn profile(name: String, configure: (var self: Profile) => Void)
}
```

The receiver of `project.trb`. A setting is a `var` field (`name = "acme/shop"` writes it), a section is a field
configured in place (`tasks { ... }`), and only what is more than that is a method (`authors`, `registry`, `source`,
`program`, `profile`). `name` is `owner/name` - owners are verified namespaces of a registry, so a bare name is not
publishable. The fields are readable, which is what lets a project file compute from what it already said:
`description = "The {name} package"`.

`program` declares a [Program](#program) and `profile` configures a [Profile](#profile). There is no `build`: a
manifest that writes `build { }` does not check.

### Dependencies

```trb fragment
public type Dependencies {
  var fn runtime(...requirements: String)
  var fn development(...requirements: String)
}
```

`runtime` is what dependents get; `development` is tests and tools and is never part of it. A requirement is written
as it is published (`"acme/http:^1.2.3"`), because the version is the package manager's business and the project file
only names what it wants.

### Program

```trb fragment
public type Program {
  name: String
  entry: String = ""
  output: String = ""
}
```

A program of the package beyond the `src/main.trb` that needs no line, declared with `Project.program`:
`program "migrate", entry: "tools/migrate.trb"`. `entry` is the file whose top-level code the program is, relative to
the project; a line without one renames the default program (`program "torb"`). `output` is where the binary goes,
relative to the project and taken literally; empty is `build/<profile>/<name>`. The toolchain reads the three before
it can run anything, so each is a plain string, and `name` is lowercase letters, digits and `-`.

```trb fragment
program "migrate", entry: "tools/migrate.trb", output: "dist/migrate"
```

### Profile

```trb fragment
public type Profile {
  name: String
  var optimize: Int
  var debugInformation: Bool = false
  var panicFrames: Bool

  static fn named(name: String): Self
}
```

How one of the two profiles builds, configured with `Project.profile`. `name` is `dev` or `release` and there is no
third. `optimize` is the level of the C compiler, 0 to 3 - `1` in `dev` and `2` in `release`. `debugInformation` makes
the binary carry debug information for a debugger. `panicFrames` - `true` in `dev`, `false` in `release` - is read and
ignored for now. `Profile.named` is the profile where no block says otherwise, and the block of `profile` starts from
it:

```trb fragment
profile "release" {
  optimize = 3
  debugInformation = true
}
```

### Test

```trb fragment
public type Test {
  var coverageThreshold: Int = 0
}
```

`coverageThreshold` is the share of lines a test run has to cover, in percent, and `0` asks for nothing. No command
reads it yet. Which files are tests is not a setting: a file called `*.test.trb` is one wherever it lies in the
package.

### Tasks

```trb fragment
public type Tasks {
  var workers: Int = 0
  var blocking: Int = 0
}
```

How the program's tasks run: `workers` threads run its tasks, the core count where the line is left out, and
`blocking` threads run what blocks (`offload`), 4 where it is left out. `TORB_WORKERS` and `TORB_BLOCKING` override both
where the program runs, so a machine can be told without a rebuild. `0` is "not said"; a number the toolchain reads
has to be 1 to 1024, and `workers = 0` is refused rather than read as "decide for me".

```trb fragment
tasks {
  workers = 4
  blocking = 8
}
```

### Workspace

```trb fragment
public type Workspace {
  var fn members(...patterns: String)
}
```

A project that consists of several projects. Every member is an ordinary project with its own `project.trb`; a pattern
is a directory or `directory/*` for every project directly below it.

### Registry

```trb fragment
public type Registry {
  owner: String
  url: String
}
```

An owner bound to a registry, so a public package can never take the place of a private one.

### Source

```trb fragment
public type Source {
  package: String
  path: String = ""
  git: String = ""
  revision: String = ""
  archive: String = ""
  hash: String = ""
}
```

Where one package comes from instead of the registry its owner is bound to: `source "acme/y", path: "../y"`. The
package manager resolves a `path:` source today; `git:` (with `revision:`) and `archive:` (with `hash:`) type check and
are refused by it ([project.lock.trb](../tooling/project-lock-trb.md)).

## Related

- [project.trb](../tooling/project-trb.md) - what the toolchain reads out of these settings, and what the names of the
  files decide instead.
- [std/sandbox](sandbox.md) - `Sandbox`, the mechanism `project.trb` is loaded through.
- [The standard library](index.md) - the other packages.
