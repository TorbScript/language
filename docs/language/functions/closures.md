---
title: Closures
summary: A brace in expression position is always a closure, its parameters are inferred from the expected type or written out, and it captures a const binding as a copy and a var binding as a box shared with its scope.
kind: reference
status: stable
order: 50
keywords:
  - closure
  - lambda
  - implicit parameter
  - capture
source:
  - CONCEPT.md#lambdas-and-closures
  - examples/tour/src/02-functions.trb
---

There is one closure form. A `{` in expression position is always a closure, never a block, and it captures the
names of the scope it was written in.

## Example

```trb check
const double = { x: Int => x * 2 }
const triple: (Int) => Int = { _ * 3 }
const add: (Int, Int) => Int = { a, b => a + b }

print double(4)
print triple(4)
print add(2, 3)
```

## Syntax

```text
{ <parameter>[: <Type>], ... => <statement>* <result expression>? }
{ <statement>* <result expression>? }              // Implicit parameters _, _2, _3, ...
```

## Rules

1. **A `{` in expression position is always a closure.** There is no block expression, so `{ 1 }` is a closure of no
   parameters that answers `1`, never a scope that runs and discards its own bindings.

2. **A parameter's type is inferred from the expected type, and has to be written out where there is none.** `const
   double = { x: Int => x * 2 }` needs the annotation; `const double: (Int) => Int = { x => x * 2 }` does not, because
   the binding's type says what `x` is.

   ```trb error
   const double = { x => x * 2 }
   // error: The parameters of this closure need types: `{ value: Int => ... }`
   ```

3. **A closure with no written parameter list uses the implicit parameters `_`, `_2`, `_3`, one per position.** `{ _ * 3 }` is
   a closure of one parameter, `{ _ + _2 }` of two. Naming even one parameter switches the whole closure to named
   parameters; `_` and a name cannot mix in the same closure.

4. **The result type is always inferred, never annotated.** A closure that needs a written result type is a local
   `fn` instead, because `fn` is a declaration and can carry a return type (see
   [Declaring a function](declaring-a-function.md)); a `fn` declared by name can be passed exactly where a closure is
   expected.

5. **`return` inside a closure returns from the closure, not from the function around it.** There is no non-local
   return: the value `return` produces is the closure's own result, at the point the closure is called.

   ```trb check
   const clamp = { x: Int, low: Int, high: Int =>
     if x < low { return low }
     if x > high { return high }
     x
   }

   print clamp(15, 0, 10)
   ```

6. **A `const` binding is captured as a copy.** Reading it inside the closure never sees a later assignment to the
   outer name, because the outer name cannot be reassigned either (see [Bindings](../values-and-types/bindings.md)).

7. **A `var` binding is captured as a box shared between the closure and the scope it was written in.** Both sides see
   every change; this is the one place in the language where a value is shared rather than copied.

   ```trb check
   fn counter(): () => Int {
     var count = 0
     {
       count = count + 1
       count
     }
   }

   const next = counter()
   print "{next()}, {next()}, {next()}"
   ```

8. **Capturing a `var` parameter or `var self` is only legal in a closure that cannot outlive the call.** Such a
   closure is a reference to the caller's value, and a reference may not be stored or returned - only written
   directly as the argument of a call that runs the closure and does not keep it, which is what a control structure,
   a receiver closure and a pipeline stage all are.

   ```trb error
   fn makeIncrementer(var target: Int): () => Void {
     {
       target = target + 1
     }
   }
   // error: This closure captures the `var` parameter `target` and may outlive the call
   ```

## What this is not

**A closure is not a block that runs inline.** `{ print "hi" }` on its own is a closure value, not a statement that
prints; call it (`{ print "hi" }()`) or pass it somewhere that calls it.

```trb check
fn runTwice(action: () => Void) {
  action()
  action()
}

runTwice { print "hi" }
```

```trb error
{ print "hi" }
// error: This value is not used
```

The closure is built and then thrown away: an unused value is a compile error (see
[Bindings](../values-and-types/bindings.md)), and a closure is a value like any other.

**A local `fn` is not a closure.** It captures nothing and sees only its own parameters and the top level of its file
(see [Declaring a function](declaring-a-function.md#what-this-is-not)); write a closure when the code has to reach
into the surrounding scope.

## Related

- [Declaring a function](declaring-a-function.md) - `fn`, the declaration a closure cannot fully replace.
- [Trailing closures](trailing-closures.md) - writing a closure argument after the call it belongs to.
- [Bindings](../values-and-types/bindings.md) - what `const` and `var` decide, which is what rules 6 and 7 build on.
- [Coming from Rust](../../explanation/coming-from-rust.md) - closures here have no borrow checker to satisfy.
