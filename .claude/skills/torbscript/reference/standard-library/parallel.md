---
title: std/parallel
summary: parallel() and Parallel, a pipeline whose stages run on the workers of the pool with the results in input order, in scope through the prelude.
kind: package
status: stable
order: 85
keywords:
  - std/parallel
  - parallel
  - Parallel
  - workers
  - data parallelism
source:
  - std/parallel/src/lib.trb
  - docs/design/CONCURRENCY.md
---

`std/parallel` is one thing going faster: `numbers.parallel()` spreads a pipeline over the workers of the process, and
the rest of the pipeline is written with the words of the sequential one - `map`, `filter`, `filterMap`, then a terminal
(`toList`, `count`, `sum`, `find`, `forEach`) that answers a `Task`. Results arrive in input order, always: the input is
cut into chunks whose borders depend on its length alone, every chunk runs as a task an idle worker may take, and the
chunks are read back in order, so the answer never depends on the number of workers. `Parallel` and `parallel` are in
scope through the prelude.

## Import

```trb fragment
use Parallel, List.parallel from "std/parallel"
```

```trb check
fn steps(start: Int): Int {
  var count = 0
  var value = start
  while value != 1 {
    value = if value % 2 == 0 { value / 2 } else { 3 * value + 1 }
    count = count + 1
  }
  count
}

const numbers = (1..=100000).toList()
const total = numbers.parallel().map({ steps _ }).sum().await() ?? 0
print total
```

## Declarations

### `List.parallel`

```trb fragment
extend<Item> List<Item> {
  fn parallel(workers: Int = Workers.count(), chunk: Int? = None): Parallel<Item>
}
```

The list, cut into `chunk` pieces - at most 64 where it is `None`, never more than there are items, none of an empty
list - of which at most `workers` run at the same time. Chunk `index` of `count` covers the items from
`index * length / count` below `(index + 1) * length / count`, so `workers` moves no border and neither does the
machine. It extends `List`, so anything else that can be iterated is made one first: `(0..rows).toList().parallel()`.

### `Parallel`

```trb fragment
public type Parallel<Item> {
  fn map<Output>(transform: Transform<Item, Output>): Parallel<Output>
  fn filter(predicate: Predicate<Item>): Parallel<Item>
  fn filterMap<Output>(transform: Transform<Item, Output?>): Parallel<Output>
  fn toList(): Task<List<Item>>
  fn count(): Task<Int>
  fn sum(): Task<Item> where Item: Add & From<Int>
  fn find(predicate: Predicate<Item>): Task<Item?>
  fn forEach(body: Action<Item>): Task<Void>
}
```

Nothing runs until a terminal is called. `sum` adds each chunk on its worker and the chunks' sums in input order, so a
`Float` sum is the same bits on every machine and with any number of workers - and not the same number as the
sequential `sum()`, which adds in another order. `find` answers the first match in input order. `forEach` runs its body
for every item; the order in which items of two chunks run is the machine's. Cancelling the task a terminal answers
stops every chunk at its next suspension point or loop turn.

A chunk moves to another worker only where its items and the closures of the pipeline may cross one. Plain items (`Int`,
`Float`, a record of those) cross as they are; `String`s, lists, maps and records of those cross as a copy, made when the
chunk's task starts, where the input still holds them. A closure that captures nothing counted crosses; a pipeline with
a closure that captures a `String` or a list, or over items with an identity (a `shared type` object) or an enum with a
counted case, runs its chunks on the caller's worker, one after the other - the same answer without the speed-up
(Concurrency, section 16). Every stage is a fork-join of its own, so `map` then `filter`
runs the chunks twice.

## Related

- [std/task](task.md) - `Task`, `spawn` and `Workers.count`, what `workers:` defaults to.
- [std/iteration](iteration.md) - the sequential pipeline with the same words.
- [The standard library](index.md) - the other packages.

