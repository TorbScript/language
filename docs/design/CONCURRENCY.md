# Concurrency and Parallelism

**Status: partly implemented** — the checker rules of section 13 (gaps 1, 2, 3 and 17) are in, and so is all of 7.3 for
the C back end: the task ABI, the single-worker scheduler, timers, channels and cancellation in C, and the lowering of
task functions, `spawn`, `await()`, the cancellation checks and the main task on top of it (section 16), with slices A
and A2 and the `std/task` surface of section 10. **The worker pool of 7.7 is built** (section 16, "The pool, as built"):
real threads, a heap and a scheduler per worker, the stealing of unstarted tasks, wake-ups, channels and cancellation
across workers, `Workers.count`, and `parallel()` in the prelude running its chunks on the pool - slices E and H, and
the environment half of D, the copy of a value that cannot move as it is ("The copy at the crossing"), and the blocking
pool with `offload` and `Workers.blocking` ("The blocking pool, as built"). **Slices B and C are built since
2026-09-26**: `Merge` and the collectors that have one, and `parallel()` on every `Iterate`, with the stages of a
pipeline fused into one pass per chunk and `Cut` for the collections that cut themselves (section 16, "`parallel()`").
The manifest setting and borrowing (`Plain`, `Window`, `windows`) are not built (section 14). **The IO poller is built for sockets** (slice G,
`docs/design/NETWORK.md` section 2): `runtime/io.c` with IOCP, epoll and kqueue, one IO thread of the process rather
than a poller per worker, for the reasons that record gives. **Files, the standard streams and child processes wait on
the blocking pool** (`runtime/stream.c`, STREAMS.md section 14): no platform's poller takes all three. **Since 2026-09-23 `await()` answers
the `Value` and passes a cancellation on to the waiter** (section 8, "The cascade"; section 15, decision 12), which
replaced `await(): Result<Value, Cancelled>` and deleted `outcome()`.

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
- **[15. What the owner decided](#15-what-the-owner-decided)**
- **[16. Runtime ABI, as built](#16-runtime-abi-as-built)** — what the compiler lowers a task to, and the pool of 7.7
- **[17. Open: a trait value that hides a shared object](#17-open-a-trait-value-that-hides-a-shared-object)** — shared-types rule 7 against trait rule 17

Every snippet below was run against the checker, in `tests/language/` so that `std` resolves, with the
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
  workers = 4
  blocking = 8
}
```

**Built** (gap 10): `Tasks` of `std/project`, read by the toolchain's static reader of the manifest, and handed to the
program by the driver - a native build compiles the numbers into the runtime (`TORB_MANIFEST_WORKERS`,
`TORB_MANIFEST_BLOCKING`), and `torb run` and `torb test` in the VM set them on the pool of `torb` before the
program's first task (the kernel operation `pool.defaults`). `workers` defaults to the core count, `blocking` to 4
(section 7). The numbers of the package the entry belongs to count; a number outside the range refuses the build or
the run with the path of the manifest. Neither is **static** in PROJECT.md's
sense: nothing an editor, a registry or `torb add` reads needs them, and they are about *this* build rather than about
the package, so they sit beside `profile` and `test` and stay out of the lock's `settings`. The accepted range is
`1..=1024`; `workers = 0` is an error that says to leave the line out instead, because a zero that silently means
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
capability list that says it has none, and the sandbox's own execution is a VM on one worker anyway. As built, the
number is in the grant (`workers`) and is an upper bound: the VM runs a script's tasks on the thread of its sandbox, so
a script uses one worker whatever it is granted.

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

The same words as `Iterate` and `Source`, and the terminal answers a `Task`:

```trb fragment
const total = numbers
  .parallel(workers: 4)
  .map({ expensive _ })
  .filter({ _ > 0 })
  .sum()
  .await()?
```

**type checks today** against a locally declared `Parallel<Item>` and an `extend<Item> Iterate<Item>` that carries
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

**type checks today**, the range and the list alike: `Range<Int>` is an `Iterate<Int>`, so the same
`extend<Item> Iterate<Item>` carries `parallel` on both and nothing has to be written twice.

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
requirement *on the accumulator*:

```trb fragment
/**
 * Two partial results of one accumulator, joined. An `Accumulator` that carries it can be used by `parallel()`, by a
 * divide-and-conquer fold, and by anything else that runs an accumulator over pieces of its input.
 */
public trait Merge<Item, Output> with Accumulator<Item, Output> {
  /** Associative: `merge(merge(a, b), c)` is `merge(a, merge(b, c))`. Never called with an empty chunk's output. */
  fn merge(first: Output, second: Output): Output
}
```

**Type checked** when it was written, as `Merge<Item, Output> with Collector<Item, Output>` with a concrete
implementation that delegated to `counting()` for `start()`. `Collector` has since been merged into `Accumulator`
(`docs/design/COLLECTIONS.md` 3.8a): an accumulator is a value, so each chunk runs on a copy of the one it was handed, and
`start()` is gone. The declaration above is the same trait restated over the merged vocabulary.

**The contract is three lines.**

1. **`merge` is associative.** It does not have to be commutative, because
2. **it is called in chunk order, left to right** — a fold over the chunk results, not a tree of arbitrary pairings.
3. **It is never called with the output of an empty chunk.** A chunk of zero items is never made, and an empty input
   produces zero chunks and takes the accumulator's own `finish()` on a fresh copy. This is what makes `joining(separator:)`
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

Two trait-typed `Accumulator`s need not have the same type, and there is no downcast to find out. Merging outputs has no such problem, and it has a second argument in its favour: the output is
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
| `into<Target>()` for a user's `Target` | whatever that type's `From<Iterate<Item>>` implies; the user writes it |
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

**It is a `var fn`**, because it changes the task - the same rule that makes `source.next()` a `var fn`. A `Task` is a
shared object, and a shared object changes through any binding that holds it, a `const` one included: there is no
read-only view of an object (CONCEPT, "Identity"), so `const worker = spawn { … }` can be cancelled, and the right to
cancel is holding the task.

### Where the flag is read

**The suspension points and the loop back-edges are the cancellation points.** The state machine already has every
suspension point, and the compiler adds one check per back-edge:

| Point | What it is |
|---|---|
| `await()` | on any task, which is also every `Source` and `Sink` verb |
| `pause()` | section 9 — the cooperative point a long loop puts in itself for fairness |
| a channel `add` or `next` | where an item crosses a heap boundary |
| an IO wait | the poller and the blocking pool, section 7 |
| a loop back-edge | in every function whose result is a `Task` and every closure passed to `spawn`: the compiler inserts a check of the flag where a `for`, `while` or `loop` turns around |

Before the worker resumes a task it reads the flag, and at a back-edge the running task reads it itself. If it is set,
the state machine **stops**: the frame is released exactly as a finished task's frame is released, every live value in
it goes with it, and the task ends cancelled - whoever `await()`s it is cancelled in turn ("The cascade" below), and
whoever observes it with `result()` reads `Fail(Cancelled)`.

**This is what "cancellation is drop" means** (`docs/design/DESTRUCTORS.md` section 7): cancelling a task releases its
*frame*, at its next suspension point or cancellation check, through the ordinary release. It does not mean that
dropping the `Task` *handle* cancels anything — it does not, and the subsection below says why.

**That is the whole implementation, and the reason it is that small is section 1.** A stackless task keeps every live
value *in its frame*, so there is no stack to unwind and nothing to leak, because releasing the frame is the ordinary
release. That release is also what runs the destructors: every value in the frame whose type has a `close()` is closed
by it, synchronously, slots in reverse declaration order as at the end of a scope (`docs/design/DESTRUCTORS.md` sections 2
and 7). A cancelled task's `end()` is never awaited, because there is no task left to resume once it would answer. A
green-threaded design would need an unwind here, which is the machinery this language spent section 2 avoiding.

**Every loop in a task is cancellable, with or without `pause()`.** The check at a back-edge is one load of the flag
and one branch, and the branch leads to the same stop a suspension point leads to — the state that the loop is in is
already a state of the machine, because a `Task` function is compiled to one. So `grind(1_000_000)` stops at its next
turn once somebody asked, and "every task is cancellable" holds for a loop that never awaits.

**A synchronous callee runs to its end.** A plain `fn` called from a task is not part of the state machine: it has no
state to stop in, and stopping it in the middle would need a stack to unwind, which a task does not have. A long
computation inside a synchronous function is therefore cancelled when it returns — at the caller's next back-edge or
suspension point — and a loop that has to be stoppable in the middle is written in a function that answers a `Task`.
That is the same trade section 9 makes about preemption rather than a second one. `pause()` is no longer what makes a
loop cancellable; it stays what makes it *fair*, because a check reads a flag and does not give the worker away.

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

### The cascade: `await()` passes a cancellation on

```trb fragment
/** Waits for the value. Where the task ended cancelled, the waiter is cancelled too, right here. */
fn await(): Value

/** Waits the same way, and answers a cancellation instead of passing it on. */
fn result(): Result<Value, Cancelled>

/** A task that stopped because somebody asked it to, as `result()` answers it. */
public type Cancelled with Show, Error {
  /** `"the task was cancelled"`. */
  fn show(): String {
    "the task was cancelled"
  }
}
```

**`await()` answers the `Value`, and a cancellation is propagated, not returned.** A task that awaits a task that ended
cancelled is itself cancelled at that suspension point: it stops there, its scopes end in reverse order - every `using`
is closed - exactly as when it is cancelled at a back-edge, and it ends cancelled, so whatever awaits *it* is cancelled
in turn. It is a cascade up the chain of waiters, and it stops at the first task that observes instead of awaiting.
The top level of an entry file that awaits a cancelled task ends the program: the main task stops like any other, the
program writes `cancelled: the program waited for a task that was cancelled` to standard error, the other tasks are
stopped as at every end of a program, and the exit code is **130** - 128 plus `SIGINT`, what a shell reports for a
program somebody stopped, and never the 101 of a panic, because a cancellation is a request that was honoured and not
a bug.

**It is one mechanism and not a second one.** The runtime sets the waiter's own cancellation flag where the awaited task
ends cancelled - at once where it had already ended, and when it wakes the waiter otherwise - and the check the machine
makes after every wait then takes the stop path a `cancel()` would have taken (section 16, "The cascade in the
runtime"). The children of the stopped waiter are cancelled with it, as with every task that stops.

**`result()` is the one way to observe a cancellation**, for the code that has to know: a supervisor that restarts
what was cancelled, a test, the task that called `cancel()` and wants to confirm it. It is the same wait with the
cascade left out, and it answers `Ok(value)` or `Fail(Cancelled)`. `Task.all` and `both` are written over it, because
they have to cancel the parts that are still running before they end cancelled themselves.

**`Cancelled` carries no reason**, and that is a decision. A reason field would be an open set nobody can match
exhaustively, and the one reason with a name of its own — a deadline — belongs to the function that made the deadline,
where it is `TimedOut` (below). What a bystander can honestly learn is that the value will not arrive; who asked for
that and why is the canceller's knowledge, not the waiter's.

**A waiter that is cancelled itself has no answer at all**, which was true before and is the reason the cascade is
sound: a waiter never has a use for the value of a task that will not deliver one, because the only thing it could do
with the knowledge is to stop - and stopping is what the cascade does for it, with the `using`s closed on the way out.

### What the cascade replaced, and why

From 2026-09-22 to 2026-09-23 `await()` answered `Result<Value, Cancelled>`, and a member `outcome()` folded the
cancellation into the task's own failure for a `Task<Result<Value, Failure>>`, whose `Failure` had to convert from
`Cancelled` - so every `Source` and `Sink` carried `Failure: From<Cancelled>`, `IoError`, `HttpError` and
`ChannelClosed` converted from it, and `Channel`'s reading end was a `Source<Item, Cancelled>`. **Every IO line nested
two `Result`s** - the cancellation around the failure of the work - and the member that folded them existed only because
`(task.await()?)?` read badly, `await()??` lexed as the fallback and `await()?.into()` as the optional chain. That was
the symptom; section 15, decision 12 has the reversal and its reasons. `outcome()` is deleted, so are the
`From<Cancelled>` implementations that existed for it, and `Source`/`Sink` have no bound on `Failure` any more.

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
| `Ok(value)` | the task finished inside the limit |
| `Fail(TimedOut)` | the limit passed first, and `within` cancelled the task |
| the waiter is cancelled | somebody else cancelled the task, or the waiter's own `within` task was cancelled: the cascade |

`TimedOut` stays an answer and not a cascade, because a deadline is something the caller asked for and has a use for.

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
with a hole in it is not a `List<Value>`. So it cancels the others, ends cancelled itself - and whoever awaits it with
it - and stops paying for results nobody can use.

Determinism is not spent on this: the answer is the whole list or a cancellation, and how far each of the others got
before it stopped is not observable in the language — the same argument section 4 makes about the chunk that `find`
never starts.

```trb fragment
const loaded: Result<List<User>, HttpError> = Task.all(fetches).await().into()
```

`extend<Value, Failure, Target: From<Iterate<Value>>> Result<Target, Failure> with From<Iterate<Result<Value,
Failure>>>` is in `std/core/src/result.trb` today. No second API, and the shape is honest about the fact that every
task that was not cancelled ran to its end.

*(Until 2026-09-23 this line needed parentheses, `(Task.all(fetches).await()?).into()`, because `await()?.into()`
lexes the `?.` as the optional chain. With `await()` answering the value there is no `?` to write.)*

### Dropping a `Task` still does not cancel it

A `spawn` starts the work immediately, so a `Task` is a handle to something already running; dropping the handle means
nobody will read the result. **The handle is not the frame**: the frame belongs to the scheduler until the task
finishes or is cancelled, so releasing the last handle releases nothing the task holds and runs no `close()` of
anything inside it. "Cancellation is drop" is about the frame (above), never about the handle.

**The argument is stronger with `cancel()` than without it.** The one thing that made drop-cancels tempting is that
there was no other way to stop work; there is one now, it is a method with a name, and it is written where the decision
is made rather than falling out of a scope's end. And the case drop-cancel is actually reached for — a child that
should die with the work that started it — is the parent link above, which stops the child whether or not anybody still
holds its handle. tokio's "drop the future and it is gone" is the mirror image of this and it is rejected for a reason,
not for a preference: a release here runs exactly one piece of user code, the `close()` of the value being released
(`docs/design/DESTRUCTORS.md`), and making the release of a *handle* stop a whole other computation would turn the lifetime of
every handle into a control-flow decision nobody wrote down.

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
| **Kotlin** (`Job`, `CancellationException`) | Structured: a child of a cancelled job is cancelled; a coroutine that awaits a cancelled one is cancelled too | An exception that propagates invisibly and that a `catch (e: Exception)` swallows by accident. Here the propagation is not an exception: the waiter stops at its `await()` through the stop path, and nothing can catch it; `result()` is the one place that observes it |
| **Swift** (structured cancellation, `Task.detached`) | Cancellation as a request the task notices at `await`; parent cancels children; a region cancelled as a whole | `Task.detached`, because the borrow of section 6 needs "no child outlives its region" to be true without exception |
| **Rust / tokio** (drop the future) | Nothing | Cancellation at a moment nobody wrote down: the work stops wherever the last handle happens to go. Here releasing a handle stops nothing, and a cancelled frame is released at a suspension point or a back-edge check |
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

**`pause()` is not what makes a loop stoppable any more** (section 8): every loop back-edge of a function that answers
a `Task` is a cancellation check the compiler inserts, so a loop that does nothing but arithmetic learns that somebody
asked it to stop at its next turn, with or without a `pause()`. The `?` on `pause().await()` still answers `Cancelled`
when the flag is set, and `pause()` stays the answer to "how do I let the other tasks of this worker run", which a check
does not do.

**There is no preemption.** A worker cannot interrupt a task that does not suspend, because interrupting one needs a
stack to unwind and a task has none. This is the same trade Rust and JavaScript make and the opposite of Go's and
BEAM's, and it is decided by the stackless choice of section 1 rather than being a separate decision.

## 10. What the standard library looks like

**`std/task`** keeps what it has — `Task`, `spawn`, `both`, `Channel`, `ChannelClosed` — and gains this:

```trb fragment
/** How many workers this process runs, and how many threads its blocking pool has. Both fixed at start. */
public type Workers {
  native static fn count(): Int
  native static fn blocking(): Int
}

/** Puts this task at the back of its worker's queue, lets the rest run, and notices a cancellation. */
public native fn pause(): Task<Void>

/** Runs the body on the blocking pool. It gets copies, cannot reach a shared object, and cannot `await`. */
public fn offload<Value>(body: () => Value): Task<Value>

/** A task that stopped at a suspension point because somebody asked it to. */
public type Cancelled with Show, Error {}

/** A deadline that passed. */
public type TimedOut with Show, Error {
  limit: Duration
}

public native shared type Task<Value> {
  /** Waits for the value. A cancellation is passed on to the waiter instead of answered. */
  fn await(): Value

  /** Waits, and answers a cancellation as `Fail(Cancelled)` instead of passing it on. */
  fn result(): Result<Value, Cancelled>

  /** Asks the task to stop at its next suspension point. A request and not a kill. */
  native var fn cancel()

  /** Cancels the task once the limit has passed. A task that finished first is unaffected. */
  native var fn within(limit: Duration): Task<Result<Value, TimedOut>>

  // `map`, `flatMap` and `Task.all` unchanged
}
```

**`await()`, `result()`, `cancel()`, `within`, `pause`, `Cancelled` and `TimedOut` are in `std/task/src/lib.trb`.**
*(The paragraph below is the migration of 2026-09-22 and describes `outcome()`, which the cascade of 2026-09-23 deleted
again: every `.outcome()` became `.await()`, every `.await() ?? fallback` and `.await()?` of a value that is no
`Result` lost its fallback, `Source` and `Sink` lost their bound, and `Channel`'s reading end became a
`Source<Item, Never>` - section 8, "What the cascade replaced, and why".)*
Changing `await()`'s result from `Value` to `Result<Value, Cancelled>` produced **97 problems in 8 files** of the
repository - `std/stream` (`source.trb` 41, `sink.trb` 13, `bytes.trb` 2), `std/http` 9, `std/fs` 1,
`examples/tour/src/13-streams.trb` 14, `examples/tour/src/10-async.trb` 13 and `examples/game-engine/src/main.trb` 4 -
and the migration rewrote **121 `.await()` calls**: 69 in `std/` (42 in `std/stream`, 13 in `std/http`, 11 in `std/fs`,
`std/io` and `std/process`, 3 in `std/task` itself), 27 in the two tour files and the game engine, and 25 in the pages of
`docs/` - `.outcome()` wherever the value is itself a `Result`, `.await()?` or `.await() ?? fallback` elsewhere. `Source`
and `Sink` carry `Failure: From<Cancelled>` on the trait, so `IoError`, `HttpError`, `ChannelClosed` and `Cancelled`
itself convert from `Cancelled`, and `Channel`'s reading end is a `Source<Item, Cancelled>`. `outcome()` is a
suspension point exactly like `await()`: the lowering puts the wait in front of the call of its TorbScript body, and
`await()` is allowed wherever `outcome()` is.

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
  fn flatMap<Output>(transform: (value: Item) => Iterate<Output>): Parallel<Output>

  fn collect<Output>(collector: Merge<Item, Output>): Task<Output>
  fn toList(): Task<List<Item>>
  fn count(): Task<Int>
  fn sum(): Task<Item> where Item: Add & From<Int>
  fn minBy<Key: Compare>(key: (value: Item) => Key): Task<Item?>
  fn find(predicate: (value: Item) => Bool): Task<Item?>
  fn forEach(body: (value: Item) => Void): Task<Void>
}

extend<Item> Iterate<Item> {
  /** Spreads this pipeline over the workers. `chunk` fixes the number of pieces; the borders never depend on `workers`. */
  fn parallel(workers: Int = Workers.count(), chunk: Int? = None): Parallel<Item>
}

/** Splits the buffer into `count` disjoint windows and runs `body` on each at the same time. Returns when all are done. */
public fn windows<Item: Plain>(var items: Buffer<Item>, count: Int, body: (var window: Window<Item>) => Void)

/** A borrowed section of a buffer: a length and `[index]`, and no way to resize what it does not own. */
public shared type Window<Item> with Length, Indexed<Int, Item> {}
```

**The whole `Parallel` trait, the `Merge` trait and the `Iterate` extension type check today** as written, with bodies
that panic. `windows` type checks with a named `fn` as the body; with a closure it is gap 5.

**A package of its own, and in the prelude.** The two halves of that are not in tension, and the count is what settles
it: the prelude does not need sixteen names, it needs **two lines**.

```trb fragment
public use Parallel from "std/parallel"
public use Iterate.parallel from "std/parallel"
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
arithmetic, `Merge` and every collector's implementation of it, `Task.await`, `Task.result`, `Cancelled`, `TimedOut`, the
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

Four probes that were accepted by the checker as written, and all of them things the design says are errors. The first
three meant that **"data races are impossible by construction" rested on nothing**; the fourth is what made section 8's
migration look smaller than it is.

**All four are rules of the checker now** (section 13's gaps 1, 2, 3 and 17). The probes are kept because they say what
each rule is for, and every one of them is a test of `compiler/tests`. What each answers today stands under it.

**1. `await()` placement.**

```trb fragment
fn sumAll(tasks: List<Task<Int>>): Int {
  tasks.map({ _.await() }).sum()
}
```

The closure is not passed to `spawn` and the enclosing function does not answer a `Task`. It is now
```await()` is only allowed in a closure that becomes a task``, with a note naming `Task.all` and `task.map`. A
closure that *is* a task says so: the one `spawn` is handed, and one whose declared result is a `Task` — which is how
`Source.produce` now declares its body, and it is what every `Parallel` closure will declare as well.

**2. A `spawn` closure may capture a `var`.**

```trb fragment
var total = 0
const first = spawn { total = total + 1 }
const second = spawn { total = total + 1 }
```

This is the data race, written in four lines, and it is the rule the whole of section 4 leans on. Both lines are now
```spawn` cannot take the `var` binding `total` with it``. A top-level `var` is not a capture at all — it is one place
the whole file shares — so the rule asks the declarations the closure read as well as the bindings it captured.

**3. A `shared type` crosses into a `spawn` closure.**

```trb fragment
var counter = Counter()
const task = spawn {
  counter.bump()
  counter.count
}
```

It is now ```spawn` cannot take `counter` with it: a `Counter` has an identity``, and a value that holds an object
anywhere inside it says `holds an object` instead. The checker computes the same fixpoint the layout does
(BACKEND 1.3), with `Task` and `Channel` as the two exceptions. A `Channel`’s item is checked where `Channel<Item>` is
written — a signature, an annotation, a construction — rather than at `sink().add`, so it is one message per item type
instead of one per call.

**4. `?` converts a failure into `Never` and into an unbounded type parameter.**

```trb fragment
fn intoNever(outcome: Result<Int, Alpha>): Result<Int, Never> {
  Ok(outcome?)
}

fn intoGeneric<Failure>(outcome: Result<Int, Alpha>): Result<Int, Failure> {
  Ok(outcome?)
}
```

Both were accepted, while the same `?` into a named second failure type was rejected with
`` `Alpha` does not convert into `Beta` ``. The two holes are closed: a bare type parameter is asked for its bound
(```Alpha` does not convert into `Failure```, with `where Failure: From<Alpha>` as the note), and `Never` is refused
as a target because it has no values at all.

This is not a cost of cancellation and it is older than section 8, but section 8 is where it mattered: while `await()`
answered a `Result<Value, Cancelled>` (until 2026-09-23), `Failure: From<Cancelled>` was the bound that made an
asynchronous pipeline type-correct, and a signature that omitted it used to type check anyway. The migration measured in section 10 is
therefore a lower bound — **80 problems is what the checker found then, not what the design requires.**

## 13. What the language, the IR and the runtime must provide

In the order it hurts, each with the smallest fix.

**1. The `await()` placement rule. Done.** Allowed in a function whose result is `Task<…>`, in a closure passed to
`spawn`, in a closure whose declared result is a `Task<…>`, and at the top level of an entry file or script; an error
everywhere else, naming the enclosing function's result type. The third form is what a library declares when it runs
a closure as a task: `Source.produce` takes a `(var sink: Sink<Item, Failure>) => Task<Result<Void, Failure>>`, and
`Parallel` will say the same.

**2. A `spawn` closure may not capture a `var`. Done.** The capture analysis of the closure conversion classifies
every capture, and a captured `var` is refused at `spawn`. A top-level `var` the body reads is refused with it: it is
no capture at all, but one place the whole file shares, which is the same race. The rule serves `offload` and every
`Parallel` closure once those exist.

**3. `shared type` confinement across a task boundary. Done for `spawn` and `Channel`.** The checker computes
`containsShared` itself, to the same fixpoint the layout does, with `Task` and `Channel` as the two exceptions. It is
asked of every capture of a `spawn` closure, and of the item of a `Channel<Item>` wherever that type is written — a
signature, an annotation, a construction — rather than at `sink().add`, which would be a message per call instead of
one per item type. `offload` and a region's body are the same predicate at two more call sites.

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
`Stage.onto<Final>` and `Decoder.sequence<Output>` need, which the C back end did not have (STREAMS section 14,
point 6). *Built: a member with type parameters of its own gets one slot per list of arguments the program calls it
with, in every table of its trait (`ir/witness.trb`, `genericSlotOf`), and the fused `Parallel` of section 16 is
written on it - its private `Plan<Item>` has `mapped<Output>` and `run<Result>`.*

**12. A profiler counter for the copy.** Section 6 says a chunk is borrowed or copied and that the difference is not
observable in the language. It has to be observable *somewhere*, or nobody can find the pipeline that copies. One
counter per run, printed by the profile that BACKEND 6.3's timing work introduces.

**13. The cancellation flag, the check at every suspension point, and the check at every back-edge.** One bit in the
task structure, set by `Task.cancel` and read by the worker before it resumes a task. Where it is set, the state
machine does not resume: the frame is released and the task ends cancelled. *Smallest fix:* one
field, one branch in the resume path of `runtime/task.c` and of `vm/task.trb`, and the same branch reached from the four
suspension points of section 8 — which are the only places a task is ever resumed, so it is one branch and not four.
Beside it, the lowering of a function whose result is a `Task` (and of a `spawn` closure) inserts a flag check at every
loop back-edge that branches to the same stop; a synchronous function gets none.

**14. The parent link.** `spawn` records the spawning task's identity in the inbox message beside the captures (gap 9),
and the target worker links the child when it copies them in. A cancelled parent cancels its children; a child whose
parent is already cancelled at first run is dropped without running a line. *Smallest fix:* two integers in the message
and a child list per task — the list is the parent's own heap block, so it needs no atomic, because a task's children
are spawned by that task alone.

**15. Waking a task that waits for the world.** Section 7's table: remove the registration on a readiness poller,
`CancelIoEx`/`IORING_OP_ASYNC_CANCEL` on a completion port with the frame released at the completion, and a discard
flag on a blocking-pool job. *Smallest fix:* one cancel entry point per mechanism in `runtime/io.c`, behind the one
interface gap 8 introduces.

**16. `Cancelled`, `TimedOut`, `Task.await`, `Task.result`, `Task.cancel` and `Task.within`. Done**, with the
migration of section 10 and its reversal of 2026-09-23: `await()` answers the `Value` and passes a cancellation on
(the cascade of section 8), `result()` answers `Result<Value, Cancelled>`, and `outcome()` is gone. The checker's
placement rule finds both waits by name (`await`, `result`) on a receiver that is a `Task`, and so does the lowering,
which marks the `Suspend` of a `result()` as observing.

**17. `?` asks the bound. Done.** Probe 4 of section 12: the `?` conversion consults a type parameter's bound instead
of accepting it, and refuses `Never` as a conversion target because it has no values at all.
`Failure: From<Cancelled>` was a rule and not documentation until the cascade made the bound unnecessary.

## 14. Slices

**Before the VM — nothing in this document needs bytecode.** Gaps 1, 2, 3 and 17 are checker work on `compiler/src`,
they close the four probes of section 12, and they can land at any time. Gate: each probe becomes a `trb error` block
with its diagnostic, plus a checker test per rule. **Gap 17 comes first of the four**, because gap 16's migration is
only verifiable once `?` asks the bound.

*(Runtime half built, 2026-09-22: `runtime/task.c` runs everything of slices A and A2 that is not the lowering - the
flag, three of the four suspension points (the IO wait is slice G's), the parent link, `pause`, and `within` with a
real timer rather than only its type - driven by hand-written state machines in `runtime/tests/task_test.c`. Section
16 is the contract the lowering is written against.)*

**With 7.3 (one worker, one heap).** The state machine, `Task`, `spawn`, `await`, `Channel` and the FIFO queue are
7.3's own scope. Added here:

- **Slice A — `pause()`.** One state split in the lowering, one enqueue in the runtime and the VM. Gate: a script whose
  two tasks interleave in a fixed order, identical in C and in the VM.
- **Slice A2 — cancellation** (gaps 13, 14 and 16), and it comes before `parallel()` rather than after it. With one
  worker the flag, the check at each suspension point, the parent link and the whole `std/task` surface are all
  testable, and they pin the type of `await()` before anything else is written against it — the same de-risking
  argument slice C makes for the pipeline. Gate: a task cancelled at each of the four suspension points stops there
  and its observer reads `Fail Cancelled`; a cancelled parent's child never runs a line; a loop in a `Task` function
  stops at its next back-edge with and without a `pause()`, and a loop inside a synchronous callee finishes before the
  task stops; `check .` and `docs check docs` are green after the 80-place migration; and the
  live-block counter is zero after every one of them. **The order matters:** every `await()` written before this slice
  has to be rewritten after it.
- **Slice B — `Merge` and the collectors.** Pure `std/iteration`, no runtime and no back end. Gate: a test per
  collector asserting `merge(a, b)` equals the sequential run over the concatenation, including the empty-chunk rule.
  *Built (2026-09-26): `Merge` and `mergingCollector` in `std/iteration`, a merge for `listing`, `counting`,
  `summing`, `minBy`, `maxBy`, `joining`, `partitioningBy` and `groupingBy`, and the gate as
  `std/iteration/tests/merge.test.trb` - every border of an input, and three pieces in both groupings. The rule is
  read one step wider than section 5 writes it: a chunk is never empty, but the stages before `collect` can leave one
  without an item, and that chunk is skipped as well, or `joining(prefix:, suffix:)` would put a separator next to
  nothing. `into<Target>()` and `then(downstream)` have no merge: the target is anybody's type and the downstream any
  `Accumulator`.*
- **Slice C — `Parallel` and `parallel()`, running sequentially.** With one worker a region is a loop over the chunks
  in order, so the whole vocabulary, the chunk arithmetic and the terminal `Task` land here and are *testable* here.
  Gate: `parallel()` and the sequential pipeline agree on every collector, and a `Float` sum is byte-identical across
  the back ends. This is the slice that de-risks 7.7, because the semantics are pinned before the threads exist.
  *Built together with slice E, on the threads directly (`std/parallel`, section 16): `map`, `filter`, `filterMap`,
  `toList`, `collect`, `count`, `sum`, `minBy`, `maxBy`, `find`, `forEach`, the chunk arithmetic of section 4 and
  the terminal `Task`, with `parallel` an extension of every `Iterate` in the prelude (since 2026-09-26; before, of
  `List` only). `flatMap` is not built: a stage that turns one item into many is no step of the fused pipeline, and
  nothing has asked for it.*
- **Slice D — gaps 10 and 12.** The manifest setting, the environment variables, the sandbox limit, `Workers.count()`
  answering 1. Gate: every `project.trb` of the repository still reads, and `TORB_WORKERS=1` is a no-op.
  *Built: `TORB_WORKERS` and `TORB_BLOCKING` (each refused with exit code 2 where it is not a whole number from 1
  to 1024), `Workers.count()` and `Workers.blocking()`, answering the real counts, the copy counter of gap 12
  (`torb_pool_statistics.copied`, which nothing prints yet), the manifest setting `tasks { workers, blocking }`
  (section 3) and `workers:` of the sandbox's `limits`, which the VM honours by running a script's tasks on one
  thread.*

**With 7.7 (threads, per-worker heaps).**

- **Slice E — the fork-join barrier** (gap 7) and slice C's region turned on. Gate: `parallel()` gives the same answer
  at `workers: 1`, `workers: 2` and `workers: Workers.count()`, for every collector, on a machine with at least four
  cores; and the live-block counter is zero after each.
  *Built: every stage of a pipeline is a fork-join of chunk tasks an idle worker may take, at most `workers` of them
  running at once, read back in input order (`tests/conformance/parallel-ordered.trb` and `parallel-sources.trb`, run
  with four workers and with one, and `benchmarks/parallel.sh`). The stages of one pipeline are fused into one pass
  (section 16).*
- **Slice F — `Plain`, `Window`, `windows`** (gaps 4, 5, 6). Gate: a data-parallel scale over a million `Float`s with
  zero copies (the counter of gap 12 at zero), zero live blocks, and the same output as the sequential loop.
  *Not built.*
- **Slice G — IO** (gap 8) **and the cancellation of a task that waits for the world** (gap 15). Gate: the conformance
  suite unchanged, plus a program that reads eight files at once and prints them in a fixed order, on all three
  platforms; and a cancelled read on each of the three mechanisms, with the live-block counter at zero afterwards —
  which is what proves the "not before" rows of section 7 rather than assuming them.
  *The blocking pool is built, with `offload` over it (section 16, "The blocking pool, as built"). `runtime/io.c` and
  the poller are built for sockets and name resolution, with the cancellation of a wait on each mechanism
  (`docs/design/NETWORK.md` sections 2 and 3, `runtime/tests/io_test.c`): one IO thread instead of a poller per worker,
  and the kernel's buffer owned by the operation instead of the frame, so a cancelled wait frees its frame at once.
  Files, the standard streams and pipes are built on the blocking pool instead (`runtime/stream.c`, STREAMS.md section
  14); the eight-file gate is not written.*
- **Slice H — the inbox and stealing** (gap 9), with the measurements of section 9. Gate: the skewed-cost benchmark
  within its stated factor, and the fraction of stolen tasks reported. The parent link of gap 14 travels in the same
  message, so this slice re-runs slice A2's parent tests with the inbox in place.
  *Built, with the run queue of each worker as its inbox (section 16): an unstarted task that may move is taken by an
  idle worker, the parent link stays where it is, and `runtime/tests/pool_test.c` cancels parents and children across
  workers. The skewed-cost benchmark and the three measurements of section 9 are not written yet; the pool counts the
  resumes and the thefts (`torb_pool_statistics_now`).*

**`offload` and `Task.within`** ride with slice G: both need the blocking pool or a timer. *(Both are built: `within`
over the timers of the pool, `offload` over the blocking pool.)* `Task.within`'s *type* lands with slice A2, because `Cancelled` and `TimedOut` are what the
migration writes against; its timer is slice G's.

## 15. What the owner decided

Everything technical above was decided and the reason is written next to it. These eight were questions of taste or
direction, and the owner answered them on 2026-09-22; the answer is recorded under each, and the document above
carries it.

1. **`std/parallel` as a package of its own**, against putting `parallel()` in `std/task`. The argument for the split
   is in section 10: concurrency and throughput are two subjects, and the import is a statement about what a file
   spends. The cost is one more package in `std` and one more import line.
   **Decided (2026-09-22):** a package of its own.
2. **`parallel()` in the prelude.** Sixteen names in every file, against one import at the point where a program
   decides to use the machine.
   **Decided (2026-09-22):** in the prelude — "a cool and important tool for fast parallelism that C# users underestimate". The
   count in the objection was wrong and section 10 has the correction: the prelude costs **two lines**, because `map`,
   `filter` and the rest are members of `Parallel` and `parallel` is an extension member imported by its qualified
   name. `windows`, `Window` and `Plain` stay imports. The counterparts of `Parallel.For` and `Parallel.ForEach` are
   `(0..rows).parallel().forEach { … }` and `cells.parallel().forEach { … }`, both of which type check today over the
   one `extend<Item> Iterate<Item>`; there is no `parallelFor`, and section 4 argues why.
3. **The name `Plain`** for "nothing reference counted inside". The alternatives considered were `Inline` (which the IR
   already uses for a different property — it also caps the size) and `Uncounted` (accurate and ugly).
   **Decided (2026-09-22):** `Plain`.
4. **`pause()` against `yield`.** The word is spent on a generator keyword the moment a function takes it, and CONCEPT
   keeps that question open. If generators are never going to use the word, `yield()` is the name everybody else uses.
   *(Note, 2026-09-22: the back-edge check of section 8 took the second job over - every loop of a `Task` function is
   cancellable without `pause()`, which is now for fairness only.)*
   **Decided (2026-09-22):** `pause()`. Section 9 gives it a second job under question 8: it is the cancellation point of a
   computation that has no other one.
5. **64 as the default chunk count.** It has to be a fixed number for the determinism of section 4; whether it is 64,
   32 or 256 is a measurement nobody has taken yet, and it is a number that cannot be changed later without changing
   every `Float` result a program recorded.
   **Decided (2026-09-22):** 64, to be measured before 7.7 and fixed by that measurement.
6. **Whether `parallel()` should ever be unordered.** The answer here is no, argued from determinism. PLINQ, rayon and
   Java's parallel streams all default the other way, so it is worth one look.
   **Decided (2026-09-22):** never unordered — "that is the point".
7. ~~**Whether the field case of ECS gap 8 is wanted at all**~~ — **answered by [ECS.md](ECS.md) section 6: it is
   wanted, and it is the smaller half.** A system's unit of work is a column group and not a window, because a system
   reads several columns and row *n* of one has nothing to do with row *n* of another, and a component that holds a
   handle, a string or a behaviour is not `Plain` and cannot be windowed at all. The proof is not missing — the
   checker already rejects two `var` arguments naming one path — so ECS gap 8 narrows to the barrier of gap 7 above,
   exposed as a fixed-arity call whose subjects are several `var` parameters, and the two parallelisms stay separate.
8. **Whether a `Task` should be cancellable in v1.**
   **Decided (2026-09-22): every task is cancellable** — "then there are not two worlds again". Section 8 is that design, and it
   replaced the channel close as the answer rather than joining it: closing a channel stays the end of a stream, and
   stopping a task is `task.cancel()`.

### Decided, from the answer to question 8

9. ~~**`Never` leaves the asynchronous side.**~~ *(Reversed with decision 12: a cancellation is no failure value any
   more, so `Channel`'s reading end is a `Source<Item, Never>` again and `Source`/`Sink` carry no bound.)* Section 8: a
   failure type that cannot carry a `Cancelled` cannot be the
   failure of work that waits, so `Channel`'s reading end becomes a `Source<Item, Cancelled>` and `Source`/`Sink` carry
   `where Failure: From<Cancelled>`. That overturns a promise STREAMS makes in its own words — "the reading end cannot
   fail; a closed channel is the end of the stream, not a failure" — which stays true about *closing* and stops being
   true about *waiting*. The alternative is a rule that `?` on a `Cancelled` with no conversion available stops the
   waiting task instead of converting, which keeps `Never` and makes `?` mean two things; this document rejects it for
   the second reason and names it here because the first reason is real.
   **Decided (2026-09-22):** yes — `Never` leaves the asynchronous side. `Cancelled` is the floor of every asynchronous failure:
   one `?`, one world, the same place Swift's `async throws` lands once it is everywhere.
10. ~~**The name `outcome`**~~ *(superseded by decision 12: `outcome()` is deleted)* for "wait, and fold a
    cancellation into the task's own failure". It is the one member the
    `Task<Result<Value, Failure>>` shape needs, `(task.await()?)?` is what it replaces, and a postfix `?` on an
    `await()` runs into an operator that starts with the same character twice over: `await()??` is a parse error
    because `??` is the fallback, and `await()?.into()` is `?.`, the optional chain. `outcome` was chosen over
    `awaited` (one letter from `await`, and the language spends that distance on the value/mutating pair) and over
    `value` (which says nothing about the failure).
    **Decided (2026-09-22):** `outcome()` stays.
11. **`cancel()` and `within` as `var fn`s**, because both change the task. The `var` says what the method does and
    not who may call it: a shared object changes through any binding that holds it, a `const` one and an element of a
    collection somebody else reads included, because there is no read-only view of an object (CONCEPT, "Identity").
    **Decided (2026-09-22):** `var fn`s, both of them — the same rule `source.next()` has: a method that changes the object is a
    `var fn`.

### Decided on 2026-09-23: a cancellation is passed on, not answered

12. **`task.await()` answers `Value` itself**, reversing the decision that it answers `Result<Value, Cancelled>`.
    Cancellation is propagated, not returned: a task that awaits a task that ended cancelled is itself cancelled at that
    suspension point - it stops there, its scopes end in reverse order (`using` and `close()` run exactly as when it is
    cancelled at a back-edge), and it ends cancelled, so whatever awaits *it* is cancelled in turn. The top level of an
    entry file that awaits a cancelled task ends the program with `cancelled: the program waited for a task that was
    cancelled` on standard error and exit code 130 (section 8, "The cascade"). **`result()`** answers
    `Result<Value, Cancelled>` for the code that observes a cancellation explicitly - supervisors, and code that
    cancelled a task and wants to confirm it. `within` keeps `TimedOut`. `Task.all` and `both` follow the rule: where one
    part is cancelled the whole is cancelled, after they cancelled the other parts. **`outcome()` is deleted**; every IO
    line is `.await()?`, and the `From<Cancelled>` implementations that existed for it (`IoError`, `HttpError`,
    `ChannelClosed`, `Cancelled` itself) are gone with the `Failure: From<Cancelled>` bound of `Source` and `Sink`.

    **Why.** A call that waits is never cancelled on its own - only the task that waits is, by its parent, a deadline or
    a supervisor. The waiter therefore never has a use for a value that says "the thing you waited for will not come":
    the only honest reaction is to stop too, and stopping is what the cascade does for it, with its resources closed on
    the way out. Answering the cancellation instead made every line of IO carry two `Result`s - the cancellation around
    the failure of the work - and `outcome()` existed only to fold them, with `Failure: From<Cancelled>` spreading into
    every stream's error type to make the fold type-check. Nesting two `Result`s per line was the symptom.

    **The comparison.** *Rust*: a future that is dropped simply stops at its last `.await`; nothing downstream receives a
    "cancelled" value, and `?` carries only the work's own error. *Swift*: structured concurrency cancels the child tasks
    of a cancelled task, and a task learns of it where it waits (`Task.checkCancellation`, `CancellationError` thrown
    from `await`) - a propagation through `throws`, not a value every caller unwraps. *C#*: a
    `OperationCanceledException` thrown from the awaited `Task` unwinds the awaiting method unless somebody catches it on
    purpose - `await` answers `T`, not a result of `T`. *Kotlin*: a coroutine that awaits a cancelled `Deferred` receives
    `CancellationException`, which structured concurrency treats as normal completion and propagates to the parent.
    All four propagate and none of them returns the cancellation from every await; TorbScript now does the same, with
    the propagation through the stop path instead of an exception nothing can intercept by accident, and `result()` as
    the one explicit place to observe it.

## 16. Runtime ABI, as built

The runtime half of 7.3 is `runtime/include/torb_task.h` (the contract, with a lowered example in its header
comment), `runtime/task.c` (the scheduler - since 7.7 a pool of workers, "The pool, as built" below) and
`runtime/tests/task_test.c` (twenty-one tests over state machines written by hand, the way the lowering writes them).
This section is the summary; the header is the reference.

**A task is one counted block**: the runtime's part (resume function, state index, cancellation flag, waiter list,
parent link), then the frame, then the result slot placed by the `torb_element` of `Value`. Its count is the handles
plus one reference the scheduler holds until the task completes, so dropping the last handle of a running task stops
nothing (section 8).

**A resume function** is `torb_poll resume(torb_task *task)`: a switch over `task->state`, the only field the machine
writes. It answers `TORB_POLL_SUSPENDED` (registered where it waits), `TORB_POLL_FINISHED` (the value is in the result
slot) or `TORB_POLL_STOPPED` (no value; every `await()` of it cancels its waiter, every `result()` answers
`Fail(Cancelled)`). A suspension primitive (`torb_task_await`, `torb_task_observe`, `torb_task_await_until`,
`torb_channel_send`, `torb_channel_receive`, `torb_task_pause`,
`torb_task_sleep_until`) answers `TORB_WAIT_SUSPENDED` - return now - or `TORB_WAIT_READY` - go on at once; either way
the machine then calls `torb_task_outcome` once.

**Cancellation is the machine's own check.** Every resume, every point after a ready wait and every loop back-edge
starts with `torb_task_cancelled(task)`; where it is set, the machine releases what is live at that point, exactly as
at a `return`, and answers `STOPPED`. The runtime releases nothing of a frame, because only the lowering knows which
slots are live - it takes a cancelled waiter out of where it waits and queues it, which is the table of section 7 for
the one worker there is. The one thing the runtime does release is what a wait handed over and the machine never took:
a channel item a stopped receiver did not accept, and the item of a sender cancelled while it waited.

**The runtime never builds a `Result`.** `torb_task_result(task, &out)` is the `.Fallible` convention - `true` and a
retained copy of the value, or `false` - and the lowering builds `Ok(value)` or `Fail(Cancelled())` in its own layout.
`within` is the one native that has to put a `Result` into a task, so it takes a `torb_within_shape`: the descriptor of
`Result<Value, TimedOut>` and two adapters (`finished`, `timed_out`) the lowering emits per instance, as it emits a
`torb_element` per type.

**Decisions this round made**, each with its reason in the header: the scheduler panics on a deadlock instead of
hanging; `torb_scheduler_finish` at the end of `main` cancels every task still running and runs it to its stop, so the
leak gate stays exact (the program ends, as tokio's and Go's do, and every frame's `close()` runs on the way); a
negative or `nan` `sleep` is zero; a task that stops without the flag ends as cancelled, which is how `within` - and
later `Task.map` - passes on a cancellation it observed; a parent that finishes hands its running children to its own
parent, so cancelling the grandparent still reaches them.

### The cascade in the runtime (2026-09-23)

**The ABI change, for every back end that drives a machine (the C back end today, the VM next).**

- `torb_task_await(self, awaited)` - the wait of `await()` - now **passes a cancellation on**: where `awaited` has
  completed as cancelled, it sets `self`'s cancellation flag before it answers `TORB_WAIT_READY`; where `self` waits and
  `awaited` completes as cancelled later, `torb_complete` sets the flag of every waiter that waits this way before it
  wakes it (under `awaited`'s lock, so the flag is set before the waiter runs again). Nothing else happens: the machine's
  existing check after the wait (`if (torb_task_cancelled(task)) goto stop_K;`) takes the stop path, which releases the
  live set - and so closes every `using` - and answers `TORB_POLL_STOPPED`; `torb_complete` then cancels the stopped
  task's children and wakes its own waiters, which cascades further. A back end therefore needs no new control flow,
  only the second primitive below.
- `torb_task_observe(self, awaited)` is **new**: the same wait without the cascade, for `result()`. After it,
  `torb_task_result(awaited, &out)` answers `false` for a cancelled task. `torb_task_await_until` (the runtime's own
  `within`) observes as well.
- A task records which of the two its last wait was in a new byte of `torb_task`, `observing`, written by the waiter
  before it joins the waiter list and read under the awaited task's lock. The struct keeps its size.
- The IR says which wait a suspension is: `Instruction.Suspend(state, awaited, observes)`, `observes` true only for
  `result()`. The C back end emits `torb_task_observe` for it and `torb_task_await` otherwise.
- `int torb_task_end_main(torb_task *main_task)` is **new**, and the generated `main` calls it in place of
  `torb_task_release(main_task)`: it releases the handle and answers 0, or - where the main task ended cancelled -
  writes `cancelled: the program waited for a task that was cancelled` to standard error and answers
  `TORB_EXIT_CANCELLED` (130), which `main` returns after `torb_scheduler_finish` and `torb_process_finish`.
- `await()` and `result()` are TorbScript over the private native `finished()` (`.Fallible` `torb_task_result`);
  `Task.await` is no manifest entry any more. `await()`'s `Fail` arm cannot be reached, because a waiter whose task
  ended cancelled stopped at its check, and it panics as an internal error if it ever is.

`runtime/tests/task_test.c` pins it with hand-written machines: a parent awaiting a cancelled child stops at its await
and runs its stop path, the grandparent awaiting the parent stops in turn, a supervisor observing the grandparent reads
the cancellation and finishes; an await of a task that had already ended cancelled stops at once; a finished task does
not cascade; the cascade cancels the stopped task's children; and `torb_task_end_main` answers 0 and 130.
`tests/conformance/task-cancel-cascade.trb` runs the same chain across four workers and one, and
`tests/conformance/task-cancel-main.trb` the end of a program whose top level awaits a cancelled task.

### The compiler half, as built

**The lowering (`ir/lower/task.trb`).** A function that *declares* `Task<Value>` - not one whose type parameter a task
happens to fill - is two functions: a **resume function** (`FunctionKind.TaskResume`, named `t_resume__<name>`) whose
parameters are the frame's and whose result is the `Value`, and in the declaration's own place a **constructor** whose
body is one `TaskNew(target, resume, element, parameters)` and the `return` of the handle. So every caller keeps an
ordinary `Call`, and only the back end knows a task is a state machine. A closure whose declared result is a `Task` is
the same pair (the lowering reads off the type of what the body ends with whether the body is the task's, because the
checker decides that by the expected type and records no flag), and `spawn { ... }` is a `Construct` of the closure's
environment plus a `TaskNew` of its body, the environment being the frame; `spawn` of a closure *value* runs it through
one generated resume function per closure type. A `var` parameter crosses into the frame as the value it holds, which
is the same thing only for an object with an identity, so one of any other type is a clean finding. `await()` - and
`result()` - is `Suspend(state, awaited, observes)` in front of the call that reads the answer, the states numbered 1, 2, ... in
block order once the body is done. `stopAsCancelled()` is `Terminator.Stop`. The top-level code of an entry file that
waits anywhere is a resume function too, and runs as the main task.

**Ownership.** Nothing new: the parameters of a resume function are `Owned` (the frame holds them), `TaskNew` takes the
count of every argument as a `Construct` takes its fields, and `Stop` leaves the frame empty exactly as a `return`
does, which the extended verifier checks.

**The frame (`ir/suspension.trb`, `TaskMachine`).** Read off the finished body, after the ownership pass. It holds every
parameter and every slot that is live across a suspension - liveness over *every* slot, not only the counted ones - and
nothing else: a slot that is not live after any suspension is a C local of one resume, zeroed at every entry. The C back
end writes the frame as a struct `F_<resume function>` with one member per frame slot, in slot order, and a slot of the
frame is `frame->member` wherever the body names it.

**The stop paths.** Because the ownership pass placed a release after every last use, a counted slot that is live is
exactly a slot that holds a count, so what a stop releases is the live set where it stops: the entry (state 0) releases
what is live at the first block, a resume point what is live right after its `Suspend`, and a back-edge what is live
before the terminator that closes the loop. A back-edge is an edge into a block the depth-first walk from the entry has
not left yet, which in the reducible graph a structured loop lowers to is exactly the edge that closes it.

**The C back end.** A resume function is `static torb_poll <name>(torb_task *task)`: the frame pointer, the locals, a
`switch (task->state)` whose case 0 falls through to the entry check and whose case K jumps to `resume_K`, the blocks,
and after them one stop path per state and per back-edge (`stop_K`, `stop_loop_B`) that releases its set and answers
`TORB_POLL_STOPPED`. A `Suspend` is `task->state = K`, `torb_task_await`, `return TORB_POLL_SUSPENDED` where it
registered, the label, the check and `torb_task_outcome`; a `return` moves the value into `torb_task_result_slot` and
answers `TORB_POLL_FINISHED`; a back-edge reads the flag before its terminator. `main` wraps an entry that waits in
`torb_task_new`/`torb_task_start`/`torb_scheduler_run`/`torb_task_release`, runs `torb_scheduler_run(NULL)` after an
entry that only starts tasks, and ends in `torb_scheduler_finish()`; a program with no resume function and no `TaskNew`
calls none of it. No resume function enters a frame of `panic.c`, so there is nothing to balance.

**Decisions of the compiler round**, each for the reason given:

- **`within` is TorbScript over `torb_task_completed_within`** - a task of the runtime that answers whether the target
  completed before the deadline, cancelling it where it did not - and not over `torb_task_within` with a generated
  `torb_within_shape`. The `Result<Value, TimedOut>` is then built by TorbScript like every other value, and the
  shape's two adapters would have been the only C of the back end that builds a variant of the program outside a body.
  `torb_task_within` stays in the runtime and is unused.
- **A channel's two ends are TorbScript shared types over one task per item each way** (`torb_channel_received`,
  `torb_channel_offered`), and not machines that wait on the channel directly: a machine then waits for a task and for
  nothing else, so the lowering has one kind of suspension point and the frame never lends a slot to a waiting send.
  The price is a task block per item; a `Wait` instruction over the channel primitives is the optimization when it is
  measured.
- **`source()` and `sink()` answer `ChannelSource<Item>` and `ChannelSink<Item>`**, the concrete shared types, which
  are a `Source` and a `Sink`. A call on one is a direct call, and a program that never needs the trait-typed value never
  builds its witness table - which also keeps it clear of the one gap in the way of every `Source` object today (below).
- **`Task.cancel` and `within` are TorbScript `var fn`s over natives that take the task by value** (`cancelTask`,
  `completedWithin`), because a native `var fn` of a runtime object would hand the runtime a pointer to the caller's
  slot rather than the task.
- **A `shared type` object is a counted block** (`layout.trb` pins its layout `Boxed`), released through
  `torb_release` with its layout's drop and never made unique - the part of 5.9b the channel ends need. The trace
  functions of a cycle are not built: an object on a cycle of objects leaks.

**What still does not build natively, and why.** A `Duration` has no representation in the back end yet (5.12), so a
program that calls `within` does not build. (A `Source` or `Sink` held as a **trait-typed value** did not either until
gap 11 closed: `through` reaches `Stage.onto<Final>` through the table, and `Source.from` called through the trait's
name is the trait's own body since bug batch 7 - `Source.produce`, `std/stream`'s tests and
`examples/tour/src/13-streams.trb` build and run natively, `tests/conformance/source-trait-value.trb` pins both.) `?`
that converts a failure through a `From` the program declares is not
lowered yet. (`examples/tour/src/10-async.trb` builds and runs natively since `std/http` became TorbScript over
`std/network`, `docs/design/NETWORK.md`, against a server of its own on loopback - which makes it a program with real IO
and no longer one the VM can be held to.) A
`Process.exit` from inside a task ends the program with the main task's block still counted, the way a panic does.

### Manifest rows that change

| Row | Before | As built |
|---|---|---|
| `Task.await`, `Task.finished` | `.Planned` `torb_task_await` | `Task.finished` is `.Fallible` `torb_task_result`; `await()` and `result()` are TorbScript over it (since 2026-09-23), and the `Suspend` in front of the call is the lowering's |
| `spawn`, `stopAsCancelled` | `.Planned` `torb_spawn` | `NativeTarget.Lowered`: a `TaskNew` over the closure's body, and `Terminator.Stop` |
| `sleep`, `pause` | `.Planned` / missing | `torb_task *torb_sleep(double seconds);`, `torb_task *torb_pause(void);` |
| `cancelTask`, `completedWithin` | missing | `torb_task_cancel(torb_task *)`, `torb_task_completed_within(torb_task *, torb_duration)`, under the TorbScript `Task.cancel` and `Task.within` |
| `Channel(capacity:)` | - | `Instruction.ChannelNew` over `torb_channel_new` and the element descriptor of `Item` |
| `Channel.source`, `Channel.sink` | `.Planned` | TorbScript: `ChannelSource`/`ChannelSink` over `received` (`torb_channel_received`), `offered` (`torb_channel_offered`), `endWriting` (`torb_channel_end`) and `closeReading` (`torb_channel_close`) |
| `Task.map`, `Task.flatMap`, `Task.all`, `both` | `.Planned` natives | TorbScript over `await()`, `result()` and `stopAsCancelled()` |
| `standardInput`/`Output`/`Error`, `Process.start`, `Child.*`, `File.create`/`chunks`/`add`/`end` | `.Planned` 7.3 | **built**: TorbScript `Source`/`Sink` objects over the tasks of `runtime/stream.c`, each of which makes its one call of the operating system on the blocking pool (STREAMS.md section 14) |

### The pool, as built (7.7)

The runtime half is `runtime/task.c` (the pool, over the same task ABI as before), `runtime/include/torb_pool.h` (what
the runtime's own files share about a worker, never included by generated C), the thread, lock and condition functions
at the end of `runtime/platform.c`, and `runtime/tests/pool_test.c`. Nothing of the task ABI above changed for the
lowering except the start of a task and the environment of a closure.

**A worker is a thread and what that thread alone touches.** The main thread is worker 0 from the first instruction on;
workers 1 to N-1 are started the first time a task that may move is started (`torb_task_start_portable`) where
`Workers.count()` is more than one, and joined in `torb_scheduler_finish`, their block counters folded into worker 0's.
A program that never starts such a task never has a second thread. Each worker owns a `torb_heap` (the block counters of
memory.c, written by its thread alone and summed for the report - a block freed by another worker than its maker lowers
that worker's counter, so the counters are unsigned and wrap and only the sum is exact), a `torb_scheduler` (the running
task, the timer heap), the recovery point of a panic, and the bottom of its stack. A thread finds its worker in one load:
a thread-local pointer on POSIX, and on 64-bit Windows its TLS slot read straight out of the TEB through `gs`, because
MinGW's `_Thread_local` is an emulated call on the path of every allocation. The stack check reads the bottom of the
running thread's stack the same way (`DeallocationStack` of the TEB; a thread-local elsewhere), so recursion that is too
deep is a panic on every worker and not only on the main thread. A panic ends the process from whichever thread it is
on, and a second one at the same time waits for the first instead of interleaving with it
(`tests/conformance/task-panic-worker.trb`).

**A test owns the tasks it starts.** Under `torb test` a panic has to fail one test and let the next one run, and a
recovery point is a thread's, so the runner (`runtime/test.c`) opens a scope of the pool around each test body: every
task the body makes on the main thread, and every task one of those makes, carries the test's number, and the test ends
only once they have completed - the main thread runs tasks meanwhile, as `await()` would. A task of a test is resumed
under a recovery point of the thread that runs it, whichever worker that is, so its panic lands there: the first one is
the test's failure, with its message and site, every task of the test is cancelled, and the one that panicked is
completed as cancelled without returning to its machine, so whoever observes it reads `Fail(Cancelled)` and the pool's
counts stay exact. Waiting for the test's tasks was chosen over reporting a panic whenever it happens, because before
it a task a test started ran after the whole file - outside every recovery point - or, with more workers, at a moment
that decided which test the panic was blamed on. Where the body runs inside the main task (an entry file that waits),
that task counts as running while the test waits, so the wait ends at one thing left rather than none; a task of the
test that waits for what never comes does not hang the run, it waits on and is cancelled at the end of the program as
before. A panic in a task of no test in progress - the top-level code's, or a test's whose wait already ended on a task
that could not go on - is nobody's to report and ends the process, as it does on the main thread. Outside a test run no
task carries a number, and nothing is resumed under a recovery point (`runtime/tests/pool_test.c`,
`tests/conformance/task-test-panic.trb`, `task-test-panic-waiting.trb`).

**The run queue of a worker is its inbox.** One FIFO per worker behind the worker's mutex, and inside it the list of the
unstarted tasks that may move. Whoever wakes a task - the completion of the task it awaited, a channel, a cancellation -
takes it out of what it waited on under that object's lock, puts it at the back of its worker's queue under the worker's
lock, and signals the worker's condition where it sleeps. A worker with nothing to run takes the oldest unstarted task
that may move from another worker's queue (the victims tried round the ring from its right neighbour) and makes it its
own; a task that has run once never moves, because its frame is its worker's. Otherwise the worker sleeps until it is
signalled or its first timer is due. One counter of the process says how much can still happen - tasks queued, tasks
running, timers armed - and the main thread reads a deadlock (or, with nothing to wait for, the end of the program) off it
reaching zero. The locks are taken in one order and nothing else: the tree lock of the parent, child and live links (a
mutex of the process), then the spin lock of a task (its `status` and `waiters`) or of a channel (its ring and queues),
then a worker's queue lock. No user code runs while one is held.

**Cancellation across workers** is the flag, set atomically, and the task queued on its own worker: the task that runs
the machine is the only one that may touch its timer and read what it waits on, so a cancellation from another worker
queues it and its own worker takes it out of the waiter list or the channel queue when it takes it from the queue. A
cancellation on the task's own worker takes it out at once, exactly as the single worker did. A parent and its children
may be on different workers; the tree they form is behind the tree lock, and cancelling a parent reaches every child
wherever it runs (`runtime/tests/pool_test.c`).

**What crosses a worker, and why nothing is copied.** Counts are plain integers, so a value may be handed to another
thread only where no count it reaches can ever be touched by two threads. The runtime cannot walk a value - it has no
layout of the program in it - so the proof is made where the value is handed over, out of its type and, where the type
does not answer, a test of the runtime on the value itself:

| A value that holds | crosses because |
|---|---|
| nothing counted (`Int`, `Float`, a record of those) | its bytes are all of it |
| an immortal block (a literal, a module constant) | nobody ever retains or releases it |
| a `Task` or a `Channel` whose value or item holds nothing counted, or a closure whose environment is **shared** | these are the only blocks with `TORB_SHARED_COUNT`: their counts change atomically while the pool runs threads, and what they hand out holds nothing counted |
| a list, a map or a text whose storage only it holds, with plain elements - **in the frame of a task that has not run** | the block is **re-homed**: the frame is its only owner, so the worker that takes the task is the only thread that ever touches it again; libc frees it from any thread and the heap counters balance in their sum |

Everything else - a shared object, a `Box`, a trait-typed value, a list of strings, a text somebody else holds too -
does not cross at all: the task that holds one is started pinned (`torb_task_start`) and runs on the worker that made
it, the way every task ran before there was a pool, and a channel or a task handle of a counted value never leaves its
worker either, because its type fails the test wherever it is written. The compiler writes the test
(`compiler/src/backend/c/crossing.trb`): beside a task's start, `torb_task_start_portable` where the frame's types
answer yes, `torb_task_start` where they answer no, and a choice between the two on `torb_text_may_move`,
`torb_list_may_move`, `torb_map_may_move` and `torb_closure_may_move` where they cannot tell; beside a closure's
environment, `torb_share` where every capture may cross without a transfer - so a closure that captures only plain values
or other shared closures may run on several workers at once. The result of a task follows from the same rule: the
worker that ran a stolen task gives up the scheduler's reference **before** it publishes the completion, so the last
release of the task block, and with it the release of a counted result, happens on a thread that held a handle - and the
handles of a counted result are all on one worker.

**The copy at the crossing.** Where the test fails on a value that may be copied soundly, the frame of a task is made
private instead - section 6's "copied into the worker's heap", decided on 2026-09-23 as follows:

- **When.** While the pool has more than one worker (`torb_task_copies`), beside the start of a task whose frame the
  test does not prove movable: `(torb_task_copies() ? <copy> : <test>) ? torb_task_start_portable : torb_task_start`.
  With one worker nothing is copied, because no other worker could take the task.
- **Where.** On the thread that starts the task, before anybody else can see the frame - never on the thread that takes
  it. A copy made by the thief would read counts the owner may change at the same moment, and would have to release the
  originals on the wrong thread; the owner has both for free. The price is that a task that is never stolen was copied
  for nothing, which a chunk of `parallel()` rarely is, and which the counter `torb_pool_statistics.copied` makes
  visible (gap 12's counter, for the copies).
- **What.** Each value of the frame is replaced in place by an equal one that shares no counted block with anything
  else, and the original's count is released: a `String` gets storage of its own (`torb_text_privatize`) where somebody
  else holds it too; a list, a map or a set gets storage of its own where it is shared or a slice of a larger one, and
  then each element is made private in place (`torb_list_privatize`, `torb_map_privatize`, with a function per element:
  `NULL` for a plain one, `torb_text_privatize_place` for a `String`, and a `P_<layout>` the emitter writes for a record
  or a tuple); an inline record or tuple field by field. Nothing is copied that is already private - a block with a
  count of 1 on a path only this value owns, an immortal block, a shared one - so a frame the test would have let
  through costs a walk and no allocation.
- **A variant** with a counted case is tested and copied by the case it is: the C back end writes a switch on its tag
  as an expression - `tag == 0 ? <the fields of case 0> : tag == 2 ? ... : true`, one arm per case that holds a count -
  both in the test and in the copy, and the VM walks the words of the case the tag names (`TORB_CROSSING_VALUE`, and the
  groups of the shape in `torb_machine_privatize_value`).
- **A closure** crosses where its environment is shared, and otherwise its environment is copied by the copy its
  closure put into it: every environment carries, beside its drop function, a `privatize` function (`PE_<layout>`) the
  emitter writes for a layout whose captures may all be copied - the block itself where only this closure holds it,
  else a block of its own with every capture retained, and then every capture made private in place
  (`torb_closure_privatize`). The VM copies its environment blocks by the shape of their contents
  (`torb_machine_privatize_environment`).
- **What never.** Whatever has an identity or cannot be walked: a `shared type` object, a task or a channel of counted
  items, a captured `var` (so a closure over one gets no copy and crosses only where its environment is shared), a
  `lazy` cell, a trait-typed value (its payload is erased), a boxed record, and a value whose type implements `Close`,
  which a copy would close twice. Where a value answers no, the task stays on its worker, correct and sequential, exactly
  as before the copy existed.

The emitter decides it per type (`compiler/src/backend/c/crossing.trb`, `privateOf`); the runtime copies
(`runtime/text.c`, `list.c`, `map.c`); `runtime/tests/pool_test.c` pins that a copy happens only where somebody else holds
the value and that the original is left as it was, and `tests/conformance/parallel-texts.trb` runs `parallel()` over
`String`s and records of them with four workers and one, leak-free.

**The counts that are atomic** are exactly the blocks with `TORB_SHARED_COUNT`, and only while the pool runs more than
one thread (`torb_pool_threaded`, which changes only while one thread is left): a program with one worker pays one
extra test of a bit it already loaded. The first build of a module constant (the immortal counted static) happens under
one recursive lock of the process and is published with a release (`torb_constant_ready`, `torb_constant_publish`),
because two workers may ask for the same constant at once; a line of `print` is written under a lock, so two workers
never interleave inside one; the console cache and the clock are made ready before a second thread exists.
`Process.exit` from a task on another worker is handed to the main thread, which is the one inside the program's `main`:
the exiting worker completes its task as cancelled and keeps running its own queue until the pool stops, then its thread
ends where it is, and the main thread drains the pool and leaves with the code - so the leak report of such an exit is
as exact as one on the main thread.

**`parallel()`** is `std/parallel`, TorbScript over task functions, and in the prelude as `Parallel`,
`Iterate.parallel`, `Cut.parallel` and `List.parallel`. Since 2026-09-26 it is there on every `Iterate`, and three
changes of the compiler made that possible: the receiver of a member of an `extend` whose target is a trait is coerced
to that trait-typed value (`ir/lower/call.trb`, `receiverOfTraitExtension`: a `Range`, a `Set` or a pipeline is boxed
or narrowed, where the lowering had handed the value over as it was); of two extensions that bring one member the more
specific one is the answer (`semantics/checker/member.trb`, `isMoreSpecificExtension`: the receiver's own type before a
trait, a trait before one every value of it has); and `self` or a constructor of a generic type's own body coerces to a
trait it implements (`matchPattern` bound none of the implementation's parameters where the target was the very same
type). A call reaches the most specific of the three: a `List`, an `Array` and a `Range<Int>` are a `Cut` - the length
and `cut(range)`, which answers a piece of a type of its own - and are cut without being read, a range arithmetically;
anything else is read into an `ArrayList` once and its slices are the pieces. `List` keeps an extension of its own,
because a value of the `List` trait cannot be narrowed to a `Cut`: `Cut` is implemented *for* `List` and is no
supertrait of it, so no table of a list holds it.

The pieces are cut by section 4's arithmetic, and the pipeline is **fused**: a private `Plan<Item>` holds the pieces and
one step per item - `(value: Source) => Item?`, `None` where a stage dropped the item - and `map`, `filter` and
`filterMap` each wrap one closure around it (a generic member through the trait-typed plan, gap 11). A terminal runs
every piece once as a task of a chunk function (a task *function*, because the checker's `spawn` rule refuses a capture
of a generic type): the step over each item of the piece, and the terminal's own reduction over what was kept - the
items for `toList`, a count, a sum, at most one item for `find`, `minBy` and `maxBy`. So a chunk crosses as its piece -
an `ArrayList` of plain items passes `torb_list_may_move`, a `Range<Int>` is a plain record - plus one closure whose
environment holds nothing but other shared closures. At most `workers:` chunks run at once, the results are read back in
input order, and `find` starts no further chunk once the results before it, read in order, hold a match. `collect` runs
the stages on the workers and the accumulator on the caller's worker, chunk by chunk, joined with its `merge`: an
`Accumulator` is a trait-typed value, whose payload is erased and which therefore never crosses. Measured with
`benchmarks/parallel.sh` on 16 logical processors (`parallel-map.trb`: the Collatz steps of two million numbers,
summed; whole process, fastest of five): 632 ms with one worker, 377 ms with two, 275 with four, 205 with eight, 167 with
sixteen - 3.8x, with the building of the list, the cutting into chunks and the start of the process in every number.
Inside the program the map itself takes 476 ms sequentially and 95 ms with sixteen workers (5.0x), 39 ms of which the
same pipeline spends on a trivial map: the cutting, the copies into and out of the chunks and the final sum.
After the fusing (2026-09-26) the same binary takes 692 ms with one worker and 194 with sixteen - one stage has nothing
to fuse - and the same pipeline over the range itself, `(1..=size).parallel()`, which cuts arithmetically and builds no
list, 600 ms with one worker, 216 with four and 132 with sixteen (4.5x).

**Determinism.** With one worker the pool is the single scheduler of 7.3, order for order, and the conformance suite
runs every program with `TORB_WORKERS=1` unless a `<program>.workers` file names more; such a program runs a second time
with one worker and has to print the same bytes (`tests/conformance/README.md`). What stays deterministic with more
workers is what a program reads - the values of tasks, the items of a channel in the order they were sent, the chunks of
`parallel()` in input order - and not the interleaving of what several tasks print at once.

### The blocking pool, as built

`offload(body)` in `std/task` runs a body that blocks on a thread of the blocking pool, so the worker that asked goes on
with its other tasks (`runtime/task.c`, "The blocking pool"; `runtime/tests/pool_test.c`, `tests/conformance/
task-offload.trb`). `Workers.blocking()` threads - `TORB_BLOCKING`, default 4, refused with exit code 2 outside 1 to
1024 - are started the first time a body moves there and joined with the workers at the end of the program.

**`offload` is TorbScript over one native**: `blockingTurn().await()`, then `body()`. The turn is a task of the runtime
that finishes at once; its completion puts the task that awaits it back on its worker's queue, and the worker, taking it,
hands it to the pool's **inbox** instead of running it. The first idle thread of the pool takes it, and the rest of the
machine - the body, and the return of its value - runs there. So the lowering knows nothing of the pool, the body is
called through the program's own closure convention (the runtime cannot call a closure of a signature it does not know,
which is why the design's `native fn offload` became TorbScript), and nothing is allocated per call but the turn.

**Decided here, with the reason for each:**

- **A body moves only where its task's frame may move**, which the task's start already proved: `offload`'s frame is
  the closure, the task is started portable exactly where the closure may cross (`torb_closure_may_move`), and the only
  thing it makes before the turn is the turn's handle, a task of nothing. Where it may not - a closure that captures a
  `String` built at run time, a list, a shared object - the turn is only a turn and the body blocks its own worker, as
  every call did before: correct and sequential, never a race. This is the section's rule that nothing crosses a thread
  unless it is proven to, and it is why `offload` takes a closure that is cheap to prove: a literal path or a number
  crosses, `readText(path)` with a path built at run time does not yet. The copy of an environment ("The copy at the
  crossing", not built) is what lifts that.
- **A thread of the pool has a heap**, where section 7 said it would have none. Section 7 describes the pool the IO
  interface uses, whose threads write into a buffer of the task and run no TorbScript; `offload` runs a body of the
  program, which allocates. The heap is only the block counters (memory.c), so a thread of the pool is a worker without
  a place in the ring: its counters are summed and folded like every worker's, and what the body answers is handed to the
  waiter exactly as the result of a stolen task is (the thread gives up its reference before it publishes the
  completion). Nobody steals from a thread of the pool and it steals from nobody; a task the body itself starts stays
  on that thread.
- **One shared inbox, not a queue per thread**, so a body never waits behind a long one while another thread is idle.
- **Cancellation** is section 7's row: a task that waits in the inbox is taken and stops at its first check without
  running its body; a body that runs is not interrupted - the worker is free at once, and the frame is released on the
  pool's thread when the body returns and the machine reaches its next check.
- **The pool starts with the first move**, not at the start of the program: a program that never offloads never has
  the threads, as one that never starts a portable task never has a second worker.

**Not built here, and why:** `runtime/io.c` and the poller (slice G), and with them the cancellation of a read that
waits (gap 15), because there was no read that waits then - the streams of files and child processes came later, on
this pool (`runtime/stream.c`), and a read of theirs that runs is not interrupted either. The `blocking` line of the manifest waits with
`workers` for the project model's `tasks`.

**Not built, and why each waits:**

- **The poller for the pipes of a POSIX child** (slice G): the streams of files, of the standard streams and of child
  processes wait on the blocking pool (STREAMS.md section 14), which every platform allows; a pipe of a POSIX child is
  the one of them epoll and kqueue could take, and the second code path waits until it is measured. The whole-file
  calls of `std/fs` stay synchronous, and `offload` is what a program wraps one in, as `Process.run` does.
- **`Plain`, `Window`, `windows`** (slice F), and `flatMap` on `Parallel`.
- **An accumulator that runs on the workers**: `collect` accumulates on the caller's worker, chunk by chunk, because an
  `Accumulator` is a trait-typed value, whose payload is erased and which the copy at the crossing does not walk.
- **The measurements of section 9** and the skewed-cost benchmark; the pool counts resumes and thefts
  (`torb_pool_statistics_now`), nothing prints a histogram.
- **A race detector**: there is no `-fsanitize=thread` for Windows targets, so the pool was verified by its tests run
  thirty times in a row with four workers, by the conformance programs with four workers and one, and by review of the
  lock order. The POSIX half (pthreads, `_Thread_local`, `-pthread` on the command line) is written against POSIX and
  was neither compiled nor run on this machine, which has no POSIX toolchain; the MSVC branch of the atomics was not
  compiled either.
- **The VM half** (`vm/task.trb`) waits for the VM.

## 17. Open: a trait value that hides a shared object

**The gap.** Shared-types rule 7 says a shared object stays in the task that made it, and `spawn` refuses everything
that may hide one: a shared object, a value that holds one, a type parameter, a function value, a value of a
`shared trait`. Trait rule 17 says a `shared type` can only implement a `shared trait`, "so a value of a trait type is
always a value" - and that is what lets a value of an ordinary trait cross. The two rules leave a hole between them: a
type that is **not** shared may **hold** a shared object and implement an ordinary trait, and the trait value then
carries the object across the one check that was meant to stop it. This type checks today (2026-09-26) and runs:

```trb fragment
shared type Counter {
  var count: Int = 0

  var fn bump() {
    count = count + 1
  }
}

trait Action {
  fn run(): Int
}

type Bumper with Action {
  counter: Counter

  fn run(): Int {
    counter.bump()
    counter.count
  }
}

const counter = Counter()
const action: Action = Bumper(counter)
const task = spawn { action.run() }
print task.await()
print counter.count
```

`spawn` sees an `Action`, which is not a `shared trait`, and lets it in; the `Counter` inside is changed by two tasks.
**It is memory safe** - a trait-typed value fails the crossing test of section 16 ("The copy at the crossing"), so the
task that holds one is started pinned (`torb_task_start`) and runs on the worker that made the object, and no count is
ever touched by two threads - but it is not what rule 7 promises: the object is reached from two tasks, and the order
of their changes is the scheduler's.

**The options.** The language rule stays as it is until the owner decides; both ways to close the hole cost something.

- **(a) `spawn` refuses every trait value that is not known to be free of objects** - in practice every value of an
  ordinary trait, since the checker cannot see through one. It closes the hole at the one place it matters, and it is
  local: nothing else changes. It contradicts trait rule 17, whose whole point is that a value of an ordinary trait
  *is* a value and may be handed to a task, and it takes `spawn { shape.area() }` over a `Shape` away from every
  program that never held an object in one.
- **(b) Converting a value that holds a shared object into a value of an ordinary trait is refused**, as `with` on a
  `shared type` already is. Rule 17 then holds as written - a trait value never hides an object - and `spawn` needs no
  change. It breaks the `Iterate` pipelines that hold a closure: `names.map({ connection.send(_) })` builds a stage that
  holds a closure capturing the `Connection`, and every stage is answered as an `Iterate<Item>`, which is an ordinary
  trait. Inside `std/iteration` the closure is a function *parameter*, so the conversion site cannot see what it
  captured; refusing every closure-holding stage refuses every pipeline, and refusing none leaves the hole open through
  closures.

**Recommendation: (b), limited to what the checker can see**, and the rest stated as the runtime's. The conversion is
refused where the value's *type* holds an object structurally - the same fixpoint `spawn` already computes for "holds
an object" (section 12, probe 3), which covers `Bumper` and every record, case and collection of the program - and a
function value keeps the treatment it has: refused where `spawn` captures it directly, carried inside a trait value
otherwise, and kept memory safe by the pinning above. That closes the hole for every type a program declares, costs no
pipeline anything, and leaves one precise residue - an object captured by a closure that a trait value holds - which
the documentation of rule 7 would have to name. (a) is the fallback if the owner wants rule 7 without any residue; it
is the smaller change in the compiler and the larger one in what programs may write.
