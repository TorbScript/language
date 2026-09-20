---
title: Tasks
summary: Task<Value> is what an asynchronous function answers, and await() unwraps it - but no back end runs one yet, so a program that spawns a task type checks and cannot finish running.
kind: reference
status: planned
order: 10
keywords:
  - Task
  - await
  - spawn
  - async
source:
  - CONCEPT.md#concurrency-draft
  - std/task/src/lib.trb
---

> **Planned.** This feature is designed but not implemented. Nothing on this page works today.

Asynchrony lives in the type system, not in a keyword. A function that returns `Task<Value>` may call `await()`
inside its body, the same way a function that returns a `Result` may use `?`. The example below passes
`torb docs check`'s type checker; running it is what no back end does yet.

## Example

```trb check
fn double(value: Int): Task<Int> {
  spawn { value * 2 }.await()
}

const doubled = double(21).await()
print doubled
```

## Syntax

```text
Task<Value>                                the type: a computation that finishes later
spawn(body: () => Value): Task<Value>      starts body as a new task, running in parallel
task.await(): Value                        waits for the value
task.map(transform): Task<Output>
task.flatMap(transform): Task<Output>
all(first: Task<A>, second: Task<B>): Task<(A, B)>
Task.all(tasks: Iterable<Task<Value>>): Task<List<Value>>
```

## Rules

1. **There is no `async` keyword.** A function's result type, `Task<Value>`, is the only marker that it may wait; a
   caller sees that from the signature alone, the same way it sees that a function may fail from `Result`. The body of
   such a function produces `Value` directly, never `Task<Value>`: `fn asNumber(value: Int): Task<Int> { value }` is
   the whole function, and the caller gets the `Task` from calling it, not from the body wrapping one.

2. **A task starts running as soon as it is created**, whether that is `spawn { ... }` or a call to a function that
   returns a `Task`. `await()` does not start anything; it only waits for a result that is already on its way.

3. **`await()` belongs only in a function that returns a `Task`, in a closure passed directly to `spawn`, and at the
   top level of an entry file or a script**, because a function that waits should say so in its own return type - the
   same reasoning `?` follows for `Result`. The checker rejects it everywhere else: `` `await()` is only allowed in a
   function that returns a `Task` ``. A closure of another shape that a library spawns on your behalf is not tracked
   back to that task, so `await()` inside such a closure is still accepted; see below.

4. **`spawn` gets a copy of everything its closure captures, and cannot capture a `var` binding.** Values are passed
   freely between tasks because a task never shares storage with the scope it was spawned from; only `Task` and
   `Channel` connect two tasks.

5. **`all` waits for two tasks of different types at once; `Task.all` waits for a list of tasks of the same type.**
   Both answer their values in the order of the tasks, which is the order they were given in, not the order they
   finish in.

   ```trb check
   fn asNumber(value: Int): Task<Int> {
     value
   }

   const (first, second) = all(asNumber(1), asNumber(2)).await()
   print "{first} {second}"
   ```

6. **A `var self` method may answer a `Task` only when its type is a `shared type`.** A value's `var self` is a copy
   in and a copy back that ends with the call; a `Task` finishes later, so only an object - which has no copy to lose
   - can be changed this way. See [Shared types](../types/shared-types.md), rule 6.

## What this is not

**`await()` is not legal in a function that does not return a `Task`.** `CONCEPT.md` names exactly three places
`await()` is allowed; a function that answers a plain `Int` and calls `await()` anyway is rejected.

```trb check
fn double(value: Int): Task<Int> {
  spawn { value * 2 }.await()
}

print double(21).await()
```

```trb error
fn sum(a: Int, b: Int): Int {
  spawn { a }.await()
}
// error: `await()` is only allowed in a function that returns a `Task`
```

**A closure of another shape that a library spawns on your behalf is not tracked back to that task, so `await()`
inside it is still accepted (TYPECHECKER gap 58).** `Source.produce`'s `body` parameter has type `(var sink:
Sink<Item, Failure>) => Result<Void, Failure>` - not a function that returns a `Task`, and not the literal closure
`spawn` is called with either, because `produce` wraps it in one of its own first. No signature can say "this closure
runs inside a task later" yet, so the checker has nothing to check.

```trb check
use Source, Sink from "std/stream"

fn ints(): Source<Int, Never> {
  Source<Int, Never>.produce { sink =>
    sink.add(1).await()?
    Ok void
  }
}
```

## Related

- [Channels](channels.md) - the other shared type that connects two tasks.
- [Streams](streams.md) - `Source` and `Sink`, whose verbs also answer a `Task`.
- [Shared types](../types/shared-types.md) - why a `var self` method may answer a `Task` only for an object.
- [Result](../errors/result.md) - the vocabulary `Task` shares with `?`, `map` and `flatMap`.
