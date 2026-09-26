---
title: std/iteration
summary: Iterate and Iterator, the lazy stages between them, and the collectors a pipeline ends in.
kind: package
status: stable
order: 50
keywords:
  - std/iteration
  - Iterate
  - Iterator
  - Stage
  - Accumulator
  - Merge
  - pipeline
source:
  - std/iteration/src/lib.trb
  - std/iteration/src/iteration.trb
  - std/iteration/src/collectors.trb
  - std/iteration/src/concatenate.trb
  - std/iteration/src/stage.trb
  - std/iteration/src/staged.trb
---

`std/iteration` is lazy pipelines: the two traits everything is pulled through, the stages between them, and the
collectors that end one. `Stage` is written once and works the same way over `std/stream`'s `Source` (see
[std/stream](stream.md)), because it is synchronous and never asks where its values come from. Every name below is
already in scope through the prelude.

## Import

```trb fragment
use Iterator, Iterate, Length from "std/iteration"
use Accumulator, Merge, ListAccumulator, collector, mergingCollector, into, listing from "std/iteration"
use counting, summing, groupingBy, joining from "std/iteration"
use Stage, mapping, filtering, taking from "std/iteration"
use concatenated from "std/iteration"
```

```trb check
const total = [1, 2, 3, 4].filter { _ > 1 }.map { _ * 2 }.fold(0) { sum, value => sum + value }
print total
```

## Declarations

### Iterator, Length

```trb fragment
public trait Iterator<Item> {
  var fn next(): Item?
}

public trait Length {
  fn length(): Int
  fn isEmpty(): Bool
  fn isNotEmpty(): Bool
}
```

`Iterator` is a cursor over a sequence: a stateful value, so whoever pulls from one holds it in a `var`. `Length` is
just a count, shared by every collection and by `Range<Int>`.

### Iterate

```trb fragment
public trait Iterate<Item> {
  fn iterate(): Iterator<Item>
  fn map<Output>(transform: Transform<Item, Output>): Iterate<Output>
  fn filter(predicate: Predicate<Item>): Iterate<Item>
  fn flatMap<Output>(transform: Transform<Item, Iterate<Output>>): Iterate<Output>
  fn filterMap<Output>(transform: Transform<Item, Output?>): Iterate<Output>
  fn mapWhile<Output>(transform: Transform<Item, Output?>): Iterate<Output>
  fn take(amount: Int): Iterate<Item>
  fn skip(amount: Int): Iterate<Item>
  fn takeWhile(predicate: Predicate<Item>): Iterate<Item>
  fn zip<Output>(other: Iterate<Output>): Iterate<(Item, Output)>
  fn indexed(): Iterate<(index: Int, item: Item)>
  fn sorted<Key: Compare>(by: Transform<Item, Key>): Iterate<Item>
  fn through<Output>(stage: Stage<Item, Output>): Iterate<Output>
  fn collect<Output>(into: Accumulator<Item, Output>): Output
  fn to<Target: From<Iterate<Item>>>(): Target
  fn toList(): List<Item>
  fn toSet(): Set<Item> where Item: Hash
  fn joined(separator: String = ""): String where Item: Show
  fn forEach(action: Action<Item>)
  fn fold<State>(initial: State, combine: (State, Item) => State): State
  fn find(predicate: Predicate<Item>): Item?
  fn first(): Item?
  fn any(predicate: Predicate<Item>): Bool
  fn all(predicate: Predicate<Item>): Bool
  fn count(): Int
  fn sum(): Item where Item: Add & From<Int>
  fn groupBy<Key: Hash>(key: Transform<Item, Key>): Map<Key, List<Item>>
}
```

Everything that works with `for ... in` and the spread operator (`...`). Only `iterate` is required; every other
member is a default. A pipeline has three parts: a source (any `Iterate`), zero or more lazy stages (`map`, `filter`,
`take`, `sorted`, ...) that run nothing and store nothing, and one terminal operation (`collect`, `toList`, `fold`,
...) that pulls the values through. Stages answer an `Iterate` again, so a pipeline is a value that can be passed
around, extended and iterated more than once. `sorted` is lazy as a stage but has to buffer every value the moment it
is iterated, unlike the rest.

### Stage

```trb fragment
public trait Stage<Input, Output> {
  fn onto<Final>(downstream: Accumulator<Output, Final>): Accumulator<Input, Final>
  fn then<Final>(other: Stage<Output, Final>): Stage<Input, Final>
}
```

The middle of a pipeline, written once for both the pull side (`Iterate.through`) and the push side of a stream
(`Source.through`, see [std/stream](stream.md)): it turns an `Accumulator<Output, Final>` into an
`Accumulator<Input, Final>` and never asks where its `Input`s come from. `mapping`, `filtering`, `filterMapping`,
`mappingWhile`, `flatMapping`, `taking`, `takingWhile`, `skipping`, `indexing` and `chunking` are the stages this
package provides as functions rather than as members of `Stage`, because a member of a trait used as a namespace would
have to fix the trait's own type arguments. The per-stage iterators behind `Iterate.map` and its neighbors
(`Mapped`, `Filtered`, ...) are still separate code from these `Stage` values until the native back end compiles a
generic member reached through a trait-typed value (`onto<Final>`); until then the two mean the same thing.

### Accumulator

