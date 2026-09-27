---
title: Iterating
summary: for pulls from Iterator.next() through Iterate.iterate(), and the subject of a for is evaluated once into a temporary, so changing it inside the loop does not affect what is walked.
kind: reference
status: stable
order: 60
keywords:
  - Iterate
  - Iterator
  - for
  - next
source:
  - std/iteration/src/iteration.trb
  - CONCEPT.md#collections-and-iteration
---

`Iterator<Item>` is a cursor with one member, `next`. `Iterate<Item>` is anything that can hand out such a cursor,
which is what `for` and every collection method (`map`, `filter`, `fold`, ...) are built on. A type takes part by
implementing `Iterate` itself.

## Example

```trb check
type CountdownIterator with Iterator<Int> {
  var current: Int

  var fn next(): Int? {
    if current <= 0 {
      return None
    }
    current = current - 1
    current + 1
  }
}

type Countdown with Iterate<Int> {
  start: Int

  fn iterate(): Iterator<Int> {
    CountdownIterator start
  }
}

for value in Countdown(3) {
  print value
}
```

## Syntax

```text
trait Iterator<Item> { var fn next(): Item? }
trait Iterate<Item> { fn iterate(): Iterator<Item> }

for <name> in <iterable> { ... }
for (<name>, <name>) in <iterable of tuples> { ... }
```

## Rules

1. **`Iterator<Item>` has exactly one member, `next(): Item?`, which answers `None` once there is nothing
   left.** `Iterate<Item>` has exactly one required member, `iterate(): Iterator<Item>`; every other method a
   collection has (`map`, `filter`, `fold`, `toList()`, ...) is a default method built from those two.

2. **`for value in xs { ... }` calls `xs.iterate()` once and then `next()` until it answers `None`.** The loop
   variable is a `const` copy of each item, produced fresh by every `next()`.

3. **The subject of a `for` is evaluated once, into a temporary.** Changing the binding named after `in` inside the
   loop body does not change what the loop walks, because the loop is already iterating a copy of the value the
   binding held when the loop started.

   ```trb check
   var numbers = [1, 2, 3]
   for value in numbers {
     numbers.append(value * 10)
   }
   print numbers
   ```

4. **A `Map` is `Iterate<(Key, Value)>`, and a `for` head can destructure the tuple directly.** `for (name, age) in
   ages { ... }` binds both parts without a separate step.

   ```trb check
   const ages: Map<String, Int> = ["Ada": 36, "Grace": 45]
   for (name, age) in ages {
     print "{name} is {age}"
   }
   ```

5. **A type takes part in `for` and in every pipeline method by implementing `Iterate`, with a private `Iterator`
   type next to it that does the work.** `Countdown` above is the whole shape: one type that answers cursors, one
   type that is a cursor.

## What this is not

**`for` is not a special form that works on every type - only on `Iterate`.** A type with no `iterate()` cannot
stand after `in`.

```trb check
for value in [1, 2, 3] {
  print value
}
```

```trb error
type Point {
  var x: Int
  var y: Int
}

for value in Point(1, 2) {
  print value
}
// error: `Point` is not `Iterate`, so `for` cannot walk it
```

## Related

- [The collection traits](collection-traits.md) - the five kinds, each built on top of `Iterate`.
- [Pipelines](pipelines.md) - the lazy stages and terminal operations `Iterate` gives every collection for free.
- [Bindings](../values-and-types/bindings.md) - why the loop variable is always a `const`.

