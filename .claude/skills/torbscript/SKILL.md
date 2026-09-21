---
name: torbscript
description: "Writes, reads and reviews TorbScript, a functional-first language with value semantics whose CLI is `torb` and whose files end in `.trb`. Use it for any task that touches TorbScript source, a `project.trb`, the `torb` toolchain, or the TorbScript standard library - and use it before writing TorbScript from memory, because the language looks like Rust, Swift and Kotlin but differs from all three in mutation, error handling, cases and call syntax."
---

# TorbScript

## The language in sixty seconds

TorbScript looks like Rust, Swift and Kotlin and is none of them. Six ideas explain almost every line of it. Read this
before writing any TorbScript; each idea links to the page that has the exact rules.

### Goal

After this page you can read TorbScript, and you know which of your habits from other languages will produce code that
does not compile.

### Everything is a value, and the binding decides

There is one `Point`, not a `Point` and a `MutablePoint`. There is one `List`, not `List` and `MutableList`. A `const`
binding never changes and nothing below it changes; a `var` binding can be changed in place.

```trb
const fixed = [1, 2]
var buffer = fixed
buffer.add 3
print "{fixed} {buffer}"
```

That prints `[1, 2] [1, 2, 3]`. Assigning, passing and capturing a value is a **copy**, so two bindings never point at
the same thing and a change happens exactly where it is written. Mutation needs a `var` all the way down: a `var`
binding, `var` parameter or `var self`, then `var` fields. See [Bindings](reference/language/values-and-types/bindings.md) and
[Why values instead of references](reference/explanation/why-values-instead-of-references.md).

The one exception is a `shared type`, which has an identity: assigning it does not copy. Files, sockets and channels are
shared types; almost nothing else is.

### There is no null and there are no exceptions

Absence is `Option<Value>`, written `Value?`. Failure is `Result<Value, Failure>`, whose cases are `Ok` and `Fail`. The
postfix `?` unwraps an `Ok` or returns the `Fail` from the surrounding function, converting the error type through `From`
on the way.

```trb
use File from "std/fs"

fn firstLine(path: String): Result<String, Error> {
  const text = File.readText(path)?
  const lines = text.lines()
  match lines.first() {
    Some(line) => Ok line
    None => Ok ""
  }
}
```

`??` is the fallback (`findUser(1)?.name ?? "anonymous"`), `?.` maps over an `Option`. `panic "..."` is for bugs: it
prints to standard error and exits with 101, and nothing else runs. See [Result](reference/language/errors/result.md).

### One keyword declares every data type

`type` is struct, class, enum and algebraic data type at once. Fields are `const` unless marked `var`; members are
public unless marked `private`. `Equals`, `Hash`, `Show` and `copy` are generated.

```trb
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)

  fn area(self): Float {
    match self {
      .Circle(radius) => math.pi * radius * radius
      .Rectangle(width, height) => width * height
    }
  }
}

const shape = Shape.Circle 2.0
print shape.area()
```

A case is written `Shape.Circle`, or `.Circle` where the expected type says which type is meant. It is **never bare**
unless the file imports it (`use Option.Some from "std/core"`), and the prelude imports `Some`, `None`, `Ok` and `Fail`
for you. A `match` is an expression and must cover every case. See
[Cases and match](reference/language/pattern-matching/cases-and-match.md).

In a pattern the first letter decides: a lowercase name **binds**, an uppercase one is a case. That is why the first
letter of every declaration is a rule the compiler reports (`type Point`, `fn distance`, `const maxSize` - there is no
`MAX_SIZE`), and why a binding of a `match` arm, an `if const` or a `while const` that the arm never reads is an error:
write `_`, or `_name` to keep the name. See [Naming](reference/language/syntax/naming.md) and
[Pattern forms](reference/language/pattern-matching/pattern-forms.md).

### Capabilities are traits, and a type comes `with` them

There is no inheritance. A trait with one required method is named after that method - `Hash`, `Equals`, `Compare`,
`Show`, `Add`, `Close` - and a type says `with Hash`. Operators are traits: `+` is `Add.add`, `==` is `Equals.equals`,
`a[i]` is `Indexed.at`.

```trb
trait Area {
  fn area(self): Float
}

type Square with Area {
  side: Float

  fn area(self): Float {
    side * side
  }
}
```

`&` intersects traits (`fn audit(entry: Show & Encode)`), `where Item: Hash` bounds a type parameter, and `extend` adds
members to a type that already exists. See [Traits](reference/language/traits/traits.md).

### A call is written as a command wherever it can be

`Ok value`, `print "hello"`, `return Fail problem`, `names.map Role`. Parentheses appear where the grammar needs them:
nested calls (`Ok Some(x)`), no arguments (`list.length()`), an operator at the top level of an argument
(`assert(sum == 3)`), several lines, and the head of an `if`, `for`, `while` or `match`. This is not a preference, it is
the formatter canon and `torb canon --check` enforces it. See
[Command calls](reference/language/syntax/command-calls.md).

Statements end at the end of the line. There are no semicolons, and two statements never share a line.

### Pipelines are lazy, and one vocabulary is shared

`map`, `filter`, `flatMap`, `take`, `sorted` and the rest return an `Iterable` and run nothing until a terminal
operation pulls the values through.

```trb
type Employee {
  name: String
  age: Int
}

const employees = [Employee("Ada", 36), Employee("Alan", 41)]
const names = employees.filter({ _.age >= 40 }).map({ _.name }).toList()
print names
```

