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
into one. Every task can be cancelled, so `await()` answers `Result<Value, Cancelled>`. `Task`, `Channel`,
`ChannelClosed`, `Cancelled`, `TimedOut`, `spawn` and `all` are in scope through the prelude; `pause` is imported from
here.

## Import

```trb fragment
use Task, Channel, ChannelClosed, Cancelled, TimedOut, Workers, spawn, all, pause from "std/task"
```

```trb check
const numbers: List<Int> = [1, 2, 3, 4]

// Stages are lazy: without `toList()` nothing would be spawned until somebody iterates
const tasks = numbers.map { number => spawn { number * number } }.toList()

// Top-level `await()` is allowed in entry files and scripts. `Task.all` waits for all of them at once
const squares = Task.all(tasks).await()?
print squares.sum()
```

## Declarations

### Task

```trb fragment
public native shared type Task<Value> {
  fn await(): Result<Value, Cancelled>
  var fn cancel()
  var fn within(limit: Duration): Task<Result<Value, TimedOut>>
  fn map<Output>(transform: (value: Value) => Output): Task<Output>
  fn flatMap<Output>(transform: (value: Value) => Task<Output>): Task<Output>
  static fn all(tasks: Iterate<Task<Value>>): Task<List<Value>>
}

extend<Value, Failure: From<Cancelled>> Task<Result<Value, Failure>> {
  fn outcome(): Result<Value, Failure>
}
```

A task starts running the moment it is created (`spawn { ... }`, or calling a function that returns one). `await()`
waits for the value and is allowed in a function that returns a `Task`, in a closure passed to `spawn`, and at the top
level of an entry file or a script - everywhere else it is a compile error, because a function that waits says so in
its return type. It answers `Fail(Cancelled)` where the task was cancelled instead of finishing. `outcome()` waits the
same way for a task whose value is a `Result` and folds a cancellation into that `Result`'s failure, so one `?`
unwraps both.

`cancel()` asks the task to stop where it next waits or where a loop of it turns around; the tasks it started stop with
it. `within(limit)` cancels the task once the limit has passed and answers `Fail(TimedOut)` then. Both are `var fn`s,
so a task somebody may stop sits in a `var` binding. Dropping the last handle stops nothing. `Task` belongs to the
vocabulary of `Option` and `Result` (`map`, `flatMap`, `all`), and all three are TorbScript over `await()`.

### `spawn`, `all`, `pause`

```trb fragment
public native fn spawn<Value>(body: () => Value): Task<Value>
public fn all<First, Second>(first: Task<First>, second: Task<Second>): Task<(First, Second)>
public native fn pause(): Task<Void>
```

`spawn` runs the closure as a new task; it gets copies of what it captures and cannot capture a `var` binding, so
there is nothing to race for. The free function `all` waits for two tasks of different types at once
(`const (user, posts) = all(fetchUser(1), fetchPosts(1)).await()?`); `Task.all` is the same idea for any number of
tasks of the same type. Where one of them is cancelled, the others are cancelled too. `pause()` puts the running task
at the back of the queue, which is what makes a long loop fair.

### Workers

```trb fragment
public native type Workers {
  native static fn count(): Int
}
```

How many workers - operating-system threads - run the tasks of this process: `TORB_WORKERS` where it is set, and one per
logical processor otherwise, at most 1024. It is fixed before the first line runs, because every worker owns a heap. A
task that is started goes to the worker that started it; an idle worker takes it before its first run where every value
it holds may cross to another thread - numbers, a `Channel` of numbers, a text or a list nobody else holds - and a task
never moves once it has run. With `TORB_WORKERS=1` the tasks run one at a time in the order they became ready, which is
the order a program that prints from several tasks at once can rely on. `Workers` is imported from here; it is what
[`parallel(workers:)`](parallel.md) defaults to.

### Channel

```trb fragment
public native shared type Channel<Item> {
  capacity: Int = 0

  fn source(): ChannelSource<Item>
  fn sink(): ChannelSink<Item>
}
```

A stream in memory of which one holder has both ends. `capacity: 0` hands every item over directly, so `add` waits
until somebody pulls; `source()` is a `Source<Item, Cancelled>` and `sink()` a `Sink<Item, ChannelClosed>` (see
[std/stream](stream.md)), and they can be handed out separately, so a producer never sees the reading end and a
consumer never sees the writing one. Reading fails only with `Cancelled`: a closed channel is the end of the stream,
not a failure.

### ChannelClosed, Cancelled, TimedOut

```trb fragment
public type ChannelClosed with Show, Error {}
public type Cancelled with Show, Error {}
public type TimedOut with Show, Error {
  limit: Duration
}
```

`ChannelClosed` is the only way a `Channel`'s writing end fails: nobody is reading any more. `Cancelled` is what a
waiter reads of a task that stopped because somebody asked it to; it carries no reason, and every stream's failure type
converts from it. `TimedOut` is the failure `within` answers, with the limit that was too small.

## Related

- [Tasks](../language/concurrency-and-streams/tasks.md) - the rules of `Task`, `await()` and cancellation.
- [std/stream](stream.md) - `Source` and `Sink`, the two ends a `Channel` hands out.
- [The standard library](index.md) - the other packages.

