---
title: Tasks
summary: Task<Value> is what an asynchronous function answers; await() waits for it and answers a Result, because every task can be cancelled.
kind: reference
status: stable
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

Asynchrony lives in the type system, not in a keyword. A function that returns `Task<Value>` may call `await()`
inside its body, the same way a function that returns a `Result` may use `?`. Every task can be cancelled, so
`await()` answers `Result<Value, Cancelled>`: `?` hands a cancellation on, `??` replaces it. A pool of workers - one
operating-system thread per processor, `TORB_WORKERS` to say otherwise - runs the tasks of a program: each worker runs
one task at a time in the order they became ready, and an idle worker takes a task that has not started yet where
everything it holds may cross to another thread (numbers, a channel of numbers, a text nobody else holds). What a program
reads of its tasks - their values, the items of a channel - does not depend on which worker ran them; what several
tasks print at once interleaves as the machine runs them, and with `TORB_WORKERS=1` in the one order of the queue.

## Example

```trb check
fn double(value: Int): Task<Result<Int, Cancelled>> {
  spawn { value * 2 }.await()
}

const doubled = double(21).outcome()?
print doubled
```

## Syntax

```text
Task<Value>                                the type: a computation that finishes later
spawn(body: () => Value): Task<Value>      starts body as a new task, running in parallel
task.await(): Result<Value, Cancelled>     waits for the value, or Fail(Cancelled) where the task was cancelled
task.outcome(): Result<Value, Failure>     for a Task<Result<Value, Failure>>: waits, and folds a cancellation in
task.cancel()                              asks the task to stop (a var fn)
task.within(limit): Task<Result<Value, TimedOut>>
pause(): Task<Void>                        lets every other ready task run first (std/task)
sleep(seconds: Float): Task<Void>          finishes once the time is up (std/time)
task.map(transform): Task<Output>
task.flatMap(transform): Task<Output>
both(first: Task<A>, second: Task<B>): Task<(A, B)>
Task.all(tasks: Iterate<Task<Value>>): Task<List<Value>>
```

## Rules

1. **There is no `async` keyword.** A function's result type, `Task<Value>`, is the only marker that it may wait; a
   caller sees that from the signature alone, the same way it sees that a function may fail from `Result`. The body of
   such a function produces `Value` directly, never `Task<Value>`: `fn asNumber(value: Int): Task<Int> { value }` is
   the whole function, and the caller gets the `Task` from calling it, not from the body wrapping one.

2. **A task starts running as soon as it is created**, whether that is `spawn { ... }` or a call to a function that
   returns a `Task`. `await()` does not start anything; it only waits for a result that is already on its way.

3. **`await()` belongs only in a function that returns a `Task`, in a closure passed directly to `spawn`, in a closure
   whose declared result is a `Task`, and at the top level of an entry file or a script**, because a function that
   waits should say so in its own return type - the same reasoning `?` follows for `Result`. The checker rejects it
   everywhere else: `` `await()` is only allowed in a function that returns a `Task` `` for a function, and
   `` `await()` is only allowed in a closure that becomes a task `` for a closure. `outcome()` waits exactly the
   same way - it is `await()` plus the conversion of a cancellation into the task's own failure - so it stands under
   the same rule, and the two messages name it.

   An ordinary closure is not a task, and `tasks.map { _.await() }` is the mistake the rule is for: the closure runs
   where the pipeline is pulled, so the wait would block the worker underneath it. `Task.all(tasks).await()` waits for
   many at once, and `task.map { ... }` goes on when one finishes.

   ```trb error
   fn sumAll(tasks: List<Task<Int>>): Int {
     tasks.map({ _.await() ?? 0 }).sum()
   }
   // error: `await()` is only allowed in a closure that becomes a task
   ```

   A library that runs a closure as a task says so in the parameter's type. `Source.produce` takes a
   `(var sink: Sink<Item, Failure>) => Task<Result<Void, Failure>>`, and the `Task` in that type is what lets its body
   wait - exactly as the result type of a `fn` does.

4. **`spawn` gets a copy of everything its closure captures, and cannot capture a `var` binding.** Values are passed
   freely between tasks because a task never shares storage with the scope it was spawned from; only `Task` and
   `Channel` connect two tasks. A captured `var` binding is the one variable the language shares - the closure and
   the scope around it both reach it - so a task that took one with it would be the data race this design does not
   have. A top-level `var` is the same race for the same reason: it is one place the whole file shares. The checker
   rejects the capture itself, so one `spawn` is already too many and two are two messages:

   ```trb error
   fn counted(): Int {
     var total = 0
     const first = spawn { total + 1 }
     const second = spawn { total + 2 }
     total
   }
   // error: `spawn` cannot take the `var` binding `total` with it
   ```

   Read the value into a `const` before the closure, and every task gets its own copy:

   ```trb check
   fn counted(): Task<Result<Int, Cancelled>> {
     var total = 0
     total = 1
     const now = total
     const first = spawn { now + 1 }
     const second = spawn { now + 2 }
     Ok(first.await()? + second.await()?)
   }

   print(counted().outcome()?)
   ```

