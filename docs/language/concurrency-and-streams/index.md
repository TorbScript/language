---
title: Concurrency and streams
summary: Task, Channel, Source and Sink - asynchrony in the type system instead of a keyword - designed and type-checked today, but not yet run by any back end.
kind: index
status: stable
order: 80
---

> **Planned.** Every page in this section is `status: planned`: the types type check against the real standard
> library, but no back end runs `await()`, `spawn` or a stream yet.

`Task<Value>`, `await()`, `Channel`, and the two ends of a stream, `Source` and `Sink`.

## What belongs here

Everything about waiting for a value and about moving values between tasks or across time: `Task`, `spawn`, `await()`,
`Channel`, `Source`, `Sink`, and the `Stage` values a stream shares with an ordinary pipeline. What does not belong
here: the lazy stages and collectors themselves, which are synchronous and live in
[`collections-and-iteration/`](../collections-and-iteration/index.md) - a stream reuses them rather than defining its
own.

<!-- torb:index:begin -->

## Pages

- **[Tasks](tasks.md)** - Task<Value> is what an asynchronous function answers; await() waits for it and answers a Result, because every task can be cancelled.
- **[Channels](channels.md)** - A Channel is a stream in memory whose one holder has both ends, handed out separately as a Source and a Sink so a producer never sees the reading end and a consumer never sees the writing one.
- **[Streams](streams.md)** - Source and Sink are the asynchronous siblings of Iterator and Accumulator, with the same verbs, the same Stage values in between, and a failure that stands in the type on both ends.

<!-- torb:index:end -->
