---
title: Collectors
summary: An Accumulator describes what to do with the values of a pipeline and is the state of one run at the same time, because a value is a copy; collect fills a copy of the one it is given.
kind: reference
status: stable
order: 80
keywords:
  - Accumulator
  - ListAccumulator
  - collect
  - groupingBy
source:
  - std/iteration/src/collectors.trb
---

`collect` is the general terminal operation of a pipeline: it takes an `Accumulator`, which says what to do with the
values, and pulls the source through it. An `Accumulator` is a **value**, so `collect` fills a copy of it and the one
the caller holds is untouched: the description of a run and the state of a run are one type, and there is no factory
step between them.

## Example

```trb check
const numbers = [1, 2, 3, 4, 5]

const total = numbers.collect(summing { value: Int => value })
const byParity = numbers.collect(groupingBy { value: Int => value % 2 })

print total
print byParity
```

## Syntax

```text
trait Accumulator<Item, Output> {
  var fn add(value: Item)
  fn finish(): Output
  fn isDone(): Bool { false }
}

source.collect(accumulator)
```

## Rules

1. **`Accumulator<Item, Output>` is the whole of it: `add`, `finish` and `isDone`.** `collect` copies what it is
   given, pushes every value into the copy with `add`, and calls `finish()` at the end. There is no `start()`, because
   a copy of a value already is a fresh run.

2. **`Accumulator` is push-based: `add` takes one value at a time, and `isDone()` says whether more values would
   change the result.** A driver asks `isDone()` before the first value and after every `add`, so `collect(counting())`
   over an infinite source never returns, but `collect` behind `taking(10)` or `first()` stops reading after enough
   values arrived - `isDone` defaults to `false`.

3. **A collection is not an `Accumulator`, and there is no trait for "something with `add`".** What gathers into a
   collection is a type of its own beside it, the way `Collector` and `Collectors.toList()` are in Java:
   `ListAccumulator<Item>` is the one the standard library ships, and `into<Target>()` is the general one for any
   `From<Iterate<Item>>` target - it gathers into a `List` and calls `Target.from` once at the end.

   ```trb check
   const numbers = [3, 1, 2, 1]
   print numbers.collect(ListAccumulator<Int>())
   print numbers.collect(into<Set<Int>>())
   ```

   A package that owns a collection may ship an accumulator that writes straight into it. Nothing picks such a one up
   automatically: a caller who wants it **names** it at the call, because which run a pipeline makes is written down
   and never inferred from the target type.

4. **The standard library's collectors cover the common shapes**: `counting()`, `summing { ... }`,
   `averaging { ... }` (an `Option`, `None` for an empty pipeline), `minBy { ... }`/`maxBy { ... }` (also `Option`),
   `joining(separator:, prefix:, suffix:)`, `partitioningBy { ... }` (a tuple of two lists), and
   `groupingBy { ... }` (a `Map<Key, List<Item>>`, or `.then(downstream)` for a different one per group).

   ```trb check
   const numbers = [1, 2, 3, 4, 5]
   const (even, odd) = numbers.collect(partitioningBy { _ % 2 == 0 })
   print even
   print odd
   ```

5. **Write one as a fold with a final step when the state is one value.** `collector(initial, finish:, step:)`
   answers an `Accumulator` that folds `step` over every value and applies `finish` once at the end.

   ```trb check
   fn median<Item>(value: Transform<Item, Int>): Accumulator<Item, Int?> {
     collector([], finish: { values: List<Int> => values.sorted { it => it }.skip(values.length() / 2).first() }) {
       values, item => values.appended value(item)
     }
   }

   const numbers = [5, 3, 9, 1, 7]
   print numbers.collect(median { value => value })
   ```

6. **Write one as a type with `var` fields when the state is more than one value.** `ListAccumulator<Item>` is the
   simplest example, and `groupingBy(...).then(...)` is the standard library's own: one downstream accumulator per
   group, held in a `Map<Key, Accumulator<Item, Output>>` and **copied** from the downstream for every new group.

   ```trb check
   type Extremes<Item: Compare> with Accumulator<Item, (Item?, Item?)> {
     var min: Item? = None
     var max: Item? = None

     var fn add(value: Item) {
       min = match min {
         Some(current) if current <= value => min
         _ => Some value
       }
       max = match max {
         Some(current) if current >= value => max
         _ => Some value
       }
     }

     fn finish(): (Item?, Item?) {
       (min, max)
     }
   }

   print([5, 3, 9, 1].collect(Extremes<Int>()))
   ```

## What this is not

**An accumulator is not a one-shot value: `collect` copies it, so one value drives many runs.** The same accumulator
runs again for a different source, and each run gets its own copy.

```trb check
const salaryStatistics = summing { value: Int => value }
print([1, 2, 3].collect(salaryStatistics))
print([10, 20].collect(salaryStatistics))
```

```trb error
const salaryStatistics = summing { value: Int => value }
print(salaryStatistics())
// error: The checker did not work out the type of this expression
```

The one thing the merged type does not say is whether what it was handed is fresh: an accumulator that has already
been `add`ed to and is then given to `collect` or to `then` starts every run from what is in it. Build one where it is
used, the way every collector of `std/iteration` answers a fresh value.

## Related

- [Pipelines](pipelines.md) - the lazy stages that produce what an accumulator consumes.
- [The collection traits](collection-traits.md) - the five kinds, and why none of them is an `Accumulator`.
- [Maps and sets](maps-and-sets.md) - the `Map<Key, List<Item>>` that `groupingBy` builds.
