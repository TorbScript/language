---
title: No higher-kinded types
summary: Option, Result, Iterable and Task share method names with the same meaning as a convention of the standard library, not as a shared trait, because the language has no way to be generic over a type constructor.
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

`Option`, `Result`, `Iterable` and `Task` all have a `map`, and `map` always means "transform what is inside, keep
the shape". Nothing in the language ties the four `map`s together - there is no trait one of them implements that
the others do too.

## Example

```trb check
const found: Int? = Some 3
print(found.map { _ * 2 })

const numbers: List<Int> = [1, 2, 3]
print(numbers.map { _ * 2 }.toList())
```

## Syntax

```text
Option<Value>.map<Output>(transform: (value: Value) => Output): Output?
Iterable<Item>.map<Output>(transform: (value: Item) => Output): Iterable<Output>
```

Same name, same shape of signature, two unrelated declarations - neither `extend`s a trait the other implements.

## Rules

1. **A shared method name across `Option`, `Result`, `Task` and `Iterable` is a convention of the standard library,
   not a language feature.** `map` transforms what is inside and keeps the shape everywhere it appears; `flatMap`
   transforms into the same shape and flattens one level; `filter` and `forEach` exist on the ones where "keep it"
   and "look at it" make sense.

2. **`Task` shares the same two names.** `map` and `flatMap` exist on `Task<Value>` with the same meaning as on
   `Option` and `Iterable`; code that uses them type checks today, but a `Task` does not run yet.

3. **There is no trait that unifies them, and none of `Option`, `Result` or `Iterable` implements one.** Each type
   declares its own `map`; nothing generic can be written that calls "the `map` of whatever container was passed
   in", because there is no bound that names it.

4. **The language has no higher-kinded types: no `Functor<F<_>>`, no `Self<U>`, no partially applied type
   constructor.** A type parameter always stands for a concrete type, never for "some type with a hole in it".

   ```trb error
   trait Functor<Item> {
     fn map<Output>(): Self<Output>
   }
   // error: `Self` takes no type arguments
   ```

5. **`Self` as a trait *argument* is not a higher-kinded type.** `Accumulator<Item, Self>` names `Self` as an
   ordinary type, exactly as `Accumulator<Item, Robot>` would - it costs nothing, because `Self` here is not a type
   constructor waiting for an argument.

6. **What higher-kinded types are reached for in other languages is covered here without one.** Chaining fallible steps is `?` on
   `Option` and `Result`, and `await()` on `Task`; turning `List<Value?>` into `List<Value>?` or
   `List<Result<Value, Failure>>` into `Result<List<Value>, Failure>` is a collection target
   (`.to<List<Value>?>()`); a function that returns an `Option` inside a pipeline is `filterMap`.

## What this is not

**The shared vocabulary is not a reason to write one generic function over all of them.** `map`, `flatMap` and the
rest look alike because the standard library was written to make them look alike on purpose - not because a bound
exists that could accept an `Option`, a `Result` and an `Iterable` in the same parameter.

```trb check
fn doubled(value: Int?): Int? {
  value.map { _ * 2 }
}

fn allDoubled(values: List<Int>): List<Int> {
  values.map { _ * 2 }.toList()
}
```

```trb error
fn anyDoubled<Container: Functor>(value: Container): Container {
  value
}
// error: Unknown type `Functor`
```

## Related

- [Type parameters](type-parameters.md) - what a type parameter can stand for, and what it cannot.
- [Witness tables](witnesses.md) - what does exist for calling a bound's members generically.
- [Bounds](bounds.md) - what a bound can name, since it cannot name a shape like `Functor`.

