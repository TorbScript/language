---
name: torbscript-concurrency
description: "Writes concurrent TorbScript: `Task`, `spawn` and `.await()`, cancellation and timeouts, `Channel`, the `Source` and `Sink` ends of a stream, and `parallel()` pipelines. Use it when TorbScript code waits on work, runs things at the same time, or reads or writes a stream of values."
license: MIT
compatibility: "Needs the torb command, which the torbscript skill checks and installs."
---

# Concurrency and streams in TorbScript

Asynchrony is in the type system, not in a keyword: a function that answers `Task<Value>` may call `.await()` in its
body, the way a function that answers a `Result` may use `?`. The `torbscript` skill has the rest of the language; this
skill has tasks, channels, streams and parallel pipelines. Paths are relative to the directory of this file.

## Which page answers what

- Starting work, waiting for it, cancelling it, a timeout - read
  [Tasks](references/language/concurrency-and-streams/tasks.md) first, then [std/task](references/standard-library/task.md)
  for `Task`, `spawn`, `Cancelled`, `TimedOut` and `pause`.
- Handing values from one task to another - [Channels](references/language/concurrency-and-streams/channels.md).
- Bytes or values that arrive over time - a file, a socket, a process, standard input - read
  [Streams](references/language/concurrency-and-streams/streams.md), then [std/stream](references/standard-library/stream.md)
  for `Source`, `Sink`, `Bytes` and the stages between bytes and text.
- A pipeline over a large collection, spread over the cores - [std/parallel](references/standard-library/parallel.md).

## Pages

- [Concurrency and streams](references/language/concurrency-and-streams/index.md) - Task, Channel, Source and Sink - asynchrony in the type system instead of a keyword, run by a pool of workers.
- [Tasks](references/language/concurrency-and-streams/tasks.md) - Task<Value> is what an asynchronous function answers; await() waits for it and answers the value, and a cancellation is passed on to the waiter instead of answered.
- [Channels](references/language/concurrency-and-streams/channels.md) - A Channel is a stream in memory whose one holder has both ends, handed out separately as a Source and a Sink so a producer never sees the reading end and a consumer never sees the writing one.
- [Streams](references/language/concurrency-and-streams/streams.md) - Source and Sink are the asynchronous siblings of Iterator and Accumulator, with the same verbs, the same Stage values in between, and a failure that stands in the type on both ends.
- [std/task](references/standard-library/task.md) - Task and Channel, the two shared types that connect concurrent work, spawn, cancellation with Cancelled and TimedOut, and pause.
- [std/parallel](references/standard-library/parallel.md) - parallel() on anything that can be iterated, Parallel, a pipeline whose fused stages run on the workers of the pool with the results in input order, and Cut, the collections that cut themselves.
- [std/stream](references/standard-library/stream.md) - Source and Sink, the asynchronous ends of a stream, plus Bytes, Utf8Error and the stages between bytes and text.
