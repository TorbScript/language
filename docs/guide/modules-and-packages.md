---
title: Modules and packages
summary: Split a program into files with public and use, import from the standard library, and lay out a project so that its tests reach its code.
kind: guide
status: stable
order: 110
prerequisites:
  - control-flow-and-dsls.md
keywords:
  - use
  - public
  - import
  - package
  - project.trb
  - test
source:
  - CONCEPT.md#modules-and-packages
  - std/test/src/lib.trb
---

Every file is a module. A name is private to its file until it says `public`, and another file brings it in with
`use`.

## Goal

At the end of this page you can split a project into files, import from the standard library, and test a function of
your own.

## Share a name between files

`src/greeting.trb`:

```trb
public fn greeting(name: String): String {
  "Hello, {name}!"
}
```

`src/main.trb`:

```trb
use greeting from "./greeting"

print greeting("World")
```

`use` names a file by its path relative to the importing file, without `.trb`. A name without `public` cannot be
imported: the `use` line is an error.

## The standard library

```trb check
use File, IoError from "std/fs"

fn readSettings(path: String): Result<String, IoError> {
  File.readText path
}
```

The standard library is a set of packages named `std/...`, and they come with TorbScript. The basics, such as `print`,
`List`, `Option` and `Result`, are in every file without a `use`. Anything that reaches outside the program, like files
or the network, needs one - so the top of a file shows what it touches.

## A project

```text
hello/
├ src/
├─ main.trb             the program that torb run starts
├─ greeting.trb         a module
├ tests/
├─ greeting.test.trb    a test
└ project.trb           the manifest
```

The names decide what each file is. `src/main.trb` is the program, and nothing imports it. So the code that the
program and the tests share lives in a module such as `src/greeting.trb`, and both import it.

## Test your code

`tests/greeting.test.trb`:

```trb
use test from "std/test"
use greeting from "../src/greeting"

test "greets by name" {
  assert(greeting("World") == "Hello, World!")
}
```

```console
$ torb test
tests/greeting.test.trb
  ok      greets by name

1 passed, 0 failed (1 file)
```

`torb test` runs every file whose name ends in `.test.trb`. `group "name" { ... }` bundles several tests under one
name. The parentheses in `assert(...)` are needed because the argument has an operator at its top level.

## Next

- [Put it together](a-small-program.md) - one program that uses everything so far.
- [use](../language/modules-and-packages/use.md) - every form of an import, including renaming.
- [Packages](../language/modules-and-packages/packages.md) - what a package is, and how another project depends on it.
