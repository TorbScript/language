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

## A tour of TorbScript

You already know how to program. This tour shows what TorbScript looks like and where it is different from what you
know. Every example runs right in the page: change it and run it again.

### Goal

After this tour you can read TorbScript and write a small program in it, and you know which habits from other
languages will not compile.

### Bindings

```trb run
const fixed = [1, 2]
var buffer = fixed
buffer.append 3
print "{fixed} {buffer}"
// prints [1, 2] [1, 2, 3]
```

`const` never changes, `var` can. Giving a value a second name makes a copy, so changing `buffer` leaves `fixed` alone.
That holds for lists, maps and your own types too: two names never share one value, and a value changes only where you
change it. The copy is cheap, because the storage is shared until one side writes.

`const` goes all the way down: through a `const` you cannot change a field or call a method that changes the value.
That is why there is no `MutableList` - a `var` list is one. The exception is a `shared type`, such as a file or a
socket: it has an identity, and a second name points at the same one.

### Calls without parentheses

```trb run
print "Hello"
print "one", "two"
const count = [1, 2, 3].length()
print(count + 1)
// prints Hello
// prints one two
// prints 4
```

A call at the start of a line, after `=`, after `return` and after `=>` is written without parentheses:
`print "Hello"`, `return Fail problem`. The parentheses stay where they are needed:

- the call has no arguments: `list.length()`
- the call sits inside another call: `print area(3, 4)`
- an argument has an operator at its top level: `print(count + 1)`
- the call is the condition of an `if`, `for`, `while` or `match`

You do not have to remember this: `torb format` writes it for you. A statement ends at the end of its line. There are
no semicolons, and two statements never share a line.

### Functions and closures

```trb run
fn area(width: Int, height: Int): Int {
  width * height
}

const numbers = [1, 2, 3]
const total = numbers.fold 0 { sum, number => sum + number }
print area(3, 4)
print numbers.map({ _ * 2 }).toList()
print total
// prints 12
// prints [2, 4, 6]
// prints 6
```

Every parameter has a type. The result type is inferred from the last line of the body, and a `public` function
writes it out. A closure is always `{ parameters => body }`, and `_` and `_2` stand for the first and second parameter
when you leave the list out. When the closure is the last argument, it follows the call: `numbers.fold 0 { ... }`.

### Types and methods

```trb run
type Rectangle {
  var width: Int
  height: Int

  fn area(): Int {
    width * height
  }

  var fn grow(by: Int) {
    width = width + by
  }

  static fn square(size: Int): Self {
    Self size, size
  }
}

var box = Rectangle 3, 4
box.grow 1
print box
print Rectangle.square(2).area()
// prints Rectangle(width: 4, height: 4)
// prints 4
```

`type` is the one keyword for data: struct, class and record in one. A field is `const` unless it says `var`. The
constructor, `==`, hashing, printing and `copy` come for free. A method does not list `self`: the word in front of `fn`
says what it is - nothing for a method that only reads, `var fn` for one that changes the value, `static fn` for one
that belongs to the type.

### Cases and match

```trb run
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)

  fn area(): Float {
    match self {
      .Circle(radius) => 3.0 * radius * radius
      .Rectangle(width, height) => width * height
    }
  }
}

const shapes = [Shape.Circle(1.0), Shape.Rectangle(2.0, 3.0)]
for shape in shapes {
  print shape.area()
}
// prints 3.0
// prints 6.0
```

A `type` can list `case`s: the shapes a value can take. This covers what other languages call an enum or a sealed
class. A case is written with its type, `Shape.Circle`, or with a dot, `.Circle`, where the type is already clear. It is
never bare, except `Some`, `None`, `Ok` and `Fail`, which every file has.

A `match` must handle every case. Add a `.Triangle` later, and every `match` on `Shape` becomes a compile error at the
line that needs a new arm. In a pattern, a lowercase name like `radius` takes the value, and an uppercase one is a
case. A name that the arm never uses is an error: write `_` instead.

### No null: Option

```trb run
type User {
  id: Int
  name: String
}

const users = [User(1, "Ada"), User(2, "Grace")]

fn findUser(id: Int): User? {
  users.find { _.id == id }
}

print(findUser(2)?.name ?? "nobody")
print(findUser(9)?.name ?? "nobody")
// prints Grace
// prints nobody
```

There is no `null`. A value that may be missing has the type `User?`, and the compiler makes you handle the missing
case. `?.` reaches into the value if there is one, and `??` gives the fallback if there is none.

### No exceptions: Result and ?

