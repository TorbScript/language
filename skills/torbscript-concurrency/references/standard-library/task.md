---
title: std/task
summary: Task and Channel, the two shared types that connect concurrent work, spawn, cancellation with Cancelled and TimedOut, and pause.
kind: package
status: stable
order: 80
keywords:
  - std/task
  - Task
  - Channel
  - spawn
  - Cancelled
  - concurrency
  - asynchrony
source:
  - std/task/src/lib.trb
  - docs/design/CONCURRENCY.md
  - docs/design/STREAMS.md
---

`std/task` is a computation that finishes later. Asynchrony lives in the type: a function that returns `Task<Value>`
may call `await()`, and its body produces the `Value` - the same way a function that returns `Result` may use `?`.
`Task` and `Channel` are the two shared types that connect concurrent work; everything else is copied when it crosses
into one. Every task can be cancelled, and `await()` passes a cancellation on instead of answering it: it answers the
`Value`, and a task that awaits a cancelled task is cancelled in turn. `result()` answers `Result<Value, Cancelled>` for
the code that has to know. `Task`, `Channel`,
`ChannelClosed`, `Cancelled`, `TimedOut`, `spawn` and `both` are in scope through the prelude; `pause`, `offload` and
`Workers` are imported from here.

## Import

```trb fragment
use Task, Channel, ChannelClosed, Cancelled, TimedOut, Workers, spawn, both, pause, offload from "std/task"
```

```trb check
const numbers: List<Int> = [1, 2, 3, 4]

// Stages are lazy: without `toList()` nothing would be spawned until somebody iterates
const tasks = numbers.map { number => spawn { number * number } }.toList()

// Top-level `await()` is allowed in entry files and scripts. `Task.all` waits for all of them at once
const squares = Task.all(tasks).await()
print squares.sum()
```

## Declarations

### Task

```trb fragment
public native shared type Task<Value> {
  fn await(): Value
  fn result(): Result<Value, Cancelled>
  var fn cancel()
  var fn within(limit: Duration): Task<Result<Value, TimedOut>>
  fn map<Output>(transform: Transform<Value, Output>): Task<Output>
  fn flatMap<Output>(transform: Transform<Value, Task<Output>>): Task<Output>
  static fn all(tasks: Iterate<Task<Value>>): Task<List<Value>>
}
```

A task starts running the moment it is created (`spawn { ... }`, or calling a function that returns one). `await()`
waits for the value and is allowed in a function that returns a `Task`, in a closure passed to `spawn`, and at the top
level of an entry file or a script - everywhere else it is a compile error, because a function that waits says so in
its return type. It answers the `Value` itself, and where the task ended cancelled the waiter is cancelled too, at
that very `await()`: it stops there, its scopes end in reverse order (every `using` is closed) as at any cancellation,
and whoever awaits it is cancelled in turn. At the top level of an entry file that ends the program with `cancelled:
the program waited for a task that was cancelled` and exit code 130. So a task whose value is a `Result` needs one `?`
and nothing more: `file.addLine(text).await()?`. `result()` waits the same way and answers a cancellation as
`Fail(Cancelled)` instead of passing it on - for a supervisor, a test, or the task that cancelled another one and wants
to confirm it.

`cancel()` asks the task to stop where it next waits or where a loop of it turns around; the tasks it started stop with
it. `within(limit)` cancels the task once the limit has passed and answers `Fail(TimedOut)` then. Both are `var fn`s,
so a task somebody may stop sits in a `var` binding. Dropping the last handle stops nothing. `Task` belongs to the
vocabulary of `Option` and `Result` (`map`, `flatMap`, `all`), and all three are TorbScript over `await()` and
`result()`; a cancelled task stays cancelled through `map` and `flatMap`.

### `spawn`, `both`, `pause`

```trb fragment
public native fn spawn<Value>(body: () => Value): Task<Value>
public fn both<First, Second>(first: Task<First>, second: Task<Second>): Task<(First, Second)>
public native fn pause(): Task<Void>
```

`spawn` runs the closure as a new task; it gets copies of what it captures and cannot capture a `var` binding, so
there is nothing to race for. The free function `both` waits for two tasks of different types at once
(`const (user, posts) = both(fetchUser(1), fetchPosts(1)).await()`); `Task.all` is the same idea for any number of
tasks of the same type. Where one of them is cancelled, the others are cancelled too, and so is the whole - and with
it whoever awaits it. `pause()` puts the running task
at the back of the queue, which is what makes a long loop fair.

### Workers

```trb fragment
public native type Workers {
  native static fn count(): Int
  native static fn blocking(): Int
}
```

How many workers - operating-system threads - run the tasks of this process: `TORB_WORKERS` where it is set, and one per
logical processor otherwise, at most 1024. It is fixed before the first line runs, because every worker owns a heap. A
task that is started goes to the worker that started it; an idle worker takes it before its first run where every value
it holds may cross to another thread - numbers, a `Channel` of numbers, a text or a list nobody else holds - and a task
never moves once it has run. With `TORB_WORKERS=1` the tasks run one at a time in the order they became ready, which is
the order a program that prints from several tasks at once can rely on. `Workers` is imported from here; it is what
[`parallel(workers:)`](parallel.md) defaults to. `blocking()` is the size of the blocking pool below: `TORB_BLOCKING`
where it is set, and 4 otherwise, at most 1024.

### `offload`

```trb fragment
public fn offload<Value>(body: () => Value): Task<Value>
```

Runs `body` on a thread of the blocking pool instead of on a worker, so a call that blocks - reading a file, running a
child process, a C library that waits - does not stall the other tasks of the worker that asked:
`offload({ File.readText("notes.txt") }).await()`. The pool has `Workers.blocking()` threads, started the first time a
body moves there, so it is for one blocking call and not for a long computation, which belongs on the workers. The body
moves only where its closure may cross to another thread - where it captures nothing counted, or only literals and
values nothing else holds a count of; a closure that captures a `String` built at run time, a list or a `shared type`
object runs on the worker that asked, with the same answer and that worker blocked. The body cannot `await`, and a
cancellation does not interrupt a body that already runs: it runs to its end and its answer is thrown away.

### Channel

```trb fragment
public native shared type Channel<Item> {
  capacity: Int = 0

  fn source(): ChannelSource<Item>
  fn sink(): ChannelSink<Item>
}
```

A stream in memory of which one holder has both ends. `capacity: 0` hands every item over directly, so `add` waits
until somebody pulls; `source()` is a `Source<Item, Never>` and `sink()` a `Sink<Item, ChannelClosed>` (see
[std/stream](stream.md)), and they can be handed out separately, so a producer never sees the reading end and a
consumer never sees the writing one. Reading cannot fail: a closed channel is the end of the stream, not a failure.

### ChannelClosed, Cancelled, TimedOut

```trb fragment
public type ChannelClosed with Show, Error {}
public type Cancelled with Show, Error {}
public type TimedOut with Show, Error {
  limit: Duration
}
```

`ChannelClosed` is the only way a `Channel`'s writing end fails: nobody is reading any more. `Cancelled` is what
`result()` answers for a task that stopped because somebody asked it to; it carries no reason, and `await()` never
answers it. `TimedOut` is the failure `within` answers, with the limit that was too small.

## Related

- [Tasks](../language/concurrency-and-streams/tasks.md) - the rules of `Task`, `await()` and cancellation.
- [std/stream](stream.md) - `Source` and `Sink`, the two ends a `Channel` hands out.
- The standard library (skill `torbscript-standard-library`: `references/standard-library/index.md`) - the other packages.

