---
title: torb new
summary: torb new scaffolds a package - project.trb, a src/main.trb that prints a greeting, and a tests/main.test.trb with one passing test - refusing where the name already exists.
kind: tooling
status: stable
order: 25
keywords:
  - torb new
  - scaffold
  - project.trb
  - getting started
source:
  - compiler/src/cli/new.trb
---

`new` is where a package starts: one command instead of copying an example by hand and editing its name everywhere it
appears. What it writes is exactly what [`torb run`](torb-run.md) and [`torb test`](torb-test.md) already know how to
build - a `project.trb` with a name and a version and nothing else, a `src/main.trb` that is the program because of
its name, and a `tests/main.test.trb` that is a test because of its name.

## Synopsis

```text
torb new <name>   Write <name>/project.trb, <name>/src/main.trb and <name>/tests/main.test.trb
```

## What it does

### The three files

```text
<name>/
├ src/
│ └ main.trb          print "Hello, <name>"
├ tests/
│ └ main.test.trb      use test from "std/test", one passing test
└ project.trb          name = "<name>", version = "0.1.0"
```

`<name>` is both the directory `new` creates and the package's `name` in the manifest it writes there, which is the
whole manifest:

```trb fragment
name = "hello"
version = "0.1.0"
```

`src/main.trb` is the package's program, named after the package, and it needs no line in the manifest: it may hold
top-level code, needs no `fn main`, and prints its one line as soon as it runs. `tests/main.test.trb` uses the bare
`use test from "std/test"` form (see [std/test](../standard-library/test.md)) with one `test` call that always passes,
so `torb test` has something to report from the first run.

### Where it finds `std` and the runtime

The package `new` writes names no dependency and belongs to no workspace, so it is a standalone project the way any
loose file is: [`torb run`](torb-run.md) and [`torb test`](torb-test.md) find the toolchain's own `std/` and `runtime/`
searched for above the new directory, the working directory and `torb` itself. That is what makes the scaffold work
the same whether `torb new` is run inside this repository or in an empty directory anywhere else.

### Refusing to overwrite

`new` checks whether `<name>` already exists - as a file or as a directory - before writing anything, and refuses with
an error rather than merge into it or overwrite a part of it. A name that is free is the only thing it asks for; there
is no flag to force it or to pick a different layout.

## Examples

```console
$ torb new hello
wrote hello/project.trb, hello/src/main.trb, hello/tests/main.test.trb
$ torb new hello
error: `hello` already exists
$ cd hello
$ torb run
Hello, hello
$ torb test
tests/main.test.trb
  ok      hello runs

1 passed, 0 failed (1 file)
```

From the directory above it, the package is a path: `torb run ./hello` and `torb test hello`.

## Related

- [torb run](torb-run.md) - builds and executes the `src/main.trb` a new package starts with.
- [torb test](torb-test.md) - builds and runs the `tests/main.test.trb` a new package starts with.
- [project.trb](project-trb.md) - the manifest `new` writes, and what the toolchain reads out of it.
- [std/test](../standard-library/test.md) - `test` and `group`, the two calls a `.test.trb` file makes.
- [Installing and running TorbScript](../guide/installing-and-running.md) - where `torb new` fits in the first steps.