```trb fragment
public trait Accumulator<Item, Output> {
  var fn add(value: Item)
  fn finish(): Output
  fn isDone(): Bool
}

public type ListAccumulator<Item> with Merge<Item, List<Item>> {
  var fn add(value: Item)
  fn finish(): List<Item>
  fn merge(first: List<Item>, second: List<Item>): List<Item>
}
```

An `Accumulator` is what to do with a pipeline's values **and** the state of doing it, in one type: it is a value, so
`collect` fills a copy of it and one accumulator drives as many independent runs as it is handed to. There is no
`start()`. `isDone()` defaults to `false` and is asked before the first value and after every `add`, which is what lets
`taking`, `first` and `find` end a pipeline over an infinite or expensive source instead of reading it to the end.
Because values are pushed one at a time, the same accumulators work for anything that produces values over time - an
`Iterate`, a `Source`, an event stream.

A collection is not one; an accumulator that gathers into a collection is a type beside it, as `Collector` and
`Collectors.toList()` are in Java. `ListAccumulator<Item>` is the one this package ships and `listing()` answers it;
`into<Target>()` is the general one for any `From<Iterate<Item>>` target - it gathers into a `List` and calls
`Target.from` once at the end. A package that owns a collection may ship its own, and a caller **names** it at the
call. `collector(initial, finish:, step:)` writes one the functional way, as a fold with a final step:

```trb fragment
public fn counting<Item>(): Merge<Item, Int>
public fn summing<Item, Total: Add & From<Int>>(value: Transform<Item, Total>): Merge<Item, Total>
public fn averaging<Item>(value: Transform<Item, Float>): Accumulator<Item, Float?>
public fn minBy<Item, Key: Compare>(key: Transform<Item, Key>): Merge<Item, Item?>
public fn maxBy<Item, Key: Compare>(key: Transform<Item, Key>): Merge<Item, Item?>
public fn joining(separator: String = "", prefix: String = "", suffix: String = ""): Merge<String, String>
public fn partitioningBy<Item>(predicate: Predicate<Item>): Merge<Item, (List<Item>, List<Item>)>
public fn groupingBy<Item, Key: Hash>(key: Transform<Item, Key>): Grouping<Item, Key>
```

`groupingBy` answers a `Grouping`, which has its own `then(downstream)` for a different one per group:
`employees.collect(groupingBy { _.department }.then(averaging { _.salary }))`.

### Merge

```trb fragment
public trait Merge<Item, Output> with Accumulator<Item, Output> {
  fn merge(first: Output, second: Output): Output
}

public fn mergingCollector<Item, State, Output>(
  initial: State,
  finish: (State) => Output,
  merge: (Output, Output) => Output,
  step: (State, Item) => State,
): Merge<Item, Output>
```

Two partial results of one accumulator, joined: what lets an accumulator run over pieces of its input -
[`parallel()`](parallel.md)'s chunks, a divide-and-conquer fold - and still answer what one run over the whole input
answers. The contract is three lines: `merge` is **associative**; it is called **in the order of the pieces**, left to
right, so it need not be commutative; and it is **never called with the output of a piece that received no value** -
such a piece is skipped, and an input without any value takes `finish()` instead. It joins two outputs and not two
accumulators, because two trait-typed accumulators need not have the same type.

| Collector | `merge` |
|---|---|
| `listing()` | concatenation |
| `counting()`, `summing(value:)` | `+` |
| `minBy(key:)`, `maxBy(key:)` | the second piece's value only where its key is smaller (larger): the earlier of two equal keys stays |
| `joining(separator:, prefix:, suffix:)` | the first piece's suffix and the second piece's prefix come off, the separator goes between |
| `partitioningBy(predicate:)` | both lists concatenated |
| `groupingBy(key:)` | the lists of a key concatenated, the keys in the order of first sight |
| `averaging(value:)`, `into<Target>()`, `then(downstream)` | none: the count is gone, the target is anybody's, the downstream is any `Accumulator` |

`mergingCollector` is `collector` with a merge. An average merges once its output keeps what the merge needs:

```trb check
const averaged = mergingCollector((0.0, 0), finish: { _ }, merge: { first, second =>
  (first.0 + second.0, first.1 + second.1)
}) { state, value: Float => (state.0 + value, state.1 + 1) }
const total = [1.0, 2.0, 6.0].collect(averaged)
print(total.0 / Float.from(total.1))
```

### Staged and Queueing

`Iterate.through(stage)` answers a `Staged`, which is an `Iterate` again. `collect` on it is fused - the stage wraps
the accumulator directly, with no queue in between - while `iterate()` needs a small queue, because one
value pushed in can become none or many coming out while the caller asks for exactly one. `Queueing` is the
`Accumulator` that tail of a staged pipeline pushes into.

### Putting many texts together

```trb fragment
fn concatenated(pieces: List<String>, separator: String): String
```

The pieces in one text, with `separator` between every two of them. It is what `joined` and the `joining` collector are
built on, and it is what to reach for instead of appending in a loop: a `String` is a value, so appending to one copies
everything that is already in it, and `n` appends copy `O(n²)` bytes. `concatenated` merges neighbours pairwise, so every
byte is copied once per level of the merge tree and there are `log n` levels.

```trb check
use concatenated from "std/iteration"

print concatenated(["a", "b", "c"], ", ")
```

## Related

- [std/collections](collections.md) - the collections a pipeline is gathered into.
- [std/stream](stream.md) - `Stage` on the asynchronous side, `Source.through` and `Sink`.
- [The standard library](index.md) - the other packages.

