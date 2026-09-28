---
title: Modules and packages
summary: How use brings a name in from another file or the standard library, and what public means for a top-level declaration.
kind: guide
status: stable
order: 100
prerequisites:
  - control-flow-and-dsls.md
keywords:
  - use
  - public
  - package
  - project.trb
source:
  - CONCEPT.md#modules-and-packages
---

A program grows past one file quickly. This page splits one into two, brings a name across with `use`, and names the
package the standard library lives in.

## Goal

At the end of this page you can split code across files with `use` and `public`, and read an import from the standard
library for what it names.

## Public declarations and use

A top-level declaration is private to its file unless marked `public`. Given `src/greeting.trb`:

```trb
public fn greeting(name: String): String {
  "Hello, {name}!"
}
```

Another file of the same project reaches it with `use`, naming the file by a relative path with no extension:

```trb
use greeting from "./greeting"

print greeting("World")
```

A name that is not `public` cannot be imported at all - not hidden by convention, but a compile error at the `use`
line that names it. See [Visibility](../language/modules-and-packages/visibility.md) and
[use](../language/modules-and-packages/use.md).

## Importing from the standard library

The standard library is a set of packages named `std/<name>`, and they need no entry in a project's dependencies -
they come with the toolchain.

```trb
use File from "std/fs"

fn readConfiguration(path: String): Result<String, IoError> {
  File.readText path
}
```

`use File from "std/fs"` at the top of a file is also the statement "this file touches files": `std/fs` is not part of
the [prelude](../language/modules-and-packages/the-prelude.md), the package whose public names - `Option`, `Result`,
`List`, `print` and the rest of what every file already has - are in scope everywhere without an import.

## A case comes in through its type

A case of a type with cases is imported through that type, and only a case can be:

```trb
use Option, Option.Some, Option.None from "./option"
```

After that, `Some` and `None` are bare in a pattern and in an expression, exactly as the prelude's own `Some` and
`None` already are - see [Importing cases](../language/pattern-matching/importing-cases.md).

## A package is a directory

```text
hello/
├ src/
├─ main.trb
├─ lib.trb
├─ greeting.trb
├ tests/
├─ greeting.test.trb
└ project.trb
```

The names of the files say what each one is. `src/main.trb` is the program `torb run` executes, `src/lib.trb` is what
another package imports, `src/greeting.trb` is a module, and a file whose name ends in `.test.trb` is a test that
`torb test` runs wherever it lies. `project.trb` names the package `owner/name` and needs no line for any of them.

The program is never imported - a file that may hold top-level code cannot be - so what the program and a test share
lives in a module like `src/greeting.trb`, and both import it. Only a package listed as a dependency can be reached
from another one, by its name and never by a relative path - see
[Packages](../language/modules-and-packages/packages.md).

A layout like this one - `src/main.trb` beside a testable `src/greeting.trb` - is exactly what
[`torb new --template app`](../tooling/torb-new.md) starts from; a library with a `src/lib.trb` and nothing to run
is `torb new`'s own default template, `package`.

## Next

- [Tests and the toolchain](tests-and-tooling.md) - writing a test and running the checks.
- [use](../language/modules-and-packages/use.md) - every import form, including renaming and namespace imports.
- [The prelude](../language/modules-and-packages/the-prelude.md) - what is in scope everywhere, and why capabilities
  are not in it.
