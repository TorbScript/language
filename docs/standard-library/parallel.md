---
title: std/parallel
summary: parallel() on anything that can be iterated, Parallel, a pipeline whose fused stages run on the workers of the pool with the results in input order, and Cut, the collections that cut themselves.
kind: package
status: stable
order: 85
keywords:
  - std/parallel
  - parallel
  - Parallel
  - Cut
  - workers
  - data parallelism
source:
  - std/parallel/src/lib.trb
  - docs/design/CONCURRENCY.md
---

`std/parallel` is one thing going faster: `numbers.parallel()` spreads a pipeline over the workers of the process, and
the rest of the pipeline is written with the words of the sequential one - `map`, `filter`, `filterMap`, then a terminal
(`toList`, `collect`, `count`, `sum`, `minBy`, `maxBy`, `find`, `forEach`) that answers a `Task`. Results arrive in input
order, always: the input is cut into chunks whose borders depend on its length alone, every chunk runs as a task an idle
worker may take, and the chunks are read back in order, so the answer never depends on the number of workers.
`parallel()` is there on every `Iterate`; `Parallel` and `parallel` are in scope through the prelude.

## Import

```trb fragment
use Parallel, Cut, Iterate.parallel, Cut.parallel, List.parallel from "std/parallel"
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

const total = (1..=100000).parallel().map({ steps _ }).filter({ _ > 100 }).sum().await()
print total
```

## Declarations

### `parallel()` on everything that iterates

```trb fragment
extend<Item> Iterate<Item> {
  fn parallel(workers: Int = Workers.count(), chunk: Int? = None): Parallel<Item>
}

extend<Item, Piece: Iterate<Item>> Cut<Item, Piece> {
  fn parallel(workers: Int = Workers.count(), chunk: Int? = None): Parallel<Item>
}

extend<Item> List<Item> {
  fn parallel(workers: Int = Workers.count(), chunk: Int? = None): Parallel<Item>
}
```

The input, cut into `chunk` pieces - at most 64 where it is `None`, never more than there are items, none of an empty
input - of which at most `workers` run at the same time. Chunk `index` of `count` covers the items from
`index * length / count` below `(index + 1) * length / count`, so `workers` moves no border and neither does the
machine.

The three are one `parallel()`: a call reaches the most specific of them. A `List`, an `Array` and a `Range<Int>` are a
[`Cut`](#cut) and are cut without being read first - a range arithmetically, so `(0..1000000).parallel()` builds no list
of a million numbers. Anything else that can be iterated - a `Set`, a `Map` (entry by entry), `text.chars()`, a pipeline,
an `Iterate` of the program's own - is read into a list once, and that list is cut. `List` has an extension of its own
because a value of the `List` trait cannot be handed over as a value of `Cut` - `Cut` is implemented for `List` and is
no supertrait of it, so no table of a list holds it - and calls its own `cut` instead; it answers what the `Cut` one
would.

### `Cut`

```trb fragment
public trait Cut<Item, Piece: Iterate<Item>> with Iterate<Item>, Length {
  fn cut(range: Range<Int>): Piece
}

extend<Item> List<Item> with Cut<Item, ArrayList<Item>>
extend<Item, const Size: Int> Array<Item, Size> with Cut<Item, ArrayList<Item>>
extend Range<Int> with Cut<Int, Range<Int>>
```

A collection that cuts itself into pieces without being read first: its length, and the piece the positions of a range
cover. The piece is a type of its own and not `Self`, because it is what crosses to another worker: a list of the items
for a `List` and an `Array`, which a worker may take as it is, and a smaller range for a `Range<Int>`, whose cut is two
additions. A type of the program implements it to be spread the same way; its pieces have to hold the same items in the
same order as iterating it over those positions.

```trb check
use Cut from "std/parallel"

type Stretch with Cut<Int, Range<Int>> {
  first: Int
  count: Int

  fn iterate(): Iterator<Int> {
    (first..(first + count)).iterate()
  }

  fn length(): Int {
    count
  }

  fn cut(range: Range<Int>): Range<Int> {
    (first + range.start)..(first + range.end)
  }
}

print Stretch(5, 10).parallel().map({ _ * 2 }).sum().await()
```

The name follows the rule for a trait with one required method: it is named after the method. `Divide`, the working name,
is the trait of `/` in `std/core`; `Slice` is the one of `a[from..to]`, whose result is `Self`; and `part(range)` is the
total twin of that slice on a list and a text, which answers `None` outside instead of panicking.

### `Parallel`

```trb fragment
public type Parallel<Item> {
  fn map<Output>(transform: Transform<Item, Output>): Parallel<Output>
  fn filter(predicate: Predicate<Item>): Parallel<Item>
  fn filterMap<Output>(transform: Transform<Item, Output?>): Parallel<Output>
  fn toList(): Task<List<Item>>
  fn collect<Output>(collector: Merge<Item, Output>): Task<Output>
  fn count(): Task<Int>
  fn sum(): Task<Item> where Item: Add & From<Int>
  fn minBy<Key: Compare>(key: Transform<Item, Key>): Task<Item?>
  fn maxBy<Key: Compare>(key: Transform<Item, Key>): Task<Item?>
  fn find(predicate: Predicate<Item>): Task<Item?>
  fn forEach(body: Action<Item>): Task<Void>
}
```

Nothing runs until a terminal is called, and then every chunk runs **once**: the stages are fused into one step per
item, so `map` followed by `filter` runs both closures item by item and builds no list between them. What a chunk hands
back is what its terminal keeps of it.

`sum` adds each chunk on its worker and the chunks' sums in input order, so a `Float` sum is the same bits on every
machine and with any number of workers - and not the same number as the sequential `sum()`, which adds in another
order. `find` answers the first match in input order; a chunk stops at its first match, and no further chunk is started
once the chunks before it have answered and one of them found one. `minBy` and `maxBy` keep the earlier of two equal
keys, as the collectors of the same names do. `forEach` runs its body for every item; the order in which items of two
chunks run is the machine's.

`collect` takes a [`Merge`](iteration.md#merge): a copy of the collector runs over every chunk that has an item left
after the stages, and the chunks' outputs are joined left to right with its `merge`. An input without any item answers
the collector's own `finish()`, and `merge` never sees the output of a chunk the stages left empty - which is what lets
`joining(prefix: "[", suffix: "]")` answer `[20, 40, 60]` and not `[20, , 40, 60]`. The stages run on the workers; the
collector runs on the caller's worker, because it is a trait-typed value and that cannot cross to another worker yet.

Cancelling the task a terminal answers stops every chunk at its next suspension point or loop turn.

A chunk moves to another worker only where its piece and the closures of the pipeline may cross one. Plain items
(`Int`, `Float`, a range, a record of those) cross as they are; `String`s, lists, maps, variants and records of those
cross as a copy, made when the chunk's task starts, where the input still holds them, and so does the environment of a
closure that captures them. A pipeline over items with an identity (a `shared type` object), or with a closure that
captures one, a `var` or a trait-typed value, runs its chunks on the caller's worker, one after the other - the same
answer without the speed-up ([Concurrency](../design/CONCURRENCY.md), section 16).

## Related

- [std/task](task.md) - `Task`, `spawn` and `Workers.count`, what `workers:` defaults to.
- [std/iteration](iteration.md) - the sequential pipeline with the same words, and `Merge`.
- [The standard library](index.md) - the other packages.
