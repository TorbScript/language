# Destructors, `close()` and `using`

`close()` stops being a method somebody remembers to call and becomes the language's one destructor: the runtime's
own reference count triggers it, exactly once, the moment the last holder of a value goes away. This reopens a
decision this repository has recorded as settled three times over — [no-destructors.md](language/execution/no-destructors.md),
`docs/BACKEND.md` section 7's gap 10, and the drop-argument of `docs/CONCURRENCY.md` section 8 — and this document is
the record of why, and exactly what changes because of it.

```text
  a local that does not escape       a value that escapes             two shared objects holding each other
  (never stored, never captured      (stored in a field, captured
   by a closing closure)              by a closure that escapes)

  the scope ends                     the last holder releases it      neither refcount ever reaches zero
  → close() now, in source order     → close() then, wherever         → section 9: handles instead of
    (Rust's Drop)                      that turns out to be              references, and a visible leak
                                        (Swift's deinit)                 instead of a silent one
```

- **[1. Today](#1-today)** — what `using` does now, what `Close` is, and that nothing forces a close
- **[2. `close()` is the destructor](#2-close-is-the-destructor)** — reference counting, one heap per worker, one call site
- **[3. `close()` cannot be called by user code](#3-close-cannot-be-called-by-user-code)** — Rust's E0040, and the states a direct call would create
- **[4. `using` binds a name that cannot escape its scope](#4-using-binds-a-name-that-cannot-escape-its-scope)** — the checker rejects a field, an escaping closure, a return
- **[5. Only a `shared type` may implement `Close`](#5-only-a-shared-type-may-implement-close)** — a value has no identity to close twice
- **[6. `close()` returns `Void` and never fails](#6-close-returns-void-and-never-fails)** — `end()` is where failure and waiting go
- **[7. A destructor never awaits, and cancellation is drop](#7-a-destructor-never-awaits-and-cancellation-is-drop)** — `using` inside a task, and what a cancelled task releases
- **[8. The cost: nothing for a type without `Close`](#8-the-cost-nothing-for-a-type-without-close)** — one static call inside a function that already exists
- **[9. The one hole: cycles](#9-the-one-hole-cycles)** — handles before `weak`, and a leak that is visible instead of silent
- **[10. Slices](#10-slices)** — R1 the runtime, R2 the escape check, R3 the task
- **[11. Open, for the owner](#11-open-for-the-owner)**

Every snippet marked **type checks today** was run against the real compiler, in `tests/language/` where `std`
resolves, exactly as `docs/CONCURRENCY.md` and `docs/COLLECTIONS.md` probe theirs. Nothing in this document's decisions
is implemented yet, so a snippet that shows the destructor itself is a `trb fragment` — lexed, not parsed against a
grammar that does not have the form yet — and a diagnostic is hand-written and marked as proposed. That is the honest
state of slice R1 in section 10: none of it exists, all of it is decided.

---

## 1. Today

**`using` is an ordinary function**, the third example of
[control structures are functions](language/extensibility/control-structures.md): its last parameter is a closure, so
a call reads like a keyword and is not one. The whole of `std/core/src/control.trb` is this:

```trb check
shared type Connection with Close {
  var isOpen: Bool = true

  fn send(message: String) {
    print message
  }

  var fn close() {
    isOpen = false
  }
}

using Connection() { connection => connection.send "hello" }
```

**type checks today**, and it is `no-destructors.md`'s own example. The function it calls is three lines and nothing
more:

```text
public fn using<Resource: Close, Value>(var resource: Resource, body: (var Resource) => Value): Value {
  const result = body resource
  resource.close()
  result
}
```

`using resource { body }` is `const result = body(resource); resource.close(); result` — a call, then a call, then
the first call's answer. There is no `try`, no `finally`, no flag: **the nesting of `using` blocks is the only
destruction order the language promises today**, because it is the only place a close ever runs at all.

**`Close` is an ordinary trait with one required method**, declared `public shared trait Close { var fn close() }` in
the same file, with a doc comment that already says the whole of today's contract: *"Releases the resource. Called
once by `using` when its block ends, never automatically."* Nothing in the checker treats it differently from `Hash`
or `Show`. It is a `shared trait`, which by
[traits.md rule 17](language/traits/traits.md) means a `shared type` may only implement it alongside other `shared
trait`s — but that rule runs in the other direction from what this document needs, and section 5 is why: it says
nothing about whether an ordinary, copyable `type` may implement `Close`, and today one can.

**Nothing forces a `close` to happen, and nothing stops one from happening twice.** `no-destructors.md` states this
as rule 5 — *"A `Close` a program forgets to call through `using` never runs at all. There are no drop flags and no
field order to reason about, because there is no automatic call in the first place for a flag to guard"* — with its
own probe:

```trb check
shared type Connection with Close {
  var fn close() {}
}

var connection = Connection()
print "using it"
```

**type checks today**, and `connection`'s `close()` runs at no point at all: not at the end of the block, not when
the last reference to it disappears, not ever. The same page's rule 4 draws the promise as wide as it goes: *"When a
reference count reaches zero is not observable, and a release never runs user code... reaching zero frees storage -
it does not call `close()` or anything else."* That promise is not local to this one page. `docs/BACKEND.md` section 7
recorded it as a settled question in its own decision log, gap 10, under the heading *"'Deterministic destruction' has
no destructors behind it"*:

> *Proposal:* state that there are no destructors: `Close` is an ordinary method, `using` an ordinary function, and
> the only observable destruction order is the nesting of `using` blocks. Releases happen where section 2 puts them
> and are not observable. *Reason:* a destructor would need drop flags, a field order rule and a story for a panic in
> one — and `using` already covers everything that must be deterministic.
>
> *Decision:* accepted.

`docs/CONCURRENCY.md` section 8 leans on the same promise for an unrelated question — whether dropping a `Task`
handle should cancel the task — and states it just as flatly: *"Anything else would make the moment a reference count
reaching zero observable, and CONCEPT says it is not."* Three documents, one sentence, and this one reopens it.

**Nothing stops a plain `type` from implementing `Close` either**, which is the sharper half of today's gap. `Close`
being a `shared trait` restricts what a `shared type` may implement, not who may implement `Close`:

```trb check
type Ticket with Close {
  var used: Bool = false

  var fn close() {
    used = true
  }
}

var a = Ticket()
var b = a
a.close()
print b.used
```

**type checks today** and prints `false` — `var b = a` made an independent copy the moment it was written, so closing
`a` never touches `b`, and a `Ticket` standing for a real, single resource would now have one live handle silently
sharing space with one dead one. Section 5 is the fix.

Finally, **the two-endings shape this document generalizes already exists**, written by hand, in `std/stream`.
`docs/STREAMS.md` section 3 states the reason a `Sink` has both a `close()` and a `finish()`: *"`Close` is what the
language uses for a resource (`using`, CONCEPT), it is synchronous and it cannot fail — so it can run on a path that
is already unwinding a failure. Finishing a stream can do neither... Collapsing them would mean either a `close()`
that can fail (and then `using` cannot use it) or a `finish()` that cannot (and then a failed flush is lost)."`
Section 7 adds that *"`close()` after the stream ended is allowed and does nothing"* and that every derived `Sink`
and `Source` closes the one above or below it. `Sink<Item, Failure>` already carries `var fn end(): Task<Result<Void,
Failure>>` beside `Close`. Sections 5 and 6 below turn this one package's convention into the language's rule.

## 2. `close()` is the destructor

**Values are reference counted**, which is not new: `docs/BACKEND.md` 5.4 already inserts `Retain` and `Release`
around every `Boxed`, `Text`, `Runtime` and `Closure`-with-an-environment operand, and 5.R1 already emits one
`R_<layout>`/`D_<layout>` pair per counted layout, the retain and the release function a value's own fields are
walked through. **There is one heap per worker thread** — `docs/CONCURRENCY.md` section 1: *"A worker... owns a heap:
a bump allocator with size-class free lists, the blocks tagged with the owning heap's id"* — and **the counts are
plain integers**, never atomic, because *"a task never leaves the worker that started it."* **There is no tracing
collector, and there will not be one**: a plain refcount is the whole of the memory model, now and for as long as the
language exists, which is what makes `close()`'s timing a fact about the program rather than a fact about when a
collector last ran. The interpreter and the AOT backend share this runtime, so a release — and now a `close()` inside
one — fires at the identical point in both.

**`close()` runs exactly once, when the last reference to the value goes away.** Two shapes, and they are the two
halves of the diagram at the top of this document:

- **A local that does not escape is released at scope end.** This is Rust's `Drop`: the value's last use is the end
  of the block it was declared in, the release is inserted there by the same liveness pass that already inserts every
  other `Release` (`docs/BACKEND.md` 5.4), and `close()` is one more call that release makes.
- **A value that escapes — stored in a field, captured by a closure that itself escapes — is closed by its last
  holder,** wherever in the program that holder's own count reaches zero. This is Swift's `deinit`: there is no fixed
  line in the source for it, because the value outlived the scope that created it.

**Nothing can be forgotten, and that is the enforcement.** Today's `close()` is a method a program calls or does not;
tomorrow's `close()` is a side effect of a release the runtime was always going to perform, whether the program
mentions `Close` or not. `no-destructors.md`'s two probes — a `Connection` built and never passed to `using`, and a
`Connection` handed through `withConnection` — both still run today exactly as written, but `close()` now runs at the
end of each: the first at the end of the block that built it, the second when `withConnection`'s own `using` releases
it, which is the case that already worked.

## 3. `close()` cannot be called by user code

**A direct call to `close()` is rejected**, the same way Rust rejects a direct call to `Drop::drop` with `E0040`: *"a
direct call would give double closes and 'already closed' states,"* and this document does not soften that into a
runtime check. The two probes of section 1 both call `close()` directly and both type check today with no complaint;
under this design, both are refused at the call.

```text
error: `close` runs automatically when the last reference to this value goes away, and cannot be called directly
  --> src/main.trb:9:1
   |
 9 | connection.close()
   |           ^^^^^^^^
   = To release a value early, let its scope end, or bind it with `using`
```

*(Proposed. `close` is not special-cased by the checker today — section 1's `Ticket` probe calls it twice with no
diagnostic at all.)*

**The reason is the states a direct call invents.** A `close()` that both runs by itself at the last reference and can
be called by hand has to answer what the second call sees — the fields half torn down, a flag nobody declared, or a
second `isOpen = false` racing nothing because there is only one thread per heap but two callers of the same method.
Every one of those answers is a case `close()`'s signature (section 6: `Void`, infallible) cannot report, so the
rule is not "check that `close` is not called twice" — it is that `close` is never in the caller's vocabulary at all,
the same way a value's release is not.

**To release early, a program has two spellings and no third one:** let the scope end, which for a value declared
mid-function means wrapping the rest of the work in a nested block, or bind the value with `using` — section 4.

## 4. `using` binds a name that cannot escape its scope

**`using name = expression` binds `name` to a value that the checker guarantees does not outlive the block it was
written in.** That is a new binding form next to `const` and `var`, not a new spelling of the trailing-closure call
of section 1 — `control-structures.md` rule 4 lists exactly six things the grammar knows how to bind (`const`, `var`,
`fn`, `type`, `trait`, `extend`, `use`); this document adds a seventh, and rule 3's claim that `using` is *"written in
ordinary TorbScript... neither one is special-cased by the checker"* stops being true the moment the escape check
below exists.

```trb fragment
fn readConfig(path: Path): Result<Config, IoError> {
  using file = File.open(path)?
  const text = file.readText()?
  Config.parse text
}
```

**The checker rejects three shapes**, all of them cases where `file`'s release could no longer happen at the end of
this block because something kept it alive past it:

```trb fragment
type Holder {
  var file: File
}

fn keep(var holder: Holder, path: Path): Result<Void, IoError> {
  using file = File.open(path)?
  holder.file = file
}
```

```text
error: `file` is bound by `using`, so it cannot be stored in a field
  --> src/main.trb:7:15
   |
 7 |   holder.file = file
   |                 ^^^^
   = `file` closes at the end of this block. Store the value itself only if this type does not need `Close`,
     or restructure so the field's owner opens the resource
```

*(Proposed; both fragments above are shapes today's checker accepts without comment, because `using` binds nothing
the checker tracks.)* A closure that captures `file` and is itself stored, returned, or spawned is refused the same
way, and a `return file` out of the function is refused for the same reason a `return` of the `var` parameter in a
fork-join region is refused today (`docs/CONCURRENCY.md` section 6, rule R1). None of this needs new machinery from
nothing: `docs/BACKEND.md` section 7's gap 14 already gives the checker a per-closure `ClosureKind.Local | .Escaping`
flag for a closure that captures a `var` reference, decided and accepted for exactly the reason this rule needs it —
*"the checker records per closure whether it escapes... conservatively, when it is written directly as an argument of
a call and is not stored by the callee."* `using` asks the same question of the name it binds.

**Without `using`, a value closes at its last holder, whenever that is** — section 2's second shape, unchanged and
still legal. `using` does not change what closes a value; it changes whether the release is guaranteed to happen at
a line a reader can point to.

## 5. Only a `shared type` may implement `Close`

**Identity in TorbScript is exactly `shared type`, and `Close` may only be declared by one.** Not a capsule — the
private-field, factory-and-accessors convention of [Data or capsule](language/types/data-or-capsule.md) is an
encapsulation discipline over an ordinary value, and `Path` is a capsule that is still copied like any other value on
`var b = a`. Identity is the narrower, already-named thing
[`docs/language/types/shared-types.md`](language/types/shared-types.md) rules 1, 4 and 5 give: *"Assigning a `shared
type` never copies it,"* `Equals`, `Hash` and `copy` are not generated for one, and `isSame` — which compares identity
and nothing else — exists for a `shared type` alone.

**A value is copied on assignment, whether it is a capsule or not, and that is what disqualifies it.** Section 1's
`Ticket` is a plain `type`, not a capsule, and the failure is the same failure a capsule would have: `var b = a` makes
two independent values, so closing one copy of a file handle leaves the other reporting `used: false` for a resource
that, if `used` stood for anything real, is already gone. Rust states the same exclusion as a language rule — `Drop`
and `Copy` cannot both be implemented by one type — for the identical reason: a type that is copied byte for byte and
a type whose destructor must run exactly once cannot be the same type.

**A resource has one thing in the world behind it** — a descriptor, a socket, a lock — **and that single thing behind
it is what identity means**, which is exactly what a `shared type` already models. [`traits.md` rule
17](language/traits/traits.md) already runs half of this restriction — *"a `shared type` can only implement a `shared
trait`"* — which keeps every value-typed value a value where a trait object is concerned. This decision is the
missing other half: a `shared trait` that is specifically `Close` may only be implemented by a `shared type`.

```text
error: `Close` may only be implemented by a `shared type`
  --> src/main.trb:1:16
   |
 1 | type Ticket with Close {
   |                  ^^^^^
   = `Ticket` is copied by `var b = a`, so two copies would give two closes of one resource, or one working on
     a resource the other already closed. Write `shared type Ticket` — see [Shared types](language/types/shared-types.md)
```

*(Proposed. Section 1's `Ticket` probe checks today.)*

**A value may still hold a `shared` field.** Nothing about this rule stops `type Request { connection: Connection }`
where `Connection` is a `shared type with Close` — that object is shared, counted, and closes at its own last holder
exactly as section 2 describes, wherever that holder turns out to be, including inside a copied value, because
copying `Request` copies the *binding* to `connection` and never the object itself (shared-types.md rule 1). What a
value may not do is declare `close()` of its own, because it has no identity for a destructor to be attached to.

## 6. `close()` returns `Void` and never fails

**`var fn close()` stays exactly the signature it is today: no `Result`, no `Task`.** A type whose graceful end can
fail, or must wait for one, offers `end()` beside it — `Sink.end(): Task<Result<Void, Failure>>`, which already
exists and is not a new member this document invents. **After `end()` returns, `close()` is a no-op**, the same
promise `docs/STREAMS.md` section 7 already states for a stream that ended on its own: *"`close()` after the stream
ended is allowed and does nothing. So a reader never has to know whether it read to the end."*

**The comparison is Rust's `File`.** Dropping a `File` closes the descriptor silently and cannot fail; a program that
needs to know whether the write actually reached disk calls `sync_all()` first and handles what it answers. TorbScript
draws the same line at the same place: `close()` is what the runtime can always do, unconditionally, on any thread,
during any release; `end()` is what the type's author can only promise conditionally, and asking for it is a call the
program writes and waits for.

**`docs/STREAMS.md` section 3 already gives the reason this split has to exist rather than one fallible ending**:
*"`Close` is what the language uses for a resource... it is synchronous and it cannot fail — so it can run on a path
that is already unwinding a failure. Finishing a stream can do neither... Collapsing them would mean either a
`close()` that can fail (and then `using` cannot use it) or a `finish()` that cannot (and then a failed flush is
lost)."* This document does not change that argument; it makes the split std/stream already lives by into the rule
every `Close` implementation follows.

## 7. A destructor never awaits, and cancellation is drop

**A destructor never awaits.** Rust has no stable `AsyncDrop`; Swift's `deinit` and Python's `__del__` cannot await
either, and this design does not try to be the first. `close()` stays synchronous, always, even on a value that lived
inside a task.

**Inside a task, `using` is where the wait happens.** `using name = expression` awaits `end()` at the end of its
scope, on the normal path and on a `?` that leaves through it — not `close()`, which still runs synchronously and
unconditionally right after:

```trb fragment
fn upload(items: Iterable<Bytes>, path: Path): Task<Result<Void, IoError>> {
  using sink = File.create(path).await()?
  for item in items {
    sink.add(item).await()?
  }
}
```

*(Proposed; `sink`'s `end()` is awaited when this function returns normally or through the `?` inside the loop, and
`close()` runs after it either way, as it always does.)*

**Cancellation is drop.** `docs/CONCURRENCY.md` section 1 already states why this costs nothing: *"A task is a
stackless state machine... Its frame is a record on the heap of the worker that runs it."* Section 8 already states
what a cancellation does to that frame: *"the frame is released exactly as a finished task's frame is released, every
live value in it goes with it"* — and, at the time that was written, *"there is no stack to unwind, no destructor to
run — the language has none... nothing to leak, because releasing the frame is the ordinary release."* Under this
design the last clause is still true and the middle one is not: releasing the frame is still the ordinary release,
and the ordinary release now runs `close()` on every counted value the frame held, synchronously, in the same pass
that already walks those fields. A cancelled task's `end()` is never awaited — there is no more task left to resume
once it answers — so a cancelled upload above leaves `sink` closed and its last bytes possibly unflushed, exactly the
abrupt half of section 6's two endings and none of the graceful half. This is consistent with, and does not soften,
*"every task is cancellable"* (`docs/CONCURRENCY.md` section 8): the two facts this document adds are that a
cancelled task's values are released the moment it stops, and that release now does the same thing it always would
have at the end of an ordinary scope.

## 8. The cost: nothing for a type without `Close`

**The per-type release function already exists.** `docs/BACKEND.md` 5.R1: *"`R_<layout>` and `D_<layout>` are
emitted per half... `retainStatement`/`releaseStatement` in `backend/c/body.trb`... the child lines of a helper are
the same two functions over `value->f_x`."* A type's `D_<layout>` already walks every counted field and releases it;
the destructor is one more static call inside that function, guarded by whether the type implements `Close`, emitted
once per layout and not once per release site. **A type without `Close` pays nothing**: its `D_<layout>` is
unchanged, because there is no call to add. This is the same "unused half gets no function at all" discipline
5.R1 already applies (*"An unused `static` function is a warning and `-Werror` is on, so a layout that is only ever
released gets a `D_` and no `R_`"*) extended by one more conditional branch inside a function the ownership pass was
always going to generate.

## 9. The one hole: cycles

**Two shared objects holding each other are never released and never closed**, exactly as in Swift, and no measure
below removes that fact — it narrows how often it is reached and makes it visible when it is. **`weak` is not the
answer**, and the order below is the order to reach for each one in.

**(a) Trees and graphs use handles, not references.** `docs/ECS.md` already writes the scene tree this way: a
`Parent` component is `type Parent { entity: Entity }` — a handle into the world, not a counted pointer to another
`shared type` — so a parent-child relationship in `std/ecs` or `std/scene` can never form a reference cycle, because
there is no reference in either direction to begin with. Rust's own answer to a tree with parent pointers is an arena
plus indices, not `Weak<T>`, and this is the same answer: a `SceneNode.parent`, when it is added, is an `Entity`, and
`Entity` is already a plain, uncounted id.

**(b) Stored callbacks are receiver closures.** The Swift leak everybody reaches eventually is `self.handler = { self.x
}` — a `shared type` holding a closure that captures the object that holds it. TorbScript's convention answers it at
the call site instead of at the capture: a callback a `shared type` stores takes its owner as a receiver,
`onClick: (self: Button) => Void`
(the shape `docs/language/configuration/receiver-closures.md` already gives a read-only closure), rather than
`onClick: () => Void` closing over `button` from outside. The closure then captures nothing of `Button` at all — the
button passes itself when it calls `onClick self` — so a `Button` that stores its own click handler never holds a
reference back to itself. `docs/explanation/why-one-member-namespace.md`'s `Button` example, which stores a plain
`onClick: () => Void` today, is the pattern that needs the receiver form once `Button` is a `shared type`; it costs
nothing while it is a value, because a value cannot hold a cycle regardless.

**(c) Leaks are visible.** They already are: `docs/BACKEND.md` 5.R1's leak gate — `torb_report_leaks`,
`TORB_REPORT_LEAKS=1`, and *"`native.rs` asserts `live blocks at exit: 0` for every gate program that does not
panic"* — counts live blocks at exit today. This document's one addition is that the exit report **names the type of
each block still alive**, in debug builds and in tests, so a cycle is a failing test with a type name in it rather
than a number with no shape. Every `std` test already runs under this gate; a cycle becomes visible the moment a test
for the type that has one is written, not months later as a slow leak in a long-running program.

**(d) `Weak<Target>` is a `std` type, decided only if (c) shows a need.** Not a keyword, not a capture list — a
reference that does not count, read as an `Option<Target>` that answers `None` once the target is released. It is
held back deliberately: Swift's own experience is that `weak` makes a leak fixable one relationship at a time but
prevents none of them, because nothing forces the field that should have been `weak` to be written that way. **There
is no cycle collector, and there will not be one** — section 2 already says why: a tracing pass over a plain-refcount
heap would make `close()`'s timing non-deterministic again, undoing the whole of this document at once.

## 10. Slices

**R1 — the destructor in the runtime.** `Close` becomes real: `semantics/checker` adds the restriction that only a
`shared type` may implement it (section 5) and rejects a direct call (section 3); `ir` marks a `Close`-implementing
layout so the ownership pass knows to add the call; `backend/c` emits the one static call inside `D_<layout>`
(section 8); `runtime/memory.c` needs no new field, because the count and the release path are the ones already
there. Gate: every existing conformance program that builds a `Close` type today still passes with the destructor
call added, and the live-block counter of section 9(c) is zero after each.

**R2 — the `using` escape check.** `using name = expression` becomes a seventh binding form next to the six
`control-structures.md` names (section 4); the checker extends the `ClosureKind.Local | .Escaping` tracking of
`docs/BACKEND.md` gap 14 to a name bound by `using`, rejecting a field store, a return, and capture by an escaping
closure. Gate: a checker test per rejected shape, with the diagnostic's exact text pinned, and every existing
`using resource { body => ... }` call site in `std` and the compiler still checks unchanged.

**R3 — `using` inside a task awaits `end()`, and cancellation drops.** The lowering of an asynchronous `using` block
calls `end()` and awaits it at the normal exit and at a `?` that leaves through the block (section 7), and the task's
cancellation path — already releasing a cancelled task's frame (`docs/CONCURRENCY.md` section 8) — runs `close()` on
every value in it without awaiting `end()` first. Gate: a task cancelled while a `using`-bound `Sink` is open leaves
it closed and never blocks on `end()`; a task that runs `using` to completion awaits `end()` exactly once; both are
conformance programs comparing the two backends byte for byte.

## 11. Open, for the owner

Nothing is open in this document. `weak`, a cycle collector, and calling `close()` from user code were all
considered in section 9 and section 3 and answered there, not left for later.
