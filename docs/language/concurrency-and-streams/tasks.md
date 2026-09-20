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
   caller sees that from the signature alone, the same way it sees that a function may fail from `Result`.

2. **A task starts running as soon as it is created**, whether that is `spawn { ... }` or a call to a function that
   returns a `Task`. `await()` does not start anything; it only waits for a result that is already on its way.

3. **`await()` is specified to belong only in a function that returns a `Task`, in a closure passed to `spawn`, and
   at the top level of an entry file or a script**, because a function that waits should say so in its own return
   type - the same reasoning `?` follows for `Result`. Today's checker does not yet reject `await()` anywhere else;
   see below.

4. **`spawn` gets a copy of everything its closure captures, and cannot capture a `var` binding.** Values are passed
   freely between tasks because a task never shares storage with the scope it was spawned from; only `Task` and
   `Channel` connect two tasks.

5. **`all` waits for two tasks of different types at once; `Task.all` waits for a list of tasks of the same type.**
   Both answer their values in the order of the tasks, which is the order they were given in, not the order they
   finish in.

   ```trb check
   fn asNumber(value: Int): Task<Int> {
     spawn { value }
   }

   const (first, second) = all(asNumber(1), asNumber(2)).await()
   print "{first} {second}"
   ```

6. **A `var self` method may answer a `Task` only when its type is a `shared type`.** A value's `var self` is a copy
   in and a copy back that ends with the call; a `Task` finishes later, so only an object - which has no copy to lose
   - can be changed this way. See [Shared types](../types/shared-types.md), rule 6.

## What this is not

**`await()` is not restricted to a `Task`-returning function by today's checker, even though the specification says
it should be.** `CONCEPT.md` names exactly three places `await()` is allowed; a function that answers a plain `Int`
and calls `await()` anyway is meant to be rejected, and is accepted instead.

```trb check
fn double(value: Int): Task<Int> {
  spawn { value * 2 }.await()
}

print double(21).await()
```

```trb skip the checker does not yet reject `await()` outside a Task-returning function, a spawn closure or a top-level script; see rule 3 above
fn sum(a: Int, b: Int): Int {
  spawn { a }.await()
}
```

## Related

- [Channels](channels.md) - the other shared type that connects two tasks.
- [Streams](streams.md) - `Source` and `Sink`, whose verbs also answer a `Task`.
- [Shared types](../types/shared-types.md) - why a `var self` method may answer a `Task` only for an object.
- [Result](../errors/result.md) - the vocabulary `Task` shares with `?`, `map` and `flatMap`.