5. **A `spawn` closure takes no object with it, and a `Channel` carries none either.** A `shared type` is confined to
   the task that made it: a task gets copies, and an object is not copied, so both tasks would reach the same one.
   `Task` and `Channel` are the two exceptions, because they are what a value crosses *through*. The rule reaches a
   value that holds an object anywhere inside it, and a channel is checked where its item type is written.

   ```trb error
   shared type Counter {
     var count: Int = 0

     static fn open(): Self {
       Self()
     }

     fn value(): Int {
       count
     }
   }

   fn counted(): Task<Result<Int, Cancelled>> {
     const counter = Counter.open()
     spawn({ counter.value() }).await()
   }
   // error: `spawn` cannot take `counter` with it: a `Counter` has an identity
   ```

   Make the object inside the closure, or send what it holds through a `Channel`.

   **What the closure cannot see is a value is refused as well.** A type parameter may be filled with a `shared
   type`, a function value does not say what it captured, and a `shared type` may stand behind a `shared trait` - so
   a `spawn` closure may capture none of the three, whatever the bounds say:

   ```trb error
   fn later<Value>(value: Value): Task<Result<Int, Cancelled>> {
     const task = spawn {
       const _ = value
       1
     }
     task.await()
   }
   // error: `spawn` cannot take `value` with it: a `Value` may hold an object
   ```

   The one function value a task takes is a **function-typed parameter** it captures: the parameter then crosses, and
   every caller of the function is held to the rule instead - a closure it hands in may capture only what a task may
   take, and a function value whose captures are not visible (a closure bound to a name, a field) is refused. A declared
   function captures nothing and always may.

6. **`both` waits for two tasks of different types at once; `Task.all` waits for a list of tasks of the same type.**
   Both answer their values in the order of the tasks, which is the order they were given in, not the order they
   finish in.

   ```trb check
   fn asNumber(value: Int): Task<Int> {
     value
   }

   fn pair(): Task<Result<String, Cancelled>> {
     const (first, second) = both(asNumber(1), asNumber(2)).await()?
     Ok "{first} {second}"
   }

   print(pair().outcome()?)
   ```

7. **A `var fn` method may answer a `Task` only when its type is a `shared type`.** A value's `var fn` receiver is a copy
   in and a copy back that ends with the call; a `Task` finishes later, so only an object - which has no copy to lose
   - can be changed this way. See [Shared types](../types/shared-types.md), rule 6.

8. **Every task is cancellable, and `cancel()` is a request, not a kill.** A task notices it where it waits and where a
   loop of it turns around, so a loop that never waits stops at its next turn too. It stops there, releasing what it
   holds as a `return` would, and whoever waits for it reads `Fail(Cancelled)`. The tasks it started are cancelled with
   it. `cancel()` is a `var fn`, so a task somebody may stop sits in a `var` binding; dropping the last handle stops
   nothing.

   ```trb check
   fn counting(): Task<Int> {
     var count = 0
     while count >= 0 {
       count = count + 1
     }
     count
   }

   var worker = counting()
   worker.cancel()
   print worker.await().isOk()
   ```

9. **`outcome()` is `await()` for a task whose value is a `Result`**, with a cancellation folded into that
   `Result`'s own failure, so one `?` unwraps both: `source.next().outcome()?`. Its failure type has to convert from
   `Cancelled`, which every stream's does.

## What this is not

**`await()` is not legal in a function that does not return a `Task`.** `CONCEPT.md` names exactly three places
`await()` is allowed; a function that answers a plain `Int` and calls `await()` anyway is rejected.

```trb check
fn double(value: Int): Task<Result<Int, Cancelled>> {
  spawn { value * 2 }.await()
}

print(double(21).outcome()?)
```

```trb error
fn sum(a: Int, b: Int): Result<Int, Cancelled> {
  spawn { a }.await()
}
// error: `await()` is only allowed in a function that returns a `Task`
```

**A library that runs a closure as a task says so in the parameter's type.** `Source.produce`'s `body` parameter is a
`(var sink: Sink<Item, Failure>) => Task<Result<Void, Failure>>`, and the `Task` in it is what makes the `await()`
inside legal - the same marker a `fn` carries in its result type. Its body still produces the `Result` directly, the
way the body of such a `fn` does.

```trb check
use Source, Sink from "std/stream"

fn ints(): Source<Int, ChannelClosed> {
  Source<Int, ChannelClosed>.produce { sink =>
    sink.add(1).outcome()?
    Ok void
  }
}
```

## Related

- [Channels](channels.md) - the other shared type that connects two tasks.
- [Streams](streams.md) - `Source` and `Sink`, whose verbs also answer a `Task`.
- [Shared types](../types/shared-types.md) - why a `var fn` method may answer a `Task` only for an object.
- [Result](../errors/result.md) - the vocabulary `Task` shares with `?`, `map` and `flatMap`.

