---
name: torbscript
description: "Writes, runs and checks TorbScript, the value-semantics language of `.trb` files, and finds or installs its `torb` toolchain. Use it for any task that touches `.trb` files, a `project.trb` or a `torb` command, and before writing TorbScript from memory: it looks like Rust, Swift and Kotlin but differs from all three in mutation, errors, cases and call syntax."
license: MIT
compatibility: "Runs the torb command in a shell. Installing it downloads the official installer from torb.dev."
---

# TorbScript

TorbScript is a functional-first language with value semantics. `torb` is its whole toolchain: it runs a program in its
VM, checks, formats and tests it, and builds a native binary through a C compiler. Source files end in `.trb`, and a
project is a directory with a `project.trb`. Paths in this skill are relative to the directory of this file.

## The toolchain

Before the first `torb` command of a task, run the check. It changes nothing:

```sh
sh scripts/toolchain.sh
```

In Windows PowerShell without a POSIX shell, run
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/toolchain.ps1` instead. Both print one line per fact and
exit 0 when `torb` runs, 1 when it does not:

```text
torb: /home/ada/.torb/bin/torb
version: 0.1.0
on-path: yes
c-compiler: clang
```

- `on-path: no` - call `torb` by the path the line names.
- `c-compiler: none` - `run`, `check`, `test` and `format` work; `torb build` and `--native` need a C compiler
  (`$TORB_CC`, or `clang`, `gcc`, `cc` on the `PATH`).
- `torb: missing` - install it, as below.

### Installing

**Ask the user before installing.** Installing downloads the official installer - `https://torb.dev/install.sh`, or
`install.ps1` on Windows - and runs it. The installer checks the archive against the release's SHA-256 sums and
unpacks the toolchain for the current user: into `~/.torb` on Linux, macOS and FreeBSD, into `%LOCALAPPDATA%\torb` on
Windows with the one in use in `%LOCALAPPDATA%\Programs\torb`. It takes the newest stable release, or the nightly while
there is no stable one yet. With the user's consent:

```sh
sh scripts/toolchain.sh --install
```

(`-Install` for the PowerShell script.) It runs the installer only when `torb` is missing or does not run, so a second
run changes nothing, and it prints the facts again when it is done. On Linux, macOS and FreeBSD nothing is written into
a shell profile: tell the user to add `export PATH="$HOME/.torb/bin:$PATH"` to theirs. On Windows the installer adds
its directory to the user's `PATH`, which a shell started before sees only after a restart. Later, `torb upgrade`
installs a newer release beside the current one.

## Starting and running

```text
torb new <name> --yes    A project: project.trb, src/main.trb, tests/main.test.trb
torb init --yes          The same, into the current directory
torb run [path]          Check and run a file, or the project's program, in the VM
torb test [path]         Run every *.test.trb file below the path
torb build [path]        A native binary through a C compiler, in the release profile
```

`new` and `init` start from a template of git.torb.dev (`--template app` for a program, `package` for a library) and
fall back to a built-in scaffold offline or with `--offline`. They run `git init` unless told `--no-git`, which is what
a project inside an existing repository wants. A `.trb` file that nothing imports is a script: `torb run hello.trb`
runs it without a project and without a `fn main`. [The torb command](references/tooling/the-torb-command.md) lists
every subcommand and flag.

## The language in sixty seconds

TorbScript looks like Rust, Swift and Kotlin and is none of them. Six ideas explain almost every line of it. Read this
before writing any TorbScript; each idea links to the page that has the exact rules.

### Goal

After this page you can read TorbScript, and you know which of your habits from other languages will produce code that
does not compile.

### Everything is a value, and the binding decides

There is one `Point`, not a `Point` and a `MutablePoint`. There is one `List`, not `List` and `MutableList`. A `const`
binding never changes and nothing below it changes; a `var` binding can be changed in place.

