---
title: std/iteration
summary: Iterable and Iterator, the lazy stages between them, and the collectors a pipeline ends in.
kind: package
status: stable
order: 50
keywords:
  - std/iteration
  - Iterable
  - Iterator
  - Stage
  - Collector
  - pipeline
source:
  - std/iteration/src/lib.trb
  - std/iteration/src/iteration.trb
  - std/iteration/src/collectors.trb
  - std/iteration/src/stage.trb
  - std/iteration/src/staged.trb
---

`std/iteration` is lazy pipelines: the two traits everything is pulled through, the stages between them, and the
collectors that end one. `Stage` is written once and works the same way over `std/stream`'s `Source` (see
[std/stream](stream.md)), because it is synchronous and never asks where its values come from. Every name below is
already in scope through the prelude.

## Import

```trb fragment
use Iterator, Iterable, Length from "std/iteration"
use Collector, Accumulator, collector, into, listing from "std/iteration"
use counting, summing, groupingBy, joining from "std/iteration"
use Stage, mapping, filtering, taking from "std/iteration"
```

```trb check
const total = [1, 2, 3, 4].filter { _ > 1 }.map { _ * 2 }.fold(0) { sum, value => sum + value }
print total
```

## Declarations

<!-- torb:declarations:begin -->

### Iterator, Length

```trb fragment
public trait Iterator<Item> {
  fn next(var self): Item?
}

public trait Length {
  fn length(self): Int
  fn isEmpty(self): Bool
  fn isNotEmpty(self): Bool
}
```

`Iterator` is a cursor over a sequence: a stateful value, so whoever pulls from one holds it in a `var`. `Length` is
just a count, shared by every collection and by `Range<Int>`.

### Iterable

```trb fragment
public trait Iterable<Item> {
  fn iterator(self): Iterator<Item>
  fn map<Output>(self, transform: (value: Item) => Output): Iterable<Output>
  fn filter(self, predicate: (value: Item) => Bool): Iterable<Item>
  fn flatMap<Output>(self, transform: (value: Item) => Iterable<Output>): Iterable<Output>
  fn filterMap<Output>(self, transform: (value: Item) => Output?): Iterable<Output>
  fn mapWhile<Output>(self, transform: (value: Item) => Output?): Iterable<Output>
  fn take(self, amount: Int): Iterable<Item>
  fn skip(self, amount: Int): Iterable<Item>
  fn takeWhile(self, predicate: (value: Item) => Bool): Iterable<Item>
  fn zip<Output>(self, other: Iterable<Output>): Iterable<(Item, Output)>
  fn indexed(self): Iterable<(Int, Item)>
  fn sorted<Key: Compare>(self, by: (value: Item) => Key): Iterable<Item>
  fn through<Output>(self, stage: Stage<Item, Output>): Iterable<Output>
  fn collect<Output>(self, collector: Collector<Item, Output>): Output
  fn to<Target: From<Iterable<Item>>>(self): Target
  fn toList(self): List<Item>
  fn toSet(self): Set<Item> where Item: Hash
  fn joined(self, separator: String = ""): String where Item: Show
  fn forEach(self, action: (value: Item) => Void)
  fn fold<State>(self, initial: State, combine: (State, Item) => State): State
  fn find(self, predicate: (value: Item) => Bool): Item?
  fn first(self): Item?
  fn any(self, predicate: (value: Item) => Bool): Bool
  fn all(self, predicate: (value: Item) => Bool): Bool
  fn count(self): Int
  fn sum(self): Item where Item: Add & From<Int>
  fn groupBy<Key: Hash>(self, key: (value: Item) => Key): Map<Key, List<Item>>
}
```

Everything that works with `for ... in` and the spread operator (`...`). Only `iterator` is required; every other
member is a default. A pipeline has three parts: a source (any `Iterable`), zero or more lazy stages (`map`, `filter`,
`take`, `sorted`, ...) that run nothing and store nothing, and one terminal operation (`collect`, `toList`, `fold`,
...) that pulls the values through. Stages answer an `Iterable` again, so a pipeline is a value that can be passed
around, extended and iterated more than once. `sorted` is lazy as a stage but has to buffer every value the moment it
is iterated, unlike the rest.

