---
title: Streams
summary: Source and Sink are the asynchronous siblings of Iterator and Accumulator, with the same verbs, the same Stage values in between, and a failure that stands in the type on both ends.
kind: reference
status: stable
order: 30
keywords:
  - Source
  - Sink
  - Stage
  - backpressure
source:
  - docs/design/STREAMS.md
---

A stream is one flow, in one direction, with two ends. `Source<Item, Failure>` reads with `next`, exactly like
`Iterator`; `Sink<Item, Failure>` writes with `add` and ends with `end`, like `Accumulator` - the only difference is
that every verb answers a `Task`, and that every stream's failure type converts from `Cancelled`, so `outcome()`
folds a cancellation into it.

## Example

```trb check
fn sumOf(var source: Source<Int, Cancelled>): Task<Result<Int, Cancelled>> {
  source.collect(counting()).outcome()
}

const total = sumOf Channel<Int>(capacity: 1).source()
print total.outcome().orElse(0)
```

## Syntax

```text
shared trait Source<Item, Failure: From<Cancelled>> with Close {
  var fn next(): Task<Result<Item?, Failure>>
}

shared trait Sink<Item, Failure: From<Cancelled>> with Close {
  var fn add(item: Item): Task<Result<Void, Failure>>
  var fn end(): Task<Result<Void, Failure>>
}

while const Some(item) = source.next().outcome()? { ... }
```

## Rules

1. **`Ok(Some(item))` is the next item, `Ok(None)` is the end of the stream, `Fail(problem)` is a failure that ends
   it.** After `Ok(None)` every further `next()` answers `Ok(None)` again; after a `Fail` the stream never delivers
   another item.

2. **Both ends are shared objects, so reading from a source or writing to a sink needs no `var` binding.** A change
   of an object is not a question of the path it is reached through ([Shared types](../types/shared-types.md), rule 2):
   every holder reads from the one stream, and an item one of them took is gone for all of them. `var` on a binding of
   a source only means that the binding may be pointed at another one.

3. **There is deliberately no `for` over a `Source`.** A `for` head has no place for the `?` that a failing pull
   needs, so the loop is a `while` that names both `outcome()` and `?`:

   ```trb check
   fn printAll(var source: Source<Int, Cancelled>): Task<Result<Void, Cancelled>> {
     while const Some(item) = source.next().outcome()? {
       print item
     }
     Ok void
   }
   ```

4. **Backpressure is the shape of the protocol, not a mechanism.** Reading pulls: nothing happens until `next()` is
   called. Writing waits: `add`'s `Task` finishes only once the target has taken the item, so a writer faster than its
   target waits by itself.

5. **The same `Stage` an `Iterate` uses drives a `Source` too, through `through(stage)`, `map`, `filter`, `take` and
   the rest.** A stage is synchronous and never asks where its values come from, so `mapping`, `filtering` and every
   other stage of [Pipelines](../collections-and-iteration/pipelines.md) work unchanged on both worlds.

6. **`close()` releases what is above or below, synchronously and without failing; `end()` is the graceful end of
   a `Sink` and can fail.** No program calls `close()`: the last release of a stream runs it (see
   [Destructors](../execution/destructors.md)). A reader that stops early (`take`, a `find` that found it, an
   abandoned loop) lets go of what it was reading from, and its release closes it; a sink that is released without
   being finished may have written less than it was given.

## What this is not

**A `Source` is not something a `for` loop can read.** Only `Iterate` works after `in`; a `Source` needs the `while`
form because its pull can fail.

```trb check
fn printAll(var source: Source<Int, Cancelled>): Task<Result<Void, Cancelled>> {
  while const Some(item) = source.next().outcome()? {
    print item
  }
  Ok void
}
```

```trb error
fn printAll(source: Source<Int, Cancelled>) {
  for item in source {
    print item
  }
}
// error: `Source<Int64, Cancelled>` is not `Iterate`, so `for` cannot walk it
```

## Related

- [Channels](channels.md) - `Channel`, the one place a `Source` and a `Sink` are made from nothing else.
- [Tasks](tasks.md) - `Task` and `await()`, which every verb of a stream answers with.
- [Pipelines](../collections-and-iteration/pipelines.md) - the `Stage` values a stream reuses without change.
- [Shared types](../types/shared-types.md) - why a `var fn` method here may answer a `Task`.