```trb run
fn parsePort(text: String): Result<Int, String> {
  const port = Int.tryFrom(text).mapError({ _ => "{text} is not a number" })?
  if port < 1 || port > 65535 {
    return Fail "{port} is not a port"
  }
  port
}

print parsePort("8080")
match parsePort("nope") {
  Ok(port) => print "listening on {port}"
  Fail(problem) => print "cannot start: {problem}"
}
// prints Ok(8080)
// prints cannot start: nope is not a number
```

A function that can fail says so in its type: `Result<Int, String>` is either `Ok` with an `Int` or `Fail` with a
`String`. The `?` after a call hands a `Fail` straight back to the caller, so one character replaces the error check
after every call. When the caller fails with another error type, `?` converts the error through `From`. The body ends
in `port`, not `Ok(port)`: the value is wrapped for you.

`panic "..."` is for bugs, not for bad input. It stops the program with exit code 101, and nothing can catch it.

### Traits instead of inheritance

```trb run
trait Area {
  fn area(): Float
}

type Square with Area {
  side: Float

  fn area(): Float {
    side * side
  }
}

type Circle {
  radius: Float
}

extend Circle with Area {
  fn area(): Float {
    3.0 * radius * radius
  }
}

const shapes: List<Area> = [Square(2.0), Circle(1.0)]
for shape in shapes {
  print shape.area()
}
// prints 4.0
// prints 3.0
```

There are no classes and no inheritance. What a type can do is a `trait`, and a type gets one with `with` or later with
`extend`. A trait with one method is named after it: `Hash`, `Equals`, `Show`, never `Hashable`. Operators are traits
too: `+` is `Add`, `==` is `Equals`. A trait works as a type, so one list can hold a `Square` and a `Circle`, and `&`
joins two of them: `Show & Hash`.

### Lists, maps and pipelines

```trb run
const names = ["Ada", "Alan"]
const more = names.appended "Grace"
var ages = ["Ada": 36, "Grace": 45]
ages["Alan"] = 41
print "{names} {more}"
print ages
print more.filter({ _.byteLength() > 3 }).map({ _.toUpperCase() }).toList()
// prints ["Ada", "Alan"] ["Ada", "Alan", "Grace"]
// prints ["Ada": 36, "Grace": 45, "Alan": 41]
// prints ["ALAN", "GRACE"]
```

A method that changes a value in place is a verb and needs a `var`: `append`, `sort`, `remove`. Its twin that returns a
changed copy is a participle and works on a `const`: `appended`, `sorted`, `removed`. `map`, `filter` and the other
pipeline steps are lazy: nothing runs until a last step such as `toList()` pulls the values through. `map` and
`flatMap` mean the same on an `Option`, a `Result` and a `Task`.

### Files and packages

```trb
use File from "std/fs"
use greeting from "./greeting"

print greeting("World")
```

A top-level declaration is private to its file unless it says `public`. `use` brings a name in from another file or
from the standard library. `print`, `List`, `Option`, `Result` and the other basics need no `use`. A project is a folder
with a `project.trb`, and that manifest is TorbScript too.

### Habits that break

What you bring from other languages that does not compile here:

- `let` and `let mut` are `const` and `var`. There is no `null` and no `throw`: use `Option` and `Result`.
- `Err(e)` is `Fail(e)`. A case is `Shape.Circle` or `.Circle`, never `Shape::Circle` or a bare `Circle`.
- `class` and `extends` are `type` and a trait. `fn area(self)` is `fn area()`, and `mutating func` is `var fn`.
- `list.add(x)` is `list.append x`: a list appends, a set inserts, a map sets.
- `MAX_SIZE` is `maxSize`: the first letter of a name is a rule, uppercase only for types, traits and cases.
- A semicolon is an error, and a call at the start of a line drops its parentheses where it can: `print "hi"`.

Each of these, with the message the compiler prints, is on
[What a model trained on other languages gets wrong](references/explanation/mistakes-models-make.md).

### Next

- [Install and run](references/guide/installing-and-running.md) - TorbScript on your own computer, and your first project.
- [Coming from another language](references/guide/coming-from/index.md) - a table for Rust, TypeScript, Python, Go, Kotlin or Swift.
- [Syntax cheat sheet](references/language/syntax/cheat-sheet.md) - every form of the language on one page, while you write.

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

- [Learn TorbScript](references/guide/index.md) - For people who already program - a fifteen-minute tour, then one short page per idea, all of it done in an evening.
- [Coming from another language](references/guide/coming-from/index.md) - One page per language - a table of the 10 to 15 things that map directly, the 5 that will surprise you, and what is deliberately missing.
- [Task recipes](references/how-to/index.md) - One page per task for somebody who already knows the language: the steps, the pitfalls, and one complete program that works.
- [The toolchain](references/tooling/index.md) - The torb command, the project files, and how to verify that what you wrote is correct and in the formatter canon.

`references/index.md` lists every page of this skill with its summary: `grep -i "<word>" references/index.md`.
