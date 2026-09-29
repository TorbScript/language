---
title: Void and Never
summary: Void has exactly one value, the keyword literal void, the way true and false are the values of Bool; Never has no value at all and converts to every type, which is why panic fits into any expression.
kind: reference
status: stable
order: 18
keywords:
  - void
  - Never
  - result type
  - panic
source:
  - CONCEPT.md#built-in-types
  - std/core/src/void.trb
---

`Void` and `Never` sit at opposite ends of "how many values does this type have": `Void` has exactly one, written
`void`, and `Never` has none at all. A function without a result type returns `Void`; `panic`, `return`, `break` and
`continue` all have type `Never`.

## Example

```trb check
fn setUp(): Void {
  print "ready"
}

fn start(): Void {
  setUp()
  void
}

fn fail(message: String): Never {
  panic message
}

fn firstOf(values: List<Int>): Int {
  values.first() ?? fail("empty")
}

start()
print firstOf([1, 2, 3])
```

## Syntax

```text
fn f(): Void { ... }                     the same as fn f() { ... }: no result type means Void
void                                     the one value of Void
fn f(): Never { ... }                    a function that never returns normally
```

## Rules

1. **`Void` has exactly one value, the keyword literal `void`**, the way `true` and `false` are the values of `Bool`.
   A function without a result type returns `Void`, and a block that ends in a statement has the value `void`.

2. **`Void` is a type, and writing the type name where a value is expected is an error.** The value is the lowercase
   keyword `void`; the uppercase `Void` names the type and is never itself a value.

   ```trb error
   const x = Void
   // error: `Void` is a type, not a value: its one value is written `void`
   ```

3. **`Never` is the type of an expression that does not return: `panic`, `return`, `break`, `continue`.** It has no
   value at all, which is why an expression of type `Never` fits into any context that expects a value.

   A **`loop` without a `break`** is the one *statement* with that type: nothing after it is reached, and a function
   whose whole body is one needs no other result. With a `break` it is `Void` like every other loop. See
   [Loops](../execution/loops.md).

   ```trb check
   fn serve(): Int {
     loop {
       print "waiting"
     }
   }
   ```

4. **`Never` converts to every type.** `values.first() ?? fail("empty")` type checks as the element type of
   `values`, because `fail`'s `Never` result converts to whatever the other side of `??` needs.

5. **An expression statement has to have type `Void` or `Never`, unless the call has a `var` receiver or a `var`
   argument.** `setUp()` above is a statement because it returns `Void`; a call that returns something else and is
   never used is a compile error (see [Bindings](bindings.md), rule 8).

6. **`Void`'s `Show` text is `void`, the same word the literal is written with.** `print setUp()` prints `void`, the
   same as printing the literal directly would.

## What this is not

**`Void` is not the absence of a value the way `Option.None` is.** `Void` always has its one value, `void`; there is
nothing to be absent. A function that may or may not produce a value returns `Value?`, not `Void`.

```trb check
fn logged(): Void {
  print "done"
}

logged()
```

```trb error
fn logged(): Void {
  print "done"
}

const result = Void
print result
// error: `Void` is a type, not a value: its one value is written `void`
```

**`Never` is not `Void` with a stricter name.** `Void` is a real value a caller receives; `Never` is a promise that
the call never returns to its caller at all. A function that sometimes returns and sometimes panics is typed by what
it returns when it does, not by `Never`.

## Related

- [Bindings](bindings.md) - the expression-statement rule `Void`/`Never` decides.
- [Loops](../execution/loops.md) - the `loop` whose type is `Never`, and the `break` that makes it `Void`.
- [Option](option.md) - the type for a value that may be absent.
- [Result](../errors/result.md) - `Ok`/`Fail`, for a call that fails instead of panicking.

