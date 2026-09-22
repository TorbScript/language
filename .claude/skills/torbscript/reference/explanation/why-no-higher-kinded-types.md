---
title: Why there are no higher-kinded types
summary: Option, Result, Task and Iterate share method names by convention of the standard library rather than by a shared abstraction, because a kind system would cost local inference and readable errors for problems a script rarely has.
kind: explanation
status: stable
order: 100
keywords:
  - higher-kinded types
  - functor
  - monad
  - kind
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#one-vocabulary-instead-of-higher-kinded-types
---

Haskell's `Functor f => f a -> (a -> b) -> f b` describes `map` once for every shape that has one. TorbScript describes
`map` four times - once each for `Option`, `Result`, `Task` and `Iterate` - and this page argues for why that
repetition is a better trade than the abstraction that would remove it.

## The decision

**`Option`, `Result`, `Task` and `Iterate` share a vocabulary because the standard library was written to use the
same words for the same meanings, not because the language can abstract over "a type with a `map`."**

- `map`, `flatMap`, `filter`, `forEach`, `orElse`/`??` and `toList()` mean the same thing on every one of them where
  they exist at all - see the full table in [No higher-kinded types](../language/generics/no-higher-kinded-types.md).
- There are no higher-kinded types: no `Functor<F<_>>`, no `Monad`, no `Self<U>`. A type parameter is always a type,
  never a type constructor waiting for one more argument.
- `Self` may still appear as a trait argument (`Accumulator<Item, Self>`), because there it names a concrete type, not
  a constructor - so this is not the thing the decision rules out.

```trb check
fn double(value: Int?): Int? {
  value.map { _ * 2 }
}

fn doubleAll(values: List<Int>): List<Int> {
  values.map { _ * 2 }.toList()
}

print double(Some(3))
print doubleAll([1, 2, 3])
```

## Why

**Because the operations that look alike are not actually the same operation, and a shared abstraction would hide
exactly the difference that matters.** `Option.map` runs immediately, because an `Option` is a value; `Iterate.map`
returns a new lazy stage and runs nothing until a terminal operation pulls it, because a pipeline is not a value in
that sense - it is a plan. A `Functor` typeclass that covers both promises one behavior under one name and delivers
two, and the only way to tell them apart is to already know which concrete type is behind the abstraction, which
defeats the abstraction.

**Because a kind system costs the local, bidirectional type inference the rest of the language relies on.** Inferring
across partially applied type constructors (`Result<_, Failure>`) and doing higher-order unification are what make
Haskell's and Scala's type errors notoriously hard to read for exactly the population TorbScript is written for
- someone reading one function's signature at a time, not someone tracing a kind mismatch through a chain of
typeclass instances.

**Because what higher-kinded types are typically reached for already has a name here.** Chaining fallible steps is `?`
and `await()`; folding a `List<Result<Value, Failure>>` into a `Result<List<Value>, Failure>` is a
[collection target](../language/generics/no-higher-kinded-types.md#related) (`to<Result<List<Int>, ParseError>>()`),
which stops at the first failure without a `traverse` function existing anywhere; bridging an `Option`-returning
function into a pipeline is `filterMap`. Each of these was added once, as an ordinary generic function or an ordinary
`From` implementation, rather than once as an instance of a general abstraction that then has to be learned on top of
the four types it covers.

**Because `Option` deliberately not being an `Iterate` is a feature the abstraction would erase.** `Option.map` is
eager and `Iterate.map` is lazy on purpose - see [Why values instead of references](why-values-instead-of-references.md)
for the same instinct applied elsewhere: make the two different things look different, so a reader does not have to
hold "well, this one is actually lazy" in their head.

### What was rejected

- **A `Functor`/`Monad` hierarchy.** Rejected because the shared behavior these typeclasses promise does not actually
  hold across `Option` (eager) and `Iterate` (lazy), and because the inference and error messages a kind system needs
  cost more than a script gains from writing one generic function instead of four similar ones.
- **F-bounded polymorphism and `Self<U>`** to let a trait describe "the same shape with a different type argument."
  Rejected for the same reason: `Self` stays a type, and a bound never asks for a type constructor.

## Consequences

**A function generic over "any of these" does not exist, and is not missed in practice**, because a script reaches for
one concrete shape at a time. Writing `map` on a type of your own is an ordinary method with an ordinary generic
result type, following the same names as the standard library's:

```trb check
type Box<Value> {
  value: Value

  fn map<Output>(transform: (value: Value) => Output): Box<Output> {
    Box transform(value)
  }
}

const boxed = Box 3
print(boxed.map({ _ * 2 }).value)
```

**Bridging between the four shapes is a named function, not a generic combinator**, so `?`, `await()`, a collection
target, and `filterMap` are what get reached for. See the table of substitutes in
[No higher-kinded types](../language/generics/no-higher-kinded-types.md).

**Adding a fifth shape to the standard library means picking the same names again, by convention, not by
implementing an interface.** A new asynchronous stream type gets a `map` that means "transform what is inside," not a
`Functor` instance that proves it.

## Related

- [No higher-kinded types](../language/generics/no-higher-kinded-types.md) - the shared vocabulary and the full
  substitution table.
- [Why there is no null](why-no-null.md) - `Option`, one of the four shapes this vocabulary covers.
- [Why there are no exceptions](why-no-exceptions.md) - `Result`, another of the four.
- [Pipelines](../language/collections-and-iteration/pipelines.md) - `Iterate`'s lazy `map`, the one that differs from
  `Option`'s.

