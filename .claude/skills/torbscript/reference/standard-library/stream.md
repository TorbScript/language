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
  - docs/design/STREAMS.md
---

"Stream" is the word for a flow in one direction, not a type: what a signature names is one of its two ends,
`Source<Item, Failure>` to read from or `Sink<Item, Failure>` to write into. They are the asynchronous siblings of
[std/iteration](iteration.md)'s `Iterator` and `Accumulator` and carry the same verbs. `Source`, `Sink` and `Bytes` are in
the prelude; `Utf8Error`, `lines` and `textOf` are imported from here.

## Import

```trb fragment
use Source, Sink, Bytes, Utf8Error, lines, textOf from "std/stream"
```

```trb check
fn totalLength(channel: Channel<String>): Task<Result<Int, Cancelled>> {
  var reading = channel.source()
  var total = 0
  while const Some(word) = reading.next().outcome()? {
    total = total + word.byteLength()
  }
  Ok total
}
```

## Declarations

### Source

```trb fragment
public shared trait Source<Item, Failure: From<Cancelled>> with Close {
  var fn next(): Task<Result<Item?, Failure>>
  var fn through<Output>(stage: Stage<Item, Output>): Source<Output, Failure>
  var fn map<Output>(transform: (value: Item) => Output): Source<Output, Failure>
  var fn filter(predicate: (value: Item) => Bool): Source<Item, Failure>
  var fn filterMap<Output>(transform: (value: Item) => Output?): Source<Output, Failure>
  var fn mapWhile<Output>(transform: (value: Item) => Output?): Source<Output, Failure>
  var fn take(amount: Int): Source<Item, Failure>
  var fn takeWhile(predicate: (value: Item) => Bool): Source<Item, Failure>
  var fn skip(amount: Int): Source<Item, Failure>
  var fn indexed(): Source<(index: Int, item: Item), Failure>
  var fn chunked(size: Int): Source<List<Item>, Failure>
  var fn then<Output>(step: (value: Item) => Task<Result<Output, Failure>>): Source<Output, Failure>
  var fn mapFailure<Other>(transform: (failure: Failure) => Other): Source<Item, Other>
  var fn collect<Output>(into: Accumulator<Item, Output>): Task<Result<Output, Failure>>
  var fn toList(): Task<Result<List<Item>, Failure>>
  var fn count(): Task<Result<Int, Failure>>
  var fn fold<State>(initial: State, combine: (State, Item) => State): Task<Result<State, Failure>>
  var fn forEach(action: (value: Item) => Void): Task<Result<Void, Failure>>
  var fn find(predicate: (value: Item) => Bool): Task<Result<Item?, Failure>>
  var fn into(var sink: Sink<Item, Failure>): Task<Result<Void, Failure>>

  static fn from(items: Iterate<Item>): Source<Item, Failure>
  static fn pulling(step: () => Task<Result<Item?, Failure>>): Source<Item, Failure>
  static fn produce(
    capacity: Int = 0,
    body: (var sink: Sink<Item, Failure>) => Result<Void, Failure>,
  ): Source<Item, Failure> where Failure: From<ChannelClosed>
}
```

The reading end of a stream. `next()` answers `Ok(Some(item))` for the next item, `Ok(None)` at the end, and
`Fail(problem)` for a failure that ends the stream for good. A source has an identity and is consumed once, so it is a
`shared type`, and reading from it needs no `var` binding: a change of an object is not a question of the path it is
reached through (see [Shared types](../language/types/shared-types.md)), so every holder reads from the one stream.
Wrapping (`map`, `filter`, `through`) hands the source to a wrapper that pulls from it afterwards, and a chain reads as
one expression. Backpressure is the pull: nothing is read
before somebody asks for it. `checked()` (an extension for `Source<Result<Item, Problem>, Failure>`) turns `Result`
items into the stream's own failure, ending the stream there. `produce` is the one way to write a producer without
generators: `body` runs as a task of its own and writes into a `Channel` (see [std/task](task.md)), with `capacity: 0`
handing every item over directly, in lock-step with the consumer.

The stages read exactly like `Iterate`'s: `filterMap` transforms and drops the items that answer `None`, `mapWhile`
transforms until one does and ends the stream there, `take`/`takeWhile`/`skip` bound how much is read, `indexed`
pairs every item with its position, and `chunked` groups items into lists of at most `size`, the last group being
whatever is left. The terminal operations beyond `toList` are `count` (how many items arrived), `fold` (every item
combined into a running state, left to right), `forEach` (an action run on every item as it arrives), and `find`
(stops at the first match and leaves the rest of the stream unread).

### Sink

