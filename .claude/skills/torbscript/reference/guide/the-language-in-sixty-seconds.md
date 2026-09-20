---
title: The language in sixty seconds
summary: "The mental model of TorbScript in one screen: values, bindings, no null, no exceptions, traits, and calls written as commands."
kind: guide
status: stable
order: 0
skill: model
keywords:
  - overview
  - mental model
  - value semantics
source:
  - CONCEPT.md#key-facts
  - CONCEPT.md#design-principles
---

TorbScript looks like Rust, Swift and Kotlin and is none of them. Six ideas explain almost every line of it. Read this
before writing any TorbScript; each idea links to the page that has the exact rules.

## Goal

After this page you can read TorbScript, and you know which of your habits from other languages will produce code that
does not compile.

## Everything is a value, and the binding decides

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
binding, `var` parameter or `var self`, then `var` fields. See [Bindings](../language/values-and-types/bindings.md) and
[Why values instead of references](../explanation/why-values-instead-of-references.md).

The one exception is a `shared type`, which has an identity: assigning it does not copy. Files, sockets and channels are
shared types; almost nothing else is.

## There is no null and there are no exceptions

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
prints to standard error and exits with 101, and nothing else runs. See [Result](../language/errors/result.md).

## One keyword declares every data type

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
[Cases and match](../language/pattern-matching/cases-and-match.md).

## Capabilities are traits, and a type comes `with` them

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
members to a type that already exists. See [Traits](../language/traits/traits.md).

## A call is written as a command wherever it can be

`Ok value`, `print "hello"`, `return Fail problem`, `names.map Role`. Parentheses appear where the grammar needs them:
nested calls (`Ok Some(x)`), no arguments (`list.length()`), an operator at the top level of an argument
(`assert(sum == 3)`), several lines, and the head of an `if`, `for`, `while` or `match`. This is not a preference, it is
the formatter canon and `torb canon --check` enforces it. See
[Command calls](../language/syntax/command-calls.md).

Statements end at the end of the line. There are no semicolons, and two statements never share a line.

## Pipelines are lazy, and one vocabulary is shared

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
[the language reference](../language/index.md).

## What to carry over from other languages, and what not

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

## Next

- [Run your first program](installing-and-running.md) - from nothing to output.
- [Values and bindings](values-and-bindings.md) - the mutation rules in full.
- [What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md) - the mistakes, with the
  diagnostics they produce.
- [The language reference](../language/index.md) - one page per construct.
