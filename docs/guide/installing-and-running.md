---
title: Run your first program
summary: Build the toolchain, run a single file, and create a project with a manifest, a source file and a test.
kind: guide
status: stable
order: 10
keywords:
  - install
  - torb run
  - project.trb
  - hello world
source:
  - README.md
  - bootstrap/README.md
  - CONCEPT.md#project-layout
---

The toolchain is one binary called `torb`. It runs a file directly, without a build step, and it compiles the same file
to a native executable. Until the compiler compiles itself, `torb` is the stage 0 interpreter in `bootstrap/`, and every
command goes through it.

## Goal

At the end of this page you have run a `.trb` file, created a project with a manifest and a test, and seen the two
commands you will use most.

## Build the toolchain

TorbScript is self-hosted: the compiler is written in TorbScript, and a small Rust interpreter runs it until it can
compile itself. Build that interpreter once, from the repository:

```console
$ cd bootstrap
$ cargo build --release
```

Everything below is run from `bootstrap/`. The pattern is `cargo run --release -q -- <command>` for the interpreter's own
commands, and `cargo run --release -q -- run ../compiler <command>` for the self-hosted toolchain.

## Run a single file

Put this in `hello.trb`:

```trb
const name = "World"
print "Hello, {name}!"
```

And run it:

```console
$ cargo run --release -q -- run hello.trb
Hello, World!
```

Two things happened that are worth naming. `print` is an ordinary function from the prelude, called as a **command**:
`print "..."` instead of `print("...")`, because a call is written without parentheses wherever the grammar allows it.
And `{name}` inside the string is interpolation, which works with any expression.

A file that nothing imports may hold top-level code like this. A file that is imported holds declarations only, which is
why there is no module initialization order in the language.

## Create a project

A project is a directory with a `project.trb` and a `src/`:

```text
hello/
├ src/
├─ main.trb        Entry point for running
├ tests/
├─ hello.test.trb
└ project.trb
```

`project.trb` is a TorbScript file, not a configuration language. It runs against a built-in `Project` value in a
sandbox with no access to files or the network, so a tool can read it safely:

```trb
name "acme/hello"
version "0.1.0"
```

`name "acme/hello"` writes the field `name`. That is a **property command**: a call on a field does not call the field,
it writes it. The names are `owner/name`, because an owner is a verified namespace of a registry.

`src/main.trb` is what runs:

```trb
fn greeting(name: String): String {
  "Hello, {name}!"
}

print greeting("World")
```

Run the project by naming its directory:

```console
$ cargo run --release -q -- run hello
Hello, World!
```

## Write a test

A test file is a script made of `test` and `group` calls, and the only assertion is `assert`:

```trb skip std/test is not loaded by stage 0 yet, so this file cannot run today
use greeting from "../src/main"

test "greets by name" {
  assert(greeting("World") == "Hello, World!")
}
```

`test` is an ordinary function whose last parameter is a closure, which is why the block can follow the string.
`assert` takes an `Expression<Bool>`: it receives the condition **and its source text**, so a failure prints the
expression and the values in it without a matcher vocabulary to learn. Note the parentheses around `sum == 3`-style
conditions: an operator at the top level of an argument is one of the places where the canon requires them.

## Check and format

The two commands you will run most often:

```console
$ cargo run --release -q -- run ../compiler check ..
144 files, no problems
$ cargo run --release -q -- canon --check ..
```

`check` type checks everything and answers `no problems` or points at a line. `canon` writes the formatter canon over
the syntax tree, and `--check` reports the files that are not in it. See
[Verify your work](../tooling/verifying-your-work.md) for the full list.

## Next

- [Values and bindings](values-and-bindings.md) - `const`, `var`, and why that is the whole mutation story.
- [The torb command](../tooling/the-torb-command.md) - every subcommand.
- [The language reference](../language/index.md) - one page per construct.
