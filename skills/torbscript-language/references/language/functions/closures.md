---
title: Closures
summary: A brace in expression position is always a closure with inferred or written parameters; it captures a const binding as a copy and a var binding as itself, which only a closure handed to a parameter that just calls it may do.
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

   A tuple position of such a parameter is still written behind a dot: `{ _.0 }`, never `{ 0 }`, because a bare `0` is
   the number and nothing else.

   ```trb check
   const pairs = [(1, "one"), (2, "two")]
   print pairs.map({ _.0 }).toList()
   ```

   An implicit parameter is what a named one would be: where the expected function type takes the parameter as `var`,
   `_` is a `var` and changes the caller's value, exactly as `{ items => items.append 2 }` does.

   ```trb run
   fn filled(body: (var items: List<Int>) => Void): List<Int> {
     var items: List<Int> = [1]
     body items
     items
   }

   print filled({ _.append 2 })
   // prints [1, 2]
   ```

4. **The result type is always inferred, never annotated.** A closure that needs a written result type is a local
   `fn` instead, because `fn` is a declaration and can carry a return type (see
   [Declaring a function](declaring-a-function.md)); a `fn` declared by name can be passed exactly where a closure is
   expected.

5. **`return` and `?` inside a closure return from the closure, not from the function around it.** There is no
   non-local return: the value `return` produces is the closure's own result, at the point the closure is called, and
   a `?` hands its failure to whoever called the closure - so a closure with a `?` in it has to produce an `Option`
   or a `Result` (see [The question mark operator](../errors/question-mark.md), rule 8).

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

7. **A `var` binding is captured as itself, shared between the closure and the scope it was written in.** Both sides
   see every change; this is the one place in the language where a variable is shared rather than copied.

   ```trb check
   fn total(numbers: List<Int>): Int {
     var sum = 0
     numbers.forEach { sum = sum + _ }
     sum
   }

   print total([1, 2, 3])
   ```

8. **A closure that captures a `var` binding may only run while the binding exists, and the checker enforces it.** A
   local `var`, a `var` parameter, a `var fn` receiver and a `var` parameter of a closure around it are all the
   same: such a closure may stand straight as the argument of a call whose parameter **only calls it**, and nowhere
   else. Bound to a name, stored in a field, a collection or a case, returned, handed to `spawn` or to a parameter that
   keeps it, it is an error. A parameter only calls its closure when it has a function type, its function has a body,
   and that body calls it (`action(value)`, `action value`), calls it inside a closure that itself only runs during
   the call, or hands it on by name to a parameter that only calls it - which is what `forEach`, `unless`, a receiver
   closure and every block of a DSL do. A lazy stage keeps its closure: `items.map { ... }` stores it in the stage it
   answers, so a closure given to `map` or `filter` may read a `var` only through a `const` copy of it.

   ```trb error
   fn counter(): () => Int {
     var count = 0
     {
       count = count + 1
       count
     }
   }
   // error: This closure captures the `var` binding `count` and may outlive it
   ```

   ```trb error
   fn makeIncrementer(var target: Int): () => Void {
     {
       target = target + 1
     }
   }
   // error: This closure captures the `var` parameter `target` and may outlive the call
   ```

   The fix is to hand the value to a `var` parameter of the function that runs the closure, or to let the closure
   return what it computed. The rule is what keeps two copies of a value from ever sharing a variable through a closure
   they hold, a closure from changing a binding while a `var` access to it runs, and a task from reaching one.

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

