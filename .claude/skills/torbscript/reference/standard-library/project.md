---
title: std/project
summary: The receiver type of project.trb - Project, Dependencies, Build, Test and Workspace.
kind: package
status: stable
order: 200
keywords:
  - std/project
  - project.trb
  - Project
  - manifest
source:
  - std/project/src/lib.trb
---

`std/project` is the vocabulary of `project.trb`. A project file is a receiver script against `Project`, so everything
a project can say about itself is a `var` field or a method of the types here, and nothing else is needed to write one:
a setting is written `name "acme/shop"` and a section `build { ... }`. The types are ordinary data, not `native` -
a project file is deterministic and has no IO, so evaluating it is nothing but running these members.

## Import

`project.trb` is loaded as a receiver script against `Project`, so it never writes a `use` for these types itself; a
tool that reads a manifest programmatically imports them like anything else.

```trb fragment
use Project, Dependencies, Build, Test, Workspace, Registry from "std/project"
```

```trb check
use Project from "std/project"

var project = Project()
project.name = "acme/shop"
print project.name
```

## Declarations

<!-- torb:declarations:begin -->

### Project

```trb fragment
public type Project {
  var name: String = ""
  var version: String = ""
  var prelude: String = "std/prelude"
  var dependencies: Dependencies = Dependencies()
  var build: Build = Build()
  var test: Test = Test()
  var workspace: Workspace = Workspace()

  var fn authors(...names: String)
  var fn registry(owner: String, url: String)
}
```

The receiver of `project.trb`. A setting is a `var` field (`name "acme/shop"` writes it), a section is a field
configured in place (`build { ... }`), and only what is more than that is a method (`authors`, `registry`). `name` is
`owner/name` - owners are verified namespaces of a registry, so a bare name is not publishable. The fields are
readable, which is what lets a project file compute from what it already said:
`const binary = name.substringAfter("/") ?? name`.

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

### Build and Test

```trb fragment
public type Build {
  var target: String = "dev"
  var input: String = "src/main.trb"
  var output: String = ""
}

public type Test {
  var input: String = "tests"
  var coverageThreshold: Int = 0
}
```

`Build` is what `torb build` produces; `output` is interpolated eagerly, so it reads `target` and the script's own
bindings. `Test.coverageThreshold` is the share of lines a test run has to cover, in percent, and `0` asks for nothing.

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

<!-- torb:declarations:end -->

## Related

- [std/sandbox](sandbox.md) - `Sandbox`, the mechanism `project.trb` is loaded through.
- [The standard library](index.md) - the other packages.
