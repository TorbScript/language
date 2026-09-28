---
title: std/collections
summary: The collection traits every signature talks about, and the implementations that only show up where one is built.
kind: package
status: stable
order: 40
keywords:
  - std/collections
  - List
  - Map
  - Set
  - Stack
  - Queue
source:
  - std/collections/src/lib.trb
  - std/collections/src/list.trb
  - std/collections/src/map.trb
  - std/collections/src/set.trb
  - std/collections/src/stack.trb
  - std/collections/src/queue.trb
---

`std/collections` is one trait per kind of collection - `List`, `Map`, `Set`, `Stack`, `Queue` - and the
implementations behind them. A signature, a field or a binding names the trait; the implementation (`ArrayList`,
`HashMap`, ...) only shows up where something is constructed. Every name below is already in scope through the prelude.

**An `Array` is not a collection**, and it is not here: it is the inline storage primitive a data structure is written
on top of, so it lives in [std/core](core.md) next to the other types the language itself refers to.

## Import

```trb fragment
use List, ArrayList, Map, TrieMap, Set, TrieSet from "std/collections"
use Stack, ArrayStack, Queue, ArrayQueue from "std/collections"
```

```trb check
var numbers: List<Int> = [3, 1, 2]
numbers.append 4
print numbers.reversed()
```

## Declarations

Every kind comes `with Iterate<Item>, Length` directly and spends the words everybody knows for its structure: a list
appends, a set inserts, a map sets, a stack pushes and pops, a queue enqueues and dequeues. There is no shared
`Collection` trait and no shared `add`. `clear`, `compact`, `contains` and `count` are declared by each kind that has
them, and `count()` answers `length()` rather than walking. A collection is **not** an `Accumulator`: what gathers a
pipeline into one is a type beside it (`ListAccumulator`, `into<Target>()` - see [std/iteration](iteration.md)).

### List

```trb fragment
public trait List<Item>
  with Iterate<Item>, Length, MutableIndex<Int, Item>, MutableSlice
{
  static fn of(...items: Item): List<Item>
  static fn filled(count: Int, value: Item): List<Item>
  var fn append(value: Item)
  var fn appendAll(values: Iterate<Item>)
  var fn insert(index: Int, value: Item)
  var fn removeAt(index: Int): Item?
  var fn reverse()
  var fn clear()
  var fn compact()
  var fn sort<Key: Compare>(by: Transform<Item, Key>)
  fn sorted<Key: Compare>(by: Transform<Item, Key>): Self
  var fn remove(value: Item): Bool where Item: Equals
  var fn swapAt(first: Int, second: Int)
  var fn update(index: Int, change: (var element: Item) => Void)
  fn appended(value: Item): Self
  fn appendedAll(values: Iterate<Item>): Self
  fn count(): Int
  fn contains(value: Item): Bool where Item: Equals
  fn first(): Item?
  fn last(): Item?
  fn indexOf(value: Item): Int? where Item: Equals
  fn part(range: Bounds<Int>): Self?
}
```

An ordered sequence, addressable by index - the type of the literal `[1, 2, 3]`. Every verb (`append`, `insert`,
`removeAt`, `reverse`, `sort`, `remove`, `swapAt`, `update`) needs a `var` path and has a participle that returns a
changed copy (`appended`, `inserted`, `removed`, `reversed`, ...). `sorted` is one of them: it overrides the lazy
`Iterate.sorted` stage and answers a list of the same kind. `compact()` hands back whatever storage the list holds
beyond its length. `ArrayList` is the default implementation, a contiguous growable buffer shared between copies and
slices until one of them is written to; `TrieList` is a bit-partitioned trie for a list kept in many versions at once,
and is a documented alias of `ArrayList` until the trie exists.

`list[index]` panics with `index 9 is out of bounds for a length of 3` where `get(index)` answers `None`, and
`list[from..to]` panics where `part(from..to)` answers `None`: every partial read has its total twin beside it.

`ArrayList.withCapacity(capacity)` builds an empty list with room for `capacity` items before it has to grow again -
what to reach for ahead of a loop of `append`s whose count is already known, instead of the default empty list that
grows as it goes. `TrieList`, `TrieMap`, `HashMap`, `TrieSet` and `HashSet` each have the same static member, for the
same reason.

### Map

```trb fragment
public trait Map<Key, Value>
  with Iterate<(key: Key, value: Value)>, Length, MutableIndex<Key, Value>
{
  static fn of(...entries: (Key, Value)): Map<Key, Value> where Key: Hash
  var fn remove(key: Key): Value?
  var fn clear()
  var fn getOrInsert(key: Key, fallback: lazy Value): Value
  var fn update(key: Key, fallback: lazy Value, change: (var Value) => Void)
  fn updated(key: Key, value: Value): Self
  fn removed(key: Key): Self
  fn count(): Int
  fn containsKey(key: Key): Bool
  fn keys(): Iterate<Key>
  fn values(): Iterate<Value>
  fn mapValues<Output>(transform: Transform<Value, Output>): Map<Key, Output> where Key: Hash
}
```

