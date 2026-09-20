---
title: Collectors
summary: A Collector describes what to do with the values of a pipeline; start() makes a fresh, push-based Accumulator for one run, and every Collection is one without extra work.
kind: reference
status: stable
order: 80
keywords:
  - Collector
  - Accumulator
  - collect
  - groupingBy
source:
  - std/iteration/src/collectors.trb
---

`collect` is the general terminal operation of a pipeline: it takes a `Collector`, which describes what to do with the
values, and pulls the source through it. A `Collector` is reusable and stateless; each run of `collect` makes a fresh
`Accumulator` that does the actual work.

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
trait Collector<Item, Output> { fn start(self): Accumulator<Item, Output> }
trait Accumulator<Item, Output> {
  fn add(var self, value: Item)
  fn finish(self): Output
  fn isDone(self): Bool { false }
}

source.collect(collector)
```

## Rules

1. **`Collector<Item, Output>` has one member, `start(self): Accumulator<Item, Output>`.** `collect` calls `start()`
   once, pushes every value into the answer with `add`, and calls `finish()` at the end.

2. **`Accumulator` is push-based: `add` takes one value at a time, and `isDone()` says whether more values would
   change the result.** A driver asks `isDone()` before the first value and after every `add`, so `collect(counting())`
   over an infinite source never returns, but `collect` behind `taking(10)` or `first()` stops reading after enough
   values arrived - `isDone` defaults to `false`.

3. **Every `Collection` is already an `Accumulator`, because `add` is the same verb and `finish` answers `self`.** Any
   `List`, `Set`, `Map`, `Stack` or `Queue` is a legal `collect` target with no extra code.

4. **The standard library's collectors cover the common shapes**: `counting()`, `summing { ... }`,
   `averaging { ... }` (an `Option`, `None` for an empty pipeline), `minBy { ... }`/`maxBy { ... }` (also `Option`),
   `joining(separator:, prefix:, suffix:)`, `partitioningBy { ... }` (a tuple of two lists), and
   `groupingBy { ... }` (a `Map<Key, List<Item>>`, or `.then(downstream)` for a collector per group).

   ```trb check
   const numbers = [1, 2, 3, 4, 5]
   const (even, odd) = numbers.collect(partitioningBy { _ % 2 == 0 })
   print even
   print odd
   ```

5. **Write a collector as a fold with a final step when the state is one value.** `collector(initial, finish:, step:)`
   answers a `Collector` whose `Accumulator` folds `step` over every value and applies `finish` once at the end.

   ```trb check
   fn median<Item>(value: (value: Item) => Int): Collector<Item, Int?> {
     collector([], finish: { values: List<Int> => values.sorted { it => it }.skip(values.length() / 2).first() }) {
       values, item => values.added value(item)
     }
   }

   const numbers = [5, 3, 9, 1, 7]
   print numbers.collect(median { value => value })
   ```

6. **Write a collector as a type with `var` fields when the state is more than one value.** `into<Target>()` (any
   `From<Iterable<Item>>`) is the simplest example, and `groupingBy(...).then(...)` is the standard library's own: one
   downstream `Accumulator` per group, held in a `Map<Key, Accumulator<Item, Output>>`.

   ```trb check
   type Extremes<Item: Compare> with Accumulator<Item, (Item?, Item?)> {
     var min: Item? = None
     var max: Item? = None

     fn add(var self, value: Item) {
       min = match min {
         Some(current) if current <= value => min
         _ => Some value
       }
       max = match max {
         Some(current) if current >= value => max
         _ => Some value
       }
     }

     fn finish(self): (Item?, Item?) {
       (min, max)
     }
   }

   type ExtremesCollector<Item: Compare> with Collector<Item, (Item?, Item?)> {
     fn start(self): Accumulator<Item, (Item?, Item?)> {
       Extremes<Item>()
     }
   }

   print([5, 3, 9, 1].collect(ExtremesCollector<Int>()))
   ```

## What this is not

**A `Collector` is not a one-shot value: it describes a run, and it can drive many of them.** The same collector value
runs again for a different source, and each run gets its own accumulator.

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

## Related

- [Pipelines](pipelines.md) - the lazy stages that produce what a collector consumes.
- [The collection traits](collection-traits.md) - why every `Collection` is already an `Accumulator`.
- [Maps and sets](maps-and-sets.md) - the `Map<Key, List<Item>>` that `groupingBy` builds.
