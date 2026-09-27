---
title: Tests and the toolchain
summary: How to write a test with test, group and assert, and the two commands that check whether what you wrote is correct.
kind: guide
status: stable
order: 110
prerequisites:
  - modules-and-packages.md
keywords:
  - test
  - assert
  - torb check
  - torb format
source:
  - CONCEPT.md#toolchain
  - std/test/src/lib.trb
---

A test here is an ordinary function call, and the compiler that type checks your program is the same tool that runs
your tests. This page writes one test and runs the two commands you will use for the rest of this path.

## Goal

At the end of this page you can write a test file with `test` and `assert`, and name the two commands that check a
program before you hand it over.

## Writing a test

A test file ends in `.test.trb`, and the name is what makes it one: it may lie anywhere in the package, and `tests/` is
where it lies by convention. It is a [script](../language/modules-and-packages/top-level-code.md) made of `test` and
`group` calls: nothing imports it, so it may hold this top-level code the way an entry file can. `torb test` in the
package's directory runs every one of them.

```trb check
use test from "std/test"

fn greeting(name: String): String {
  "Hello, {name}!"
}

test "greets by name" { assert(greeting("World") == "Hello, World!") }
```

`test` takes a name and a closure whose body is the check; `group` names a closure of `test` calls, for organizing a
suite. `assert` takes the condition as an
[`Expression<Bool>`](../language/functions/quoted-expressions.md): a failure prints the source text of the condition
and the values it captured, so there is no matcher vocabulary to learn. See [std/test](../standard-library/test.md).

Note the parentheses around `greeting("World") == "Hello, World!"`: an operator at the top level of an argument is
one of the places the formatter canon requires them, even inside a command call's own argument.

## Checking a program

`torb check` type checks a whole project or a single file and answers `no problems`, or points at the exact line:

```console
$ torb check examples/tour
14 files, no problems
```

Run it from the repository root - see [Run your first program](installing-and-running.md). `torb check` is the gate:
a false positive of it is a bug in the checker, never a reason to change a correct program.

## Formatting

`torb format` writes the one layout of the language over the syntax tree - a call becomes a command wherever the grammar
allows it and gets parentheses everywhere else, two spaces per level, one space around an operator - and `--check`
reports the files that are not in it without writing anything:

```console
$ torb format --check examples/tour
0 of 14 files would change
```

A change is done when both commands are green. See [Verify your work](../tooling/verifying-your-work.md) for the full
list, including the test runner.

## Next

- [Put it together](a-small-program.md) - one program that uses everything on this path.
- [The torb command](../tooling/the-torb-command.md) - every subcommand.
- [Verify your work](../tooling/verifying-your-work.md) - the commands to run before you are done, in order.
