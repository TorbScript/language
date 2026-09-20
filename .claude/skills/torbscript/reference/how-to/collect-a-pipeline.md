---
title: Collect a pipeline into what you need
summary: Reach for the named terminal operation when there is one - toList, sum, joined, groupBy - and fall back to collect with a Collector for anything else, including your own accumulator.
kind: how-to
status: stable
order: 110
keywords:
  - terminal operation
  - collect
  - Collector
  - toList
  - groupBy
source:
  - std/iteration/src/iteration.trb
  - std/iteration/src/collectors.trb
---

Nothing in a pipeline runs until its terminal operation pulls the values through. Most of the time the result you
need already has a name on `Iterable` itself; `collect` with a `Collector` is what is left for everything that does
not.

## Steps

1. **Reach for the named terminal operation first.** `toList()`, `toSet()`, `to<Target>()` for any other
   `From<Iterable<Item>>`, `sum()`, `count()`, `joined(separator:)`, `fold(initial, combine)`, `first()`,
   `find { ... }`, `any { ... }`, `all { ... }` and `groupBy { ... }` cover most pipelines without ever naming a
   `Collector`.

   ```trb fragment
   const total = orders.map { order => order.amount }.sum()
   const byCity = customers.groupBy { customer => customer.city }
   ```

2. **Reach for `collect` with a standard-library `Collector` for a shape none of those name.** `counting()`,
   `summing { ... }`, `averaging { ... }`, `minBy { ... }`/`maxBy { ... }`, `partitioningBy { ... }`, and
   `groupingBy { ... }.then(downstream)` for one collector per group.

   ```trb fragment
   const stats = orders.collect(groupingBy { order: Order => order.city }.then(summing { order: Order => order.amount }))
   ```

3. **Run the same collector more than once when several results come from one pipeline of values.** A `Collector` is
   a reusable description; each call to `collect` starts a fresh `Accumulator` of its own.

   ```trb fragment
   const amounts = summing { order: Order => order.amount }
   const thisMonth = orders.filter { order => order.isThisMonth() }.collect(amounts)
   const lastMonth = orders.filter { order => order.isLastMonth() }.collect(amounts)
   ```

4. **Write `collector(initial, finish:, step:)` when the state a run needs is one value.** It answers a `Collector`
   whose accumulator folds `step` over every value and applies `finish` once, which covers most custom aggregates
   without a type of their own.

5. **Write a `type` with `var` fields that implements `Accumulator` when the state is more than one value**, the way
   the standard library's own `groupingBy(...).then(...)` keeps one accumulator per group in a `Map`.

## Pitfalls

- **`isDone()` is what lets a terminal operation stop early.** `first()`, `find { ... }` and `collect` behind
  `take(n)` never read a source to the end; a custom `Accumulator` that never overrides `isDone()` is asked to read
  everything, which is correct for a sum or a count and wrong for anything that could stop sooner.
- **A `Collector` value is not itself a result.** `orders.collect(summing { order: Order => order.amount })` runs the
  collector; the collector value on its own is a description that can be handed to `collect` again for a different
  source.
- **`fold` and a hand-written `Collector` do the same pull.** Reach for `fold` when the state is a simple running
  value and there is only one call site; reach for a named `Collector` the moment two pipelines need the same
  aggregate, so the logic is written once.
- **Every `Collection` is already an `Accumulator`.** `pipeline.collect(existingList)` needs no `into<...>()` wrapper
  when the target is a `List`, `Set`, `Map`, `Stack` or `Queue` you already have, because `add` is the same verb and
  `finish` answers `self`.

## Full example

```trb check
type Order {
  city: String
  amount: Int
}

const orders = [
  Order("Berlin", 120),
  Order("Berlin", 80),
  Order("Munich", 200),
]

const total = orders.map { order => order.amount }.sum()
print total

const totalsByCity = orders.collect(groupingBy { order: Order => order.city }.then(summing { order: Order => order.amount }))
print totalsByCity

const highValue = orders.collect(partitioningBy { order => order.amount >= 100 })
print highValue
```

## Related

- [Pipelines](../language/collections-and-iteration/pipelines.md) - source, lazy stages and terminal operation, in full.
- [Collectors](../language/collections-and-iteration/collectors.md) - `Collector`, `Accumulator`, and every standard
  collector in one place.
- [Sort by more than one key](sort-by-more-than-one-key.md) - a `sorted` stage in front of the same kind of pipeline.
