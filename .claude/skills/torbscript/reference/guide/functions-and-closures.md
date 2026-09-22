---
title: Functions and closures
summary: How to declare a function, when it must spell out its return type, and the one closure form the language has.
kind: guide
status: stable
order: 30
prerequisites:
  - values-and-bindings.md
keywords:
  - fn
  - closure
  - default value
  - variadic
  - trailing closure
source:
  - CONCEPT.md#functions
  - examples/tour/src/02-functions.trb
---

A function is declared with `fn`, and a closure is the one place a value is a piece of code instead of data. This page
gets you from a first `fn` to passing a closure the way the rest of this language expects.

## Goal

At the end of this page you can declare a function with defaults and labels, write a closure, and hand one to another
function as a trailing closure.

## Declaring a function

```trb
fn distance(x: Int, y: Int): Int {
  const dx = x * x
  const dy = y * y
  dx + dy
}

print distance(3, 4)
```

Every parameter needs a type; the return type does not, and the compiler infers it from the last expression of the
body. A `fn` is [hoisted](../language/functions/declaring-a-function.md): it can be called above the line it is
declared on, which is what lets two functions call each other.

```trb
fn isEven(n: Int): Bool {
  if n == 0 { true } else { isOdd(n - 1) }
}

fn isOdd(n: Int): Bool {
  if n == 0 { false } else { isEven(n - 1) }
}

print isEven(10)
```

A `public` function is the one case where the return type has to be written even when it could be inferred: a caller
outside the file must not have to read the body to know what comes back.

## Defaults and labels

```trb
fn connect(host: String, port: Int = 5432, timeout: Int = 30): String {
  "{host}:{port} (timeout {timeout}s)"
}

print connect("localhost")
print connect("localhost", 3306)
print connect("localhost", timeout: 10)
```

`port` and `timeout` are filled from their defaults when the call omits them, and a call can name any later parameter
by its [label](../language/functions/arguments.md) instead of its position. A default is evaluated at every call that
needs it, so `limits(memory: Int = 64.megabytes())` runs `64.megabytes()` again each time. See
[Default values](../language/functions/default-values.md) for the exact scope it runs in.

A trailing `...name: Type` parameter is a [variadic parameter](../language/functions/variadics.md): it collects every
remaining positional argument into a `List`.

```trb
fn sumAll(...numbers: Int): Int {
  numbers.fold 0 { a, b => a + b }
}

print sumAll(1, 2, 3)
```

A collection is never unpacked into it by itself; spreading it with `...` is what does that (`sumAll(1, ...someList)`).

## The one closure form

A `{` in expression position is always a closure, never a block. It captures the scope it is written in.

```trb
const double = { x: Int => x * 2 }
const triple: (Int) => Int = { _ * 3 }

print double(21)
print triple(7)
```

`double` writes out the parameter's type; `triple` leaves it out because the binding's own type annotation already says
what a value passed to the closure has to be, and then `_` is the implicit first parameter. Both forms are the same
[closure](../language/functions/closures.md). `return` inside a closure returns from the closure, never from the
function around it.

Where a return type has to be spelled out, or the closure must call itself, a local `fn` is the tool instead - it is
the same declaration a top-level `fn` is, so it can be passed by name:

```trb
fn fib(n: Int): Int {
  if n < 2 { n } else { fib(n - 1) + fib(n - 2) }
}

print([1, 2, 3, 4, 5].map(fib).toList())
```

## Passing a closure as the last argument

When the last parameter of a call is a function, the closure can follow the call instead of sitting inside its
parentheses - a [trailing closure](../language/functions/trailing-closures.md).

```trb
const numbers = [1, 2, 3, 4]
const doubled = numbers.map { _ * 2 }
const total = numbers.fold 0 { sum, number => sum + number }

print doubled.toList()
print total
```

`map` names its implicit closure parameter after the parameter of its own signature, so `numbers.map { value * 2 }`
also compiles. A call with no arguments still needs `()`: a bare name such as `distance` refers to the function itself
rather than calling it.

## Next

- [Types and methods](types-and-methods.md) - declaring a type and giving it behaviour.
- [Declaring a function](../language/functions/declaring-a-function.md) - the exact rules, including hoisting and
  inference.
- [Parameter modes](../language/functions/parameter-modes.md) - `var`, `lazy`, receiver closures and
  `Expression<Value>` parameters.