```trb fragment
public shared trait Sink<Item, Failure: From<Cancelled>> with Close {
  var fn add(item: Item): Task<Result<Void, Failure>>
  var fn end(): Task<Result<Void, Failure>>
  var fn addAll(items: Iterate<Item>): Task<Result<Void, Failure>>
  var fn fill(var source: Source<Item, Failure>): Task<Result<Void, Failure>>
  var fn mapFailure<Other>(transform: (failure: Failure) => Other): Sink<Item, Other>
  var fn buffered(capacity: Int = 64): Buffered<Item, Failure>

  static fn pushing(
    accept: (item: Item) => Task<Result<Void, Failure>>,
    complete: () => Task<Result<Void, Failure>>,
  ): Sink<Item, Failure>
  static fn discarding(): Sink<Item, Failure>
}
```

The writing end, the asynchronous sibling of `Accumulator`. Backpressure is the `await` on `add`: the task finishes
once the target has taken the item, so a writer faster than its target waits by itself. `end()` is the graceful end
- everything buffered is written, and whatever went wrong is reported here at the latest; `close()` (from `Close`) is
the abrupt end, cannot fail, and may leave less written than `end()` would have. `buffered(capacity:)` answers a
`Buffered`, whose own `flush()` is where "when was it actually written" gets an answer; `end()` flushes too, but
`close()` does not.

Both traits carry `Failure: From<Cancelled>`: every asynchronous read and write can be cancelled, so a stream's failure
type is one a cancellation converts into, and `outcome()` of [std/task](task.md) folds it in with one `?`.

### Bytes, Utf8Error

```trb fragment
public type Bytes = List<UInt8>

public type Utf8Error with Show, Error {
  offset: Int
}
```

`Bytes` is an alias for `List<UInt8>`, not a type of its own, so every list operation already works on a chunk - and a
chunk belongs to whoever received it, since a list is a value and there is no borrowed buffer to copy before the next
`await`. `Utf8Error` is bytes that are not UTF-8, with the offset of the byte that broke it; a `String` is never anything
else. The offset is counted from the start of what was decoded, and which that is belongs to the stage that failed:
`textOf` counts from the start of its chunk, `lines()` from the start of the line, and `decodedText()` from the start of
the whole stream, because a chunk border is nobody's choice and an offset inside one would name a byte the caller cannot
point at.

### `lines`, `decodedText`, `textOf`, `encodedText`

```trb fragment
public fn lines(): Stage<Bytes, Result<String, Utf8Error>>
public fn decodedText(): Stage<Bytes, Result<String, Utf8Error>>
public fn textOf(bytes: Bytes): Result<String, Utf8Error>
public fn encodedText(): Stage<String, Bytes>
```

Bytes to lines, split at `\n` with a trailing `\r` dropped so CRLF files read like LF files; a last line without a
break is still a line. `decodedText` cuts at character borders instead, holding back a character split across two
chunks. Both are resumable `Stage`s (see [std/iteration](iteration.md)) and work on any `Iterate<Bytes>` as well as on
a `Source<Bytes, Failure>` - they need no `Task` themselves, only reading the `Source` they sit in front of does.
A sequence that is still incomplete when the stream ends is reported as invalid UTF-8, at the offset it starts on - there
is no more input coming to complete it. `textOf` is the short form for a whole chunk that has already arrived;
`encodedText` is the reverse, and needs no decision because a `String` already is UTF-8.

### `Sink<Bytes, Failure>`.addText, addLine

```trb fragment
extend<Failure> Sink<Bytes, Failure> {
  var fn addText(text: String): Task<Result<Void, Failure>>
  var fn addLine(text: String = ""): Task<Result<Void, Failure>>
}
```

An extension of the *instantiated* trait, so it is there for every byte sink - a file, standard output, a socket, the
input of a child process - and nowhere else. `addText` encodes `text` as UTF-8 and writes it, the text form of `add`;
`addLine` calls it with `\n` appended, the byte a wire uses and not the line break of the operating system running the
program.

## What is missing

What the native back end does not run yet is a `Source` or a `Sink` held as a **trait-typed value**: its witness
table holds every default member some implementation overrides, `through` among them, and `through` reaches
`Stage.onto` - a generic member through a trait-typed value, which the back end cannot call yet. A concrete source or
sink - a `Channel`'s two ends, which `source()` and `sink()` answer as `ChannelSource` and `ChannelSink` - runs, stages
and terminal operations included. `Bytes`, `Utf8Error`, `textOf`, `lines()` and `decodedText()` do not depend on
`Task` at all and work wherever a `Stage` does - over an `Iterate<Bytes>` in a test, for instance.

## Related

- [std/iteration](iteration.md) - `Iterator`, `Accumulator` and `Stage`, the synchronous siblings this package reuses.
- [std/task](task.md) - `Task` and `Channel`, which every `Source`/`Sink` verb rides on.
- [std/fs](fs.md) - `File`, which is both a `Source` and a `Sink`.
- [The standard library](index.md) - the other packages.