### Stage

```trb fragment
public trait Stage<Input, Output> {
  fn onto<Final>(self, downstream: Accumulator<Output, Final>): Accumulator<Input, Final>
  fn then<Final>(self, other: Stage<Output, Final>): Stage<Input, Final>
}
```

The middle of a pipeline, written once for both the pull side (`Iterable.through`) and the push side of a stream
(`Source.through`, see [std/stream](stream.md)): it turns an `Accumulator<Output, Final>` into an
`Accumulator<Input, Final>` and never asks where its `Input`s come from. `mapping`, `filtering`, `filterMapping`,
`mappingWhile`, `flatMapping`, `taking`, `takingWhile`, `skipping`, `indexing` and `chunking` are the stages this
package provides as functions rather than as members of `Stage`, because a member of a trait used as a namespace would
have to fix the trait's own type arguments. The per-stage iterators behind `Iterable.map` and its neighbors
(`Mapped`, `Filtered`, ...) are still separate code from these `Stage` values until the native back end compiles a
generic member reached through a trait-typed value (`onto<Final>`); until then the two mean the same thing.

### Collector, Accumulator

```trb fragment
public trait Accumulator<Item, Output> {
  fn add(var self, value: Item)
  fn finish(self): Output
  fn isDone(self): Bool
}

public trait Collector<Item, Output> {
  fn start(self): Accumulator<Item, Output>
}
```

A `Collector` is a reusable description of what to do with a pipeline's values; `start()` creates a fresh `Accumulator`
for one run, held in a `var`. `isDone()` defaults to `false` and is asked before the first value and after every `add`,
which is what lets `taking`, `first` and `find` end a pipeline over an infinite or expensive source instead of reading
it to the end. Because values are pushed one at a time, the same collectors work for anything that produces values over
time - an `Iterable`, a `Source`, an event stream.

`into<Target>()` collects into any `From<Iterable<Item>>`; `listing()` is `into<List<Item>>()`. `collector(initial,
finish:, step:)` writes one the functional way, as a fold with a final step:

```trb fragment
public fn counting<Item>(): Collector<Item, Int>
public fn summing<Item, Total: Add & From<Int>>(value: (value: Item) => Total): Collector<Item, Total>
public fn averaging<Item>(value: (value: Item) => Float): Collector<Item, Float?>
public fn minBy<Item, Key: Compare>(key: (value: Item) => Key): Collector<Item, Item?>
public fn maxBy<Item, Key: Compare>(key: (value: Item) => Key): Collector<Item, Item?>
public fn joining(separator: String = "", prefix: String = "", suffix: String = ""): Collector<String, String>
public fn partitioningBy<Item>(predicate: (value: Item) => Bool): Collector<Item, (List<Item>, List<Item>)>
public fn groupingBy<Item, Key: Hash>(key: (value: Item) => Key): Grouping<Item, Key>
```

`groupingBy` answers a `Grouping`, which has its own `then(downstream)` for a collector per group:
`employees.collect(groupingBy { _.department }.then(averaging { _.salary }))`.

### Staged and Queueing

`Iterable.through(stage)` answers a `Staged`, which is an `Iterable` again. `collect` on it is fused - the stage wraps
the collector's accumulator directly, with no queue in between - while `iterator()` needs a small queue, because one
value pushed in can become none or many coming out while the caller asks for exactly one. `Queueing` is the
`Accumulator` that tail of a staged pipeline pushes into.

<!-- torb:declarations:end -->

## Related

- [std/collections](collections.md) - the collections that already are an `Iterable` and an `Accumulator`.
- [std/stream](stream.md) - `Stage` on the asynchronous side, `Source.through` and `Sink`.
- [The standard library](index.md) - the other packages.
