---
title: Verify your work
summary: The commands that decide whether TorbScript you wrote is correct and in the formatter canon, in the order to run them.
kind: tooling
status: stable
order: 20
skill: verify
keywords:
  - check
  - canon
  - test
  - gate
  - verify
source:
  - compiler/CONTRIBUTING.md
  - bootstrap/README.md
---

Never hand over TorbScript you have not run through the compiler. The language has a type checker, an exhaustiveness
check, a dead-change check and a formatter canon, and all four of them answer in seconds. A snippet that looks right and
was not checked is the most expensive thing you can produce.

## Synopsis

All of these run from `bootstrap/`, which is where stage 0 lives.

```text
cargo build --release                                      Rebuild stage 0 after a change to its Rust sources
cargo run --release -q -- run ../compiler check <path>      Type check: "no problems", or a line and a caret
cargo run --release -q -- run ../compiler check --statistics <path>   Every expression has a type: "0 deferred"
cargo run --release -q -- run ../compiler parse <path>      Syntax only, recursively
cargo run --release -q -- canon --check <path>              Is it in the formatter canon?
cargo run --release -q -- canon <path>                      ...write it
cargo run --release -q -- test ../compiler/tests            The TorbScript tests
cargo run --release -q -- run ../compiler docs check ../docs   The documentation gate
```

## What it does

### The order to run them in

1. **`check`** first. It resolves every name and types every expression, so it finds the mistakes that matter: an
   undeclared name, a wrong type, a non-exhaustive `match`, a change that cannot be seen, a `var` that is missing.
2. **`canon --check`** second. It reports every file whose call form or multi-line string is not in the canon. Run plain
   `canon` to write it rather than fixing it by hand - it edits over the syntax tree and re-parses, so it cannot change what
   a program means.
3. **`test`** last, when there are tests. One process per file, as many at a time as the machine has cores.

A change is done when all three are green **and** `check` still answers `no problems` over the whole repository. A false
positive of the checker is a bug in the checker.

### What each one catches

| Command | Catches |
|---------|---------|
| `parse` | A semicolon, an unclosed brace, a command call in the wrong position, a `{` where a block was meant |
| `check` | An undeclared name, a wrong type, a missing `var`, a non-exhaustive `match`, an unreachable arm, a dead change, a discarded result, a visibility violation |
| `check --statistics` | Expressions that have no type yet, reported as `deferred`. The number has to be 0 |
| `canon --check` | A parenthesized call that should be a command, a command that should have parentheses, a multi-line string that is not indented |
| `test` | Everything a test asserts, including the exact text of a diagnostic |
| `docs check` | A page of `docs/` whose front matter, sections, links, headings or code blocks are wrong |

### Reading a diagnostic

A diagnostic points at a span and says one thing:

```text
error: Comparisons do not chain. Use `&&`: `a < b && b < c`
 --> src/main.trb:12:19
  |
12 | const chained = a < b > c
  |                       ^
```

One root cause, one message. When a message offers a fix, take it: the diagnostics of this language are written to name the
correct line rather than to describe the rule.

### Checking a snippet that is not a file yet

Write it to a file and check the file. A `.trb` file that nothing imports is a script, so it may hold top-level code and
needs no `fn main`:

```console
$ cd bootstrap
$ cargo run --release -q -- run ../compiler parse ../scratch.trb
1 files, no problems
$ cargo run --release -q -- run scratch.trb
```

To type check it against the standard library, put it in a package: a directory with a `project.trb` that has a `name`, and
`src/main.trb` with the code. Then `check <that directory>`.

## Examples

A clean run over the whole repository:

```console
$ cd bootstrap
$ cargo run --release -q -- run ../compiler check ..
255 files, no problems
$ cargo run --release -q -- run ../compiler check --statistics ..
149982 of 149982 expressions typed (100%), 0 deferred
$ cargo run --release -q -- canon --check ..
$ cargo run --release -q -- test ../compiler/tests
```

A run that found something:

```console
$ cargo run --release -q -- run ../compiler check ../scratch
error: `add` needs a `var`. Did you mean `added`?
 --> ../scratch/src/main.trb:3:7
  |
3 | fixed.add 3
  |       ^^^

1 problems in 1 of 1 files
```

### Before a commit

The full list, from `compiler/CONTRIBUTING.md`, in the order it is written there:

```console
cargo build --release
cargo run --release -q -- run ../compiler check ..
cargo run --release -q -- run ../compiler check --statistics ..
cargo run --release -q -- test ../compiler/tests
cargo run --release -q -- canon --check --rule calls --rule strings --rule imported-case-patterns --rule unused-bindings ..
cargo fmt --check
cargo clippy --all-targets -- -D warnings
cargo test --release
```

The last one includes the differential tests, which compare stage 0 against the self-hosted front end, and it takes
minutes.

## Related

- [The torb command](the-torb-command.md) - every subcommand and its flags.
- [Command calls](../language/syntax/command-calls.md) - the canon that `canon --check` decides.
- [What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md) - what to look for before
  running anything.
- [The toolchain](index.md) - the other pages about the commands.