`map` and `flatMap` mean the same thing on `Option`, `Result`, `Task` and `Iterable`, by convention rather than through
higher-kinded types. A closure is always `{ parameters => body }`, and `_`, `_2` are its implicit parameters. See
[the language reference](reference/language/index.md).

### What to carry over from other languages, and what not

| Your habit | In TorbScript |
|------------|---------------|
| `let mut` / `mutating func` | `var`, on the binding or on `self` |
| a borrow, `&mut` | a copy, or a `var` parameter for the duration of one call |
| `null`, `nil`, `undefined` | `Option<Value>`, and there is no implicit `Some` |
| `throw` / `try` / `catch` | `Result`, `?`, and `panic` only for bugs |
| a bare enum case (`Circle`) | `Shape.Circle` or `.Circle`, bare only when imported |
| `class`, inheritance | `type` plus traits, or `shared type` for identity |
| `f(a, b)` everywhere | `f a, b` wherever the grammar allows it |
| `;` at the end of a line | nothing |
| `x.length` on a string | `text.chars().count()` or `text.byteLength()` |
| a `for` loop that mutates elements | `items[index].field = value`, or `map` into a new collection |
| `MAX_SIZE`, `type point`, `fn Distance` | `maxSize`, `type Point`, `fn distance` - the first letter is a rule |
| `Some(_)` or `Some(x)`, whichever | `Some(x)` only where the arm reads `x`; `_` or `_x` otherwise |

### Next

- [Run your first program](reference/guide/installing-and-running.md) - from nothing to output.
- [Values and bindings](reference/guide/values-and-bindings.md) - the mutation rules in full.
- [What a model trained on other languages gets wrong](reference/explanation/mistakes-models-make.md) - the mistakes, with the
  diagnostics they produce.
- [The language reference](reference/language/index.md) - one page per construct.

Source: `reference/guide/the-language-in-sixty-seconds.md`

## Read these two files before writing TorbScript

- `reference/language/syntax/cheat-sheet.md` - Every form of the language, with its exact spelling.
- `reference/explanation/mistakes-models-make.md` - The mistakes a model trained on other languages makes, each with its diagnostic.

They are lookup material, not background: the first one is the only place every form of the language is
written down, and the second one is the list of lines that look right and are not. Read both once at the start
of a task that writes TorbScript.

## How to verify your work

Never hand over TorbScript you have not run through the compiler. The language has a type checker, an exhaustiveness
check, a dead-change check and a formatter canon, and all four of them answer in seconds. A snippet that looks right and
was not checked is the most expensive thing you can produce.

### Synopsis

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

### What it does

#### The order to run them in

1. **`check`** first. It resolves every name and types every expression, so it finds the mistakes that matter: an
   undeclared name, a wrong type, a non-exhaustive `match`, a change that cannot be seen, a `var` that is missing.
2. **`canon --check`** second. It reports every file whose call form or multi-line string is not in the canon. Run plain
   `canon` to write it rather than fixing it by hand - it edits over the syntax tree and re-parses, so it cannot change what
   a program means.
3. **`test`** last, when there are tests. One process per file, as many at a time as the machine has cores.

A change is done when all three are green **and** `check` still answers `no problems` over the whole repository. A false
positive of the checker is a bug in the checker.

#### What each one catches

| Command | Catches |
|---------|---------|
| `parse` | A semicolon, an unclosed brace, a command call in the wrong position, a `{` where a block was meant |
| `check` | An undeclared name, a wrong type, a missing `var`, a non-exhaustive `match`, an unreachable arm, a dead change, a discarded result, a visibility violation |
| `check --statistics` | Expressions that have no type yet, reported as `deferred`. The number has to be 0 |
| `canon --check` | A parenthesized call that should be a command, a command that should have parentheses, a multi-line string that is not indented |
| `test` | Everything a test asserts, including the exact text of a diagnostic |
| `docs check` | A page of `docs/` whose front matter, sections, links, headings or code blocks are wrong |

#### Reading a diagnostic

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

#### Checking a snippet that is not a file yet

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

### Examples

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

#### Before a commit

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

### Related

- [The torb command](reference/tooling/the-torb-command.md) - every subcommand and its flags.
- [Command calls](reference/language/syntax/command-calls.md) - the canon that `canon --check` decides.
- [What a model trained on other languages gets wrong](reference/explanation/mistakes-models-make.md) - what to look for before
  running anything.
- [The toolchain](reference/tooling/index.md) - the other pages about the commands.

Source: `reference/tooling/verifying-your-work.md`

## Where to look things up

Read `reference/index.md` first: it is one file with every page of the documentation and a one-sentence
summary of each, so the right page can be chosen without opening any. The sections are:

| Section | What is in it |
|---------|---------------|
| `reference/explanation/index.md` | The arguments behind the decisions, and the contrast pages for people and models arriving from Rust, Swift, Kotlin or TypeScript. |
| `reference/guide/index.md` | The learning path from nothing to a working program, in order, one step per page. |
| `reference/how-to/index.md` | One page per task for somebody who already knows the language: the steps, the pitfalls, and one complete program that works. |
| `reference/language/index.md` | One page per construct of TorbScript, grouped by area, with the exact rules and the mistakes each construct invites. |
| `reference/standard-library/index.md` | One page per package of std, what each contains, and which of them are in scope everywhere without an import. |
| `reference/tooling/index.md` | The torb command, the project files, and how to verify that what you wrote is correct and in the formatter canon. |

Every page is self-contained: it defines or links every term it uses, so one page is enough to answer one
question. A page marked `status: draft` may still be wrong; verify it against the compiler.

There are 201 pages. Pages of features that are designed but not implemented are not part of
this skill at all, so everything in `reference/` is a feature that exists today.