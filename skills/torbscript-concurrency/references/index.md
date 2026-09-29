# The pages of torbscript-concurrency

Every page of this skill, with what it answers. Search this file for a word, then open the one page that
answers the question. A page marked planned describes a feature that does not compile yet.

## Contents

- language/concurrency-and-streams
- standard-library

## language/concurrency-and-streams

- `language/concurrency-and-streams/index.md` - **Concurrency and streams** (index): Task, Channel, Source and Sink - asynchrony in the type system instead of a keyword, run by a pool of workers.
- `language/concurrency-and-streams/tasks.md` - **Tasks** (reference): Task<Value> is what an asynchronous function answers; await() waits for it and answers the value, and a cancellation is passed on to the waiter instead of answered.
- `language/concurrency-and-streams/channels.md` - **Channels** (reference): A Channel is a stream in memory whose one holder has both ends, handed out separately as a Source and a Sink so a producer never sees the reading end and a consumer never sees the writing one.
- `language/concurrency-and-streams/streams.md` - **Streams** (reference): Source and Sink are the asynchronous siblings of Iterator and Accumulator, with the same verbs, the same Stage values in between, and a failure that stands in the type on both ends.

## standard-library

- `standard-library/task.md` - **std/task** (package): Task and Channel, the two shared types that connect concurrent work, spawn, cancellation with Cancelled and TimedOut, and pause.
- `standard-library/parallel.md` - **std/parallel** (package): parallel() on anything that can be iterated, Parallel, a pipeline whose fused stages run on the workers of the pool with the results in input order, and Cut, the collections that cut themselves.
- `standard-library/stream.md` - **std/stream** (package): Source and Sink, the asynchronous ends of a stream, plus Bytes, Utf8Error and the stages between bytes and text.
