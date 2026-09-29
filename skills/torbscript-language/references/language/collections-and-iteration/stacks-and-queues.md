---
title: Stacks and queues
summary: Stack is LIFO with push, pop and peek, Queue is FIFO with enqueue, dequeue and peek - the words everybody knows for each structure.
kind: reference
status: stable
order: 40
keywords:
  - Stack
  - Queue
  - ArrayStack
  - ArrayQueue
  - push
  - pop
  - enqueue
  - dequeue
  - peek
source:
  - std/collections/src/queue.trb
  - std/collections/src/stack.trb
---

`Stack<Item>` is last-in-first-out and `Queue<Item>` is first-in-first-out, and each spends the words everybody knows
for it: a stack has `push`, `pop` and `peek` on its top, a queue has `enqueue` at the back and `dequeue` and `peek` at
the front. Neither has an index; the only way to look further inside one is to take items out of it, or to iterate it.

## Example

```trb check
var pending = Queue.of 1
pending.enqueue 2
print pending.dequeue()

var brackets = Stack<Char>.of()
brackets.push '('
print brackets.peek()
print brackets.pop()
```

## Syntax

```text
Stack.of(...items)          the last argument ends up on top
Queue.of(...items)          the first argument ends up at the front

stack.push(value)       stack.pop(): Item?          in place
queue.enqueue(value)    queue.dequeue(): Item?      in place

stack.peek(): Item?     queue.peek(): Item?         look without taking
```

## Rules

1. **Each structure has its own words, so a call says what it does without a look at the receiver's type.** On top of a
   stack and at the back of a queue are different meanings, and `push` and `enqueue` say which one it is.

2. **A `Stack` iterates from top to bottom; a `Queue` iterates front to back.** Both iterate in the order they hand
   items out, so `peek()` is what the next `pop()` or `dequeue()` would answer, without taking anything.

   ```trb check
   const stack = Stack.of 1, 2, 3
   print stack.peek()
   print stack.toList()
   ```

3. **There are no participles on a stack or a queue.** Taking an item out answers the item, and a changed copy would
   have to answer the pair `(Item, Self)?` beside it. A copy does the same thing without a second word: a stack is a
   value, so a `var` copy can be popped while the original keeps every item.

   ```trb check
   const stack = Stack.of 1, 2, 3
   var rest = stack
   if const Some(top) = rest.pop() {
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

**Neither trait has an index.** There is no `stack[0]`: the only way to see an element below the top is to pop down to
it or to iterate the whole collection.

```trb check
const stack = Stack.of 1, 2, 3
print stack.toList()
```

```trb error
const stack = Stack.of 1, 2, 3
print stack[0]
// error: `Stack<Int64>` does not implement `Index`, so `a[key]` has no meaning for it
```

**There is no `add` on either.** `add` would be one word for "on top" and "at the back", which is what the words of each
structure exist to tell apart.

## Related

- [The collection traits](collection-traits.md) - `Iterate` and `Length`, which both traits sit on top of.
- [Lists](lists.md) - the `List` each default implementation is built from.
- [Verbs and participles](../types/verbs-and-participles.md) - where a participle pays for itself, and where it does not.
- [Option](../values-and-types/option.md) - the `Item?` that `pop()`, `dequeue()` and `peek()` all answer.

