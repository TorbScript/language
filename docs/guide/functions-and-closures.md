---
title: Functions and closures
summary: Declare a function with typed parameters, defaults and labels, write a closure, and pass it as the last argument of a call.
kind: guide
status: stable
order: 40
prerequisites:
  - values-and-bindings.md
keywords:
  - fn
  - closure
  - default value
  - label
  - trailing closure
source:
  - CONCEPT.md#functions
  - examples/tour/src/02-functions.trb
---

A function is declared with `fn`. A closure is a piece of code you can store in a binding or hand to another function.

## Goal

At the end of this page you can declare a function with defaults and labels, write a closure, and pass one to a call.

## Declare a function

```trb run
fn distance(x: Int, y: Int): Int {
  const dx = x * x
  const dy = y * y
  dx + dy
}

print distance(3, 4)
// prints 25
```

Every parameter has a type. The result is the last line of the body, and its type can be left out: the compiler
works it out. A `public` function writes it, so a caller in another file sees it without reading the body. A function
can be called above the line it is declared on.

## Defaults and labels

```trb run
fn connect(host: String, port: Int = 5432, timeout: Int = 30): String {
  "{host}:{port}, timeout {timeout}s"
}

print connect("localhost")
print connect("localhost", 3306)
print connect("localhost", timeout: 10)
// prints localhost:5432, timeout 30s
// prints localhost:3306, timeout 30s
// prints localhost:5432, timeout 10s
```

A parameter with a default can be left out. A call can name a parameter, `timeout: 10`, to skip the ones before it.

## Closures

```trb run
const double = { x: Int => x * 2 }
const triple: (Int) => Int = { _ * 3 }

print double(21)
print triple(7)
// prints 42
// prints 21
```

A closure is always written `{ parameters => body }`. When the type is clear from where the closure goes, you can leave
the parameters out and write `_` for the first one. A `{` where a value is expected is always a closure, never a
block. `return` inside a closure leaves the closure, not the function around it.

## The last argument can follow the call

```trb run
const numbers = [1, 2, 3, 4]
const doubled = numbers.map { _ * 2 }
const total = numbers.fold 0 { sum, number => sum + number }

print doubled.toList()
print total
// prints [2, 4, 6, 8]
// prints 10
```

When the last parameter is a function, the closure can stand after the call instead of inside the parentheses. This is
how `test "name" { ... }` and your own control structures read like built-in syntax. A call without arguments still
needs `()`: `distance` alone is the function itself, not a call.

## Next

- [Types and methods](types-and-methods.md) - your own types, and functions that belong to them.
- [Declaring a function](../language/functions/declaring-a-function.md) - the exact rules.
- [Closures](../language/functions/closures.md) - what a closure captures, and when.
