---
title: The collection traits
summary: Every kind of collection is a trait - List, Set, Map, Stack, Queue - so a signature names what a value can do, and only its construction names the data structure behind it.
kind: reference
status: stable
order: 10
keywords:
  - Collection
  - Iterable
  - ArrayList
  - TrieMap
  - HashMap
source:
  - CONCEPT.md#collections-and-iteration
  - std/collections/src/collection.trb
---

> **Not built natively yet.** A `HashMap()` built through its constructor is not built by the native back end yet, so
> `torb run` refuses the examples here that use it. `torb check` accepts them, and the rules are the language's.

A collection type is a trait, one per kind, and never a concrete data structure. A function that takes a `Map` accepts
a `TrieMap`, a `HashMap`, or a type somebody else wrote; only the expression that builds the value names which one it
is.

## Example

```trb check
fn describe<Item>(items: Collection<Item>): String {
  if items.isEmpty() { "nothing" } else { "{items.length()} items" }
}

const numbers = [1, 2, 3]
print describe(numbers)

var index: Map<String, Int> = HashMap()
index["a"] = 1
print describe(index)
```

## Syntax

```text
Iterable<Item>   Length
└────────────────────┘
Collection<Item>                 add, clear, compact, count, the participles, contains
   ├─ List<Item>                 ArrayList (default), TrieList
   ├─ Set<Item>                  TrieSet (default), HashSet
   ├─ Map<Key, Value>            TrieMap (default), HashMap        a Collection<(Key, Value)>
   ├─ Stack<Item>                ArrayStack
   └─ Queue<Item>                ArrayQueue
```

## Rules

1. **There is one trait per kind, and never a mutable and an immutable version of it.** `List<Item>` is the whole
   list; a `const` binding is the read-only list, a `var` binding is the same type with `add`, `remove` and the rest
   of the verbs available. See [Bindings](../values-and-types/bindings.md) for what decides that.

2. **`Collection<Item>` requires only `add`, `clear`, `length` and `iterator`.** `addAll`, `added`, `addedAll`,
   `contains`, `containsAll`, `count` and `compact` are default methods every implementation gets for free, from
   `std/collections`'s own `Collection` trait.

3. **`count()` answers `length()`.** `Iterable.count()` walks every value, which is the wrong answer to give a reader
   of a collection that knows its size, so `Collection` overrides it - a subtrait's member overrides the one it
   inherits rather than standing beside it.

4. **A `Collection` is not an `Accumulator`, and there is no trait for "something with `add`".** A container that is
   merely filled has no result of a run to give and no `isDone()` to answer, so it shows neither. What gathers a
   pipeline into one is a type of its own beside it - `ListAccumulator<Item>`, or `into<Target>()` for any
   `From<Iterable<Item>>` target. See [Collectors](collectors.md).

5. **`Collection` is not the bound to reach for.** A signature asks for the smallest thing it uses: `Iterable<Item>` to
   read, `Iterable<Item> & Length` to read and to size, and `Collection<Item>` where it really needs to fill as well.

6. **A trait asks nothing of its type parameters; the implementations and the factories do.** `Map<Key, Value>` places
   no bound on `Key`, but `TrieMap<Key: Hash, Value>` and `Map.of(...)` need `Key: Hash`, because only a data structure
   and a factory that actually hash the key have to say so.

   ```trb check
   fn lookup(table: Map<String, Int>, name: String): Int {
     table.get(name) ?? 0
   }

   print lookup(["Ada": 36], "Ada")
   ```

7. **An implementation is named after its data structure, and a hand-written one is a type `with` the trait.** A type
   that declares `with Map<Key, Value>` and the members the trait requires works everywhere a `Map` is expected,
   without a change anywhere else in the program.

## What this is not

**There is no `MutableList`, no `ImmutableList`, and no read-only view type.** `const` and `var` on the same
`List<Item>` are the whole story: mutability is a property of the binding, not of a second type.

```trb check
var numbers = [1, 2, 3]
numbers.add 4
print numbers
```

```trb error
const numbers = [1, 2, 3]
numbers.add 4
// error: `add` needs a `var`
```

## Related

- [Lists](lists.md) - the first implementation, and the verb/participle pairs a `List` adds to `Collection`.
- [Maps and sets](maps-and-sets.md) - the two collections keyed by more than a position.
- [Stacks and queues](stacks-and-queues.md) - the two collections with no index at all.
- [Iterating](iterating.md) - `Iterable` and `Iterator`, which every collection trait sits on top of.
- [Declaring a type](../types/declaring-a-type.md) - `with`, for a type that implements a collection trait itself.

