---
title: Stacks and queues
summary: Stack is LIFO and Queue is FIFO, both spend the two words add and remove, and the type name says which end they reach.
kind: reference
status: stable
order: 40
keywords:
  - Stack
  - Queue
  - ArrayStack
  - ArrayQueue
  - remove
  - removed
source:
  - std/collections/src/queue.trb
  - std/collections/src/stack.trb
---

`Stack<Item>` is last-in-first-out and `Queue<Item>` is first-in-first-out, and both spend the same two words:
`add` puts an item in, `remove` takes out the one the structure hands over next - the top of a stack, the front of a
queue. Neither has an index; the only way to look inside one is to remove from it, or to iterate it.

## Example

```trb check
var pending = Queue.of 1
pending.add 2
print pending.remove()

var brackets = Stack<Char>.of()
brackets.add '('
print brackets.remove()
```

## Syntax

```text
Stack.of(...items)          the last argument ends up on top
Queue.of(...items)          the first argument ends up at the front

stack.add(value)    stack.remove(): Item?          in place
queue.add(value)    queue.remove(): Item?          in place

stack.added(value): Self              queue.added(value): Self
stack.removed(): (Item, Self)?        queue.removed(): (Item, Self)?

stack.first()   queue.first()         look without taking
```

## Rules

1. **The type name carries the word, so one verb is enough for both ends.** `add` comes from
   [Collection](collection-traits.md) and `remove` is declared by each trait, and `stack.remove()` and
   `queue.remove()` read correctly because the receiver says which end is meant. That is also what lets `addAll`, a
   collector or a channel fill either kind without knowing which one it got.

2. **A `Stack` iterates from top to bottom; a `Queue` iterates front to back.** Both iterate in the order they hand
   items out, so `first()` is what the next `remove()` would answer, without removing anything.

   ```trb check
   const stack = Stack.of 1, 2, 3
   print stack.first()
   print stack.toList()
   ```

3. **The participle answers a pair, not a bare value, because removing loses information a copy has to keep.**
   `removed()` answers `(Item, Self)?`: the removed item next to the stack or queue it came from, or `None` when there
   was nothing to remove. A list's `removedAt(index)` answers `Self` alone, because `list[index]` is there to be read
   first; a stack has no index.

   ```trb check
   const stack = Stack.of(1, 2).added(3)
   if const Some((top, rest)) = stack.removed() {
     print "top: {top}, remaining: {rest.length()}, before: {stack.length()}"
   }
   ```

4. **`Equals` and `Hash` follow the items, and both are order-dependent like a `List`'s.** Two stacks or two queues are
   equal when they hold equal items in the order they iterate in - top down for a `Stack`, front to back for a `Queue` -
   so two stacks with the same items in a different order are unequal and need not hash alike.

   ```trb check
   const stack = Stack.of 1, 2
   print(stack == Stack.of(1, 2))
   print(stack == Stack.of(2, 1))
   ```

5. **Each trait has exactly one implementation, written in plain TorbScript rather than as a `native type`.**
   `ArrayStack` is a `List` used from the end; `ArrayQueue` is a ring buffer on top of a `List`, so nothing is moved
   and nothing is allocated until the buffer is full. Unlike `List`, `Map` and `Set`, there is no second, trie-backed
   implementation of either trait.

## What this is not

**Neither trait has an index.** There is no `stack[0]`: the only way to see an element is to remove it or to iterate
the whole collection.

```trb check
const stack = Stack.of 1, 2, 3
print stack.toList()
```

```trb error
const stack = Stack.of 1, 2, 3
print stack[0]
// error: `Stack<Int64>` does not implement `Indexed`, so `a[key]` has no meaning for it
```

**`remove()` here and `remove(value)` on a `List` or a `Set` are not the same member**, and they never meet: a type has
one namespace of members, so a type is a stack or a set and never both. Which one a call means is decided by the
receiver's type, not by the number of arguments.

## Related

- [The collection traits](collection-traits.md) - `Collection` and `Iterable`, which both traits sit on top of.
- [Lists](lists.md) - the `List` each default implementation is built from.
- [Verbs and participles](../types/verbs-and-participles.md) - why `removed()` answers the pair and `removedAt` does not.
- [Option](../values-and-types/option.md) - the `Item?` that `remove()`, `first()` and `last()` all answer.
