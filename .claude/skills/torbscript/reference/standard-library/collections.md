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
  - std/collections/src/collection.trb
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
use Collection, List, ArrayList, Map, TrieMap, Set, TrieSet from "std/collections"
use Stack, ArrayStack, Queue, ArrayQueue from "std/collections"
```

```trb check
var numbers: List<Int> = [3, 1, 2]
numbers.add 4
print numbers.reversed()
```

## Declarations

<!-- torb:declarations:begin -->

### Collection

```trb fragment
public trait Collection<Item>
  with Iterable<Item>, Length, Accumulator<Item, Self>
{
  var fn add(value: Item)
  var fn clear()
  var fn addAll(values: Iterable<Item>)
  fn added(value: Item): Self
  fn addedAll(values: Iterable<Item>): Self
  fn contains(value: Item): Bool where Item: Equals
  fn containsAll(values: Iterable<Item>): Bool where Item: Equals
}
```

Only `add`, `clear`, `length` and `iterator` are required; everything else is a default. A collection already is an
`Accumulator` - it has `add`, and it is its own result - so every collection can be the target of a collector, a
channel or an event stream. `added`/`addedAll` are the participles of `add`/`addAll`: a changed copy, usable through a
`const` binding.

### List

```trb fragment
public trait List<Item>
  with Collection<Item>, MutableIndexed<Int, Item>, MutableSlice
{
  static fn of(...items: Item): List<Item>
  static fn filled(count: Int, value: Item): List<Item>
  var fn insert(index: Int, value: Item)
  var fn removeAt(index: Int): Item?
  var fn reverse()
  var fn sort<Key: Compare>(by: (value: Item) => Key)
  var fn remove(value: Item): Bool where Item: Equals
  var fn swapAt(first: Int, second: Int)
  var fn update(index: Int, change: (var element: Item) => Void)
  fn first(): Item?
  fn last(): Item?
  fn indexOf(value: Item): Int? where Item: Equals
}
```

An ordered sequence, addressable by index - the type of the literal `[1, 2, 3]`. Every verb (`insert`, `removeAt`,
`reverse`, `sort`, `remove`, `swapAt`, `update`) needs a `var` path and has a participle that returns a changed copy
(`inserted`, `removed`, `reversed`, ...). `ArrayList` is the default implementation, a contiguous growable buffer shared
between copies and slices until one of them is written to; `TrieList` is a bit-partitioned trie for a list kept in many
versions at once, and is a documented alias of `ArrayList` until the trie exists.

### Map

```trb fragment
public trait Map<Key, Value>
  with Collection<(Key, Value)>, MutableIndexed<Key, Value>
{
  static fn of(...entries: (Key, Value)): Map<Key, Value> where Key: Hash
  var fn remove(key: Key): Value?
  var fn merge(other: Iterable<(Key, Value)>)
  var fn getOrInsert(key: Key, fallback: lazy Value): Value
  var fn update(key: Key, fallback: lazy Value, change: (var Value) => Void)
  fn containsKey(key: Key): Bool
  fn keys(): Iterable<Key>
  fn values(): Iterable<Value>
  fn mapValues<Output>(transform: (value: Value) => Output): Map<Key, Output> where Key: Hash
}
```

Mapping from keys to values, consisting of `(Key, Value)` tuples as a collection (`add((key, value))` is
`set(key, value)`). The trait asks nothing of `Key`; what a key has to be able to do is a matter of the implementation
(`TrieMap` and `HashMap` need `Hash`). **Every implementation iterates in insertion order**, and removing an entry does
not reorder the rest; an empty map shows as `[:]`. `TrieMap` is the default (a hash array mapped trie, cheap to keep in
many versions); `HashMap` is a flat hash table with the fastest lookups and writes, at the cost of copying all of a
shared table on a write.

### Set

```trb fragment
public trait Set<Item>
  with Collection<Item>
{
  static fn of(...items: Item): Set<Item> where Item: Hash
  fn contains(value: Item): Bool
  var fn remove(value: Item): Bool
  var fn removeAll(values: Iterable<Item>)
  var fn retainAll(values: Set<Item>)
  fn union(other: Iterable<Item>): Self
  fn intersection(other: Set<Item>): Self
  fn difference(other: Iterable<Item>): Self
  fn isSubsetOf(other: Set<Item>): Bool
}
```

A collection without duplicates; `add` of a value already present does nothing. The set operations (`union`,
`intersection`, `difference`) are nouns and never change anything, unlike the verbs above them. `{a, b}` is the literal
- braces, so a set is never mistaken for a list, and `{}` is empty. `TrieSet` and `HashSet` mirror `TrieMap` and
`HashMap`.

### Stack and Queue

```trb fragment
public trait Stack<Item>
  with Collection<Item>
{
  var fn push(value: Item)
  var fn pop(): Item?
  fn popped(): (Item, Self)?
  fn peek(): Item?
}

public trait Queue<Item>
  with Collection<Item>
{
  var fn enqueue(value: Item)
  var fn dequeue(): Item?
  fn dequeued(): (Item, Self)?
  fn peek(): Item?
}
```

`Stack` is LIFO (`add` is `push`, and iterates from top to bottom); `Queue` is FIFO (`add` is `enqueue`, and iterates in
the order of `dequeue`). `popped()`/`dequeued()` are the participle form for a `const` binding: the top or next element
together with the rest, or `None` when empty. `ArrayStack` is a `List` underneath; `ArrayQueue` is a ring buffer that
grows only when it is full. Both are `Equals` and `Hash` wherever `Item` is, order-dependent like a `List`.

<!-- torb:declarations:end -->

## Related

- [Mutation and var paths](../language/types/var-paths.md) - what has to be `var` for a verb like `add` or `sort`.
- [std/iteration](iteration.md) - `Iterable`, the lazy stages and the collectors every collection works with.
- [The standard library](index.md) - the other packages.
