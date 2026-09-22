# Concurrency and Parallelism

Many things waiting is one problem; one thing going faster is another. This is the specification of both: what a task
is and what runs it, how many cores a program uses and who decides, how a pipeline is spread over them without copying
its data, and where the world's input meets the run queue. `Task<Value>` is not a function colour — it is a return
value like any other, and there are no async keywords.

```text
  one process                     one worker per core, fixed at start            nothing shared but these

  ┌──────────────────────┐   ┌──────────────────────┐   ┌──────────────────────┐
  │ worker 0             │   │ worker 1             │   │ worker 2             │      Channel   a value crosses,
  │  heap  run queue     │   │  heap  run queue     │   │  heap  run queue     │                transferred or copied
  │  timers  poller      │   │  timers  poller      │   │  timers  poller      │      ready     one atomic flag
  └──────────────────────┘   └──────────────────────┘   └──────────────────────┘      window    a borrowed pointer,
             └───────────────── fork/join barrier ─────────────────┘                            no count at all
  ─────────────────────────────────────────────────────────────────────────────
  a task is a state machine      it never leaves its heap        counts stay plain integers
  built by the lowering          once it has started             because nothing is shared implicitly
```

- **[1. The model](#1-the-model)** — workers, stackless tasks, one heap each
- **[2. Why this shape and not another](#2-why-this-shape-and-not-another)** — green threads, a thread per task, one shared heap
- **[3. How many workers, and who says so](#3-how-many-workers-and-who-says-so)** — the manifest, the environment, the sandbox, the operation
- **[4. `parallel()`](#4-parallel)** — one vocabulary, ordered, and deterministic to the bit
- **[5. `Merge`](#5-merge)** — the associative join, and which collectors have one
- **[6. Borrowing instead of copying](#6-borrowing-instead-of-copying)** — the region, and the four rules that make it sound
- **[7. The world reaches the run queue](#7-the-world-reaches-the-run-queue)** — completion, readiness, and the blocking pool
- **[8. Waiting, stopping, failing](#8-waiting-stopping-failing)** — cancellation, timeouts, panics, `Task.all`, backpressure
- **[9. Load balance and long computations](#9-load-balance-and-long-computations)**
- **[10. What the standard library looks like](#10-what-the-standard-library-looks-like)**
- **[11. Where this comes from](#11-where-this-comes-from)** — Go, Rust, C#, Java, BEAM, JavaScript, Swift, Kotlin
- **[12. What the checker does not enforce](#12-what-the-checker-does-not-enforce)** — four probes, all green, all wrong
- **[13. What the language, the IR and the runtime must provide](#13-what-the-language-the-ir-and-the-runtime-must-provide)**
- **[14. Slices](#14-slices)** — what fits 7.3, what waits for 7.7
- **[15. Open, for the owner](#15-open-for-the-owner)**

Every snippet below was run against the checker, in `bootstrap/tests/scripts/` so that `std` resolves, with the
proposed declarations written out in the probe file. A snippet marked **type checks today** was accepted as written;
where one is not, the prose names the gap of section 13 that is in its way. The four probes of section 12 are the ones
that were accepted and should not have been.

---

## 1. The model

**A worker is an operating-system thread, and there are as many of them as the program was told to make.** The count is
one per core by default and is fixed for the life of the process. Each worker owns four things and shares none of them:

| | |
|---|---|
| a **heap** | a bump allocator with size-class free lists, the blocks tagged with the owning heap's id (BACKEND 2.5) |
| a **run queue** | FIFO, of tasks that are ready to make progress |
| a **timer wheel** | what `sleep` and a timeout wait on |
| an **IO poller** | IOCP, epoll or kqueue, section 7 |

**A task is a stackless state machine**, built by the lowering and therefore identical in the interpreter, in the VM and
in a native binary (BACKEND 5.3). Its frame is a record on the heap of the worker that runs it. `await()` is not a
block: it is a `Suspend(state, awaited)` that returns to the worker's loop, and the worker takes the next ready task.
`resume(task, state)` is a `Switch` over the state number in the entry block.

**A task never leaves the worker that started it,** because its frame is in that worker's heap and moving a heap block
is what nothing in this design does. That is the single fact from which everything else follows: counts stay plain
integers (BACKEND 2.5), nothing needs a lock, and the only atomics in the program are a channel's transfer and a ready
flag.

So the shape has a name in three parts, and each part matters: **stackless tasks, on a fixed pool of threads, each
running its own event loop, with a heap per thread.** It is tokio's multi-thread runtime with BEAM's memory model and
without work stealing — and section 9 says what that last omission costs and what is done about it.

**A value crosses a worker boundary in exactly three places**, and nowhere else:

1. **`spawn`** copies the closure's captures into the target worker.
2. **`Channel`** moves one item per `add`: transferred when every counted block it reaches has count 1, copied
   otherwise (BACKEND 2.5).
3. **A fork-join region** (section 6) hands out a borrowed pointer into the caller's heap and takes results back like
   a channel message.

`Task` and `Channel` are the two `shared type`s that may be held by more than one task. Every other `shared type` is
confined to the task that made it, which is what `containsShared` in the layout is computed for.

## 2. Why this shape and not another

**Green threads with a real stack** (Go's goroutines, Java's virtual threads) make `await()` unnecessary, and CONCEPT
rejected them before this document existed: a stack per task, a stack switch written per platform, and nothing a
JavaScript back end could do. The cost in portable C11 is the point. A state machine is a `switch (state)` in a
function that takes a frame pointer — C11 and nothing else, identical on clang, gcc and MSVC. A stack switch is
assembly per architecture, a guard page per platform, and a debugger that no longer understands the call stack. The VM
has the mirror problem: a green thread inside an interpreter means a separate interpreter stack per task, so the two
back ends would be implementing two different things and the conformance suite could not hold them to one behaviour.

**A thread per task** costs a kernel object and one to eight megabytes of address space per task. A task here is a
record in a heap. The ratio is about four orders of magnitude, which is the whole reason the word "task" exists.

**One shared heap with atomic counts** is the road not taken, and it is worth being precise about the trade. Atomic
reference counting costs a locked increment on every copy of every counted value — the measurement everybody repeats
is two to ten times a plain increment, on every `List`, `String` and boxed record the program touches, whether or not
it ever uses a second thread. Per-worker heaps pay instead at the boundary, once per crossing, and the boundaries are
few and named. The price is this document's hardest section: data that has to be worked on by several workers must be
copied, or borrowed under rules that make the copy unnecessary. Section 6 is that.

**What both back ends must agree on** is not a matter of taste. The state-machine transformation happens in the
lowering, so the VM and the C back end execute the same states in the same order; the run queue is FIFO with the same
insertion order in `runtime/task.c` and in `vm/task.trb`; the chunk borders of section 4 are a function of the input
length alone. Therefore **a program that does no real IO prints the same bytes in every back end**, which is what
`10-async.trb` is for. Real IO is the only boundary of that promise, and section 7 draws it.

## 3. How many workers, and who says so

Four levels, from the whole program down to one operation. None of them is a global runtime knob, because the worker
count is fixed at start: each worker owns a heap, and removing a worker would mean evacuating a heap — every live block
of it moved into another heap behind a stop-the-world forwarding pass. Adding a worker is cheap and removing one is
not, so neither happens.

**1. The manifest**, in the vocabulary `project.trb` already uses for `resources` and `test`:

```trb
language "0.3.0"
name "acme/mill"

tasks {
  workers 4
  blocking 8
}
```

**type checks today** — it parses and is in the canon; `tasks` is a setting the project model does not have yet
(gap 10). `workers` defaults to the core count, `blocking` to 4 (section 7). Neither is **static** in PROJECT.md's
sense: nothing an editor, a registry or `torb add` reads needs them, and they are about *this* build rather than about
the package, so they sit beside `profile` and `test` and stay out of the lock's `settings`. The accepted range is
`1..=1024`; `workers 0` is an error that says to leave the line out instead, because a zero that silently means
"decide for me" is the kind of setting nobody can read back.

**2. The environment**, so that a machine can be told without a rebuild:

```console
$ TORB_WORKERS=2 ./build/release/mill
```

`TORB_WORKERS` and `TORB_BLOCKING` override the manifest. A value that is not an integer in range makes the program
refuse to start with one line naming the variable — not a fallback to the default, because a typo that halves a
production machine's throughput in silence is worse than a program that does not start.

**3. The sandbox**, where it is a limit like memory and steps:

```trb fragment
native var fn limits(steps: Int = 1_000_000, memory: Int = 64.megabytes(), time: Duration = 2.seconds(), workers: Int = 1)
```

**The default is 1.** A sandboxed script that could fan out over the host's cores is a denial of service with a
capability list that says it has none, and the sandbox's own execution is a VM on one worker anyway.

**4. The operation**, where work is actually spread:

```trb fragment
fn parallel(workers: Int = Workers.count(), chunk: Int? = None): Parallel<Item>
```

**Readable in code**, because a program that sizes its own chunks needs the number:

```trb fragment
public type Workers {
  /** How many workers this process runs. Fixed for the life of the process. */
  native static fn count(): Int

  /** How many threads the blocking pool has. */
  native static fn blocking(): Int
}
```

**type checks today** (with bodies in place of the natives). It is a reading namespace and there is no `Workers.set`:
the count is decided before the first line of the program runs, by the three levels above it.

## 4. `parallel()`

The same words as `Iterable` and `Source`, and the terminal answers a `Task`:

```trb fragment
const total = numbers
  .parallel(workers: 4)
  .map({ expensive _ })
  .filter({ _ > 0 })
  .sum()
  .await()?
```

**type checks today** against a locally declared `Parallel<Item>` and an `extend<Item> Iterable<Item>` that carries
`parallel` — the extension is found on a `List<Int>`, which is worth saying because ECS gap 3 is that a *blanket*
`extend<World: Bound> World` is not. An extension of an instantiated trait is a different thing and it works. The `?`
is section 8: a terminal answers a `Task` and every task is cancellable, so `await()` answers a `Result`. A script may
write it at its top level, which was probed too.

### `Parallel.For` and `Parallel.ForEach`, without a second name

C#'s two most-used parallel constructs are a counted loop and a loop over a collection, and both are a pipeline whose
terminal is `forEach`:

```trb fragment
(0..rows).parallel().forEach({ shade(_) }).await()?        // Parallel.For
cells.parallel().forEach({ shade(_) }).await()?            // Parallel.ForEach
```

**type checks today**, the range and the list alike: `Range<Int>` is an `Iterable<Int>`, so the same
`extend<Item> Iterable<Item>` carries `parallel` on both and nothing has to be written twice.

**There is no `parallelFor`.** A second spelling would buy the word "for" and cost the thing this section is about: a
`parallelFor` is not a pipeline, so it has no `map`, no `filter`, no `chunk:` and no `workers:` without growing its own
copies of them — which is exactly how `Parallel.For` ended up with `ParallelOptions`, `ParallelLoopState` and
`ParallelLoopResult` beside it. The pipeline form has the two knobs already, and `.forEach()` is where a loop with no
result ends in the sequential vocabulary too.

**What C# gets from those types and where it is here.** `ParallelLoopState.Break()` is `find` (section 4: the first
match in input order, and later chunks that cannot change the answer are never started); `Stop()` is
`task.cancel()` (section 8); the thread-local `localInit`/`localFinally` overloads are `collect(collector(...))` with a
`Merge` (section 5), which is the same idea with the associativity written down.

**Everything a `Parallel` does, it does with the names it already has.** `map`, `filter`, `filterMap`, `flatMap`,
`collect`, `toList`, `count`, `sum`, `minBy`, `maxBy`, `find`, `forEach`. Nothing is called `AsParallel`,
`WithDegreeOfParallelism`, `AsOrdered` or `WithMergeOptions`: `parallel()` is the one word, its two arguments are the
two knobs, and the rest of the pipeline is the pipeline.

**The closures obey the `spawn` rule**, which is the thing PLINQ cannot say: a closure that runs on another worker may
not capture a `var`, so a data race in a parallel pipeline is a compile error rather than a `lock` somebody forgot.
That rule is gap 2 — the checker accepts the capture today, and section 12 has the probe.

### Ordered, always

Results arrive in input order. There is no unordered mode, and that is a decision rather than an omission: the only
thing unordered merging buys is the tail of one chunk, chunk sizing addresses the same tail better, and an order that
depends on which worker finished first would make the language's determinism a property of the machine. Where an
unordered result is genuinely wanted, it is a collector that says so in its name (a set does not have an order to
lose), not a mode on the pipeline that changes what every collector below it means.

### The chunk borders depend on the length and nothing else

```text
chunkCount = chunk ?? minimum(length, 64)
chunk index covers  [index * length / chunkCount, (index + 1) * length / chunkCount)      integer division
```

**`workers:` bounds how many chunks run at once and moves no border.** So the result of a pipeline depends on the
input, the closures and `chunk:` — never on the core count, the scheduling, the load of the machine or the back end.

**A `Float` sum is therefore bit-identical on every machine**, which is what `Fixed`, lockstep and replay need. It is
**not** the same number as `numbers.sum()` without `parallel()`, because floating-point addition is not associative and
the two associate differently. Both are stable; they are two different stable numbers, and a program that replays a
recording must use the same one it recorded with. Saying this is better than a promise that quietly does not hold.

Sixty-four is the default chunk count because it is enough to balance over the machines that exist, small enough that
the per-chunk overhead is amortised over anything worth parallelising, and — the reason it is a count and not a size —
a *count* makes the borders a function of the length, while a *size* makes them a function of the length too but leaves
the last chunk ragged in a way that changes with every input. A machine with more than 64 cores gets 64-way
parallelism at the default; `chunk:` is the answer, and it is one argument.

### `find` and short-circuiting

`find` answers the first match **in input order**. A chunk stops itself as soon as its accumulator says `isDone()`, the
mechanism `std/iteration` already has. Across chunks, a chunk that has not started yet and all of whose predecessors
have already produced a `Some` is never started — an optimisation that cannot change the answer, because the answer was
already decided by an earlier chunk.

### What is not on `Parallel`

**`fold`** is not, because its signature cannot carry the merge without becoming three arguments, which is exactly what
`collect(collector(...))` already is. **`sorted`** is not: a parallel sort is a different algorithm and not a stage of
this pipeline, so it is `.parallel()…toList().await().sorted()`. **`zip`** is not, for the reason STREAMS gives: it
reads two inputs and a stage has one.

## 5. `Merge`

A chunked reduction needs a way to put two partial results together. The trait is named after its method and it is a
requirement *on the collector*:

```trb fragment
/**
 * Two partial results of one collector, joined. A `Collector` that carries it can be used by `parallel()`, by a
 * divide-and-conquer fold, and by anything else that runs a collector over pieces of its input.
 */
public trait Merge<Item, Output> with Collector<Item, Output> {
  /** Associative: `merge(merge(a, b), c)` is `merge(a, merge(b, c))`. Never called with an empty chunk's output. */
  fn merge(first: Output, second: Output): Output
}
```

**type checks today**, including a concrete implementation that delegates to `counting()` for `start()`.

**The contract is three lines.**

1. **`merge` is associative.** It does not have to be commutative, because
2. **it is called in chunk order, left to right** — a fold over the chunk results, not a tree of arbitrary pairings.
3. **It is never called with the output of an empty chunk.** A chunk of zero items is never made, and an empty input
   produces zero chunks and takes the collector's own `start().finish()`. This is what makes `joining(separator:)`
   implementable at all: without it every merge would have to decide whether a separator belongs between two pieces
   one of which is not there.

**Why the merge joins two *outputs* and not two accumulators.** Java's `Collector` combines two containers `A` before
the finisher runs, which keeps more information — `averaging` combines `(sum, count)` rather than two averages. That
shape cannot be written in this language, and the checker says so in one line:

```trb fragment
var fn combine(other: Self)
```

```text
error: `combine` cannot be called on a `Combine<Int64, Int64>` value
  --> probe.trb:24:9
   |
24 |   first.combine second
   |         ^^^^^^^
   = It takes a second `Self`, and two values of a trait type need not have the same type
```

A `Collector.start()` answers a trait-typed `Accumulator`, two of them need not have the same type, and there is no
downcast to find out. Merging outputs has no such problem, and it has a second argument in its favour: the output is
the thing that crosses a heap boundary anyway, so the merge runs exactly where the value arrives.

**`merge` is a member of the collector**, so it has the collector's own configuration in hand. That is what lets
`joining(separator:, prefix:, suffix:)` work: the merge drops the suffix of the first output and the prefix of the
second before joining with the separator, because it knows what they are. A free binary operator could not.

**Which collectors of `std/iteration` get one.**

| Collector | `merge` |
|---|---|
| `listing()`, `into<List<Item>>()` | concatenation |
| `into<Set<Item>>()` | union |
| `counting()`, `summing(value:)` | `+` |
| `joining(separator:, prefix:, suffix:)` | strip, join, re-wrap, as above |
| `minBy(key:)`, `maxBy(key:)` | the smaller of the two, the first on a tie — which is what the sequential run answers |
| `partitioningBy(predicate:)` | the two lists concatenated pairwise |
| `groupingBy(key:)` | the maps merged, the lists per key concatenated |
| `into<Target>()` for a user's `Target` | whatever that type's `From<Iterable<Item>>` implies; the user writes it |
| **`averaging(value:)`** | **none.** Its output is a `Float?` and the count is gone |

`averaging` is the honest exception, and the shape of the workaround is the general one: a collector whose output
carries what the merge needs. `collector(...)` with a `(sum: Float, count: Int)` state and a pair as its output merges
by adding both fields, and the division happens at the call site.

## 6. Borrowing instead of copying

This is the section the memory model makes hard. One heap per worker plus plain reference counts means a value that
another worker works on is **copied** into that worker's heap. For `expensive(item)` per item that is nothing. For
`item * 2` it is everything: the copy costs more than the work.

**The answer is structured parallelism.** In a fork-join region the caller is inside one call for the whole duration,
and three facts hold at once:

- The caller cannot free the data, because it is suspended inside a call that holds a `var` access to it.
- Nothing else can reach the data, because a `var` access is exclusive — and **the checker already enforces that**:

  ```text
  error: `world.positions` is being changed by `both` right now
    --> probe.trb:11:23
     |
  11 | both world.positions, world.positions
     |                       ^^^^^^^^^^^^^^^
     = While a `var` access runs, the same path cannot be reached a second time
  ```

  Two `var` arguments naming two **different** fields of one value are accepted; a path and a prefix of it
  (`world`, `world.positions`) are rejected. Both were probed.
- Therefore a worker may hold a **pointer** into the caller's heap with no count change at all: no atomic, no traffic,
  no copy. A borrow is a pointer, and it is safe because the only thread that could drop the count is blocked inside
  the call.

### The four rules that make it sound

**R1. A window does not outlive the region.** It is a `var` parameter of the body, and a `var` parameter is already
what the language means by "a reference for the duration of one call" — the escape analysis of BACKEND 5.8 decides
whether a closure may keep one. Nothing else is needed and nothing is invented.

**R2. The element type contains nothing reference counted.** This is the rule that answers "what about elements that
are themselves counted — strings, inner lists". Reading `window[index]` of a `List<String>` would `Retain` a block in
*the caller's* heap with a plain integer increment, from another thread. That is the race, and it is not fixable by
being careful; it is fixable only by not having it. So the region carries a bound:

```trb fragment
public fn windows<Item: Plain>(var items: Buffer<Item>, count: Int, body: (var window: Window<Item>) => Void)
```

The signature **type checks today** with `List` in place of the planned `Buffer` and `Window`, and with a named `fn`
as the body; a closure with a `var` parameter is gap 5.

**`Plain` is a marker the compiler grants**, and it is exactly the layout property the IR already computes to a
fixpoint: `containsCounted == false` (BACKEND 1.3). `Int`, `Float`, `Point`, `Array<Float, 16>` and any record of
those are `Plain`. `String`, `List`, a type with one inside, every `shared type` and every trait-typed value are not.
A user never writes an implementation of it, the same way nobody writes a layout.

This is not a restriction dressed up as a design: `std/tensor`'s scalars are `Plain`, an ECS component is a plain data
value, and the numeric half of `parallel()` is where the copy hurt in the first place. The widening — a borrow that
reaches a counted element without retaining it — is a real extension of the ownership pass and is named in section 13
as gap 4b, not promised here.

**R3. What a worker allocates, it allocates in its own heap.** Anything it does not write into the window dies with the
region, freed by its own heap with no foreign-free traffic. A value it hands back travels like a channel message:
transferred when every counted block has count 1, copied otherwise — the machinery BACKEND 2.5 already describes, used
unchanged.

**R4. A window is a window, not a list.** `Window<Item>` has a length and `[index]` and it has **no** `add`, `insert`
or `removeAt`, because a resize would reallocate the caller's block from another thread. That is a type rather than a
rule somebody has to remember, and it is the whole difference between a window and a `Buffer`.

### How results come back

Two shapes, and the design offers both because they answer different questions.

- **Into a pre-sized result buffer.** The caller makes the output `Buffer` before the region and hands out disjoint
  windows of it alongside the input windows. Zero allocation, and the shape `std/tensor` wants for `c = a * b`.
- **Built in the worker's heap and transferred.** The region answers one value per window, and `parallel()` merges
  them (section 5). This is the shape a pipeline wants, where the size of the output is not known in advance.

### The region blocks its caller, and that is correct

`windows` answers `Void` and does not return until every window is done. It cannot answer a `Task`: the language
already forbids a `var` parameter of a value on a function that answers a `Task`, because the copy back would happen
before the task has run (STREAMS section 2, TYPECHECKER gap 49) — and that rule is exactly right here, since the whole
argument for the borrow is that the caller is *inside* the call.

The calling worker is therefore busy for the length of the region, and it spends that time as one of the region's own
workers: it runs window 0 itself. So N workers give N-way parallelism with nothing idle, which is what
`ForkJoinPool.invoke` and rayon's `join` do for the same reason.

**Cancelling a region does not shorten it below the barrier.** Section 8 makes every task cancellable, and the three
facts above are the reason a cancelled region still joins: each chunk stops at its next suspension point, and the
region returns when the last of them has stopped. A cancellation that let the caller return early would hand out a
window into a heap the caller is free to change, which is the one thing the borrow is built to prevent. So
cancellation shortens the *work* and not the *scope*, and that is why this design has no detached task (section 8).

`parallel()` is different: it takes its input **by value** and holds no `var` access, so its terminal may answer a
`Task` and the caller's worker is free while the region runs. What the caller is suspended on is this region, and
nothing else can resume it, so the pipeline's own value cannot be freed underneath the workers.

### Which path a pipeline takes

**`parallel()` borrows a chunk when `Item: Plain` and the source is indexable; otherwise it copies the chunk into the
worker's heap.** Both are correct and the difference is not observable in the language — that is CONCEPT's rule about
value semantics, and a copy that can be skipped is the compiler's business. It is observable in the profiler, and it
should be, because a pipeline that copies when the programmer expected a borrow is the one performance question this
design invites.

### One primitive underneath

`parallel()` and `windows` are not two mechanisms. Both are **a fork-join barrier on the worker pool**: run these `n`
bodies, do not return until all `n` are done. `parallel()` is that plus chunking plus `Merge`; `windows` is that plus
window arithmetic. The runtime provides one function and the standard library provides two vocabularies over it.

**This is ECS gap 8 and the tensor loop, answered once** — with one narrowing that has to be said out loud. ECS gap 8
asks for two *different systems* over disjoint fields, run at the same time. What is built here is **data parallelism
inside one system**: a column split into windows. The field case would need a form that hands several `var` paths to
several workers, and the checker's exclusivity diagnostic above shows the proof already exists for the arguments of one
call — what is missing is a way to spell "and these run on different workers". It is left out because splitting one
column over eight cores beats running two systems on two cores in every world big enough to care, and because it is the
form `std/tensor` needs. Section 15 asks the owner whether the field case is wanted anyway.

### What the index path cannot do

Two windows are disjoint by construction, computed by the library. Two *elements* are not, and the checker says so:

```text
error: `numbers[...]` is being changed by `slots` right now
 --> probe.trb:9:22
  |
9 | slots numbers[left], numbers[right]
  |                      ^^^^^^^^^^^^^^
  = `items[i]` and `items[j]` cannot be told apart. Use `items.swapAt(i, j)`
```

Two literal indices that differ are accepted; two computed ones are not. So the region's disjointness can never come
from comparing index expressions — it comes from the library computing the borders, which is why `windows` takes a
`count` and hands windows out rather than taking two ranges from the caller.

## 7. The world reaches the run queue

BACKEND 5.3 says nothing about how a file, a socket, a timer or a child process meets the scheduler. This is the
decision.

**Completion-based where the platform has it, readiness-based where it does not, behind one interface in
`runtime/io.c`.**

| Platform | Mechanism |
|---|---|
| Windows | IOCP |
| Linux | epoll; io_uring later behind the same interface, as a speed change and not a semantic one |
| macOS, the BSDs | kqueue |

**Each worker owns its own poller**, and an IO registration belongs to the worker that issued it. This is not a choice:
the task's frame lives in that worker's heap and the completion has to resume it there, so a shared poller would need a
cross-worker hand-off on every completion. With a poller per worker, IO needs no cross-worker wakeup at all.

**A small blocking pool sits behind the loop** for the operations no platform makes asynchronous: file-system metadata,
directory reads, name resolution, and waiting on a child process where the platform has no handle for it. Size
`blocking` (section 3), default 4. A pool thread **has no heap**: it is handed a plain byte buffer that belongs to the
issuing task's heap and that the task keeps alive across the `await`, it writes into that, and it sets the ready flag.
So nothing is ever allocated outside a worker's heap and the pool needs no memory model of its own.

**Cancelling a task that is waiting for the world** is section 8's rule plus one fact: a frame may not be released
while somebody outside the language may still write into it. The three mechanisms answer differently and the difference
is worth a table, because getting it wrong is a use-after-free rather than a missed wakeup:

| Waiting on | What `cancel()` does |
|---|---|
| a readiness poller (epoll, kqueue) | the registration is removed, the task is made ready, and it stops at that point. Nothing was handed to the kernel, so there is nothing to wait for |
| a completion port (IOCP, io_uring) | **the buffer was handed to the kernel.** The request is cancelled (`CancelIoEx`, `IORING_OP_ASYNC_CANCEL`) and the task stops **when the completion arrives**, whichever way it arrives; the result is discarded. The frame is released there and not before |
| the blocking pool | **the thread cannot be interrupted.** The task is marked, the pool thread runs to its end, and the frame is released and the result discarded when it returns. So `cancel()` on a name resolution frees the waiter's *core* at once and its *frame* when the platform is done |
| a timer | the timer is removed from the wheel and the task stops at that point |

The two "not before" rows are the price of the model rather than of cancellation: section 7 says a pool thread has no
heap and writes into a buffer that belongs to the issuing task's heap, and a cancellation that released that heap block
early would be exactly the race the per-worker heaps exist to make impossible. **A cancelled task therefore frees its
worker immediately and its memory at the next honest moment**, and the documentation has to say so instead of promising
that `cancel()` returns everything at once.

**`offload` is the same pool, offered to a program** that has to call a C library that blocks:

```trb fragment
/** Runs the body on the blocking pool instead of on a worker. It gets copies, cannot reach a shared object, and cannot `await`. */
public fn offload<Value>(body: () => Value): Task<Value>
```

**type checks today.** It is `spawn_blocking` with a name that says what it does to the caller's core count.

**The VM implements none of this a second time.** The VM calls the same `runtime/` natives that a compiled binary
does — BACKEND 5.2's "exactly one `ArrayList` in the process" applies to the poller as well — so the VM's task support
is the state machine plus the queue, and the operating system is reached through the same C.

**What stays deterministic, exactly.** A program that touches no file, socket, clock, environment variable, child
process or random source has a FIFO run queue fed in a fixed order and chunk borders that are a function of the input,
so **both back ends print the same bytes**. Timers are ordered among themselves — two `sleep`s of one and two
milliseconds resume in that order — but not against IO. Real IO is the boundary and there is no other. The conformance
programs for tasks are therefore IO-free by construction, which is what `10-async.trb` already is.

## 8. Waiting, stopping, failing

**Every task is cancellable, and there is one kind of task.** A `Task<Value>` carries a cancellation flag and
`task.cancel()` sets it. Nothing else distinguishes one task from another: there is no token to thread through a
signature, no scope to open, no detached variant and no second `spawn`. A design in which only some work can be stopped
is two worlds, and the whole of this document is about not having two of anything.

```trb fragment
/** Asks the task to stop at its next suspension point. A request and not a kill; asking twice changes nothing. */
native var fn cancel()
```

**It is a `var fn`**, so a task somebody intends to stop sits in a `var` binding — `var worker = spawn { … }` — and a
`const` handle is the read-only view every shared object has (STREAMS section 2). That puts the right to cancel at the
binding, where a reader sees it, and it is the same rule that makes `source.next()` a `var fn`.

### Where the flag is read

**The suspension points are the cancellation points**, and the state machine already has every one of them:

| Point | What it is |
|---|---|
| `await()` | on any task, which is also every `Source` and `Sink` verb |
| `pause()` | section 9 — the cooperative point a long loop puts in itself |
| a channel `add` or `next` | where an item crosses a heap boundary |
| an IO wait | the poller and the blocking pool, section 7 |

Before the worker resumes a task it reads the flag. If it is set, the state machine **stops**: the frame is released
exactly as a finished task's frame is released, every live value in it goes with it, and the handle answers
`Fail Cancelled` to whoever waits.

**That is the whole implementation, and the reason it is that small is section 1.** A stackless task keeps every live
value *in its frame*, so there is no stack to unwind, no destructor to run — the language has none, and CONCEPT's
"a release never runs user code" is what makes this free — and nothing to leak, because releasing the frame is the
ordinary release. A green-threaded design would need an unwind here, which is the machinery this language spent
section 2 avoiding.

**A computation that never suspends runs to its end.** `grind(1_000_000)` with no `pause()` ignores `cancel()` the way
it ignores everything else, and that is the same trade section 9 makes about preemption rather than a second one:
interrupting a task that does not suspend needs a stack to unwind, and a task has none. **`pause()` in the loop is what
makes a computation cancellable**, and it is one line — which is a second reason for it to exist and the reason
question 4 of section 15 was worth answering.

### Cancellation is structured

**A task's parent is the task that ran its `spawn`**, and cancelling a parent cancels its children.

**What "parent" means when `spawn` puts a message in another worker's inbox** (section 9): the parent is decided at the
`spawn`, not at the first run. The inbox message carries the spawning task's identity beside its captures, and the
target worker links the child to it when it copies the captures in. So a task that is still in an inbox is already a
child, and **a child whose parent is cancelled before the child's first run is cancelled before its first state** — it
never executes a line, and the message is dropped instead of copied. Placement stays free, because an identity is not a
heap block: it is a worker number and a counter, copied like an `Int`.

**A task spawned at the top level of an entry file or a script** has the **main task** as its parent, which the runtime
makes for the entry file itself. There is therefore no task without a parent and no special case in the rule. A program
does not cancel its main task; it ends.

**A region is cancelled as a whole.** `parallel()`'s terminal answers a `Task`, and cancelling that task cancels the
region: every chunk stops at its next suspension point and the fork-join barrier returns when all of them have
stopped — **the barrier still waits**, and section 6 is why it has to. A window is a borrowed pointer into the caller's
heap that is sound only because the caller is suspended inside the call for the whole duration; a cancellation that let
the caller return while a worker still held a window would be the use-after-free the four rules exist to prevent. So
cancelling a region is "ask every chunk to stop, then join", never "abandon". `windows` has no `Task` of its own to
cancel (it answers `Void` and blocks its caller), so it is stopped by cancelling its caller, at the same barrier.

**There is no detached task in v1.** Swift has `Task.detached` and this design rejects it, for a reason that is specific
to this document rather than a matter of taste: the borrow of section 6 rests on "a child cannot outlive its region",
and a child that may opt out of its parent is a child that may outlive it. One `Task.detached` inside a `windows` body
would turn every window into a dangling pointer. The work `detached` is reached for — something that should outlive the
request that started it — is spelled by spawning it from the main task, which every program has, and that spelling is
visible at the place the decision is made instead of at the place the work is written.

### It is visible in the type

```trb fragment
/** Waits for the value, or answers `Cancelled` when the task stopped instead of finishing. */
native fn await(): Result<Value, Cancelled>

/** A task that stopped at a suspension point because somebody asked it to. */
public type Cancelled with Show, Error {
  /** `"the task was cancelled"`. */
  fn show(): String {
    "the task was cancelled"
  }
}
```

**type checks today**, with the members on a stand-in `Job<Value>` and bodies in place of the natives. So a waiter
writes `task.await()?` or matches, and the compiler makes it decide — which is the point of answering a `Result` rather
than a `Value?` or a sentinel.

**`Cancelled` carries no reason**, and that is a decision. A reason field would be an open set nobody can match
exhaustively, and the one reason with a name of its own — a deadline — belongs to the function that made the deadline,
where it is `TimedOut` (below). What a bystander can honestly learn is that the value will not arrive; who asked for
that and why is the canceller's knowledge, not the waiter's.

**A waiter that is itself cancelled has no failure at all.** If task A awaits task B and A is cancelled, A's `await()`
does not answer: A stops at that suspension point and its frame is released, the ordinary case of the rule above. So
`Fail Cancelled` is observed in exactly one situation — **the awaited task was cancelled and the waiter was not** — and
never as a cancellation of one's own. That is why there is one failure value and not a pair.

### The `Task<Result<Value, Failure>>` shape, and the one member it needs

Fallible asynchronous work answers `Task<Result<Value, Failure>>`, and `await()` makes that
`Result<Result<Value, Failure>, Cancelled>`: two unwraps. The obvious spelling does not exist —

```text
error: Expected `Int64`, found `Result<Int64, Cancelled>`
  --> probe.trb:47:6
   |
47 |   Ok(job.await()??)
   |      ^^^^^^^^^^^^^
```

— because `??` is the fallback operator and lexes as one token. `(task.await()?)?` **type checks today** and reads
badly in the place it would occur most, which is every pull of every stream. So `std/task` carries one member for the
shape:

```trb fragment
extend<Value, Failure: From<Cancelled>> Task<Result<Value, Failure>> {
  /** Waits, and folds a cancellation into the task's own failure: `source.next().outcome()?`. */
  fn outcome(): Result<Value, Failure>
}
```

**type checks today on the real `Task`** — an extension of a doubly instantiated generic is accepted, and so is the body
that matches `await()` and converts through `From`. It is not a second vocabulary: it is `await()` plus the conversion
the `?` would have applied anyway, written once instead of at every call site. And it is not an overload, because the
language has one name per member and this one is `outcome` — what the task ended with, its own failure or a
cancellation, folded into one channel.

```trb fragment
fn total(var reports: Source<Int, ReportError>): Task<Result<Int, ReportError>> {
  var sum = 0
  while const Some(size) = reports.next().outcome()? {
    sum = sum + size
    pause().await()?
  }
  Ok sum
}
```

**type checks today against the real `Source` and the real `Task`**, with `outcome` declared as above and a
`ReportError` that carries `From<Cancelled>` — so the shape this design asks of `std/stream` is one the checker already
accepts, and only `await()`'s own result type is missing. The loop is cancellable twice over: at the pull, and at the
`pause()`.

### `Cancelled` is the floor of every asynchronous failure type

The bound on `outcome` is the honest consequence and it has to be said out loud: **`Failure: From<Cancelled>`**, the
same shape `Source.produce` already carries for `ChannelClosed` (STREAMS section 8). A failure type that cannot carry a
cancellation cannot be the failure of work that waits — and `Never` is such a type. So **`Channel`'s reading end is a
`Source<Item, Cancelled>` and not a `Source<Item, Never>`**, and `Source`/`Sink`'s `Failure` parameter carries the
bound. STREAMS' decided answer 5 is untouched (the `extend<Target> Target with From<Never>` blanket is about `Never` as
a *source* of a conversion, and there is still nothing to write); what changes is that no asynchronous read promises
`Never` any more, because with universal cancellation that promise is false. Swift reaches the same place from the
other side: its `AsyncSequence.next()` is `async throws`, and a failure channel on everything asynchronous is the price
of being able to stop it.

**The checker does not enforce this today**, which is probe 4 of section 12 and the reason the migration below looks
smaller than it is.

### `within`: a deadline cancels the task

```trb fragment
/** Cancels the task once the limit has passed. A task that finished first is unaffected. */
native var fn within(limit: Duration): Task<Result<Value, TimedOut>>

/** A deadline that passed. */
public type TimedOut with Show, Error {
  /** How long the waiter was willing to wait. */
  limit: Duration

  /** `"the task did not finish within {limit}"`. */
  fn show(): String {
    "the task did not finish within {limit}"
  }
}
```

**type checks today.** `within` races the task against a timer on the caller's own timer wheel, and the three outcomes
have three spellings:

| `worker.within(2.seconds()).await()` | What happened |
|---|---|
| `Ok(Ok(value))` | the task finished inside the limit |
| `Ok(Fail(TimedOut))` | the limit passed first, and `within` cancelled the task |
| `Fail(Cancelled)` | somebody else cancelled the task, or the waiter's own `within` task was cancelled |

**`TimedOut` carries the limit** because the one thing a reader of a log wants is the number that was too small, and
`within` is the only place in this design that knows it. **It is `var fn`** for the same reason `cancel()` is: it can
stop the task.

The name still says what happens, and the documentation no longer has to say "the task keeps running". A deadline that
leaves the work running is what a `context` does to a goroutine that ignores it; here the deadline is the cancel.

### `Task.all`

**`Task.all` waits for every task, and cancels the ones it still waits for when one of them is cancelled.**

Its answer is `Task<List<Value>>` and there is no failure channel in it, so "stop at the first failure" is not
something `Task.all` can see: for fallible work `Value` is a `Result`, and a `Fail` is an ordinary value that it
collects like any other. Swift's task group cancels on the first `throw` because a throw reaches its scope;
`join_all` and rayon do not cancel at all. This design is neither, and the reason is the one vocabulary: **a
cancellation is the only event `Task.all` can observe that makes its own answer impossible to build**, because a list
with a hole in it is not a `List<Value>`. So it answers `Fail Cancelled` and stops paying for results nobody can use.

Determinism is not spent on this: the answer is the whole list or `Fail Cancelled`, and how far each of the others got
before it stopped is not observable in the language — the same argument section 4 makes about the chunk that `find`
never starts.

```trb fragment
const loaded: Result<List<User>, HttpError> = (Task.all(fetches).await()?).into()
```

`extend<Value, Failure, Target: From<Iterable<Value>>> Result<Target, Failure> with From<Iterable<Result<Value,
Failure>>>` is in `std/core/src/result.trb` today. No second API, and the shape is honest about the fact that every
task that was not cancelled ran to its end.

**The parentheses are the second lexing trap and not a style**: `await()?.into()` lexes the `?.` as the optional chain
and answers `` error: `?.` needs an `Option`, and `Result<…, Cancelled>` is not one ``, which was probed. Together with
`??` that is two places where a postfix `?` on an `await()` runs into an operator that starts with the same character,
and it is the second argument for `outcome`: the member that folds the two failures answers one `Result`, so nothing
follows a `?` that the lexer can mistake for something else.

### Dropping a `Task` still does not cancel it

A `spawn` starts the work immediately, so a `Task` is a handle to something already running; dropping the handle means
nobody will read the result. Anything else would make the moment a reference count reaches zero observable, and CONCEPT
says it is not.

**The argument is stronger with `cancel()` than without it.** The one thing that made drop-cancels tempting is that
there was no other way to stop work; there is one now, it is a method with a name, and it is written where the decision
is made rather than falling out of a scope's end. And the case drop-cancel is actually reached for — a child that
should die with the work that started it — is the parent link above, which stops the child whether or not anybody still
holds its handle. tokio's "drop the future and it is gone" is the mirror image of this and it is rejected for CONCEPT's
reason, not for a preference: a language whose releases run no user code cannot make one release run the most important
piece of user code there is.

### Panics are unchanged

**A panic inside a task ends the process.** It prints the task's own trace and exits 101, like a panic anywhere else,
and it does **not** become the awaiter's `Result` — `Cancelled` is a request that was honoured, and a panic is a bug,
and folding one into the other would make every `await()?` a `catch`. The one place a panic is recoverable stays the
sandbox, where the VM is interpreting and the script has a heap of its own (BACKEND 5.4).

### Where cancellation comes from

| System | Taken | Rejected, and why |
|---|---|---|
| **Go** (`context.Context`) | Cooperative, checked at the points the program already has | The token in every signature. A `ctx` parameter is a colour by another name, and it is forgotten exactly where it matters; the flag rides with the task |
| **C#** (`CancellationToken`, `CancellationTokenSource`) | The split between *asking* and *observing* | Two types and a parameter for what is one bit on a handle; `ThrowIfCancellationRequested()` in a body that already has suspension points |
| **Kotlin** (`Job`, `CancellationException`) | Structured: a child of a cancelled job is cancelled | An exception that propagates invisibly and that a `catch (e: Exception)` swallows by accident. Here it is a `Result` the compiler makes the waiter handle |
| **Swift** (structured cancellation, `Task.detached`) | Cancellation as a request the task notices at `await`; parent cancels children; a region cancelled as a whole | `Task.detached`, because the borrow of section 6 needs "no child outlives its region" to be true without exception |
| **Rust / tokio** (drop the future) | Nothing | Cancellation at an unobservable moment. A drop that stops work makes a reference count reaching zero the most important event in the program, which CONCEPT says it is not |
| **Erlang** (`exit/2`, kill) | Nothing | A kill that a process cannot decline needs somebody to clean up after it. Per-process heaps make that affordable there and a per-*worker* heap does not: a task killed mid-frame would leave its allocations in a heap somebody else is still using |

**Backpressure is the `Channel`, unchanged** (STREAMS section 3): `add` finishes when the reader has taken the item,
capacity 0 is a rendezvous. Between two workers it has a second job: a channel `add` is where a value crosses a heap
boundary, so the capacity is also the bound on how much memory is in flight between two heaps. A program that wants
that bound writes a number; a program that does not care writes 0 and gets lock-step. **Closing a channel stays what it
is** — the end of a stream, and the way a producer learns that nobody wants its items. It is not also the way to stop a
task: `Source.produce`'s relay closing its producer (STREAMS section 7) is a stream ending, `task.cancel()` is a task
stopping, and the two read the same only in the one case where the task exists to feed the stream.

## 9. Load balance and long computations

**Round robin with pinned tasks skews**, and it skews worst in the case that matters: `spawn` in a loop over work of
uneven size. Worker 3 draws the four long ones and the others idle.

**What can be done without moving a heap.** A task that has **not started yet** owns nothing in any heap: its captures
are still the message `spawn` built. So the placement decision can be deferred:

> **`spawn` puts the task in a worker's inbox as a transferable message, and the target worker copies the captures into
> its own heap at first run.** A task that is still in an inbox may be taken by any idle worker.

That is real work stealing, of exactly the tasks for which stealing is free, and it costs one change to BACKEND 5.3's
sentence "`spawn` distributes round robin and copies the captures into the target heap" — the copy moves from the
spawn to the first run. A task that has started is never moved, which keeps the memory model intact.

**Placement hints (`spawn(on:)`) are rejected** for v1: a knob nobody can set correctly without a profiler, and one
that becomes wrong the moment the worker count changes.

**What to measure**, because none of the above is worth believing without numbers: the per-worker queue-length
histogram over a run; the fraction of tasks that ran on a worker other than the one that spawned them; the time from
`spawn` to first run, at the median and the 99th percentile. The gate is a benchmark of N tasks with a deliberately
skewed cost distribution whose wall time stays within a stated factor of the ideal split.

**A long computation without an `await()` blocks its worker.** In 7.3, where there is one worker, it blocks everything,
and that is correct rather than broken: the answer is `parallel()` or a smaller unit of work, and the documentation has
to say so instead of the scheduler pretending. In 7.7 it blocks one worker of N and round robin keeps the others fed.

**A cooperative point exists, and it is one line of the lowering:**

```trb fragment
/** Puts this task at the back of its worker's queue and lets the rest run. */
public fn pause(): Task<Void>
```

```trb fragment
fn grind(rounds: Int): Task<Result<Int, Cancelled>> {
  var total = 0
  for round in 0..rounds {
    total = total + round * round
    if round.remainder(1024) == 0 {
      pause().await()?
    }
  }
  Ok total
}
```

**type checks today.** `pause()` is `Suspend` followed by an immediate enqueue — the state machine already has both, so
it costs one state split and no runtime machinery. **It is called `pause` and not `yield` on purpose:** CONCEPT keeps
`yield` open as a *keyword* for generators, and spending the word on a function would close a question that has nothing
to do with this one.

**`pause()` is also the cancellation point of a computation that has no other one** (section 8): the worker reads the
flag before it resumes, so the `?` on `pause().await()` is where a loop that does nothing but arithmetic learns that
somebody asked it to stop. The line costs a state split and buys both fairness and cancellability, which is why it is
the answer to "how do I make this loop stoppable" and not a second mechanism.

**There is no preemption.** A worker cannot interrupt a task that does not suspend, because interrupting one needs a
stack to unwind and a task has none. This is the same trade Rust and JavaScript make and the opposite of Go's and
BEAM's, and it is decided by the stackless choice of section 1 rather than being a separate decision.

## 10. What the standard library looks like

**`std/task`** keeps what it has — `Task`, `spawn`, `all`, `Channel`, `ChannelClosed` — and gains this:

```trb fragment
/** How many workers this process runs, and how many threads its blocking pool has. Both fixed at start. */
public type Workers {
  native static fn count(): Int
  native static fn blocking(): Int
}

/** Puts this task at the back of its worker's queue, lets the rest run, and notices a cancellation. */
public native fn pause(): Task<Void>

/** Runs the body on the blocking pool. It gets copies, cannot reach a shared object, and cannot `await`. */
public native fn offload<Value>(body: () => Value): Task<Value>

/** A task that stopped at a suspension point because somebody asked it to. */
public type Cancelled with Show, Error {}

/** A deadline that passed. */
public type TimedOut with Show, Error {
  limit: Duration
}

public native shared type Task<Value> {
  /** Waits for the value, or answers `Cancelled` when the task stopped instead of finishing. */
  native fn await(): Result<Value, Cancelled>

  /** Asks the task to stop at its next suspension point. A request and not a kill. */
  native var fn cancel()

  /** Cancels the task once the limit has passed. A task that finished first is unaffected. */
  native var fn within(limit: Duration): Task<Result<Value, TimedOut>>

  // `map`, `flatMap` and `all` unchanged
}

extend<Value, Failure: From<Cancelled>> Task<Result<Value, Failure>> {
  /** Waits, and folds a cancellation into the task's own failure: `source.next().outcome()?`. */
  fn outcome(): Result<Value, Failure>
}
```

**`await()`, `cancel()`, `within` and `outcome` are not in `std/task/src/lib.trb` yet**, and the reason is a migration
rather than a doubt: changing `await()`'s result from `Value` to `Result<Value, Cancelled>` was probed against the
whole repository and produces **80 problems in 8 files** — `std/stream` (`source.trb` 27, `sink.trb` 10, `bytes.trb` 2),
`std/http` 9, `std/fs` 1, `examples/tour/src/13-streams.trb` 14, `examples/tour/src/10-async.trb` 13 and
`examples/game-engine/src/main.trb` 4. `compiler/src` and `compiler/tests` are untouched, because the checker's own
tests declare a `Task` of their own. Every one of those is `.await()` becoming `.outcome()` or `.await()?`, plus the
`Source<Item, Never>` change of section 8 — one commit, and it belongs with the slice that writes `outcome`, not with a
document.

**`std/iteration`** gains `Merge` (section 5) and the implementations for the collectors that have one. `Merge` belongs
here rather than beside `parallel()` because it is synchronous, it says nothing about tasks, and it is useful on its
own: a divide-and-conquer fold and a map-reduce over pieces of a file both want it. Keeping it here also keeps the
dependency one-way, the same way `Stage` lives here and not in `std/stream`.

**`std/parallel`** is a package of its own:

```trb fragment
/** A pipeline whose stages run on several workers. Ordered: results arrive in input order. */
public trait Parallel<Item> {
  fn map<Output>(transform: (value: Item) => Output): Parallel<Output>
  fn filter(predicate: (value: Item) => Bool): Parallel<Item>
  fn filterMap<Output>(transform: (value: Item) => Output?): Parallel<Output>
  fn flatMap<Output>(transform: (value: Item) => Iterable<Output>): Parallel<Output>

  fn collect<Output>(collector: Merge<Item, Output>): Task<Output>
  fn toList(): Task<List<Item>>
  fn count(): Task<Int>
  fn sum(): Task<Item> where Item: Add & From<Int>
  fn minBy<Key: Compare>(key: (value: Item) => Key): Task<Item?>
  fn find(predicate: (value: Item) => Bool): Task<Item?>
  fn forEach(body: (value: Item) => Void): Task<Void>
}

extend<Item> Iterable<Item> {
  /** Spreads this pipeline over the workers. `chunk` fixes the number of pieces; the borders never depend on `workers`. */
  fn parallel(workers: Int = Workers.count(), chunk: Int? = None): Parallel<Item>
}

/** Splits the buffer into `count` disjoint windows and runs `body` on each at the same time. Returns when all are done. */
public fn windows<Item: Plain>(var items: Buffer<Item>, count: Int, body: (var window: Window<Item>) => Void)

/** A borrowed section of a buffer: a length and `[index]`, and no way to resize what it does not own. */
public shared type Window<Item> with Length, Indexed<Int, Item> {}
```

**The whole `Parallel` trait, the `Merge` trait and the `Iterable` extension type check today** as written, with bodies
that panic. `windows` type checks with a named `fn` as the body; with a closure it is gap 5.

**A package of its own, and in the prelude.** The two halves of that are not in tension, and the count is what settles
it: the prelude does not need sixteen names, it needs **two lines**.

```trb fragment
public use Parallel from "std/parallel"
public use Iterable.parallel from "std/parallel"
```

`map`, `filter`, `sum` and the rest are *members of `Parallel`*, so they cost nothing in a file scope, and `parallel`
itself is an extension member imported by its qualified name — the form the prelude already uses for
`public use Int64.seconds from "std/time"`. `windows`, `Window` and `Plain` stay imports, because a borrowed section of
a buffer is not something a program reaches for by accident.

**And `numbers.parallel().map { … }` should not need an import line.** A pipeline that spends the machine is the same
sentence as a pipeline that does not, one word longer, and the word is the whole documentation. Hiding it behind an
import is how `Parallel.For` and `AsParallel` became things people know about and do not use: the cost of finding out
that they exist is paid once per programmer, and a prelude pays it once per language. The package stays separate for
what a package is for — `std/task` is one thing waiting for another, `std/parallel` is one thing going faster, and
merging them is what makes a task library feel like a threading library — and the prelude decides what is in scope,
which is a different question with a different answer.

That is the one place this design spends a name to save a decision, and it is spent on the tool that is otherwise
underused.

**Natives.** `Workers.count`, `Workers.blocking`, `pause`, `offload`, `Task.cancel`, `Task.within`, the fork-join
barrier, `Window`'s two members, and the poller. Everything else — `Parallel` and all of its stages, the chunk
arithmetic, `Merge` and every collector's implementation of it, `Task.outcome`, `Cancelled`, `TimedOut`, the
borrow-or-copy decision as far as the standard library can see it — is TorbScript over those. Cancellation adds exactly
**two** natives to the list (`cancel` and `within`) and one bit to a structure that already exists, which is the
measure of how little a cooperative design costs when the machine is already a state machine.

## 11. Where this comes from

| System | Taken | Rejected, and why |
|---|---|---|
| **Go** (goroutines, `GOMAXPROCS`, work stealing) | A worker count set outside the program; stealing as the answer to skew | A stack per goroutine, and preemption that needs one. `GOMAXPROCS` as a *runtime* knob: a heap per worker makes the count a start-up fact |
| **Rust / tokio** (stackless futures, multi-thread runtime, `spawn_blocking`) | Stackless state machines; the blocking pool, as `offload`; one poller per worker | `Pin`, `Poll`, `Context`, `Waker` — the lowering builds the machine, so none of it is a library's problem. Work stealing of *started* tasks, which would move a heap |
| **Rust / rayon** (`par_iter`, join, work stealing) | Fork-join as the one primitive; the caller working as one of the workers | Stealing at every split point; the borrow here is bounded by `Plain` instead of by lifetimes |
| **C# / PLINQ** (`AsParallel`, `WithDegreeOfParallelism`, `AsOrdered`, `WithMergeOptions`, partitioners) | One vocabulary shared with the sequential pipeline; a degree of parallelism per operation | Four configuration verbs where two arguments do. `AsOrdered` as an opt-*in*: ordered is the default here. Custom partitioners: the borders are a function of the length, or determinism is gone. And PLINQ cannot say "this closure captures nothing mutable" — the `spawn` rule can |
| **Java** (virtual threads; parallel streams on a common pool) | `Collector` and its combiner as the shape of a reduction | A stack per virtual thread. **The common-pool problem**: parallel streams share one JVM-wide pool, so one library's blocking work starves another's. Here the pool is the program's, sized by the program, and blocking work has a pool of its own |
| **Erlang / BEAM** (per-process heaps, copied messages) | The memory model, almost exactly: a heap per scheduler, copying at the boundary, no shared mutable state, plain counts | A heap per *process* rather than per worker — millions of tiny heaps, and a migration story. Preemptive reduction counting, which needs a stack |
| **JavaScript** (event loop; workers; structured clone; transferables) | The event loop per worker; transfer instead of copy when the sender gives the value up, which is `Channel`'s rule | One loop and no cores by default; and the split between the loop's language and the worker's, which this language does not have — a task and a worker task are the same thing |
| **Swift** (structured concurrency, task groups, actors) | Structured parallelism: a region whose children cannot outlive it, which is what makes the borrow sound; cancellation as a request the task notices at its next `await`, and a parent that cancels its children | Actors, because confinement of a `shared type` to its task already gives what an actor gives, without a second calling convention. `Task.detached`, which would let a child outlive the region whose heap it borrowed |
| **Kotlin** (coroutines, dispatchers, `limitedParallelism`) | A bound per operation (`parallel(workers:)` is `limitedParallelism`) | Dispatchers as a value passed around: a task here belongs to the worker that made it, so there is nothing to choose. `suspend` as a function colour — `Task<Value>` is a return type, which is the whole point |

## 12. What the checker does not enforce

Four probes, all accepted by the checker as written, and all of them things the design says are errors. The first three
mean that **"data races are impossible by construction" rests on nothing today**; the fourth is what makes section 8's
migration look smaller than it is. They are listed first because a reader of CONCEPT would otherwise assume the
opposite.

**1. `await()` placement.**

```trb fragment
fn sumAll(tasks: List<Task<Int>>): Int {
  tasks.map({ _.await() }).sum()
}
```

The closure is not passed to `spawn` and the enclosing function does not answer a `Task`. `check` answers
`1 files, no problems`. This gap was already queued before this document; it is named here because every `Parallel`
closure depends on the same rule.

**2. A `spawn` closure may capture a `var`.**

```trb fragment
var total = 0
const first = spawn { total = total + 1 }
const second = spawn { total = total + 1 }
```

`check` answers `1 files, no problems`. This is the data race, written in four lines, and it is the rule the whole of
section 4 leans on.

**3. A `shared type` crosses into a `spawn` closure.**

```trb fragment
var counter = Counter()
const task = spawn {
  counter.bump()
  counter.count
}
```

`check` answers `1 files, no problems`, for a `shared type Counter` with a `var fn bump()`. CONCEPT says shared objects
are confined to the task that created them; nothing checks it. The layout already computes `containsShared` for this
purpose (BACKEND 1.3) — the fixpoint exists, the rule that reads it does not.

**4. `?` converts a failure into `Never` and into an unbounded type parameter.**

```trb fragment
fn intoNever(outcome: Result<Int, Alpha>): Result<Int, Never> {
  Ok(outcome?)
}

fn intoGeneric<Failure>(outcome: Result<Int, Alpha>): Result<Int, Failure> {
  Ok(outcome?)
}
```

`check` answers `1 files, no problems` for both, while the same `?` into a named second failure type is rejected with
`` `Alpha` does not convert into `Beta` `` and the suggestion to write the `From`. So the rule exists and it has two
holes: a bare type parameter, where the bound is what should carry the requirement, and `Never`, which has no values
and therefore cannot be the target of any conversion at all.

This is not a cost of cancellation and it is older than section 8, but section 8 is where it matters: with `await()`
answering a `Result<Value, Cancelled>`, `Failure: From<Cancelled>` is the bound that makes an asynchronous pipeline
type-correct, and today a signature that omits it type checks anyway. The migration measured in section 10 is therefore
a lower bound — **80 problems is what the checker finds, not what the design requires.** *Smallest fix:* the `?`
conversion asks the bound of a type parameter instead of accepting it, and refuses `Never` as a target.

## 13. What the language, the IR and the runtime must provide

In the order it hurts, each with the smallest fix.

**1. The `await()` placement rule.** Checker. Allowed in a function whose result is `Task<…>`, in a closure passed to
`spawn`, and at the top level of an entry file or script; an error everywhere else, naming the enclosing function's
result type. *Smallest fix:* one predicate over the enclosing declaration, consulted where `await` resolves.

**2. A `spawn` closure may not capture a `var`.** Checker. The capture analysis of the closure conversion already
classifies every capture; this is a rejection of one class at one call site. *Smallest fix:* mark `spawn`'s parameter
as a task boundary and refuse a captured `Box` there. The same mark serves `offload` and every `Parallel` closure.

**3. `shared type` confinement across a task boundary.** Checker, reading `containsShared` off the layout — which is
computed to a fixpoint already. `Task` and `Channel` are the two exceptions. Applies at `spawn`, at `offload`, at a
`Channel`'s item type and at a region's body. *Smallest fix:* one predicate, four call sites.

**4. `Plain`.** A marker trait the compiler grants for `containsCounted == false`, usable as a bound and never written
by a user. *Smallest fix:* a synthetic trait id the checker answers for from the layout, the way it answers derived
`Show`. **4b, later:** the widening — a borrowed element that is itself counted, read without a `Retain` — is a real
extension of the ownership pass (BACKEND 2.2's last-use analysis has to learn a borrowed operand class) and is not
promised by this document.

**5. A closure may bind a `var` parameter.** This is ECS gap 4, and it is on the critical path here: the body of
`windows` wants to be a closure that captures the frame time, and a named `fn` cannot capture. Today a single `var`
parameter is `error: A binding needs a value`, and two parameters with a `var` among them is a parse error. *Smallest
fix:* `var` in front of a closure parameter name, with the existing escape rule deciding whether the closure may keep
the reference.

**6. `Buffer<Item>` and `Window<Item>`.** ECS gap 9 names the buffer; the window is this document's addition — a
borrowed section with `Length` and `Indexed` and no resize. *Smallest fix:* the window is a `shared type` over a block
pointer, an offset and a length, with no `Close`, produced only by the region.

**7. The fork-join barrier.** `runtime/task.c` and `vm/task.trb`: run `n` bodies on the pool, return when all `n` are
done, the calling worker taking body 0. Both back ends, the same order. This is the one primitive under `parallel()`
and `windows`.

**8. IO in `runtime/io.c`.** One interface, IOCP on Windows, epoll on Linux, kqueue on the BSDs; a poller per worker;
a blocking pool of `blocking` threads behind it for what no platform makes asynchronous. The VM calls the same C.

**9. The deferred capture copy.** BACKEND 5.3's `spawn` copies the captures into the target heap; this document needs
it to place a *message* in an inbox and copy at first run, so that an unstarted task can be taken by an idle worker
(section 9). One change to `runtime/task.c` and the same to the VM.

**10. `tasks { workers, blocking }` in the project model**, `TORB_WORKERS`/`TORB_BLOCKING` at start-up, and `workers:`
in `SandboxCapabilities.limits` with a default of 1. Three places, one number.

**11. A generic member through a trait-typed value.** `Parallel.map<Output>` needs the same witness-table entry
`Stage.onto<Final>` and `Decoder.sequence<Output>` need, which the C back end does not have (STREAMS section 14,
point 6). It is not a cost of this design and it blocks `Parallel` from compiling until it exists.

**12. A profiler counter for the copy.** Section 6 says a chunk is borrowed or copied and that the difference is not
observable in the language. It has to be observable *somewhere*, or nobody can find the pipeline that copies. One
counter per run, printed by the profile that BACKEND 6.3's timing work introduces.

**13. The cancellation flag and the check at every suspension point.** One bit in the task structure, set by
`Task.cancel` and read by the worker before it resumes a task. Where it is set, the state machine does not resume: the
frame is released and the handle is completed with `Fail Cancelled`. *Smallest fix:* one field, one branch in the
resume path of `runtime/task.c` and of `vm/task.trb`, and the same branch reached from the four points of section 8 —
which are the only places a task is ever resumed, so it is one branch and not four.

**14. The parent link.** `spawn` records the spawning task's identity in the inbox message beside the captures (gap 9),
and the target worker links the child when it copies them in. A cancelled parent cancels its children; a child whose
parent is already cancelled at first run is dropped without running a line. *Smallest fix:* two integers in the message
and a child list per task — the list is the parent's own heap block, so it needs no atomic, because a task's children
are spawned by that task alone.

**15. Waking a task that waits for the world.** Section 7's table: remove the registration on a readiness poller,
`CancelIoEx`/`IORING_OP_ASYNC_CANCEL` on a completion port with the frame released at the completion, and a discard
flag on a blocking-pool job. *Smallest fix:* one cancel entry point per mechanism in `runtime/io.c`, behind the one
interface gap 8 introduces.

**16. `Cancelled`, `TimedOut`, `Task.await`'s result, `Task.cancel`, `Task.within` and `Task.outcome`** in
`std/task`, with the migration section 10 measures: `.await()` becomes `.outcome()` or `.await()?` at 80 measured
places in `std/stream`, `std/http`, `std/fs` and `examples/`, and `Channel`'s reading end becomes a
`Source<Item, Cancelled>`. *Smallest fix:* one commit over `std/`, gated by `check .` and `docs check docs`.

**17. `?` asks the bound.** Probe 4 of section 12: the `?` conversion accepts a bare type parameter and `Never` as a
target. Until it does not, `Failure: From<Cancelled>` is documentation rather than a rule. *Smallest fix:* the
conversion lookup consults the type parameter's bound, and `Never` is refused as a conversion target with the message
that it has no values.

## 14. Slices

**Before the VM — nothing in this document needs bytecode.** Gaps 1, 2, 3 and 17 are checker work on `compiler/src`,
they close the four probes of section 12, and they can land at any time. Gate: each probe becomes a `trb error` block
with its diagnostic, plus a checker test per rule. **Gap 17 comes first of the four**, because gap 16's migration is
only verifiable once `?` asks the bound.

**With 7.3 (one worker, one heap).** The state machine, `Task`, `spawn`, `await`, `Channel` and the FIFO queue are
7.3's own scope. Added here:

- **Slice A — `pause()`.** One state split in the lowering, one enqueue in the runtime and the VM. Gate: a script whose
  two tasks interleave in a fixed order, identical on stage 0, in C and in the VM.
- **Slice A2 — cancellation** (gaps 13, 14 and 16), and it comes before `parallel()` rather than after it. With one
  worker the flag, the check at each suspension point, the parent link and the whole `std/task` surface are all
  testable, and they pin the type of `await()` before anything else is written against it — the same de-risking
  argument slice C makes for the pipeline. Gate: a task cancelled at each of the four suspension points stops there
  and its waiter reads `Fail Cancelled`; a cancelled parent's child never runs a line; a loop with a `pause()` stops
  and a loop without one does not; `check .` and `docs check docs` are green after the 80-place migration; and the
  live-block counter is zero after every one of them. **The order matters:** every `await()` written before this slice
  has to be rewritten after it.
- **Slice B — `Merge` and the collectors.** Pure `std/iteration`, no runtime and no back end. Gate: a test per
  collector asserting `merge(a, b)` equals the sequential run over the concatenation, including the empty-chunk rule.
- **Slice C — `Parallel` and `parallel()`, running sequentially.** With one worker a region is a loop over the chunks
  in order, so the whole vocabulary, the chunk arithmetic and the terminal `Task` land here and are *testable* here.
  Gate: `parallel()` and the sequential pipeline agree on every collector, and a `Float` sum is byte-identical across
  the back ends. This is the slice that de-risks 7.7, because the semantics are pinned before the threads exist.
- **Slice D — gaps 10 and 12.** The manifest setting, the environment variables, the sandbox limit, `Workers.count()`
  answering 1. Gate: every `project.trb` of the repository still reads, and `TORB_WORKERS=1` is a no-op.

**With 7.7 (threads, per-worker heaps).**

- **Slice E — the fork-join barrier** (gap 7) and slice C's region turned on. Gate: `parallel()` gives the same answer
  at `workers: 1`, `workers: 2` and `workers: Workers.count()`, for every collector, on a machine with at least four
  cores; and the live-block counter is zero after each.
- **Slice F — `Plain`, `Window`, `windows`** (gaps 4, 5, 6). Gate: a data-parallel scale over a million `Float`s with
  zero copies (the counter of gap 12 at zero), zero live blocks, and the same output as the sequential loop.
- **Slice G — IO** (gap 8) **and the cancellation of a task that waits for the world** (gap 15). Gate: the conformance
  suite unchanged, plus a program that reads eight files at once and prints them in a fixed order, on all three
  platforms; and a cancelled read on each of the three mechanisms, with the live-block counter at zero afterwards —
  which is what proves the "not before" rows of section 7 rather than assuming them.
- **Slice H — the inbox and stealing** (gap 9), with the measurements of section 9. Gate: the skewed-cost benchmark
  within its stated factor, and the fraction of stolen tasks reported. The parent link of gap 14 travels in the same
  message, so this slice re-runs slice A2's parent tests with the inbox in place.

**`offload` and `Task.within`** ride with slice G: both need the blocking pool or a timer, and neither has anything to
say before real IO exists. `Task.within`'s *type* lands with slice A2, because `Cancelled` and `TimedOut` are what the
migration writes against; its timer is slice G's.

## 15. Open, for the owner

Everything technical above was decided and the reason is written next to it. These are the questions where the answer
is taste or direction. The eight that were asked have been answered; the answer is recorded under each, and the
document above carries it.

1. **`std/parallel` as a package of its own**, against putting `parallel()` in `std/task`. The argument for the split
   is in section 10: concurrency and throughput are two subjects, and the import is a statement about what a file
   spends. The cost is one more package in `std` and one more import line.
   **Decided:** a package of its own.
2. **`parallel()` in the prelude.** Sixteen names in every file, against one import at the point where a program
   decides to use the machine.
   **Decided:** in the prelude — "a cool and important tool for fast parallelism that C# users underestimate". The
   count in the objection was wrong and section 10 has the correction: the prelude costs **two lines**, because `map`,
   `filter` and the rest are members of `Parallel` and `parallel` is an extension member imported by its qualified
   name. `windows`, `Window` and `Plain` stay imports. The counterparts of `Parallel.For` and `Parallel.ForEach` are
   `(0..rows).parallel().forEach { … }` and `cells.parallel().forEach { … }`, both of which type check today over the
   one `extend<Item> Iterable<Item>`; there is no `parallelFor`, and section 4 argues why.
3. **The name `Plain`** for "nothing reference counted inside". The alternatives considered were `Inline` (which the IR
   already uses for a different property — it also caps the size) and `Uncounted` (accurate and ugly).
   **Decided:** `Plain`.
4. **`pause()` against `yield`.** The word is spent on a generator keyword the moment a function takes it, and CONCEPT
   keeps that question open. If generators are never going to use the word, `yield()` is the name everybody else uses.
   **Decided:** `pause()`. Section 9 gives it a second job under question 8: it is the cancellation point of a
   computation that has no other one.
5. **64 as the default chunk count.** It has to be a fixed number for the determinism of section 4; whether it is 64,
   32 or 256 is a measurement nobody has taken yet, and it is a number that cannot be changed later without changing
   every `Float` result a program recorded.
   **Decided:** 64, to be measured before 7.7 and fixed by that measurement.
6. **Whether `parallel()` should ever be unordered.** The answer here is no, argued from determinism. PLINQ, rayon and
   Java's parallel streams all default the other way, so it is worth one look.
   **Decided:** never unordered — "that is the point".
7. ~~**Whether the field case of ECS gap 8 is wanted at all**~~ — **answered by [ECS.md](ECS.md) section 6: it is
   wanted, and it is the smaller half.** A system's unit of work is a column group and not a window, because a system
   reads several columns and row *n* of one has nothing to do with row *n* of another, and a component that holds a
   handle, a string or a behaviour is not `Plain` and cannot be windowed at all. The proof is not missing — the
   checker already rejects two `var` arguments naming one path — so ECS gap 8 narrows to the barrier of gap 7 above,
   exposed as a fixed-arity call whose subjects are several `var` parameters, and the two parallelisms stay separate.
8. **Whether a `Task` should be cancellable in v1.**
   **Decided: every task is cancellable** — "then there are not two worlds again". Section 8 is that design, and it
   replaced the channel close as the answer rather than joining it: closing a channel stays the end of a stream, and
   stopping a task is `task.cancel()`.

### Open, from the answer to question 8

9. **`Never` leaves the asynchronous side.** Section 8: a failure type that cannot carry a `Cancelled` cannot be the
   failure of work that waits, so `Channel`'s reading end becomes a `Source<Item, Cancelled>` and `Source`/`Sink` carry
   `where Failure: From<Cancelled>`. That overturns a promise STREAMS makes in its own words — "the reading end cannot
   fail; a closed channel is the end of the stream, not a failure" — which stays true about *closing* and stops being
   true about *waiting*. The alternative is a rule that `?` on a `Cancelled` with no conversion available stops the
   waiting task instead of converting, which keeps `Never` and makes `?` mean two things; this document rejects it for
   the second reason and names it here because the first reason is real.
10. **The name `outcome`** for "wait, and fold a cancellation into the task's own failure". It is the one member the
    `Task<Result<Value, Failure>>` shape needs, `(task.await()?)?` is what it replaces, and a postfix `?` on an
    `await()` runs into an operator that starts with the same character twice over: `await()??` is a parse error
    because `??` is the fallback, and `await()?.into()` is `?.`, the optional chain. `outcome` was chosen over
    `awaited` (one letter from `await`, and the language spends that distance on the value/mutating pair) and over
    `value` (which says nothing about the failure).
11. **`cancel()` and `within` as `var fn`s**, so that the right to stop a task is visible at the binding
    (`var worker = spawn { … }`) and a `const` handle is the read-only view. The cost is that a `Task` somebody may
    cancel cannot be held in a `const`, including inside a collection somebody else reads.
