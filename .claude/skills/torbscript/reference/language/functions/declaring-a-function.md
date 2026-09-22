---
title: Declaring a function
summary: fn declares a function with a mandatory parameter type on every parameter; the last expression of the body is the result, and a public function or a trait method must always spell out its return type.
kind: reference
status: stable
order: 10
keywords:
  - fn
  - hoisting
  - return type
  - public function
source:
  - CONCEPT.md#functions
  - examples/tour/src/02-functions.trb
---

`fn` declares a function. A parameter always carries its type, the last expression of the body is what the function
answers, and `return` leaves the function early with a value.

## Example

```trb check
fn isOdd(n: Int): Bool {
  if n == 0 { false } else { isEven(n - 1) }
}

fn isEven(n: Int): Bool {
  if n == 0 { true } else { isOdd(n - 1) }
}

fn factorial(n: Int): Int {
  if n <= 1 {
    return 1
  }
  n * factorial(n - 1)
}

print isOdd(7)
print factorial(5)
```

## Syntax

```text
fn <name>[<type parameters>](<parameters>) [: <ReturnType>] {
  <statement>*
  <result expression>?
}
```

## Rules

1. **Every parameter has a written type.** `fn`, unlike a closure, never infers a parameter's type from how it is
   called.

2. **The result type may be left out on a function that is not `public` and is not a trait method.** The compiler
   infers it from the body.

   ```trb check
   fn double(x: Int) {
     x * 2
   }

   print double(21)
   ```

3. **A `public` function or a trait method never infers its result type.** Its signature is the only thing a caller
   in another package reads, so it has to be complete without the body.

   ```trb error
   public fn double(x: Int) {
     x * 2
   }
   // error: A `public` function does not infer its result: declare it (`: Int64`)
   ```

4. **The last expression of the body is the result, and `return` leaves early with a value.** A function whose body
   ends in a statement rather than an expression answers `void`, the one value of `Void`.

5. **A top-level `fn` is hoisted to the top of its file.** A function may call another one declared later in the same
   file, which is what makes mutual recursion possible there without a forward declaration. A local `fn`, nested
   inside another function, is not hoisted this way: it sees only its own parameters and the top level of its file
   (see [What this is not](#what-this-is-not)), not a sibling local `fn` declared next to it.

   ```trb check
   fn ping(n: Int): Int {
     if n <= 0 { 0 } else { pong(n - 1) }
   }

   fn pong(n: Int): Int {
     if n <= 0 { 0 } else { ping(n - 1) }
   }

   print ping(4)
   ```

## What this is not

**A local `fn` is not a closure.** It captures nothing: it sees only its own parameters and the top level of its
file, exactly like a function declared outside any block. Write a closure when the code needs to reach into the
surrounding scope (see [Closures](closures.md)).

```trb check
fn makeAdder(step: Int): (Int) => Int {
  { value => value + step }
}

print makeAdder(3)(4)
```

```trb error
fn makeAdder(step: Int): (Int) => Int {
  fn add(value: Int): Int {
    value + step
  }
  add
}
// error: Cannot find `step` here
```

## Related

- [Arguments and labels](arguments.md) - positional and labelled parameters, and the order they are written in.
- [Closures](closures.md) - the one closure form, and what it captures instead.
- [Bindings](../values-and-types/bindings.md) - the same `const`/`var` split applies to a parameter.

