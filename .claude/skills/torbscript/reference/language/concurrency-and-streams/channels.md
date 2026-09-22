---
title: Channels
summary: A Channel is a stream in memory whose one holder has both ends, handed out separately as a Source and a Sink so a producer never sees the reading end and a consumer never sees the writing one.
kind: reference
status: planned
order: 20
keywords:
  - Channel
  - ChannelClosed
  - Source
  - Sink
source:
  - CONCEPT.md#concurrency-draft
  - std/task/src/lib.trb
---

> **Planned.** This feature is designed but not implemented. Nothing on this page works today.

`Channel<Item>` connects two tasks with a queue between them. Its one holder gets both ends through `source()` and
`sink()`, and can then hand each one to a different task: a producer writes into the `Sink` and never sees the
`Source`, a consumer reads the `Source` and never sees the `Sink`. The example below type checks; no back end runs it
yet.

## Example

```trb check
const channel = Channel<Int>(capacity: 8)

const producer = spawn {
  var writing = channel.sink()
  for value in 0..5 {
    writing.add(value).await()?
  }
  writing.end().await()
}

const total = channel.source().collect(counting()).await()
print "sent: {producer.await().isOk()}, total: {total}"
```

## Syntax

```text
Channel<Item>(capacity: Int = 0)          a stream in memory
channel.source(): Source<Item, Never>     the reading end
channel.sink(): Sink<Item, ChannelClosed> the writing end
```

## Rules

1. **`channel.source()` and `channel.sink()` are ordinary `Source` and `Sink` values, and the whole vocabulary of
   [Streams](streams.md) works on them.** They are methods, not fields, because they are computed and a field would
   have to be passed to the constructor.

2. **The reading end cannot fail.** `channel.source()` is `Source<Item, Never>`: a closed channel is the end of the
   stream, not a failure, so `next()` answers `Ok(None)` rather than a `Fail`.

3. **The writing end fails with `ChannelClosed` once nobody is reading any more.** `channel.sink()` is
   `Sink<Item, ChannelClosed>`; `ChannelClosed` is the one way a channel's writing end can fail, and it is a value, not
   a panic, because a producer that is no longer needed should stop, not crash.

4. **`capacity: 0` hands every item over directly: `add` waits until somebody pulls.** A capacity above zero lets that
   many items queue up before a writer has to wait, which is the only difference `capacity` makes.

5. **`Channel` is the exception that is allowed to cross a task boundary; the `Source` and `Sink` it hands out are
   not.** Every other shared object is confined to the task that made it, but a producer and a consumer both need to
   reach the same channel, so `Channel` is named next to `Task` as the two shared types built for exactly that. The
   pattern this rule implies: capture the `const channel` binding itself in each spawned closure, and call
   `source()`/`sink()` from inside the task that is going to use the result, rather than calling either one outside
   and handing the answer in.

## What this is not

**A `Channel` is not the same as its two ends, and not something a producer and a consumer both hold.** The channel
itself stays with whoever created it; only `source()` and `sink()` travel.

```trb check
const channel = Channel<Int>(capacity: 1)
const reading = channel.source()
const writing = channel.sink()
```

```trb error
const channel = Channel<Int>(capacity: 1)
channel.next()
// error: `Channel<Int64>` has no member `next`
```

## Related

- [Tasks](tasks.md) - `spawn` and `Task`, the two halves a `Channel` connects.
- [Streams](streams.md) - the `Source`/`Sink` contract `channel.source()` and `channel.sink()` satisfy.
- [Shared types](../types/shared-types.md) - why an object, and not a value, can have two independent ends.

