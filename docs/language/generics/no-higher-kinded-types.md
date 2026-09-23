---
title: No higher-kinded types
summary: Option, Result, Iterate and Task share method names with the same meaning as a convention of the standard library, not as a shared trait, because the language has no way to be generic over a type constructor.
kind: reference
status: stable
order: 40
keywords:
  - higher-kinded types
  - Functor
  - Monad
  - type constructor
source:
  - CONCEPT.md#one-vocabulary-instead-of-higher-kinded-types
---

`Option`, `Result`, `Iterate` and `Task` all have a `map`, and `map` always means "transform what is inside, keep
the shape" - by convention of the standard library, not because a trait ties the four `map`s together.

## Example

```trb check
const found: Int? = Some 3
print(found.map { _ * 2 })

const numbers: List<Int> = [1, 2, 3]
print(numbers.map { _ * 2 }.toList())
```

## Syntax

```text
Option<Value>.map<Output>(transform: Transform<Value, Output>): Output?
Iterate<Item>.map<Output>(transform: Transform<Item, Output>): Iterate<Output>
```

Same name, same shape of signature, two unrelated declarations - neither `extend`s a trait the other implements.

## Rules

1. **The language has no higher-kinded types: no `Functor<F<_>>`, no `Self<U>`, no partially applied type
   constructor.** A type parameter always stands for a concrete type, never for "some type with a hole in it."

   ```trb error
   trait Functor<Item> {
     fn map<Output>(): Self<Output>
   }
   // error: `Self` takes no type arguments
   ```

## What this is not

**The shared vocabulary is not a reason to write one generic function over all of them.** No bound exists that could
accept an `Option`, a `Result` and an `Iterate` in the same parameter.

```trb error
fn anyDoubled<Container: Functor>(value: Container): Container {
  value
}
// error: Unknown type `Functor`
```

## Related

- [Why there are no higher-kinded types](../../explanation/why-no-higher-kinded-types.md) - why this trade-off was
  made, the alternatives it rejected, and what stands in for a `Functor` on each of the four types.
- [Type parameters](type-parameters.md) - what a type parameter can stand for, and what it cannot.
- [Witness tables](witnesses.md) - what does exist for calling a bound's members generically.
- [Bounds](bounds.md) - what a bound can name, since it cannot name a shape like `Functor`.
