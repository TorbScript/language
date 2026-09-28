---
title: Run your first program
summary: Build the toolchain, run a single file, and create a project with a manifest, a source file and a test.
kind: guide
status: stable
order: 10
keywords:
  - install
  - torb run
  - torb new
  - project.trb
  - hello world
source:
  - README.md
  - tools/bootstrap.sh
  - CONCEPT.md#project-layout
  - compiler/src/cli/new.trb
---

The toolchain is one binary called `torb`. `torb run` checks a file and runs it at once in the VM inside `torb`, which
needs no C compiler, and `torb build` compiles it to a native executable instead - `torb run --native` does both in one
step. The two run the same program with the same output.

## Goal

At the end of this page you have run a `.trb` file, created a project with a manifest and a test, and seen the two
commands you will use most.

## Build the toolchain

TorbScript is self-hosted: the compiler is written in TorbScript and compiles itself from a seed - a `torb` that
already exists. Put one under `seed/` and build it once, from the repository root:

```console
$ sh tools/bootstrap.sh
seed: seed/torb
step 1: the seed builds the compiler
step 2: that compiler builds the compiler again

the fixpoint holds: both steps emitted the same C.
torb: build/release/torb
```

`build/release/torb` is the compiler that comes out, and `torb` below is that binary. A C compiler on the `PATH` is
what it needs (`$TORB_CC`, or `clang`, `gcc`, `cc`).

## Run a single file

Put this in `hello.trb`:

```trb
const name = "World"
print "Hello, {name}!"
```

And run it:

```console
$ torb run hello.trb
Hello, World!
```

Two things happened that are worth naming. `print` is an ordinary function from the prelude, called as a **command**:
`print "..."` instead of `print("...")`, because a call is written without parentheses wherever the grammar allows it.
And `{name}` inside the string is interpolation, which works with any expression.

A file that nothing imports may hold top-level code like this. A file that is imported holds declarations only, which is
why there is no module initialization order in the language.

## Create a project

`torb new <name>` is the start: it writes a project for you, in the shape [`torb run`](../tooling/torb-run.md) and
[`torb test`](../tooling/torb-test.md) already know how to build. Without `--offline` it starts from a template of
[git.torb.dev](https://git.torb.dev) and asks a few questions first - see [`torb new`](../tooling/torb-new.md) for all
of that. This page uses the plain, built-in scaffold `--offline` writes at once, because it is what the rest of the
page builds on line by line:

```console
$ torb new hello --offline
wrote hello/project.trb, hello/src/main.trb, hello/tests/main.test.trb
```

```text
hello/
├ src/
├─ main.trb          print "Hello, hello"
├ tests/
├─ main.test.trb      use test from "std/test", one passing test
└ project.trb          name = "hello", version = "0.1.0"
```

Run it from its directory:

```console
$ cd hello
$ torb run
Hello, hello
```

`project.trb` is a TorbScript file, not a configuration language. It runs against a built-in `Project` value in a
sandbox that may read files below its own directory and nothing outside it, so a tool can read it safely:

```trb fragment
name = "hello"
version = "0.1.0"
```

`name = "hello"` writes the field `name` of that `Project`, and that is the whole manifest: `src/main.trb` is the
program because of its name and `tests/main.test.trb` is a test because of its name, so neither needs a line.
`torb new` writes the bare name it was given; a package meant to be published uses `owner/name` instead, because an
owner is a verified namespace of a registry. See [torb new](../tooling/torb-new.md) for the rest of what it writes and
why.

## Extend it

`src/main.trb` is what runs, and a program is never imported - a file that may hold top-level code cannot be - so the
function the test below calls goes into a module of its own, `src/greeting.trb`:

```trb
public fn greeting(name: String): String {
  "Hello, {name}!"
}
```

`public` is what lets another file import `greeting`: a declaration is private to its file unless it says otherwise.
Replace the one line `torb new` wrote in `src/main.trb` with a call of it:

```trb skip it imports the 'src/greeting.trb' of the project this page creates, which one snippet of this documentation cannot provide
use greeting from "./greeting"

print greeting("World")
```

```console
$ torb run
Hello, World!
```

## Write a test

A test file is a script made of `test` and `group` calls, and the only assertion is `assert`. Replace the placeholder
test `torb new` wrote in `tests/main.test.trb` with one that calls `greeting`:

```trb skip it imports the 'src/greeting.trb' of the project this page creates, which one snippet of this documentation cannot provide
use test from "std/test"
use greeting from "../src/greeting"

test "greets by name" {
  assert(greeting("World") == "Hello, World!")
}
```

`torb test` runs every file of the package whose name ends in `.test.trb`:

```console
$ torb test
tests/main.test.trb
  ok      greets by name

1 passed, 0 failed (1 file)
```

`test` is an ordinary function of `std/test` whose last parameter is a closure, which is why the block can follow the
string. `assert` takes an `Expression<Bool>`: it receives the condition **and its source text**, so a failure prints
the expression and the values in it without a matcher vocabulary to learn. Note the parentheses around `sum == 3`-style
conditions: an operator at the top level of an argument is one of the places where the canon requires them.

## Check and format

The two commands you will run most often:

```console
$ torb check .
4 files, no problems
$ torb format --check .
0 of 4 files would change
```

`check` type checks everything and answers `no problems` or points at a line. `format` writes the one layout of the
language over the syntax tree, and `--check` reports the files that are not in it. See
[Verify your work](../tooling/verifying-your-work.md) for the full list.

## Next

- [Values and bindings](values-and-bindings.md) - `const`, `var`, and why that is the whole mutation story.
- [The torb command](../tooling/the-torb-command.md) - every subcommand.
- [The language reference](../language/index.md) - one page per construct.

