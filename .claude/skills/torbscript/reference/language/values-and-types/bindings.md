---
title: Bindings
summary: A const binding never changes and nothing below it changes; a var binding can be changed in place. That one rule replaces every mutable-and-immutable type pair.
kind: reference
status: stable
order: 10
keywords:
  - const
  - var
  - shadowing
  - dead change
  - deep const
source:
  - CONCEPT.md#bindings
  - examples/tour/src/01-bindings-and-values.trb
---

A binding gives a value a name. `const` means the name and everything reachable through it never changes; `var` means it
can be changed through this name. There is no third form, and no type in the language has a mutable twin.

## Example

```trb
const answer = 42
var counter = 0
counter = counter + 1

var list = [1, 2]
list.add 3
const fixed = list

print "{answer} {counter} {list} {fixed}"
```

## Syntax

```text
const <pattern> [: <Type>] = <expression>
var   <pattern> [: <Type>] = <expression>
```

A pattern destructures here too, so `const (quotient, remainder) = divide(7, 2)` and `const Point(x, y) = p` are
bindings that split their value into several names at once. See
[Cases and match](../pattern-matching/cases-and-match.md) for the pattern vocabulary a binding shares with `match`.
A module's own top-level `const` is the one exception: it binds a single name, because a name another file imports
has to be one thing (see [Top-level code](../modules-and-packages/top-level-code.md)).

## Rules

1. **A binding is always initialized.** `var x` and `var x: Int` are both compile errors. No type in the language has an
   implied default value, so there is nothing a bare declaration could mean.

   ```trb error
   var later: Int
   // error: A binding needs a value
   ```

2. **`const` is deep from the perspective of the binding.** Through a `const` binding you can neither reassign, nor
   assign a field, nor call a method that is a `var fn`. `var` means "changeable through this path".

   ```trb error
   const fixed = [1, 2]
   fixed.add 3
   // error: `add` needs a `var`
   ```

3. **The binding decides about the value, not the type.** There is no `List` and `MutableList`, no `Point` and
   `MutablePoint`, and no read-only view of anything. A parameter without `var` *is* the immutable form.

4. **Assigning, passing and capturing is a copy.** Two bindings never refer to the same value, so a change through one is
   invisible through the other. The exception is a `shared type`, which has an identity.

   ```trb
   type Point {
     var x: Int
     var y: Int
   }

   var first = Point 1, 2
   const second = first
   first.x = 99
   print "{first} {second}"
   ```

   That prints `Point(x: 99, y: 2) Point(x: 1, y: 2)`.

5. **A type annotation is optional, and a literal adapts to it.** Without one, an integer literal is `Int64` and a
   decimal literal is `Float64`. With one, the literal takes the expected type, and a literal that does not fit it is a
   compile error at the literal.

   ```trb
   const ratio: Float = 1
   const byte: UInt8 = 0xFF
   const million = 1_000_000
   ```

   ```trb error
   const small: Int8 = 300
   // error: `300` does not fit into `Int8`
   ```

6. **An empty collection literal needs a type from context.** `[]` and `[:]` carry no element type of their own.

   ```trb
   const empty: List<String> = []
   const table: Map<String, Int> = [:]
   print "{empty} {table}"
   ```

7. **A name can be shadowed in a nested scope and never redeclared in the same one.** The parameters of a function and
   the top level of its body are **one** scope, so a `const` there may not take a parameter's name. A nested block and a
   closure are scopes of their own. There is no silent shadowing anywhere in the language.

8. **A change that cannot have an effect is a compile error.** A `var` that is changed and never read afterwards, and the
   discarded result of a method that only reads its receiver, are both errors: with value semantics they are always mistakes rather
   than defensive lines. Discard on purpose with `const _ = ...`.

9. **An expression statement has the type `Void` or `Never`**, unless the call has a `var` receiver or a `var` argument.
   That is the rule behind rule 8: `parser.bump()` changes something and stays a statement, while `Email.tryFrom(text)` and
   `1 + 2` are values that go nowhere.

10. **Assignment is a statement, not an expression.** `a = b = c` does not parse, and `if x = y` is not a comparison that
    was mistyped - it does not parse either.

## What this is not

**`const` is not `final`, and it is not `readonly`.** In Java, C# and TypeScript those words freeze the *binding* and
leave the object writable. Here the value is frozen too:

```trb
var writable = [1, 2]
writable.add 3
print writable
```

```trb error
const frozen = [1, 2]
frozen.add 3
// error: `add` needs a `var`
```

**`var` is not `let mut`, because there is nothing to borrow.** A `var` binding owns its value. Handing it to a function
that may change it is a `var` parameter, and that reference exists only for the duration of the call.

**Taking an element out of a collection is not a reference to it.** This is the copy trap. The first program below
prints `1`, the second prints `1 0`: the copy changed and the list did not. Rule 8 makes a change that is never read a
compile error. The second program reads the copy in its last line, so there is nothing to report: a copy that is
changed and then read is a legal program that does something else than was meant.

```trb check
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
counters[0].increment()
print counters[0].count
```

```trb check
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
var first = counters[0]
first.increment()
print "{first.count} {counters[0].count}"
```

**A `const` binding is not a compile-time constant.** Its initializer runs at runtime like any other expression. The one
place where a constant has to be evaluable at compile time is a top-level `const` of a module, which is a different rule.

## Related

- [Declaring a type](../types/declaring-a-type.md) - fields, `var` fields and what is generated.
- [Mutation and var paths](../types/declaring-a-type.md#rules) - what has to be `var` from the binding down.
- [Values and bindings](../../guide/values-and-bindings.md) - the same material as a learning step.
- [Why values instead of references](../../explanation/why-values-instead-of-references.md) - the argument behind rule 4.
- [Coming from Rust](../../explanation/coming-from-rust.md) - if `let mut` and `&mut` are what you reach for.
