# Streams

**Status: partly implemented** — `std/stream` declares the whole vocabulary and it type checks; nothing asynchronous
runs before tasks arrive in milestone 7.3, and `Stage` does not compile natively yet (section 14, point 6).

One flow, in one direction, with two ends. This is the specification of `std/stream` and of the `Stage` that
`std/iteration` carries, of the contracts both ends promise, and of what HTTP, the file system, the standard streams, a
child process and the formats look like once they all speak it. Nothing here decides a question of the language: the
traits are ordinary traits and the drivers are ordinary loops.

```text
                       Stage<Input, Output>          (synchronous, source-independent, a value)
                  ┌──────────── through ────────────┐
Iterate<Item> ───┤                                 │
 Source<Item, F> ─┘        onto(Accumulator)        └───  Accumulator<Item, Output>   ← the same accumulators
```

- **[1. The model](#1-the-model)** — what a stream is and what it is not
- **[2. `Source`](#2-source)** and **[3. `Sink`](#3-sink)** — the two ends, with their contracts
- **[4. `Stage`](#4-stage)** — the middle, written once
- **[5. Synchronous, asynchronous, blueprint](#5-synchronous-asynchronous-blueprint)** — the table
- **[6. The drivers](#6-the-drivers)** — who pulls, who pushes, where the queue is
- **[7. Cancelling and `Close`](#7-cancelling-and-close)**
- **[8. Producing without generators](#8-producing-without-generators)**
- **[9. Bytes and text](#9-bytes-and-text)**
- **[10. Formats](#10-formats)** — `Format`, framing, and why a single huge value is not streamed into a type
- **[11. What the standard library looks like](#11-what-the-standard-library-looks-like)**
- **[12. Where this comes from](#12-where-this-comes-from)** — Rust, C#, Swift, Scala, Java, Node, Web Streams, Bun
- **[13. Open points](#13-open-points)**
- **[14. What milestones 7 and 10 have to build](#14-what-milestones-7-and-10-have-to-build)**

---

## 1. The model

**"Stream" is a word, not a type.** It names the whole flow — this chapter, and the package `std/stream`. What a
signature names is one of its two **ends**, because at any point in a program you hold one end and not both:

```trb
public shared trait Source<Item, Failure> with Close {
  var fn next(): Task<Result<Item?, Failure>>
}

public shared trait Sink<Item, Failure> with Close {
  var fn add(item: Item): Task<Result<Void, Failure>>
  var fn end(): Task<Result<Void, Failure>>
}
```

These are the asynchronous siblings of `Iterator` (`next`) and `Accumulator` (`add`) — **the same verbs**, so nothing
new has to be learned and the two worlds read alike. The one word that differs is the ending: an accumulator's
`finish()` answers the result of a run, a sink's `end()` answers only whether the end went through, because the end
of a flow is not a result (section 3, and `docs/COLLECTIONS.md` 6a for why it is not `close()`).

| Synchronous              | Asynchronous                      | Verb        |
|--------------------------|-----------------------------------|-------------|
| `Iterator<Item>`         | `Source<Item, Failure>`           | `next`      |
| `Accumulator<Item, Out>` | `Sink<Item, Failure>`             | `add`; `finish` against `end` |
| `Iterate<Item>`          | — (a source *is* the flow)        | `iterate`   |
| `List<Item>`, `Set<Item>` | — (a flow is not a container)     | `append`, `insert` |
| `Stage<Input, Output>`   | the same `Stage`                  | `onto`      |

Five decisions carry everything below.

**A stream has an identity and is consumed once**, so its ends are `shared type`s, and the verbs that consume take
`var fn`s exactly as `Iterator.next` and `Accumulator.add` do. **A `var fn` method of a shared type may answer a
`Task`**: "an exclusive `var` access cannot stay open across an `await`" is true for a *value*, where `var` is an exclusive
in-out access whose copy back ends with the call — for an object there is no copy, `var` is the permission to change the
one object, and a permission survives an `await` (CONCEPT, "Identity"; TYPECHECKER gap 49). The reverse is an error with
a message of its own: a `var fn` receiver or a `var` parameter of a **value** on a function that answers a `Task` would write its
change back before the task has run.

So **a source that is read from sits in a `var` binding** and a `const` handle is the read-only view every shared object
has. The second consequence of identity: a source cannot be handed to another task, because shared objects are confined
to the task that made them. Nothing in `std/stream` spawns except `Source.produce`, which spawns a task that captures a
`Channel` and nothing else shared.

**A failure ends the stream and stands in the type**, on both ends, symmetrically. An end that cannot fail is
`Source<Item, Never>` — `Channel`'s reading end is one. This is what every language except Rust does: Rust's
`Stream<Item = Result<T, E>>` puts the failure in the item and therefore has to answer "what does the stream do after an
`Err`" per implementation, which is why `futures` needs `TryStreamExt` next to `StreamExt` for every combinator.

**Backpressure is the shape of the protocol, not a mechanism.** At the reading end it is the pull: nothing is read until
somebody asks. At the writing end it is the `await` on `add`: the task finishes when the target has *taken* the item.
There is no `poll_ready`/`start_send`/`poll_flush` (Rust), no `desiredSize`/`highWaterMark` (Web Streams), no
`request(n)` (Reactive Streams). A writer that is faster than its target waits by itself.

**Buffering is always a wrapper.** `sink.buffered(capacity:)` answers a `Buffered`, which has `flush()`; `end()`
flushes, `close()` does not. "When was it actually written" is the one question a writer must be able to answer, so it
is never a hidden property of a sink.

**Only what has to wait is asynchronous.** Everything in the middle — `map`, `filter`, `take`, framing, codecs, the
collectors, the terminal operations — is synchronous and shared with `Iterate`. See section 5.

## 2. `Source`

```trb
var fn next(): Task<Result<Item?, Failure>>
```

`Ok(Some(item))` is the next item, `Ok(None)` the end of the stream, `Fail(problem)` a failure that ends it.

**Whoever reads needs the permission — now or later.** `next`, `collect`, `toList`, `count`, `fold`, `forEach`, `find`
and `into` read items, so they are `var fn`s. So do `through`, `map`, `filter`, `take`, `then`, `mapFailure` and
`checked`: they hand the source to a wrapper that reads it from then on, which is the same permission one step later.
One rule, no exceptions — and a `const` source cannot be consumed by wrapping it either, which is exactly what the
read-only view of a shared object promises.

A chain is still **one expression**, because a freshly produced object *is* a `var` path (gap 52): the rule against a
temporary protects values, where the change would be lost with the copy it was made in, and an object has no copy —
nobody else holds a view of something that was just made, so the full permission is the caller's to give.

```trb
const all = body.through(Json.items<User>()).checked().toList().await()?
```

What needs a `var` binding is only what a `const` handle would have withheld anyway:

```trb
var response = http.get(url).await()?     // `response.body` is a `var` path through it
const text = response.body.text().await()?
```

**The contract.**

1. **After `Ok(None)`, every further `next()` answers `Ok(None)`.** The end is final and may be asked about again.
2. **After `Fail(problem)` the stream is over.** A source may keep answering the same failure and it may answer
   `Ok(None)`; it must never deliver an item again. A reader is not required to stop pulling, and a source is not
   required to remember which of the two it chose.
3. **One puller at a time.** `next()` must not be called again before the task it answered has finished. Two
   overlapping pulls are a bug in the caller; a source does not have to defend against them, and nothing in the
   standard library does.
4. **Items arrive in the order they were produced.** There is no reordering anywhere in `std/stream`.
5. **Whoever produced a chunk gave it away.** A chunk is a value (`Bytes` is a `List<UInt8>`), so a reader may keep it,
   slice it and pass it on. There is no borrowed buffer that has to be copied before the next `await` — the mistake
   every `read(&mut buf)` interface makes a caller think about.
6. **`close()` releases everything above.** See section 7.
7. **The task a source belongs to is the task that made it.** A `shared type` does not cross a task boundary.

**The loop** is a `while` with a pattern, and it works today:

```trb
var lines = file.lines()
while const Some(line) = lines.next().await()? {
  print line
}
```

**There is deliberately no `for` over a source in v1.** A `for` head has no place for the `?`: the pull can fail, and
the only honest spellings are a new keyword combination (`for try await` in Swift) or a silent decision about what
happens to the failure. Both cost more than the `while` saves — the `while` names `await` and `?` at the point where
they happen, which is the same reason there is no `async` keyword in the language. It stays an open question
(section 13, and CONCEPT's Open Questions).

## 3. `Sink`

```trb
var fn add(item: Item): Task<Result<Void, Failure>>
var fn end(): Task<Result<Void, Failure>>
```

**The contract.**

1. **`add` finishes when the target has taken the item.** That is the whole of backpressure.
2. **`add` must not be called again before the task it answered has finished,** and items arrive in the order of the
   calls.
3. **`end()` is the graceful end.** Everything buffered is written, the target learns that no more is coming, and
   whatever a buffered write could only report now is reported here. After `end()`, `add` fails.
4. **After a failure, neither `add` nor `end` will succeed again.**
5. **`close()` is the abrupt end.** It releases the target, cannot fail, and does **not** flush. A sink that is closed
   without being ended may have written less than it was given — that is exactly the difference between abandoning a
   sink and ending one, and it is why both exist. `close()` is the destructor (`docs/DESTRUCTORS.md`): it runs by
   itself when the last holder releases the sink, and user code never calls it.
6. `end()` may be called once. Calling it twice is a bug in the caller, like calling `add` after it.

**Why two endings.** `Close` is what the language uses for a resource (`using`, CONCEPT), it is synchronous and it
cannot fail — so it can run on a path that is already unwinding a failure. Ending a stream can do neither: writing
out a buffer takes time and can fail, and that failure is the most important one a writer gets. Collapsing them would
mean either a `close()` that can fail (and then the runtime cannot run it on a release) or an `end()` that cannot (and
then a failed flush is lost). Neither ending is implicit in a task: `using` never awaits `end()` (DESTRUCTORS section
7), so a writer that wants the graceful end writes `sink.end().await()?` where it wants it.

## 4. `Stage`

A stage hangs on the **target**, not on the source:

```trb
public trait Stage<Input, Output> {
  fn onto<Final>(downstream: Accumulator<Output, Final>): Accumulator<Input, Final>

  fn then<Final>(other: Stage<Output, Final>): Stage<Input, Final>
}
```

It turns an accumulator into an accumulator — a **transducer** (Clojure's word; it is also how Java's streams are built
inside). Because a stage never asks where its values come from, **every stage exists exactly once** and works on both
worlds:

```trb
const interesting = filtering<Event>({ _.level >= .Warning }).then(mapping { _.message })

const fromList = events.through(interesting).toList()                    // a list, synchronously
const fromBody = response.body.through(interesting).toList().await()?   // an HTTP body, asynchronously
```

`Accumulator` gains one member for it:

```trb
fn isDone(): Bool { false }
```

Whether more values would change the result. A driver asks **before it pulls the first value and after every `add`**,
and stops as soon as the answer is `true`. That is what lets `taking(10)`, `first()` and `find(...)` end a pipeline over
an infinite or expensive source without reading one value it will not deliver. Every wrapper forwards what the
accumulator below it answers and adds what it knows itself (`taking` at zero, `takingWhile` and `mappingWhile` once
their predicate said no).

**The stages that exist,** all in `std/iteration`: `mapping`, `filtering`, `filterMapping`, `mappingWhile`,
`flatMapping`, `taking`, `takingWhile`, `skipping`, `indexing`, `chunking(size:)`; in `std/stream`: `lines()`,
`decodedText()`, `encodedText()`; and whatever a format provides (`Json.items<User>()`, `Json.encoded<User>()`).

**Why they are functions and not `Stage.map`, `Stage.filter`, …** A member of a trait used as a namespace has to fix the
trait's own type arguments from the member's signature, and `Stage.filter` cannot: its answer is `Stage<Input, Input>`
and says nothing about `Output`, so the checker reports "Cannot infer `Output` of `Stage`" — verified. Defaulting
`Output` to `Input` fixes `filter` and breaks `map` (a defaulted argument is filled in, never inferred; gap 6). So the
factories are free functions, named with the gerunds `std/iteration` already uses for collectors (`counting`, `joining`,
`groupingBy`), which has the second benefit of keeping them apart from the methods of the same meaning on `Iterate`
and `Source`: `items.map(f)` *is* `items.through(mapping(f))`. Section 13 records what a language rule could look like
that would make `Stage.map` possible.

**`zip` is not a stage** and stays a driver matter: it reads from two sources, and a stage has exactly one input.
**`sorted` is not a stage either** in any streaming sense — it collects and then delivers, which is what it does today.

**Fallible stages answer `Result` items.** A stage is synchronous and knows nothing about the failure of the stream
around it, so `Json.items<User>()` is a `Stage<Bytes, Result<User, JsonError>>`. `source.checked()` lifts those into the
stream's own failure and ends it there:

```trb
extend<Item, Problem, Failure: From<Problem>> Source<Result<Item, Problem>, Failure> {
  fn checked(): Source<Item, Failure>
}
```

On an `Iterate` the same job is already done by `Result … with From<Iterate<Result<…>>>` in `std/core`
(`.to<Result<List<User>, JsonError>>()`), which stops at the first failure.

**Static or dynamic dispatch.** The drivers take `stage: Stage<Item, Output>` as a trait-typed value rather than a
bounded generic parameter (`<Chosen: Stage<Item, Output>>`). The bound would give static dispatch, and where a stage is
built and used in one expression that would be free — but a stage has to be **storable**: `const activeNames:
Stage<User, String> = …` is the point of the whole design, and `then` composes two stage values into a third, so
`onto<Final>` is reached through a witness table either way. Where new code has a genuine choice and does not need to
store the value, the bounded form is preferred; the drivers do not have that choice. This is the one open back-end item
of the design (section 14), and `Decoder.sequence<Output>` in `std/encoding` has exactly the same one.

## 5. Synchronous, asynchronous, blueprint

| | What it is | Where it lives |
|---|---|---|
| `Iterator`, `Iterate` | **synchronous** — pulled, no waiting | `std/iteration` |
| `Accumulator` | **synchronous** — pushed into, `isDone()` ends it; the description AND the state of one run | `std/iteration` |
| `Stage` | **blueprint** — `onto()` makes an accumulator chain per run | `std/iteration` |
| the stage factories (`mapping`, `taking`, `lines`, `Json.items`) | **blueprint** | `std/iteration`, `std/stream`, a format |
| `Source.next` | **asynchronous** | `std/stream` |
| `Sink.add`, `Sink.end` | **asynchronous** | `std/stream` |
| `source.then { … }` | **asynchronous** — the one stage-shaped thing that waits | `std/stream` |
| `source.into(sink)`, `sink.fill(source)` | **asynchronous** — the pump | `std/stream` |
| `source.collect/toList/count/fold/find/forEach` | **asynchronous drivers over synchronous accumulators** | `std/stream` |
| `Encode`, `Decode`, `Encoder`, `Decoder` | **synchronous, unchanged** | `std/encoding` |
| `Format.items`, `Format.encoded` | **blueprint** (`Stage`s) | `std/encoding` |
| `Channel` | asynchronous, and the only native place stream state lives | `std/task` |

`Stage` lives in `std/iteration` and not in `std/stream` for two reasons: it is synchronous, and what it turns into what
(`Accumulator` → `Accumulator`) is `std/iteration`'s own vocabulary. It also keeps the dependency one-way —
`std/stream` needs `std/iteration`, never the other way round.

### The three pairs, and what each one is about

| | reading end | writing end | what it is about |
|---|---|---|---|
| **values, synchronous** | `Iterate` (`iterate()`) | the kind's own verb (`append`, `insert`, `push`, ...) | a finite thing that is there already |
| **a flow, asynchronous** | `Source` (`next()`) | `Sink` (`add`, `end()`) | values over time, fallible, `shared` |
| **one run** | — | `Accumulator` (`add`, `finish()`, `isDone()`) | what to do with the values, and how far it has got |

`Accumulator` is used by **both** time axes: `iterable.collect(a)` and `source.collect(a)` are the same accumulator
driven two ways. `finish()` is its word alone — a stream's end is `Sink.end()`, because the end of a flow is not a
result. And a collection is not an `Accumulator`: the writing end of the value world is a container, not a run.

## 6. The drivers

There are exactly **two** drivers, and they are the only place the two worlds differ:

```trb
fn through<Output>(stage: Stage<Item, Output>): Iterate<Output>              // Iterate, a loop
fn through<Output>(stage: Stage<Item, Output>): Source<Output, Failure>       // Source, a loop with `await`
```

Each answer has two halves:

**`collect` is fused.** The stage wraps the accumulator it was handed and every value goes straight through — one push
per value, no queue, no intermediate collection. An accumulator is a value, so `onto` wraps a copy and every run starts
fresh (`docs/COLLECTIONS.md` 3.8a, where `Collector` and its `start()` were merged into `Accumulator`):

```trb
var accumulator = stage.onto(target)
if accumulator.isDone() { return accumulator.finish() }
for value in source {                    // ... or: while const Some(value) = upstream.next().await()?
  accumulator.add value
  if accumulator.isDone() { break }
}
accumulator.finish()
```

Every terminal operation of both worlds ends here: `toList`, `count`, `fold`, `joined`, `groupBy`, and `collect` itself.

**`iterate()` / `next()` needs a queue,** because one value pushed in can become none or many coming out while the
caller asks for exactly one. The queue holds what *one* input produced and is emptied before the next input is pulled,
so it stays as wide as the widest stage of the pipeline and no wider. This is what Clojure's `sequence` does and what
Java's `Spliterator` bridge does. The driver keeps it in a `var` field; the tail of the chain is `Queueing<Item>`, an
accumulator that writes into a `Shared<List<Item>>` the driver also holds — an accumulator is a **value**, so handing one
over would hand over a copy and the driver would never see what arrived. That box is the one piece of indirection left,
and it is there because accumulators are values, not because a shared object could not hold state.

**Chaining composes instead of stacking.** `through` on an already-staged source or iterable answers
`upstream.through(stage.then(other))`, so `source.through(a).through(b)` is one driver over one composed stage and stays
fused all the way down. `map`, `filter`, `take`, … on both traits are one-liners over `through`.

**`zip` reads two sources** and is therefore a driver and not a stage. On `Iterate` it exists today (`Zipped`); on
`Source` it belongs to milestone 7, where "wait for the next item of both" is a scheduler question.

## 7. Cancelling and `Close`

A reader that stops early — `take(5)`, a `find` that found it, a loop that broke, a failure it handled — has to release
what is above it, or a file handle or a socket stays open until the program ends. The language already has the
mechanism, so streams use it and add nothing:

- **`Source` and `Sink` both carry `Close`**, whose `var fn close()` is synchronous and cannot fail. It is the
  destructor (`docs/DESTRUCTORS.md`): the runtime runs it when the last holder releases the end, and user code never
  calls it.
- **Every derived end closes the one it came from.** Each wrapper holds its upstream in a `var` field and closes it:
  `Staged`, `Stepping` (`then`), `Remapped` (`mapFailure`), `Checked`, `Buffered`. `Pulling` and `Pushing`, the two
  closure-shaped escape hatches, hold whatever is above or below them as a `Close?`.
- **`using` is the form that pins the moment**: the name it binds cannot escape its block, so the release — and the
  `close()` inside it — happens at the end of that block (DESTRUCTORS section 4). It never awaits `end()`; a writer
  that wants the graceful end writes `sink.end().await()?` itself:

  ```trb
  using file = File.open(path)?
  const firstLines = file.lines().take(5).toList().await()?
  ```

- **Closing a produced source ends its producer.** `Produced.close()` closes the relay channel, so the producer's next
  `add` fails with `ChannelClosed`, its `?` returns, and its task ends. That is the whole cancellation story for
  `Source.produce`, and it needs no cancellation token and no separate signal.
- **`close()` after the stream ended is allowed and does nothing.** So a reader never has to know whether it read to the
  end.
- **A panic closes nothing** — the language runs no cleanup while a program falls over (CONCEPT, "Error Handling"), and
  a stream is no exception.

What the language does *not* have is a way to interrupt an `await` that is already waiting from inside the reader. A
`next()` that is in flight runs to its end; the release of the source takes effect for everything after it. Cancelling
the whole task is `docs/CONCURRENCY.md` section 8: the task's frame is released at its next suspension point or
cancellation check, and every end it held is closed by that release.

## 8. Producing without generators

The language has no `yield`, and three factories cover what generators are used for:

```trb
fn from(items: Iterate<Item>): Source<Item, Failure>                        // everything an Iterate has
fn pulling(step: () => Task<Result<Item?, Failure>>): Source<Item, Failure>   // the closure *is* `next`
fn produce(capacity: Int = 0, body: (sink: Sink<Item, Failure>) => Result<Void, Failure>): Source<Item, Failure>
    where Failure: From<ChannelClosed>
```

- `from` is the bridge from the synchronous world. `Failure` is whatever the caller needs: a source over a list cannot
  fail, and fixing it to `Never` would make it unusable where a failing stream is expected.
- `pulling` is the escape hatch for something that already knows how to answer one item and has no state worth a type
  of its own. With state, write an ordinary `shared type` with `var` fields — that is what the traits are for.
- `produce` is the push-shaped producer: `body` runs as a task of its own and writes into a sink, and the items travel
  through a `Channel` of `capacity`. **`capacity: 0` hands every item over directly**, so the producer runs in lock-step
  with the consumer — which is exactly what a generator does, without a language feature. The bound
  `Failure: From<ChannelClosed>` is there because a consumer that stops pulling ends the producer, and that end needs a
  name in the producer's own failure type.

`from` and `pulling` are not two spellings of `from`, because the language has no overloading: one member namespace, one
name per member. `yield` stays an open question (CONCEPT's Open Questions): it is the same state machine `Task` already
needs, so it is cheap to add later and nothing here would have to change.

## 9. Bytes and text

```trb
public type Bytes = List<UInt8>
```

An alias, not a type of its own — every list operation works on it, and `List<UInt8>` in twenty signatures reads like
arithmetic. It lives in `std/stream` because a *chunk* is the streams chapter's idea; `Encoder`/`Decoder` keep
`List<UInt8>` in their signatures, which is one of the ways in which `std/encoding` did not change.

Chunk borders are nobody's choice, so a line, a JSON value or even a single character can be split across two of them.
Hence three stages, all resumable, all synchronous, all working on an `Iterate<Bytes>` in a test just as well:

| | |
|---|---|
| `lines()` | `Stage<Bytes, Result<String, Utf8Error>>` — split at `\n`, a `\r` before it dropped, a last line without one is still a line |
| `decodedText()` | `Stage<Bytes, Result<String, Utf8Error>>` — text cut at character borders, an incomplete character held back |
| `encodedText()` | `Stage<String, Bytes>` — the other direction, which needs no decision: a `String` already is UTF-8 |

UTF-8 decoding is **written in TorbScript**, not asked of the runtime: the language has the bit operations (`Bits`),
`Char.tryFrom` already rejects surrogates and everything above `0x10FFFF`, and the only thing left is the overlong
check, which is a comparison. One native fewer for every back end. `textOf(bytes)` is the whole-chunk form for
everything that has all of its bytes already.

Text on a byte sink is an extension of the **instantiated** trait, so it is there for every byte sink and nowhere else:

```trb
extend<Failure> Sink<Bytes, Failure> {
  fn addText(text: String): Task<Result<Void, Failure>>
  fn addLine(text: String = ""): Task<Result<Void, Failure>>
}
```

`addLine` writes `\n` and not the line break of the platform: a stream is bytes on a wire, not a text file of an
operating system.

## 10. Formats

**`Encode`, `Decode`, `Encoder` and `Decoder` do not change, and that is the point.** They stay synchronous. A decoder
that could wait would have to be written differently for every source, every derived implementation would change with
it, and the whole standard library would be coloured by it — which is the one thing the design of `Encode`/`Decode` was
supposed to avoid.

Streaming happens one level up, at the level at which it happens in practice: **the element.**

```trb
public trait Format<Failure> {
  static fn encodeAll(value: Encode): Bytes
  static fn decodeAll<Value: Decode>(bytes: Bytes): Result<Value, Failure>
  static fn items<Item: Decode>(): Stage<Bytes, Result<Item, Failure>>
  static fn encoded<Item: Encode>(): Stage<Item, Bytes>
}
```

`items` is a resumable **framer**: it finds where one element ends in whatever bytes have arrived — across chunk borders
— and hands that element to the ordinary synchronous `Decode`. **Memory is one element**, whatever the stream weighs,
and no decoder had to learn anything. `Failure` is a type parameter because the language has no associated types, so
`Json` is a `Format<JsonError>` and a function that works for any format says
`fn load<Chosen: Format<Problem>, Problem>(…)`.

**`Json` implements it in TorbScript.** The framer tracks three things — bracket depth, whether it is inside a string,
and whether the last byte was a backslash — which is ordinary code, and a native would have to be rebuilt by every back
end. The first byte that is not whitespace decides what the stream is: a `[` opens **one top-level array** whose
elements are the items; anything else makes it a sequence of values written one after another, which covers NDJSON
without a rule of its own. `Json.encoded<Item>()` writes one value per line (NDJSON) and not an array, because a stream
has no end at which a closing bracket could be written and a reader would have to hold the whole thing to find one.

**Why a single huge value is not streamed into a type.** It would colour every decoder (above) and it buys almost
nothing: the value is in memory either way, so all that is saved is the text it came from. What that case actually needs
is an **event level** — `Json.events`, SAX-like, a `Stage<Bytes, Result<JsonEvent, JsonError>>` over
`.StartObject`/`.Key`/`.Value`/`.EndObject` — which is named here as later work (section 14) and needs nothing of the
design to change.

## 11. What the standard library looks like

**`std/task`.** `Channel<Item>` is a stream in memory of which one holder has both ends: `channel.source()` is a
`Source<Item, Never>`, `channel.sink()` a `Sink<Item, ChannelClosed>`, and the two can be handed out separately so that
a producer never sees the reading end. They are methods and not fields because they are computed and because a field
would have to be passed to the constructor. `send`, `receive`, `close` and `collect` are gone — two natives replace
four, and everything else a channel could do is TorbScript on top of its ends. The reading end cannot fail: a closed
channel is the end of the stream, not a failure. The writing end fails with `ChannelClosed`, which is the cancellation
signal of `Source.produce`. **Everything bidirectional is a type with a `source` and a `sink`** — a child process today,
a socket and a WebSocket with milestone 10.

**`std/http`.** `Response.body` and `Request.body` are a `Body`, and `shared type Body with Source<Bytes, HttpError>`.
`Response` is a `shared type` too, because it owns one end of a stream that is read once and a copy would promise a
second read of a body that is already gone — the same reason a `File` is one. `Request` stays a value: it is a
description you build, and nothing on it reads its body. Both declare `var body`, because reading a body changes it and a
reader needs a `var` path to it.
Convenience first, which is the lesson of `fetch` and Bun (`await response.json()` is what almost every program wants):
`body.bytes(limit:)`, `body.text(limit:)`, `body.json<Value>(limit:)`, `body.lines()`, all answering a `Task`. **Every
one of them takes a limit**, defaulting to `Body.defaultLimit` = 16 MiB — a server that decides how much memory a client
allocates is a denial of service, and streaming is the way past the limit rather than a bigger number. Making one is
`Body.from(text)` and `Body.from(bytes)` (two `From` implementations, so one name), `Body.of(source)` (its own name,
because `From.from` takes its argument by value and handing a stream over needs the permission to read it),
`Body.jsonOf(value)` and `Body.empty()`. `Response.json<Value>()` is a `Task` now, which it always should have been: it
was a plain `Result` only because the whole response had been read before anybody looked at it.

**`std/fs`.** `File` is `with Close, Sink<Bytes, IoError>` — an open file *is* the writing end, so everything that writes
into a sink writes into a file. `file.chunks(size:)` is the reading end and the one native of it; `file.lines()` is
TorbScript over `chunks().through(lines()).checked()`, which also fixes what the old `File.lines(path)` did wrong: it
answered an `Iterate<String>` and swallowed a read failure halfway through, and now the failure is in the type.
`File.create(path)`, `File.write(path, source)` and the whole-file helpers (`readText`, `writeText`) stay.

**`std/io`.** `standardInput()` is a `Source<Bytes, IoError>`, `standardOutput()` and `standardError()` are
`Sink<Bytes, IoError>`, `lines()` is the line form of standard input. `print`, `printError` and `readLine()` stay as the
short form: a program that prints one line should not have to think about a stream.

**`std/process`.** `Process.start(command, arguments)` answers a `Child` whose `input()`, `output()` and `errors()` are
the same sources and sinks, so a pipeline between two programs is `first.output().into(second.input())`.
`Process.run(...)` stays for everything that fits in memory.

**`std/encoding`, `std/json`.** Section 10.

**Natives.** Only the real sources and sinks: `File.create`/`chunks`/`add`/`end`, the three standard streams,
`Process.start` and the `Child` pipes, `Channel.source`/`sink`, and later the socket. Everything else — the traits, the
stages, the drivers, the framers, UTF-8, `Buffered`, `Body`'s conveniences — is TorbScript. Manifest entries for all of
them are in `compiler/src/backend/c/natives.trb`, planned for 7.3.

**The prelude** re-exports `Source`, `Sink` and `Bytes` from `std/stream`, `Stage` and the stage factories from
`std/iteration`, and `Format` from `std/encoding`. They are *vocabulary*, exactly like `Iterate` and `Accumulator`: a
signature that says which end of a stream it wants should be writable without an import, and the traits touch nothing by
themselves. What touches something stays an import — `File`, `standardInput`, `http`, `Process` — because there the
import *is* the statement "this file reads files", which is what reviews and `torb add` read and what the per-target
capability tables need.

## 12. Where this comes from

**Taken.**

| From | What |
|---|---|
| Rust (`Iterator`, `AsyncIterator`) | Pull-based, lazy, stages as values. `next()` answering an option. |
| Clojure (transducers) | `Stage`: the middle piece hangs on the target, so it is source-independent and exists once. |
| Java (`Stream`, `Collector`) | `Accumulator` shared between the worlds; the fused push driver is how Java's streams work inside. Java needs `Collector` and `Supplier` because a Java accumulator is a reference; here a copy is a run. |
| C# (`IAsyncEnumerable`) | Asynchrony belongs to the *interface*, not to a second library of combinators. |
| Swift (`AsyncSequence`, `AsyncStream`) | `Source.produce { sink => … }` is `AsyncStream`'s continuation, with a real capacity. |
| Scala fs2 / Akka Streams | Backpressure as the default and not as an add-on; a pipeline as a value that can be reused. |
| Node (`stream.pipeline`) | One pump that also handles the ending (`into`/`fill`), instead of `pipe` plus error plumbing. |
| Bun, `fetch` | Convenience first: `body.text()`, `body.json<User>()` are what a program reaches for, the stream is the layer below. |
| Go (channels) | `Channel` as a stream in memory with both ends held by one owner. |

**Deliberately not.**

| Not taken | Why |
|---|---|
| Rust's `Pin`, `poll`, `Context`, `Waker` | The language compiles tasks to state machines itself; a stream is a trait with `Task`-answering methods and needs no self-referential-future machinery. |
| Rust's failure in the item (`Stream<Item = Result<…>>`) | Forces "what after an `Err`" per implementation and doubles every combinator (`StreamExt` + `TryStreamExt`). The failure is in the type, on both ends. |
| Rust's `Sink` (`poll_ready`/`start_send`/`poll_flush`) | Four methods to say what one `await` on `add` says. |
| Reactive Streams / Reactor / RxJava (`request(n)`, `onNext`/`onError`/`onComplete`) | Push with a credit protocol: three callbacks, a subscription object, and backpressure as a second channel. Pulling is the same guarantee with none of it. |
| Web Streams (`ReadableStream`, `highWaterMark`, `tee`, locking) | A reader lock, a controller, a queuing strategy and a byte-stream variant — machinery a language with values and `shared type`s does not need. `tee` is a `Channel` plus two sinks when somebody wants it. |
| Node's `EventEmitter` streams (`'data'`, `'end'`, `'error'`) | Push without backpressure, and a failure that arrives on a channel the type system cannot see. |
| Making *everything* asynchronous (fs2, Web Streams) | It kills `list.map(…).toList()`: a list would answer a task. The synchronous world stays synchronous and the stages are shared. |
| Effect polymorphism / higher-kinded types (one set of combinators for both worlds) | This is Rust's unsolved "keyword generics". The language has no HKT on purpose (CONCEPT, "One Vocabulary instead of Higher-Kinded Types"); `Stage` shares the *logic* without sharing the names, which is the 90% that matters. |
| A blocking `await()` with a stack per task (Go, Java's Loom) | Already rejected in CONCEPT: it needs a stack per task in every back end and does not fit a JavaScript target. |
| `IntoStream`/`AsyncIterate` protocol sugar (`for await`) | See section 2: there is no place for the `?`. |
| A separate async `Accumulator` | The accumulators are push-based already, so they work unchanged for values that arrive over time. |
| Streaming a single value into a type (a "streaming decoder") | Section 10: it colours every decoder and saves only the text buffer. |

## 13. Open points

**1. The state of a shared object, and the hand-over.**
_Decision:_ **a `var fn` method of a `shared type` or a `shared trait` may answer a `Task`** (gap 49), so every
stateful end here is an ordinary shared type with `var` fields: `Iterating` holds a cursor, `Staged` a queue and an
accumulator chain, `Buffered` a list. `Pulling` and `Pushing` keep closures because a closure is what they are for. The
reverse — a `var fn` receiver or a `var` parameter of a value on a function that answers a `Task` — is an error, because the
copy back would happen before the task has run.

_Decision:_ and **the hand-over needs no new language feature** (gap 52). A freshly produced object is a `var` path, so
every member that reads — now or one wrapper later — is a `var fn`, and a chain is still one expression. With that the
design rests on no hole: gap 20 is enforced in all four places (a `var` binding, a `var` field through a generated
constructor, a `var` argument, and a trait-typed value of a `shared trait`, which needed the two `isSharedType` helpers
of the checker to become one). A `const` source can no longer be consumed at all, not even by wrapping it.

What remains true is that the language has no `move`: a `var` holder can hand its source to a wrapper and go on pulling
from the original. The `Source` contract says what that is — one puller, consumed once — and it is the same class of bug
as two overlapping pulls, which nothing here promises to catch.

**2. `Stage.map` instead of `mapping`.** Section 4 has the reason the members do not work today. The rule that would
make them work: *a type parameter of a namespace that the static member's signature does not mention need not be
inferred.* It is sound (the member's body cannot depend on it) and it would also fix `Task.done`-shaped factories.
_Decision:_ free functions now, because the rule is a change to inference in a pass other agents are working in, and the
gerund names are consistent with the collectors that are already there. **The owner's call:** whether to have that rule
and rename the ten factories afterwards.

**3. `for` over a source.** Section 2.
_Decision:_ no `for` in v1; `while const Some(item) = source.next().await()? { … }`. **The owner's call** if a spelling
turns up that keeps the `?` visible.

**4. Interrupting an `await` that is already waiting.** Releasing an end takes effect for everything after it, but a
`next()` in flight runs to its end. Cancelling a task is decided in `docs/CONCURRENCY.md` section 8: its frame is
released at the next suspension point or cancellation check.
_Decision:_ out of scope here; the release of an end (its `close()`) plus `ChannelClosed` covers the cases that matter
(stop reading, stop producing).

**5. `?` on a `Result<Value, Never>`.** `Channel`'s reading end cannot fail, so `channel.source().next().await()`
answers a `Result<Item?, Never>`, and `?` on it would need `Failure: From<Never>`.
_Decision:_ **done.** `std/core` has the blanket `extend<Target> Target with From<Never>`, whose body is the argument —
`Never` coerces to anything, so there is nothing to write. It overlaps nothing (an overlap would need a second
implementation for `Never` as the *source*, and `Never` has no values), and `Produced.next` reads
`reading.next().await()?` because of it. TYPECHECKER gap 50.

**6. Names.** `Source`/`Sink` are decided. **The owner's call** on the rest: the stage factories (`mapping`,
`filtering`, `taking`, … versus `Stage.map` under point 2), `Pulling`/`Pushing`/`Staged`/`Queueing` (the implementation
types that are public because a driver outside `std/iteration` needs them), `textOf`, `Body.jsonOf` (which is not
`Body.json` because a type has one namespace for its members and the reading side needs that name more), `source.then`
for the asynchronous step, and `chunked`/`chunking` for the grouping stage.

**7. Prelude membership.** `Source`, `Sink`, `Bytes`, `Stage`, the ten stage factories and `Format` are in.
The argument is in section 11. **The owner's call** if ten free function names in every file is too many — the
alternative is to leave the factories in `std/iteration` and import them where they are used.

**8. `Buffered` on the reading side.** There is none: `Source.produce(capacity:)` is where read-ahead would live, and a
`source.buffered(capacity:)` would have to read upstream from another task, which shared-object confinement forbids.
_Decision:_ no read-ahead in v1. Milestone 7 may add it natively where the platform reads ahead anyway (a file).

## 14. What milestones 7 and 10 have to build

**Milestone 7 (tasks).**

1. `Task`, `spawn`, `await()`, the state-machine transformation — as planned in BACKEND 7.3. Everything here rides on it.
2. `Channel.source()` and `Channel.sink()` as native shared objects over the existing ring buffer plus waiter queues,
   including `ChannelClosed` when the reading end is closed, and capacity `0` as a real rendezvous.
3. The `File` natives of the stream side: `create`, `chunks`, `add`, `end`.
4. `standardInput`, `standardOutput`, `standardError`.
5. `Process.start` and the four `Child` members.
6. **The back-end item of section 4:** a generic member reached through a trait-typed value (`Stage.onto<Final>`) needs a
   witness/vtable entry that the C back end does not have yet. `Decoder.sequence<Output>`, `Decoder.record<Output>` and
   `Decoder.map<Output>` need the same one, so it is not a cost of this design. Until it is there, `Stage` type checks
   and does not compile.
7. **The scheduled refactoring:** `std/iteration/stages.trb`'s per-stage iterators (`Mapped`, `Filtered`, `Taken`, …)
   become `Stage` values, so `Iterate.map` is `through(mapping(transform))` and the duplication in the standard library
   goes away. It waits for two things: point 6, and the native back end compiling `list.map(…)` at all (5.7). The two
   forms mean the same thing meanwhile and are tested against each other.

**Milestone 10 (breadth).**

1. `std/net`: a socket is a type with `source()` and `sink()`, and nothing about the protocol changes.
2. The HTTP client's natives (`get`, `post`, `request`) against a real `Body`, and the HTTP *server*, whose response
   body is a `Sink<Bytes, HttpError>` — the same trait from the other side.
3. `Json.events` (section 10) and the framers of the other formats (`yaml`, `toml`, `csv`, MessagePack sequences, SSE);
   each is one `Stage` and needs nothing new.
4. Compression as a stage pair (`gzip`, `deflate`), which is the case `Stage` was shaped for: resumable, synchronous,
   many-to-many.
5. `zip` on two sources (section 6), and whatever read-ahead point 8 of section 13 turns into.
