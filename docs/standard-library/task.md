---
title: std/task
summary: Task and Channel, the two shared types that connect concurrent work, and spawn - designed, but not run by any back end yet.
kind: package
status: planned
order: 80
keywords:
  - std/task
  - Task
  - Channel
  - spawn
  - concurrency
  - asynchrony
source:
  - std/task/src/lib.trb
  - docs/STREAMS.md
---

> **Planned.** This feature is designed but not implemented. Nothing on this page works today.

`std/task` is a computation that finishes later. Asynchrony lives in the type: a function that returns `Task<Value>`
may call `await()`, and its body produces the `Value` - the same way a function that returns `Result` may use `?`.
`Task` and `Channel` are the two shared types that connect concurrent work; everything else is copied when it crosses
into one. `CONCEPT.md` carries this design under "Concurrency (Draft)", and the tour's own async example
(`examples/tour/src/10-async.trb`) is marked `DRAFT` for the same reason. Every name below is already in scope through
the prelude.

## Import

```trb fragment
use Task, Channel, ChannelClosed, spawn, all from "std/task"
```

```trb check
const numbers: List<Int> = [1, 2, 3, 4]

// Stages are lazy: without `toList()` nothing would be spawned until somebody iterates
const tasks = numbers.map { number => spawn { number * number } }.toList()

// Top-level `await()` is allowed in entry files and scripts. `Task.all` waits for all of them at once
const total = Task.all(tasks).await().sum()
print total
```

## Declarations

<!-- torb:declarations:begin -->

### Task

```trb fragment
public native shared type Task<Value> {
  fn await(self): Value
  fn map<Output>(self, transform: (value: Value) => Output): Task<Output>
  fn flatMap<Output>(self, transform: (value: Value) => Task<Output>): Task<Output>
  fn all(tasks: Iterable<Task<Value>>): Task<List<Value>>
}
```

A task starts running the moment it is created (`spawn { ... }`, or calling a function that returns one). `await()`
waits for the value and is allowed in a function that returns a `Task`, in a closure passed to `spawn`, and at the top
level of an entry file or a script - everywhere else it is a compile error, because a function that waits says so in
its return type. `Task` belongs to the vocabulary of `Option` and `Result` (`map`, `flatMap`, `all`).

### `spawn`, `all`

```trb fragment
public native fn spawn<Value>(body: () => Value): Task<Value>
public native fn all<First, Second>(first: Task<First>, second: Task<Second>): Task<(First, Second)>
```

`spawn` runs the closure as a new task, in parallel; it gets copies of what it captures and cannot capture a `var`
binding, so there is nothing to race for. The free function `all` waits for two tasks of different types at once
(`const (user, posts) = all(fetchUser(1), fetchPosts(1)).await()`); `Task.all` is the same idea for any number of tasks
of the same type.

### Channel

```trb fragment
public native shared type Channel<Item> {
  capacity: Int = 0

  fn source(self): Source<Item, Never>
  fn sink(self): Sink<Item, ChannelClosed>
}
```

A stream in memory of which one holder has both ends. `capacity: 0` hands every item over directly, so `add` waits
until somebody pulls; `source()` and `sink()` are ordinary `Source`/`Sink` values (see [std/stream](stream.md)) and can
be handed out separately, so a producer never sees the reading end and a consumer never sees the writing one. The
reading end cannot fail (`Never`): a closed channel is the end of the stream, not a failure.

### ChannelClosed

```trb fragment
public type ChannelClosed with Show, Error {}
```

The only way a `Channel`'s writing end fails: nobody is reading any more. A value rather than a panic, because a
producer that is no longer needed should stop, not crash.

<!-- torb:declarations:end -->

## What is missing

The example above type checks against the real standard library, which is what `torb docs check` verifies - so `spawn`,
`Task.await`, `Channel` and `all` are all accepted by the type checker today. What is missing is a back end that gives
any of them a value: the interpreter (stage 0) does not load `std/` at all yet, and the native back end's manifest
marks `spawn`, every `Task` member and both `Channel` members as work for a future milestone, listed under "What
milestones 7 and 10 have to build" in [`docs/STREAMS.md`](../STREAMS.md). Until then, a program that calls `spawn`
or `await()` type checks and cannot be run to completion by any current back end - the same gap `Decimal` documents for
arithmetic, described in [Decimal](../language/values-and-types/decimal.md).

## Related

- [std/stream](stream.md) - `Source` and `Sink`, the two ends a `Channel` hands out.
- [Decimal](../language/values-and-types/decimal.md) - another type that type checks today and has no back end yet.
- [The standard library](index.md) - the other packages.
