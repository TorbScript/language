---
title: The collection traits
summary: Every kind of collection is a trait - List, Set, Map, Stack, Queue - so a signature names what a value can do, and only its construction names the data structure behind it.
kind: reference
status: stable
order: 10
keywords:
  - collection
  - Iterate
  - ArrayList
  - TrieMap
  - HashMap
source:
  - CONCEPT.md#collections-and-iteration
  - std/collections/src/list.trb
---

A collection type is a trait, one per kind, and never a concrete data structure. A function that takes a `Map` accepts
a `TrieMap`, a `HashMap`, or a type somebody else wrote; only the expression that builds the value names which one it
is.

## Example

```trb check
fn describe<Item>(items: Iterate<Item> & Length): String {
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
                 Iterate<Item>   Length
   ┌──────────────┬──────────────┬──────────────┬──────────────┐
List<Item>     Set<Item>     Map<Key, Value>  Stack<Item>    Queue<Item>
append         insert        set              push           enqueue
insert         remove        remove           pop, peek      dequeue, peek
removeAt
ArrayList      TrieSet       TrieMap          ArrayStack     ArrayQueue
TrieList       HashSet       HashMap
```

## Rules

1. **There is one trait per kind, and never a mutable and an immutable version of it.** `List<Item>` is the whole
   list; a `const` binding is the read-only list, a `var` binding is the same type with `append`, `remove` and the
   rest of the verbs available. See [Bindings](../values-and-types/bindings.md) for what decides that.

2. **Every kind has its own words, the ones everybody knows for that structure.** A list appends, a set inserts, a map
   sets, a stack pushes and pops, a queue enqueues and dequeues. There is no shared `add`: at the back of a list, in a
   set and on top of a stack are different meanings, and the word at the call says which one it is.

3. **The five kinds come `with Iterate<Item>, Length` directly, and there is no `Collection` trait above them.**
   `clear`, `compact`, `contains` and `count` are declared by each kind that has them, and `count()` answers
   `length()` there: `Iterate.count()` walks every value, which is the wrong answer for something that knows its size.

4. **A collection is not an `Accumulator`.** A container that is merely filled has no result of a run to give and no
   `isDone()` to answer, so it shows neither. What gathers a pipeline into one is a type of its own beside it -
   `ListAccumulator<Item>`, or `into<Target>()` for any `From<Iterate<Item>>` target. See [Collectors](collectors.md).

5. **A signature asks for the smallest thing it uses.** `Iterate<Item>` to read, `Iterate<Item> & Length` to read and
   to size, and the kind itself - `List<Item>`, `Set<Item>` - where it changes the value.

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
numbers.append 4
print numbers
```

```trb error
const numbers = [1, 2, 3]
numbers.append 4
// error: `append` needs a `var`
```

## Related

- [Lists](lists.md) - the first kind, and the verb/participle pairs a `List` has.
- [Maps and sets](maps-and-sets.md) - the two collections keyed by more than a position.
- [Stacks and queues](stacks-and-queues.md) - the two collections with no index at all.
- [Iterating](iterating.md) - `Iterate` and `Iterator`, which every collection trait sits on top of.
- [Declaring a type](../types/declaring-a-type.md) - `with`, for a type that implements a collection trait itself.
