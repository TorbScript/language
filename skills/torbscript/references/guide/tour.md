---
title: A tour of TorbScript
summary: The whole language in fifteen minutes for somebody who already programs - bindings, calls, functions, types, cases, errors, traits and pipelines, one short example each.
kind: guide
status: stable
order: 10
keywords:
  - overview
  - quick tour
  - mental model
  - value semantics
source:
  - CONCEPT.md#key-facts
  - CONCEPT.md#design-principles
---

You already know how to program. This tour shows what TorbScript looks like and where it is different from what you
know. Every example runs right in the page: change it and run it again.

## Goal

After this tour you can read TorbScript and write a small program in it, and you know which habits from other
languages will not compile.

## Bindings

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

## Calls without parentheses

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

## Functions and closures

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

## Cases and match

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

## No null: Option

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

## No exceptions: Result and ?

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

## Traits instead of inheritance

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

## Lists, maps and pipelines

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

## Files and packages

```trb
use File from "std/fs"
use greeting from "./greeting"

print greeting("World")
```

A top-level declaration is private to its file unless it says `public`. `use` brings a name in from another file or
from the standard library. `print`, `List`, `Option`, `Result` and the other basics need no `use`. A project is a folder
with a `project.trb`, and that manifest is TorbScript too.

## Habits that break

What you bring from other languages that does not compile here:

- `let` and `let mut` are `const` and `var`. There is no `null` and no `throw`: use `Option` and `Result`.
- `Err(e)` is `Fail(e)`. A case is `Shape.Circle` or `.Circle`, never `Shape::Circle` or a bare `Circle`.
- `class` and `extends` are `type` and a trait. `fn area(self)` is `fn area()`, and `mutating func` is `var fn`.
- `list.add(x)` is `list.append x`: a list appends, a set inserts, a map sets.
- `MAX_SIZE` is `maxSize`: the first letter of a name is a rule, uppercase only for types, traits and cases.
- A semicolon is an error, and a call at the start of a line drops its parentheses where it can: `print "hi"`.

Each of these, with the message the compiler prints, is on
[What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md).

## Next

- [Install and run](installing-and-running.md) - TorbScript on your own computer, and your first project.
- [Coming from another language](coming-from/index.md) - a table for Rust, TypeScript, Python, Go, Kotlin or Swift.
- [Syntax cheat sheet](../language/syntax/cheat-sheet.md) - every form of the language on one page, while you write.

