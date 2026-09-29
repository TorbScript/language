---
title: TorbScript in 15 minutes
summary: The fastest honest tour of TorbScript for a working programmer, one short example and a few sentences per idea.
kind: guide
status: stable
order: 5
keywords:
  - overview
  - quick tour
  - bindings
  - toolchain
source:
  - CONCEPT.md#key-facts
  - CONCEPT.md#design-principles
---

You already know how to program. This page is everything about TorbScript that is not just "what you already know with
different keywords" - read it once, then write real code and look things up as you go.

## Goal

At the end of this page you can write an ordinary TorbScript program - bindings, functions, a type, a `match`, a
`Result`, a pipeline - and you know which command to run for what.

## Bindings and mutation

```trb run
const answer = 42
var counter = 0
counter = counter + 1

var list = [1, 2]
const frozen = list
list.append 3
print "{counter} {list} {frozen}"
// prints 1 [1, 2, 3] [1, 2]
```

`const` never changes; `var` can. That is the whole story - there is no second `MutableList` type to reach for.
Assigning, passing or capturing a value always **copies** it (value semantics (skill `torbscript-language`: `references/glossary.md`)), so
`frozen` above never sees the `append`. There is no borrow checker to satisfy, and no reference to accidentally share:
the trade is that a copy happens on every assignment, though the compiler skips the actual copying where nothing could
tell the difference.

## Functions and command calls

```trb run
fn area(width: Int, height: Int): Int {
  width * height
}

print area(3, 4)
print "{[1, 2, 3].map({ _ * 2 }).toList()}"
// prints 12
// prints [2, 4, 6]
```

A parameter always has a type; the return type is inferred from the last expression unless the function is `public`.
The bigger difference is how a call is written: `print area(3, 4)` has no parentheses around `print`'s own call, because
a call drops them wherever the grammar allows it - this is enforced by `torb format`, not a matter of taste. A nested
call, an argument starting with `[` or a trailing closure still needs its parentheses, as the second line shows.

## Types and methods

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
}

var box = Rectangle 3, 4
box.grow 1
print box.area()
// prints 16
```

One keyword, `type`, is struct, class and record at once - there is no separate `class`. A method never lists its
receiver (no `self` parameter); two words in front of `fn` say what kind of member it is: nothing for a plain method,
`var fn` for one that changes the receiver in place, `static fn` for one that belongs to the type itself. Fields and
methods generate `Equals`, `Hash` and `Show` for free, which is why `print` above needs no extra code.

## Cases and match

```trb run
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
}

fn area(shape: Shape): Float {
  match shape {
    .Circle(radius) => 3.14159 * radius * radius
    .Rectangle(width, height) => width * height
  }
}

print area(Shape.Circle(2.0))
// prints 12.56636
```

A `type` can carry `case`s instead of, or next to, fields - one declaration covers what other languages split into an
`enum` and a sealed hierarchy. `match` is an expression and has to cover every case: add `.Triangle` later and every
`match` on `Shape` becomes a compile error at the exact line that needs one more arm, instead of a silent wrong answer.
A case is written with its type, or `.Circle` where the type is already known - never bare, unless the file imports it.

## Errors as values, and `?`

```trb run
fn parsePort(text: String): Result<Int, String> {
  const port = Int.tryFrom(text).mapError({ _ => "{text} is not a number" })?
  if port < 1 || port > 65535 {
    return Fail "{port} is not a port"
  }
  port
}

print parsePort("8080")
print parsePort("nope")
// prints Ok(8080)
// prints Fail("nope is not a number")
```

There are no exceptions. A function that can fail answers `Result<Value, Failure>`, whose cases are `Ok` and `Fail`.
`?` on its own is the whole story for propagating one: it unwraps an `Ok`, or returns the `Fail` right away, converting
the error type on the way if the target type says how. `panic "..."` exists too, but it is for a bug, not for bad
input - it exits the process and cannot be caught.

## No null: `Option`

```trb run
type User {
  id: Int
  name: String
}

const users = [User(1, "Ada"), User(2, "Grace")]

fn findUser(id: Int): User? {
  users.find({ _.id == id })
}

