---
name: torbscript-concurrency
description: "Writes concurrent TorbScript: `Task`, `spawn` and `.await()`, cancellation and timeouts, `Channel`, the `Source` and `Sink` ends of a stream, and `parallel()` pipelines. Use it when TorbScript code waits on work, runs things at the same time, or reads or writes a stream of values."
license: MIT
compatibility: "Needs the torb command, which the torbscript skill checks and installs."
---

<!-- carry: language/concurrency-and-streams/ standard-library/task.md standard-library/stream.md -->
<!-- carry: standard-library/parallel.md -->

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

<!-- pages -->
