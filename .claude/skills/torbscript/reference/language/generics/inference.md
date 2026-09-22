---
title: Inference
summary: A type argument is inferred from a call's arguments or its expected type, a closure's parameter types follow the same rule, and a fn's own parameter types and a public fn's result are always written out.
kind: reference
status: stable
order: 30
keywords:
  - type inference
  - bidirectional
  - closure parameter
source:
  - CONCEPT.md#key-facts
  - CONCEPT.md#functions
  - CONCEPT.md#lambdas-and-closures
---

Inference is local: the compiler works out a type from the one call or the one closure it is written in, never by
looking at how a result is used three lines later.

## Example

```trb check
fn identity<Value>(value: Value): Value {
  value
}

const number = identity 5

const double: (Int) => Int = { x => x * 2 }
print double(number)

fn twice<Value>(value: Value, transform: (Value) => Value): Value {
  transform value
}

const result = twice 5 { x => x * 2 }
print result
```

## Syntax

```text
identity(5)              Value inferred from the argument
empty<String>()          Item written out: nothing in the call says what it is
into<Set<Employee>>()    the one type argument written, the rest (here, none) inferred
{ x => x * 2 }            parameter type inferred from an expected function type
```

## Rules

1. **A `fn`'s type arguments are inferred from the arguments of a call**, by matching each argument's type against
   the parameter it fills. `identity 5` fills `Value` with `Int` because `5` is an `Int`.

2. **A type argument that cannot be inferred is written out, in the same angle brackets the declaration uses.**
   `empty<String>()` writes `Item` because nothing about calling `empty` with no arguments says what it is.

3. **Type arguments can be given partially, from the left, and the rest is inferred.** `pair<Int>(5, "x")` writes
   `Left` and lets `Right` follow from `"x"`.

4. **A `fn`'s own parameter types are always written out; they are never inferred.** Only the result type can be
   left out, and only for a `fn` that is not `public` and not a trait method.

   ```trb error
   public fn double(value: Int) {
     value * 2
   }
   // error: A `public` function does not infer its result: declare it (`: Int64`)
   ```

5. **A closure's parameter types are inferred from an expected function type; without one, they have to be
   annotated.** `const double: (Int) => Int = { x => x * 2 }` needs no annotation on `x`; the closure alone does.

   ```trb error
   const double = { x => x * 2 }
   // error: The parameters of this closure need types: `{ value: Int => ... }`
   ```

6. **A closure's return type is always inferred, and cannot be written on the closure itself.** To spell it out,
   annotate the binding the closure is assigned to, or write a local `fn` instead - which is the same declaration as
   a top-level one and can be passed by name.

7. **Coercion to a trait type or a literal type never solves an inference variable.** It only fires once the
   expected type is already known some other way, so it cannot be the reason a type argument gets picked.

## What this is not

**Inference is not global.** An annotation two lines below a call does not reach back into it - a call is solved
against its own arguments and its own expected type, not against how its result gets used afterward.

```trb check
fn empty<Item>(): List<Item> {
  []
}

const numbers: List<Int> = empty()
print numbers
```

```trb error
fn empty<Item>(): List<Item> {
  []
}

const items = empty()
const numbers: List<Int> = items
print numbers
// error: Cannot infer `Item` of `empty`
```

`const items = empty()` has to stand on its own: nothing on that line says what `Item` is, and the annotation on the
next line, `numbers`, arrives one statement too late to help it.

## Related

- [Type parameters](type-parameters.md) - where the type argument being inferred comes from.
- [Bounds](bounds.md) - a bound restricts what an inferred type can be, it does not help infer it.
- [Traits as types](../traits/trait-types.md) - the coercion that never solves an inference variable.