`map[key]` of a missing key panics with `the key "Alan" is not in the map` - the key as it shows inside of another
value, cut after 60 characters, or `the key` alone where its type has no `Show` - and `get(key)` answers `None`.

Mapping from keys to values, iterated as `(key, value)` tuples. Its verb is `set` (`map[key] = value`), whose
participle is `updated`. The trait asks nothing of `Key`; what a key has to be able to do is a matter of the
implementation (`TrieMap` and `HashMap` need `Hash`). **Every implementation iterates in insertion order**, and
removing an entry does not reorder the rest; an empty map shows as `[:]`. `TrieMap` is the default (a hash array
mapped trie, cheap to keep in many versions); `HashMap` is a flat hash table with the fastest lookups and writes, at
the cost of copying all of a shared table on a write.

### Set

```trb fragment
public trait Set<Item>
  with Iterate<Item>, Length
{
  static fn of(...items: Item): Set<Item> where Item: Hash
  fn contains(value: Item): Bool
  fn count(): Int
  var fn insert(value: Item)
  var fn insertAll(values: Iterate<Item>)
  var fn remove(value: Item): Bool
  var fn removeAll(values: Iterate<Item>)
  var fn retainAll(values: Set<Item>)
  var fn clear()
  fn inserted(value: Item): Self
  fn insertedAll(values: Iterate<Item>): Self
  fn removed(value: Item): Self
  fn union(other: Iterate<Item>): Self
  fn intersection(other: Set<Item>): Self
  fn difference(other: Iterate<Item>): Self
}
```

A collection without duplicates; `insert` of a value already present does nothing. The set operations (`union`,
`intersection`, `difference`) are nouns and never change anything, unlike the verbs above them. `{a, b}` is how a set
shows - braces, so a set is never mistaken for a list, and `{}` is empty. `TrieSet` and `HashSet` mirror `TrieMap`
and `HashMap`.

### Stack and Queue

```trb fragment
public trait Stack<Item>
  with Iterate<Item>, Length
{
  static fn of(...items: Item): Stack<Item>
  var fn push(value: Item)
  var fn pop(): Item?
  var fn clear()
  var fn compact()
  fn peek(): Item?
  fn count(): Int
}

public trait Queue<Item>
  with Iterate<Item>, Length
{
  static fn of(...items: Item): Queue<Item>
  var fn enqueue(value: Item)
  var fn dequeue(): Item?
  var fn clear()
  var fn compact()
  fn peek(): Item?
  fn count(): Int
}
```

`Stack` is LIFO and iterates from top to bottom: `push` puts an item on top, `pop` takes it off, `peek` looks at it
without taking it. `Queue` is FIFO and iterates front to back: `enqueue` puts an item at the back, `dequeue` takes the
one at the front, `peek` looks at the front. Neither has participles - a stack or a queue is a value, so a `var` copy
is taken from while the original keeps every item. `ArrayStack` is a `List` underneath; `ArrayQueue` is a ring buffer
that grows only when it is full. Both are `Equals` and `Hash` wherever `Item` is, order-dependent like a `List`.

### Flags

```trb fragment
public type Flags<Case: RawValue> with Iterate<Case>, Length, From<Iterate<Case>>, TryFrom<UInt64, FlagsError> {
  static fn of(...cases: Case): Self
  static fn fromBits(mask: UInt64): Result<Self, FlagsError>
  fn bits(): UInt64
  fn contains(flag: Case): Bool
  var fn insert(flag: Case)
  var fn insertAll(flags: Iterate<Case>)
  var fn remove(flag: Case): Bool
  var fn removeAll(flags: Iterate<Case>)
  var fn clear()
  fn inserted(flag: Case): Self
  fn removed(flag: Case): Self
  static fn union(first: Self, second: Self): Self
  static fn intersection(first: Self, second: Self): Self
  static fn difference(first: Self, second: Self): Self
}
```

A set of the cases of a type whose cases stand for powers of two (`case Read = 1`,
[Cases that stand for numbers](../language/types/case-values.md)), stored as one bit mask: the flags of a C API, a
file format or a protocol. A list literal builds one, it has the words of a `Set`, and it iterates over the cases that
are set in the order of their bits - which is why it is no `Set`, whose order is the insertion order. The checker holds
every case of the type argument to be a power of two wherever `Flags<Case>` is written. `bits()` and `UInt64.from(flags)`
write the mask, `fromBits` and `tryFrom` read one back and answer `FlagsError` for a bit no case stands for, and that
pair is how a value is encoded: as the number. Not in the prelude: `use Flags from "std/collections"`.

## Related

- [Mutation and var paths](../language/types/var-paths.md) - what has to be `var` for a verb like `append` or `sort`.
- [std/iteration](iteration.md) - `Iterate`, the lazy stages and the collectors every collection works with.
- [The standard library](index.md) - the other packages.

