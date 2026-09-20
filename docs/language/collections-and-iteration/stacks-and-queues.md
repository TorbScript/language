---
title: Stacks and queues
summary: Stack is LIFO with push and pop, Queue is FIFO with enqueue and dequeue, and Collection.add reaches whichever end the kind chooses.
kind: reference
status: stable
order: 40
keywords:
  - Stack
  - Queue
  - ArrayStack
  - ArrayQueue
  - push
  - dequeue
source:
  - std/collections/src/queue.trb
  - std/collections/src/stack.trb
---

`Stack<Item>` is last-in-first-out: `push` and `pop` both work on the top. `Queue<Item>` is first-in-first-out:
`enqueue` adds at the back, `dequeue` removes from the front. Neither has an index; the only way to look inside one is
to remove from it, or to iterate it.

## Example

```trb check
var pending = Queue.of 1
pending.enqueue 2
print pending.dequeue()

var brackets = Stack<Char>.of()
brackets.push '('
print brackets.pop()
```

## Syntax

```text
Stack.of(...items)          the last argument ends up on top
Queue.of(...items)          the first argument ends up at the front

stack.push(value)   stack.pop(): Item?           in place
queue.enqueue(value)   queue.dequeue(): Item?     in place

stack.pushed(value): Self             queue.enqueued(value): Self
stack.popped(): (Item, Self)?         queue.dequeued(): (Item, Self)?
```

## Rules

1. **`Collection.add` is `push` for a `Stack` and `enqueue` for a `Queue`.** Both traits give `add` as a one-line
   default over their own verb, which is what lets `addAll` and a collector target either kind without knowing which
   one it got.

2. **A `Stack` iterates from top to bottom; a `Queue` iterates in the order of `dequeue`, front to back.** `peek()` on
   either one answers `first()` without removing anything, which is why it agrees with what the next `pop()` or
   `dequeue()` would return.

   ```trb check
   const stack = Stack.of 1, 2, 3
   print stack.peek()
   print stack.toList()
   ```

3. **The participle of `push`/`pop`/`enqueue`/`dequeue` returns a tuple, not a bare value, because removing loses
   information a copy has to keep.** `popped()` and `dequeued()` answer `(Item, Self)?`: the removed item next to the
   stack or queue it came from, or `None` when there was nothing to remove.

   ```trb check
   const stack = Stack.of(1, 2).pushed(3)
   if const Some((top, rest)) = stack.popped() {
     print "top: {top}, remaining: {rest.length()}, before: {stack.length()}"
   }
   ```

4. **Each trait has exactly one implementation, written in plain TorbScript rather than as a `native type`.**
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

## Related

- [The collection traits](collection-traits.md) - `Collection` and `Iterable`, which both traits sit on top of.
- [Lists](lists.md) - the `List` each default implementation is built from.
- [Option](../values-and-types/option.md) - the `Item?` that `pop()`, `dequeue()`, `first()` and `last()` all answer.
