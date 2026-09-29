---
title: Install and run
summary: Install TorbScript with one command, run a file, and make a project with a test - the five commands you will use every day.
kind: guide
status: stable
order: 20
keywords:
  - install
  - torb run
  - torb new
  - torb test
  - project.trb
  - hello world
source:
  - compiler/src/cli/new.trb
---

TorbScript is one program, `torb`. It runs your code, tests it, checks it and formats it.

## Goal

At the end of this page TorbScript is installed, you have run a file, and you have a project with a passing test.

## Install

Run the one command for your system from the [install page](../site/install.md), then open a new terminal and check:

```console
$ torb --version
torb 0.1.0
```

## Run a file

Save this as `hello.trb`:

```trb run
const name = "World"
print "Hello, {name}!"
// prints Hello, World!
```

And run it:

```console
$ torb run hello.trb
Hello, World!
```

`{name}` puts a value into the text. A file like this one may hold code at the top level; it needs no `main`.

## Make a project

```console
$ torb new hello --offline
wrote hello/project.trb, hello/src/main.trb, hello/tests/main.test.trb
$ cd hello
$ torb run
Hello, hello
```

`--offline` writes a small starter at once. Without it, `torb new` asks a few questions and starts from a template
([torb new](../tooling/torb-new.md)). The project is three files:

```text
hello/
├ src/
├─ main.trb           the program that torb run starts
├ tests/
├─ main.test.trb      a test, because its name ends in .test.trb
└ project.trb         the manifest: name = "hello", version = "0.1.0"
```

`project.trb` is TorbScript too. File names decide the rest, so the manifest needs no more lines.

## Test it

`tests/main.test.trb` holds one test:

```trb check
use test from "std/test"

test "hello runs" {
  assert(1 + 1 == 2)
}
```

```console
$ torb test
tests/main.test.trb
  ok      hello runs

1 passed, 0 failed (1 file)
```

`test` takes a name and a block. `assert` takes a condition, and when it fails it prints the condition and the values
in it, so there is nothing else to learn.

## Check and format

```console
$ torb check .
3 files, no problems
$ torb format --check .
0 of 3 files would change
```

`torb check` finds mistakes without running anything and points at the line. `torb format` writes your files in the one
layout of the language; `--check` only reports. Run both before you call something done.

## Ship it

`torb run` starts at once. When you want a program file that runs without TorbScript, build one:

```console
$ torb build --output hello
wrote hello
$ ./hello
Hello, hello
```

`torb build` needs a C compiler on your computer; the [install page](../site/install.md) says which.

## Next

- [Values and bindings](values-and-bindings.md) - `const`, `var`, and what a copy means.
- [Modules and packages](modules-and-packages.md) - more than one file, and how a test reaches your code.
- [The torb command](../tooling/the-torb-command.md) - every command and flag.
