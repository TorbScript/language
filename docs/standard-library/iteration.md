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
  - Accumulator
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
use Iterator, Iterable, Length from "std/iteration"
use Accumulator, ListAccumulator, collector, into, listing from "std/iteration"
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

### Iterable

```trb fragment
public trait Iterable<Item> {
  fn iterator(): Iterator<Item>
  fn map<Output>(transform: (value: Item) => Output): Iterable<Output>
  fn filter(predicate: (value: Item) => Bool): Iterable<Item>
  fn flatMap<Output>(transform: (value: Item) => Iterable<Output>): Iterable<Output>
  fn filterMap<Output>(transform: (value: Item) => Output?): Iterable<Output>
  fn mapWhile<Output>(transform: (value: Item) => Output?): Iterable<Output>
  fn take(amount: Int): Iterable<Item>
  fn skip(amount: Int): Iterable<Item>
  fn takeWhile(predicate: (value: Item) => Bool): Iterable<Item>
  fn zip<Output>(other: Iterable<Output>): Iterable<(Item, Output)>
  fn indexed(): Iterable<(index: Int, item: Item)>
  fn sorted<Key: Compare>(by: (value: Item) => Key): Iterable<Item>
  fn through<Output>(stage: Stage<Item, Output>): Iterable<Output>
  fn collect<Output>(into: Accumulator<Item, Output>): Output
  fn to<Target: From<Iterable<Item>>>(): Target
  fn toList(): List<Item>
  fn toSet(): Set<Item> where Item: Hash
  fn joined(separator: String = ""): String where Item: Show
  fn forEach(action: (value: Item) => Void)
  fn fold<State>(initial: State, combine: (State, Item) => State): State
  fn find(predicate: (value: Item) => Bool): Item?
  fn first(): Item?
  fn any(predicate: (value: Item) => Bool): Bool
  fn all(predicate: (value: Item) => Bool): Bool
  fn count(): Int
  fn sum(): Item where Item: Add & From<Int>
  fn groupBy<Key: Hash>(key: (value: Item) => Key): Map<Key, List<Item>>
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
  fn onto<Final>(downstream: Accumulator<Output, Final>): Accumulator<Input, Final>
  fn then<Final>(other: Stage<Output, Final>): Stage<Input, Final>
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

### Accumulator

```trb fragment
public trait Accumulator<Item, Output> {
  var fn add(value: Item)
  fn finish(): Output
  fn isDone(): Bool
}

public type ListAccumulator<Item> with Accumulator<Item, List<Item>> {
  var fn add(value: Item)
  fn finish(): List<Item>
}
```

An `Accumulator` is what to do with a pipeline's values **and** the state of doing it, in one type: it is a value, so
`collect` fills a copy of it and one accumulator drives as many independent runs as it is handed to. There is no
`start()`. `isDone()` defaults to `false` and is asked before the first value and after every `add`, which is what lets
`taking`, `first` and `find` end a pipeline over an infinite or expensive source instead of reading it to the end.
Because values are pushed one at a time, the same accumulators work for anything that produces values over time - an
`Iterable`, a `Source`, an event stream.

A `Collection` is not one; an accumulator that gathers into a collection is a type beside it, as `Collector` and
`Collectors.toList()` are in Java. `ListAccumulator<Item>` is the one this package ships and `listing()` answers it;
`into<Target>()` is the general one for any `From<Iterable<Item>>` target - it gathers into a `List` and calls
`Target.from` once at the end. A package that owns a collection may ship its own, and a caller **names** it at the
call. `collector(initial, finish:, step:)` writes one the functional way, as a fold with a final step:

```trb fragment
public fn counting<Item>(): Accumulator<Item, Int>
public fn summing<Item, Total: Add & From<Int>>(value: (value: Item) => Total): Accumulator<Item, Total>
public fn averaging<Item>(value: (value: Item) => Float): Accumulator<Item, Float?>
public fn minBy<Item, Key: Compare>(key: (value: Item) => Key): Accumulator<Item, Item?>
public fn maxBy<Item, Key: Compare>(key: (value: Item) => Key): Accumulator<Item, Item?>
public fn joining(separator: String = "", prefix: String = "", suffix: String = ""): Accumulator<String, String>
public fn partitioningBy<Item>(predicate: (value: Item) => Bool): Accumulator<Item, (List<Item>, List<Item>)>
public fn groupingBy<Item, Key: Hash>(key: (value: Item) => Key): Grouping<Item, Key>
```

`groupingBy` answers a `Grouping`, which has its own `then(downstream)` for a different one per group:
`employees.collect(groupingBy { _.department }.then(averaging { _.salary }))`.

### Staged and Queueing

`Iterable.through(stage)` answers a `Staged`, which is an `Iterable` again. `collect` on it is fused - the stage wraps
the accumulator directly, with no queue in between - while `iterator()` needs a small queue, because one
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
