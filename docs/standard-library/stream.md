---
title: std/stream
summary: Source and Sink, the asynchronous ends of a stream, plus Bytes, Utf8Error and the stages between bytes and text.
kind: package
status: stable
order: 220
keywords:
  - std/stream
  - Source
  - Sink
  - Bytes
  - backpressure
source:
  - std/stream/src/lib.trb
  - std/stream/src/source.trb
  - std/stream/src/sink.trb
  - std/stream/src/bytes.trb
  - docs/STREAMS.md
---

"Stream" is the word for a flow in one direction, not a type: what a signature names is one of its two ends,
`Source<Item, Failure>` to read from or `Sink<Item, Failure>` to write into. They are the asynchronous siblings of
[std/iteration](iteration.md)'s `Iterator` and `Accumulator` and carry the same verbs. `Source` and `Sink` are in the
prelude; `Bytes` and `Utf8Error` too.

## Import

```trb fragment
use Source, Sink, Bytes, Utf8Error, lines, textOf from "std/stream"
```

```trb check
fn totalLength(channel: Channel<String>): Task<Result<Int, Never>> {
  var reading = channel.source()
  var total = 0
  while const Some(word) = reading.next().await()? {
    total = total + word.byteLength()
  }
  Ok total
}
```

## Declarations

<!-- torb:declarations:begin -->

### Source

```trb fragment
public shared trait Source<Item, Failure> with Close {
  fn next(var self): Task<Result<Item?, Failure>>
  fn through<Output>(var self, stage: Stage<Item, Output>): Source<Output, Failure>
  fn map<Output>(var self, transform: (value: Item) => Output): Source<Output, Failure>
  fn filter(var self, predicate: (value: Item) => Bool): Source<Item, Failure>
  fn then<Output>(var self, step: (value: Item) => Task<Result<Output, Failure>>): Source<Output, Failure>
  fn mapFailure<Other>(var self, transform: (failure: Failure) => Other): Source<Item, Other>
  fn collect<Output>(var self, collector: Collector<Item, Output>): Task<Result<Output, Failure>>
  fn toList(var self): Task<Result<List<Item>, Failure>>
  fn into(var self, var sink: Sink<Item, Failure>): Task<Result<Void, Failure>>

  fn from(items: Iterable<Item>): Source<Item, Failure>
  fn pulling(step: () => Task<Result<Item?, Failure>>): Source<Item, Failure>
  fn produce(
    capacity: Int = 0,
    body: (var sink: Sink<Item, Failure>) => Result<Void, Failure>,
  ): Source<Item, Failure> where Failure: From<ChannelClosed>
}
```

The reading end of a stream. `next()` answers `Ok(Some(item))` for the next item, `Ok(None)` at the end, and
`Fail(problem)` for a failure that ends the stream for good. A source has an identity and is consumed once, so it is a
`shared type`, and **a source that is read from sits in a `var` binding**; a `const` handle is the read-only view every
shared object has (see [Shared types](../language/types/shared-types.md)). Wrapping (`map`, `filter`, `through`) needs
`var self` too, because the wrapper keeps the source in a `var` field and pulls from it afterwards - a chain still
reads as one expression, because a freshly produced object is a `var` path. Backpressure is the pull: nothing is read
before somebody asks for it. `checked()` (an extension for `Source<Result<Item, Problem>, Failure>`) turns `Result`
items into the stream's own failure, ending the stream there. `produce` is the one way to write a producer without
generators: `body` runs as a task of its own and writes into a `Channel` (see [std/task](task.md)), with `capacity: 0`
handing every item over directly, in lock-step with the consumer.

### Sink

```trb fragment
public shared trait Sink<Item, Failure> with Close {
  fn add(var self, item: Item): Task<Result<Void, Failure>>
  fn finish(var self): Task<Result<Void, Failure>>
  fn addAll(var self, items: Iterable<Item>): Task<Result<Void, Failure>>
  fn fill(var self, var source: Source<Item, Failure>): Task<Result<Void, Failure>>
  fn mapFailure<Other>(var self, transform: (failure: Failure) => Other): Sink<Item, Other>
  fn buffered(var self, capacity: Int = 64): Buffered<Item, Failure>

  fn pushing(
    accept: (item: Item) => Task<Result<Void, Failure>>,
    complete: () => Task<Result<Void, Failure>>,
  ): Sink<Item, Failure>
  fn discarding(): Sink<Item, Failure>
}
```

The writing end, the asynchronous sibling of `Accumulator`. Backpressure is the `await` on `add`: the task finishes
once the target has taken the item, so a writer faster than its target waits by itself. `finish()` is the graceful end
- everything buffered is written, and whatever went wrong is reported here at the latest; `close()` (from `Close`) is
the abrupt end, cannot fail, and may leave less written than `finish()` would have. `buffered(capacity:)` answers a
`Buffered`, whose own `flush()` is where "when was it actually written" gets an answer; `finish()` flushes too, but
`close()` does not.

### Bytes, Utf8Error

```trb fragment
public type Bytes = List<UInt8>

public type Utf8Error with Show, Error {
  offset: Int
}
```

`Bytes` is an alias for `List<UInt8>`, not a type of its own, so every list operation already works on a chunk - and a
chunk belongs to whoever received it, since a list is a value and there is no borrowed buffer to copy before the next
`await`. `Utf8Error` is bytes that are not UTF-8, with the offset of the byte that broke it, counted from the start of
the chunk being decoded; a `String` is never anything else.

### `lines`, `decodedText`, `textOf`, `encodedText`

```trb fragment
public fn lines(): Stage<Bytes, Result<String, Utf8Error>>
public fn decodedText(): Stage<Bytes, Result<String, Utf8Error>>
public fn textOf(bytes: Bytes): Result<String, Utf8Error>
public fn encodedText(): Stage<String, Bytes>
```

Bytes to lines, split at `\n` with a trailing `\r` dropped so CRLF files read like LF files; a last line without a
break is still a line. `decodedText` cuts at character borders instead, holding back a character split across two
chunks. Both are resumable `Stage`s (see [std/iteration](iteration.md)) and work on any `Iterable<Bytes>` as well as on
a `Source<Bytes, Failure>` - they need no `Task` themselves, only reading the `Source` they sit in front of does.
`textOf` is the short form for a whole chunk that has already arrived; `encodedText` is the reverse, and needs no
decision because a `String` already is UTF-8.

<!-- torb:declarations:end -->

## What is missing

The example above type checks against the real standard library, which is what `torb docs check` verifies. What does
not run yet is everything that has to produce a real value through `.await()`: every `Source`/`Sink` verb answers a
`Task`, and [std/task](task.md) is `status: planned` because no back end gives `Task.await` a value. `Bytes`,
`Utf8Error`, `textOf`, `lines()` and `decodedText()` do not depend on `Task` at all and work today wherever a `Stage`
does - over an `Iterable<Bytes>` in a test, for instance - which is why this page stays `status: stable` while
[std/task](task.md) does not.

## Related

- [std/iteration](iteration.md) - `Iterator`, `Accumulator` and `Stage`, the synchronous siblings this package reuses.
- [std/task](task.md) - `Task` and `Channel`, which every `Source`/`Sink` verb rides on.
- [std/fs](fs.md) - `File`, which is both a `Source` and a `Sink`.
- [The standard library](index.md) - the other packages.