print(findUser(2)?.name ?? "nobody")
print(findUser(9)?.name ?? "nobody")
// prints Grace
// prints nobody
```

`Value?` is `Option<Value>`, and it is the only way to say "maybe nothing" - there is no `null` and no pointer that
happens to be empty. `?.` maps over the `Option` instead of unwrapping it, and `??` supplies the fallback for `None`.

## Collections and pipelines

```trb run
const employees = [("Ada", 36), ("Alan", 41), ("Grace", 45)]

const names = employees
  .filter({ _.1 >= 40 })
  .map({ _.0 })
  .toList()

print names
// prints ["Alan", "Grace"]
```

`List`, `Map` and `Set` are values like everything else, so the mutation rule above applies to them unchanged: a verb
(`append`, `insert`, `set`) changes in place and needs a `var`, its participle (`appended`, `inserted`) answers a
changed copy. `map`, `filter` and the rest of a pipeline are lazy - they build an `Iterate` and run nothing until a
terminal operation such as `toList()` pulls the values through, so stages compose without a hidden intermediate list.

## Traits

```trb run
trait Area {
  fn area(): Float
}

type Square {
  side: Float
}

extend Square with Area {
  fn area(): Float {
    side * side
  }
}

const shapes: List<Area> = [Square(2.0)]
print shapes[0].area()
// prints 4.0
```

There is no inheritance. A capability is a `trait`, given to a type at its declaration (`type Square with Area`) or
afterwards with `extend`, as above. A trait with exactly one required method is named after that method - `Hash`,
`Equals`, `Show` - and operators are traits too: `+` is `Add.add`, `==` is `Equals.equals`. A trait can stand wherever
a type can (`List<Area>`), which is how one collection holds several concrete types behind one capability.

## Concurrency, in short

A function that waits answers `Task<Value>`; `.await()` gets the value back, and a cancellation stops whoever is
waiting rather than being caught as an error. `numbers.parallel()` runs an ordinary pipeline over the machine's
worker threads and still hands back results in input order - that much already runs today. `Channel` and a full
async `Stream` are designed (see Concurrency and streams (skill `torbscript-concurrency`: `references/language/concurrency-and-streams/index.md`)) but no
back end runs them yet, so `Task`, `await()` and `parallel()` are the whole story for now.

## Modules and `project.trb`

```trb
public fn greeting(name: String): String {
  "Hello, {name}!"
}
```

```trb skip a second file's declaration, shown only to name the import that reaches it
use greeting from "./greeting"

print greeting("World")
```

A top-level declaration is private to its file unless marked `public`; `use` brings a name in from a relative path or
from the standard library (`use File from "std/fs"`), never a bare unqualified import of everything. `project.trb` is
the manifest, and it is TorbScript itself, run in a sandbox that can only read files below its own directory:

```trb fragment
name = "hello"
version = "0.1.0"
```

## The toolchain

One binary, `torb`, does all of this:

| Command | What it does |
|---|---|
| `torb run <file>` | Checks and runs a file in the VM at once - no C compiler needed |
| `torb run --native`, `torb build` | Compiles to a native binary first (`build` always does; `run` only with `--native`) |
| `torb test` | Runs every `*.test.trb` file below the given paths |
| `torb format`, `torb format --check` | Writes the one layout of the language, or reports what is not in it |
| `torb lsp` | The language server: diagnostics, hover, go to definition, completion |
| `torb debug` | The debugger: breakpoints, stepping, locals, evaluate |

`torb check` and `torb format --check` are the two commands to run before you consider anything finished. See
[Verify your work](../tooling/verifying-your-work.md) for the full list and the order to run them in.

## Next

- [Values and bindings](values-and-bindings.md) and the rest of this path - the same ideas, one page each, slower and
  with the exact rules.
- [Syntax cheat sheet](../language/syntax/cheat-sheet.md) - every form of the language, at a glance, while you write.
- [Coming from Rust, TypeScript, Python, Go, Kotlin or Swift](coming-from/index.md) - what maps directly from your
  language and what will surprise you.
- [What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md) - the habits that look
  right and are not.