```trb run
const fixed = [1, 2]
var buffer = fixed
buffer.append 3
print "{fixed} {buffer}"
// prints [1, 2] [1, 2, 3]
```

That prints `[1, 2] [1, 2, 3]`. Assigning, passing and capturing a value is a **copy**, so two bindings never point at
the same thing and a change happens exactly where it is written. Mutation needs a `var` all the way down: a `var`
binding, `var` parameter or a `var fn` receiver, then `var` fields. See Bindings (skill `torbscript-language`: `references/language/values-and-types/bindings.md`) and
Why values instead of references (skill `torbscript-language`: `references/explanation/why-values-instead-of-references.md`).

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
    Some(line) => line
    None => ""
  }
}
```

`??` is the fallback (`findUser(1)?.name ?? "anonymous"`), `?.` maps over an `Option`. `panic "..."` is for bugs: it
prints to standard error and exits with 101, and nothing else runs. See Result (skill `torbscript-language`: `references/language/errors/result.md`).

### One keyword declares every data type

`type` is struct, class, enum and algebraic data type at once. Fields are `const` unless marked `var`; members are
public unless marked `private`. `Equals`, `Hash`, `Show` and `copy` are generated.

A member says what it is with two words in front of `fn`: nothing for a method, which does not list its receiver;
`var fn` for one that changes it; `static` for one that belongs to the type (`static fn square(size: Int): Self`,
`static origin = Point(0, 0)`). See Methods and `static fn`s (skill `torbscript-language`: `references/language/types/methods.md`).

```trb
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)

  fn area(): Float {
    match self {
      .Circle(radius) => Float.pi * radius ** 2
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
Cases and match (skill `torbscript-language`: `references/language/pattern-matching/cases-and-match.md`).

In a pattern the first letter decides: a lowercase name **binds**, an uppercase one is a case. That is why the first
letter of every declaration is a rule the compiler reports (`type Point`, `fn distance`, `const maxSize` - there is no
`MAX_SIZE`), and why a binding of a `match` arm, an `if const` or a `while const` that the arm never reads is an error:
write `_`, or `_name` to keep the name. See Naming (skill `torbscript-language`: `references/language/syntax/naming.md`) and
Pattern forms (skill `torbscript-language`: `references/language/pattern-matching/pattern-forms.md`).

### Capabilities are traits, and a type comes `with` them

There is no inheritance. A trait with one required method is named after that method - `Hash`, `Equals`, `Compare`,
`Show`, `Add`, `Close` - and a type says `with Hash`. Operators are traits: `+` is `Add.add`, `==` is `Equals.equals`,
`a[i]` is `Index.at`.

```trb
trait Area {
  fn area(): Float
}

type Square with Area {
  side: Float

  fn area(): Float {
    side * side
  }
}
```

`&` intersects traits (`fn audit(entry: Show & Encode)`), `where Item: Hash` bounds a type parameter, and `extend` adds
members to a type that already exists. See Traits (skill `torbscript-language`: `references/language/traits/traits.md`).

### A call is written as a command wherever it can be

`Ok value`, `print "hello"`, `return Fail problem`, `names.map Role`. Parentheses appear where the grammar needs them:
nested calls (`Ok Some(x)`), no arguments (`list.length()`), an operator at the top level of an argument
(`assert(sum == 3)`), several lines, and the head of an `if`, `for`, `while` or `match`. This is not a preference, it is
the formatter canon and `torb format --check` enforces it. See
Command calls (skill `torbscript-language`: `references/language/syntax/command-calls.md`).

Statements end at the end of the line. There are no semicolons, and two statements never share a line.

### Pipelines are lazy, and one vocabulary is shared

`map`, `filter`, `flatMap`, `take`, `sorted` and the rest return an `Iterate` and run nothing until a terminal
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

`map` and `flatMap` mean the same thing on `Option`, `Result`, `Task` and `Iterate`, by convention rather than through
higher-kinded types. A closure is always `{ parameters => body }`, and `_`, `_2` are its implicit parameters. See
the language reference (skill `torbscript-language`: `references/language/index.md`).

### What to carry over from other languages, and what not

The habits that break are one list, each with the diagnostic it produces:
[What a model trained on other languages gets wrong](references/explanation/mistakes-models-make.md). In short: `var` instead
of `let mut` and `mutating func`, a copy or a `var` parameter instead of a borrow, `Option` instead of `null`,
`Result` and `?` instead of exceptions, `Shape.Circle` instead of a bare case, `type` and traits instead of classes,
`f a, b` instead of `f(a, b)`, and no semicolons.

### Next

- [Run your first program](references/guide/installing-and-running.md) - from nothing to output.
- [Values and bindings](references/guide/values-and-bindings.md) - the mutation rules in full.
- [What a model trained on other languages gets wrong](references/explanation/mistakes-models-make.md) - the mistakes, with the
  diagnostics they produce.
- [Idiomatic TorbScript](references/guide/idiomatic-torbscript.md) - the habits the standard library follows once the code compiles:
  names, verbs and participles, capsules, `using`, tasks.
- The language reference (skill `torbscript-language`: `references/language/index.md`) - one page per construct.

## Two files to look things up in

- [references/language/syntax/cheat-sheet.md](references/language/syntax/cheat-sheet.md) - every form of the language
  with its exact spelling: how is this written?
- [references/explanation/mistakes-models-make.md](references/explanation/mistakes-models-make.md) - the lines that
  look right and are not, each with its diagnostic: why does this not compile?

Search them instead of reading them whole: `grep -n "^###" <file>` lists the sections of either one, and a section is a
few dozen lines. Before handing over code, scan the section headings of the second one.

## Verify your work

Never hand over TorbScript you have not run through the compiler. The language has a type checker, an exhaustiveness
check, a dead-change check and a formatter, and all four of them answer in seconds. A snippet that looks right and
was not checked is the most expensive thing you can produce.

### Synopsis

All of these take a file or a directory, and default to the current directory: run them in the directory of the
project, the one with `project.trb`, or name a single file.

```text
torb check <path>                   Type check: "no problems", or a line and a caret
torb parse <path>                   Syntax only, recursively
torb format --check <path>          Is it in the layout of the formatter?
torb format <path>                  ...write it
torb lint <path>                    The rules of style the checker leaves alone
torb test <path>                    Every *.test.trb file below the path
```

### What it does

#### The order to run them in

1. **`check`** first. It resolves every name and types every expression, so it finds the mistakes that matter: an
   undeclared name, a wrong type, a non-exhaustive `match`, a change that cannot be seen, a `var` that is missing.
2. **`format --check`** second. It reports every file whose call form, indentation, spaces, blank lines or line breaks
   are not in the layout. Run plain `format` to write it rather than fixing it by hand - it edits over the syntax tree
   and re-parses, so it cannot change what a program means.
3. **`test`** last, when there are tests. One program runs every test file, and a test that fails is reported and the
   next one runs.

A change is done when all three are green **and** `check` still answers `no problems` over the whole project. A false
positive of the checker is a bug in the checker.

#### What each one catches

| Command | Catches |
|---------|---------|
| `parse` | A semicolon, an unclosed brace, a command call in the wrong position, a `{` where a block was meant |
| `check` | An undeclared name, a wrong type, a missing `var`, a non-exhaustive `match`, an unreachable arm, a dead change, a discarded result, a visibility violation |
| `format --check` | A parenthesized call that should be a command, a command that should have parentheses, a multi-line string that is not indented, a line indented wrong, a missing or extra space, a second blank line, a line over 120 columns that a line break makes fit, a broken call that fits on one line again |
| `lint` | The own name of a type instead of `Self`, a `Bool` field named as a question, an unread binding, an unlabeled literal option |
| `test` | Everything a test asserts, including the exact text of a diagnostic |

#### Reading a diagnostic

A diagnostic points at a span and says one thing:

```text
error: Comparisons do not chain. Use `&&`: `a < b && b < c`
 --> src/main.trb:12:19
  |
12 | const chained = a < b > c
  |                       ^
```

One root cause, one message. When a message offers a fix, take it: the diagnostics of this language are written to
name the correct line rather than to describe the rule.

#### Checking a snippet that is not a file yet

Write it to a file and check the file. A `.trb` file that nothing imports is a script, so it may hold top-level code and
needs no `fn main`. A loose file, anywhere, is checked against the standard library of the `torb` that checks it and
built with that toolchain's C runtime; `TORB_STD=<path to std>` and `TORB_RUNTIME=<path to runtime>` point it at
others:

```console
$ torb check scratch.trb
1 file, no problems
$ torb run scratch.trb
```

`run` runs it in the VM at once; `run --native` builds it natively first, so its first run costs a C compile.

### Examples

A clean run over a project:

```console
$ torb check .
4 files, no problems
$ torb format --check .
0 of 4 files would change
$ torb test
tests/main.test.trb
  ok      greets by name

1 passed, 0 failed (1 file)
```

A run that found something:

```console
$ torb check scratch
error: `append` needs a `var`. Did you mean `appended`?
 --> scratch/src/main.trb:3:7
  |
3 | fixed.append 3
  |       ^^^^^^

1 problems in 1 of 1 files
```

### Related

- [The torb command](references/tooling/the-torb-command.md) - every subcommand and its flags.
- Command calls (skill `torbscript-language`: `references/language/syntax/command-calls.md`) - the rule of the canon `format --check` decides first.
- [What a model trained on other languages gets wrong](references/explanation/mistakes-models-make.md) - what to look for before
  running anything.
- [The toolchain](references/tooling/index.md) - the other pages about the commands.

## The other TorbScript skills

Load the one a task needs, when it needs it:

- `torbscript-concurrency` - Writes concurrent TorbScript: `Task`, `spawn` and `.await()`, cancellation and timeouts, `Channel`, the `Source` and `Sink` ends of a stream, and `parallel()` pipelines.
- `torbscript-language` - Looks up the exact rules of every TorbScript construct - types, traits, generics, patterns, closures, errors, collections, modules, configuration blocks - with the reasons behind them and recipes.
- `torbscript-networking` - Writes networked TorbScript: HTTP and HTTPS clients and servers with `std/http`, TCP and UDP with `std/network`, TLS, DNS, IP addresses and URIs.
- `torbscript-projects` - Sets up and maintains TorbScript projects: `project.trb` and `project.lock.trb`, dependencies (`torb add`, `remove`, `update`, `install`), workspaces, build profiles and native binaries, `torb doc`, and publishing to a registry.
- `torbscript-standard-library` - Looks up the TorbScript standard library: which `std/` package has a type or function, how it is imported and its exact API - text, numbers, collections, files and paths, JSON and YAML, regex, time, processes, hashing, compression and more.
- `torbscript-testing` - Writes and runs TorbScript tests: `*.test.trb` files, `test` and `group` from `std/test`, `assert` with its report of the source and the values, and `torb test` with filters, shards, JSON reports and native runs.

## What else this skill holds

- [Learn TorbScript](references/guide/index.md) - A guide for a programmer who already knows another language - a 15-minute tour, then one idea per page, finishable in an evening.
- [Coming from another language](references/guide/coming-from/index.md) - One page per language - a table of the 10 to 15 things that map directly, the 5 that will surprise you, and what is deliberately missing.
- [Task recipes](references/how-to/index.md) - One page per task for somebody who already knows the language: the steps, the pitfalls, and one complete program that works.
- [The toolchain](references/tooling/index.md) - The torb command, the project files, and how to verify that what you wrote is correct and in the formatter canon.

`references/index.md` lists every page of this skill with its summary: `grep -i "<word>" references/index.md`.
