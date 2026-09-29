# Destructors, `close()` and `using`

**Status: implemented in the C back end** — decided by the owner and made precise on 2026-09-22 after the language
review. The checker half is in (2026-09-23, `compiler/src/semantics/checker/close.trb`): only a `shared type`
implements `Close` (section 5), no program calls `close()` (section 3), `self` does not escape `close()` (section 2a),
and `using name = expression` is a binding whose name does not escape its block, with `fn using` deleted from
`std/core` (section 4). The release half is in too (2026-09-23): the drop function of an object runs its `close()`,
bindings and temporaries are released at the ends of their scopes, fields and elements in reverse order, and a
cancelled task's frame the same way (section 10, slices R1 and R3), a temporary that only one path of its statement
made included, and a slice of a list of such objects copies. What is still open is listed there: the type names in the
leak report of section 9.

`close()` stops being a method somebody remembers to call and becomes the language's one destructor: the runtime's
own reference count triggers it, exactly once, the moment the last holder of a value goes away. This reopens a
decision this repository has recorded as settled three times over — [no-destructors.md](language/execution/destructors.md),
`docs/BACKEND.md` section 7's gap 10, and the drop-argument of `docs/design/CONCURRENCY.md` section 8 — and this document is
the record of why, and exactly what changes because of it.

```text
  a local that does not escape       a value that escapes             two shared objects holding each other
  (never stored, never captured      (stored in a field, captured
   by a closing closure)              by a closure that escapes)

  the scope ends                     the last holder releases it      neither refcount ever reaches zero
  → close() now, in reverse          → close() then, wherever         → section 9: handles instead of
    declaration order                  that turns out to be              references, and a visible leak
    (Rust's Drop)                      (Swift's deinit)                  instead of a silent one
```

- **[1. Today](#1-today)** — what `using` does now, what `Close` is, and that nothing forces a close
- **[2. `close()` is the destructor](#2-close-is-the-destructor)** — reference counting, one heap per worker, one call site
- **[2a. When exactly a release happens](#2a-when-exactly-a-release-happens)** — scope end in reverse declaration order, temporaries at the end of the statement, fields in reverse, no slice keeps `Close` storage
- **[3. `close()` cannot be called by user code](#3-close-cannot-be-called-by-user-code)** — Rust's E0040, and the states a direct call would create
- **[4. `using` binds a name that cannot escape its scope](#4-using-binds-a-name-that-cannot-escape-its-scope)** — the checker rejects a field, an escaping closure, a return
- **[5. Only a `shared type` may implement `Close`](#5-only-a-shared-type-may-implement-close)** — a value has no identity to close twice
- **[6. `close()` returns `Void` and never fails](#6-close-returns-void-and-never-fails)** — `end()` is where failure and waiting go
- **[7. A destructor never awaits, and cancellation is drop](#7-a-destructor-never-awaits-and-cancellation-is-drop)** — `using` inside a task, and what a cancelled task releases
- **[8. The cost: nothing for a type without `Close`](#8-the-cost-nothing-for-a-type-without-close)** — one static call inside a function that already exists
- **[9. The one hole: cycles](#9-the-one-hole-cycles)** — handles before `weak`, and a leak that is visible instead of silent
- **[10. Slices](#10-slices)** — R1 the runtime, R2 the escape check, R3 the task
- **[11. Open, for the owner](#11-open-for-the-owner)**

Section 1 is the record of the state this document started from: its three probes type checked on 2026-09-22 and
are refused since the checker half landed - the `using` call because the function is gone, the direct `close()` calls
by section 3, the `Ticket` by section 5.

Every snippet marked **type checks today** was run against the real compiler, in `tests/language/` where `std`
resolves, exactly as `docs/design/CONCURRENCY.md` and `docs/design/COLLECTIONS.md` probe theirs. The snippets were
written before any of it was implemented, so one that shows the destructor itself is a `trb fragment` and a diagnostic
marked as proposed is hand-written. The diagnostics of sections 3 and 5 are now the checker's own, and the one of
section 4 exists in the general form "`file` is bound by `using`, so it cannot be stored in a field", with its own note.

---

## 1. Today

**`using` is an ordinary function**, the third example of
[control structures are functions](language/extensibility/control-structures.md): its last parameter is a closure, so
a call reads like a keyword and is not one. The whole of `std/core/src/control.trb` is this:

```trb check
shared type Connection with Close {
  var open: Bool = true

  fn send(message: String) {
    print message
  }

  var fn close() {
    open = false
  }
}

using Connection() { connection => connection.send "hello" }
```

**type checks today**, and it is `no-destructors.md`'s own example. The function it calls is three lines and nothing
more — and it goes: section 4 replaces it with a binding form, and there is one `using`, not two:

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

`docs/design/CONCURRENCY.md` section 8 leans on the same promise for an unrelated question — whether dropping a `Task`
handle should cancel the task — and states it just as flatly: *"Anything else would make the moment a reference count
reaching zero observable, and CONCEPT says it is not."* Three documents, one sentence, and this one reopens it. (All
three have since been brought in line: `no-destructors.md` became [destructors.md](language/execution/destructors.md),
the page of the destructor, gap 10 is superseded, and CONCURRENCY section 8 argues from "the handle is not the frame"
instead.)

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
`docs/design/STREAMS.md` section 3 states the reason a `Sink` has both a `close()` and an `end()`: *"`Close` is what the
language uses for a resource (`using`, CONCEPT), it is synchronous and it cannot fail — so it can run on a path that
is already unwinding a failure. Ending a stream can do neither... Collapsing them would mean either a `close()` that
can fail (and then the runtime cannot run it on a release) or an `end()` that cannot (and then a failed flush is
lost)."*
Section 7 adds that *"`close()` after the stream ended is allowed and does nothing"* and that every derived `Sink`
and `Source` closes the one above or below it. `Sink<Item, Failure>` already carries `var fn end(): Task<Result<Void,
Failure>>` beside `Close`. Sections 5 and 6 below turn this one package's convention into the language's rule.

## 2. `close()` is the destructor

**Values are reference counted**, which is not new: `docs/BACKEND.md` 5.4 already inserts `Retain` and `Release`
around every `Boxed`, `Text`, `Runtime` and `Closure`-with-an-environment operand, and 5.R1 already emits one
`R_<layout>`/`D_<layout>` pair per counted layout, the retain and the release function a value's own fields are
walked through. **There is one heap per worker thread** — `docs/design/CONCURRENCY.md` section 1: *"A worker... owns a heap:
a bump allocator with size-class free lists, the blocks tagged with the owning heap's id"* — and **the counts are
plain integers**, never atomic, because *"a task never leaves the worker that started it."* **There is no tracing
collector, and there will not be one**: a plain refcount is the whole of the memory model, now and for as long as the
language exists, which is what makes `close()`'s timing a fact about the program rather than a fact about when a
collector last ran. The interpreter and the AOT backend share this runtime, so a release — and now a `close()` inside
one — fires at the identical point in both.

**`close()` runs exactly once, when the last reference to the value goes away.** Two shapes, and they are the two
halves of the diagram at the top of this document:

- **A local that does not escape is released at scope end.** This is Rust's `Drop`: the slot is released at the end
  of the block it was declared in — not at its last use, section 2a says why — and `close()` is one more call that
  release makes.
- **A value that escapes — stored in a field, captured by a closure that itself escapes — is closed by its last
  holder,** wherever in the program that holder's own count reaches zero. This is Swift's `deinit`: there is no fixed
  line in the source for it, because the value outlived the scope that created it. The slot it was bound to is still
  released at the end of its scope; that release is simply not the last one.

**Nothing can be forgotten, and that is the enforcement.** Today's `close()` is a method a program calls or does not;
tomorrow's `close()` is a side effect of a release the runtime was always going to perform, whether the program
mentions `Close` or not. `no-destructors.md`'s two probes — a `Connection` built and never passed to `using`, and a
`Connection` handed through `withConnection` — both still run today exactly as written, but `close()` now runs at the
end of each: the first at the end of the block that built it, the second when `withConnection`'s own `using` releases
it, which is the case that already worked.

## 2a. When exactly a release happens

"When the last reference goes away" is only a rule once every release has a place. These are the places, and each
one is a rule the lowering follows and the conformance suite pins by printing from `close()`.

**A slot whose type may contain a `Close` object is released at the end of its scope, in reverse declaration order.**
Not at its last use. Today the ownership pass releases every counted slot right after its last use (`docs/BACKEND.md`
2.2 and 5.4), and for a value without a destructor that is the right call: the release is invisible, and releasing
early frees memory early. For a value with a destructor it would make the output of a program depend on an
optimisation. Where `close()` prints, or flushes, or unlocks, relative to the program's other output would follow the
liveness pass — and, through the Owned/Borrowed summary a call records for its arguments, on the *body* of every callee
the value was passed to: changing whether a function keeps its argument would move another function's `close()`. The
end of the scope is a line a reader can point to, and reverse declaration order is the order in which anything built
on an earlier value is taken down before it — Rust's order for locals, for the same reason.

- **"May contain" is a derived fact, like `containsShared`.** A type may contain a `Close` object when it is a `shared
  type` that implements `Close`, when a field, a case payload or an element type may contain one, or when it is a
  trait-typed value, whose implementation the slot does not know — conservatively, unless no implementation of that
  trait in the program may contain one. A type parameter is no case of its own: the lowering asks the question per
  instance, where the parameter is a concrete type. The layout computes the fact to a fixpoint over the field graph,
  as it computes `containsShared` today.
- **Every other slot keeps last-use release.** A `List<Int>`, a `String`, a `Point` and every value with no `Close`
  inside are released where the liveness pass puts them now, so the optimisation stays for everything a destructor
  cannot observe — which is almost every slot of almost every program, and every slot of the compiler.
- **A parameter is a binding of the body's outermost scope.** One the caller keeps is borrowed and the body releases
  nothing; one the frame owns — every parameter of a task, and one the body stores on some path — is released where
  the body ends, at its end, a `return`, a `?` or the `stop` of the task. Until 2026-09-29 an owned parameter kept
  last-use release, and `TcpStream.receiveBuffer` closed the socket of a caller that had let go of it between the
  receive and the taking of its bytes (`tests/conformance/destructor-task-parameter.trb`).
- **A move leaves nothing behind.** A slot whose value was moved out — returned, stored into a field or a collection,
  handed to a callee that keeps it — is cleared by the move, and the release at the end of the scope finds nothing to
  release. The slot is its own drop flag; there is no second one.

**A temporary is released at the end of its statement.** A value that is produced and never bound —
`Connection().send "hello"`, `File.open(path)?.readAll()` — lives until the statement that made it has finished, and
is released there, temporaries in the reverse order of their creation. A temporary never outlives its line and never
dies in the middle of it.

**The fields of a released value are released in reverse declaration order.** When the count of a value reaches zero,
its own `close()` runs first (if its type has one), and then its fields are released, the last declared field first;
the elements of a collection are released from the last index to the first. A value is taken down in the reverse of
the order it was built, so a field that was built from another field is gone before the one it was built from.

**`self` cannot escape `close()`.** Inside `close()` the checker rejects storing `self` (in a field, a collection or
anything that outlives the call), returning it, and capturing it in a closure. `close()` runs because the count reached
zero; a `self` that got a new holder there would be an object that is being destroyed and is referenced at the same
time — Swift's resurrection bug, which Swift answers with a crash at run time and this language answers at the
declaration. `close()` may read and change the object's fields and call its other members, which is what closing is.

```text
error: `close` may not keep `self`: the object is being released
  --> src/main.trb:6:15
   |
 6 |     registry.append self
   |                     ^^^^
   = `close` runs because the last reference went away, so nothing may hold the object afterwards
```

*(Proposed.)*

**A temporary that only one path of its statement made ends there too.** The arm of an `if` expression, the fallback
of a `??`: the other path never made the temporary, and the end of the statement still releases it where it was made.
The slot is its own drop flag - the ownership pass clears it where the function begins and after an end inside a
loop, so a path that made nothing releases the zero value, which is nothing, and the next turn of a loop never sees what
the last one released (`Instruction.Clear`, `withClearedTemporaries` in `ir/ownership.trb`). The last expression of a block is a
statement as well: its temporaries end with it, and only the value it hands on to the block outlives it.

**No slice shares storage that holds `Close` objects.** A slice of a list shares the list's storage
(`docs/design/COLLECTIONS.md` 3.7); for an element type that may contain a `Close` object, slicing **copies** — the new list
retains exactly the elements it holds and nothing else. Shared storage would keep every element of the original alive,
and its `close()` deferred, for as long as any slice of it lives, so the moment an element closes would depend on the
lifetime of a slice that never mentioned it. The copy costs a retain per element and happens only for such element
types: a trait-typed `List` slices through the copying default of `std/collections`, and the native slice of a
concrete `ArrayList` - which the back end does not build yet, because its `Bounds` argument is not the runtime's
`from, to` - calls `torb_list_slice_copied` instead of `torb_list_slice` where the program's closing fact says the
element may hold one (`sliceSymbolOf` in `backend/c/body.trb`).

**A back end with a garbage collector still counts these types.** JavaScript and PHP (after the VM) could leave every
other value to the host's collector, but a type that may contain a `Close` object needs its reference count in every
back end, because the moment its `close()` runs is part of the program's meaning and a tracing collector does not give
one. The two sides are the same derived fact: counted where it may contain `Close`, the host's business everywhere
else.

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
second `open = false` racing nothing because there is only one thread per heap but two callers of the same method.
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
below exists. **There is one `using`, and it is this one:** the `using(resource) { … }` function of section 1 is
deleted from `std/core/src/control.trb` in slice R2, and its call sites become bindings. Two forms would be two answers
to "when does this close", and the binding form is the one that has a line.

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
fork-join region is refused today (`docs/design/CONCURRENCY.md` section 6, rule R1). None of this needs new machinery from
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
promise `docs/design/STREAMS.md` section 7 already states for a stream that ended on its own: *"`close()` after the stream
ended is allowed and does nothing. So a reader never has to know whether it read to the end."*

**The comparison is Rust's `File`.** Dropping a `File` closes the descriptor silently and cannot fail; a program that
needs to know whether the write actually reached disk calls `sync_all()` first and handles what it answers. TorbScript
draws the same line at the same place: `close()` is what the runtime can always do, unconditionally, on any thread,
during any release; `end()` is what the type's author can only promise conditionally, and asking for it is a call the
program writes and waits for.

**`docs/design/STREAMS.md` section 3 already gives the reason this split has to exist rather than one fallible ending**:
*"`Close` is what the language uses for a resource... it is synchronous and it cannot fail — so it can run on a path
that is already unwinding a failure. Ending a stream can do neither... Collapsing them would mean either a `close()`
that can fail (and then the runtime cannot run it on a release) or an `end()` that cannot (and then a failed flush is
lost)."* This document does not change that argument; it makes the split std/stream already lives by into the rule
every `Close` implementation follows.

## 7. A destructor never awaits, and cancellation is drop

**A destructor never awaits.** Rust has no stable `AsyncDrop`; Swift's `deinit` and Python's `__del__` cannot await
either, and this design does not try to be the first. `close()` stays synchronous, always, even on a value that lived
inside a task.

**`using` never awaits, not even inside a task.** The end of a `using` scope releases the value, and the release runs
`close()`, synchronously — nothing else. A graceful end that can wait or fail is a call the program writes where it
wants it, and awaits there:

```trb fragment
fn upload(items: Iterate<Bytes>, path: Path): Task<Result<Void, IoError>> {
  using sink = File.create(path)?
  for item in items {
    sink.add(item).await()?
  }
  sink.end().await()
}
```

*(Proposed.)* On the normal path `end()` is awaited by the last line and `close()` runs at the end of the scope, where
it does nothing because the sink has ended (section 6). On a `?` inside the loop the function leaves without `end()`:
`close()` runs, the sink is abandoned rather than finished, and the caller has the failure that caused it. An implicit
await at the end of a scope was the alternative and it is rejected: it would put a suspension point — and with it a
cancellation point and a place other tasks run — on a line where nothing is written, and it would have to decide on
its own what a failed `end()` on an already failing path means.

**Cancellation is drop — of the frame, not of the handle.** `docs/design/CONCURRENCY.md` section 1 already states why this
costs nothing: *"A task is a stackless state machine... Its frame is a record on the heap of the worker that runs it."*
Cancelling a task releases that frame at the task's next suspension point or cancellation check (section 8 there: the
compiler puts a check at every loop back-edge of a function that answers a `Task`, so a loop that never awaits is still
cancellable, and a synchronous callee runs to its end first — there is no unwinding). Releasing the frame is the
ordinary release, and the ordinary release now runs `close()` on every value the frame held that has one,
synchronously, slots in reverse declaration order exactly as at the end of a scope (section 2a). A cancelled task's
`end()` is never awaited — there is no more task left to resume once it answers — so a cancelled upload above leaves
`sink` closed and its last bytes possibly unflushed, exactly the abrupt half of section 6's two endings and none of the
graceful half. **Dropping the `Task` handle still cancels nothing** (`docs/design/CONCURRENCY.md` section 8, "Dropping a
`Task` still does not cancel it"): the handle is not the frame, and releasing the last handle releases nothing the task
holds. This is consistent with, and does not soften, *"every task is cancellable"*: the two facts this document adds
are that a cancelled task's values are released the moment it stops, and that release does the same thing it would
have done at the end of an ordinary scope.

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

**Values cannot form a cycle, and neither can a captured `var`.** A closure that captures a `var` binding may not
escape its scope (CONCEPT, "`var` Paths and `var` Parameters"; decided 2026-09-22, not yet enforced by the checker), so
the box of a captured `var` never outlives the scope that declared it and a recursive closure cannot hold itself alive.
State that has to outlive its scope is a `shared type`, which is where every cycle there can be lives — and where the
four measures below apply.

**(a) Trees and graphs use handles, not references.** `docs/design/ECS.md` already writes the scene tree this way: a
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

**R1 — the destructor in the runtime.** *Status: done in the C back end (2026-09-23), except the slice. The checker
half: sections 3, 5 and the `self` rule of 2a, with a test per rule in `compiler/tests/destructors.test.trb`. The
release half: the lowering writes an `EndOfScope` for every binding whose type may hold a `Close` object at every way
out of its block - the end, `return`, `?`, `break`, `continue`, the `stop` of a task - for the names of a pattern in
the block they are bound for, and for such a temporary at the end of its statement (`ir/lower/scope.trb`); which types
may hold one is a fixpoint over the whole program (`ir/closing.trb`); the ownership pass keeps an end as the last use
where the value is still owned, drops it where a move emptied the slot, keeps every end of a binding that is moved on
one path and not on another (the moves become retains there), and drops an end that some path reaches without the
binding (`resolvedScopeEnds` in `ir/ownership.trb`). The lowering asks for the `close()` of every object layout that
implements `Close` once everything else is lowered (`requestCloseFunctions`), and `emitDropHelper` in
`backend/c/emit.trb` calls it statically between `torb_closing_begin` and `torb_closing_end` of `runtime/memory.c`,
which lend the object a count while `close()` runs and panic where it kept one; a layout that may hold such an object
releases its fields last to first, and a list and a map release their elements last to first. The C of every program
of `benchmarks/` is byte for byte what it was. Every `close()` call `std` had was removed with the checker's rule
(`Pushing`, `Remapped`, `Buffered` of `sink.trb`; `Pulling`, `Stepping`, `Remapped`, `Staged`, `Produced`,
`Checked` of `source.trb`; `Body` of `std/http`; `File.write` of `std/fs`), and the release of the field makes each of
those closes now. A slice of a list whose elements may hold `Close` copies, and a temporary that only one path of its
statement made is released at the end of that statement through a cleared slot (section 2a). A `File` of `std/fs` is
an object the runtime owns (`RuntimeKind.FileHandle`, no layout of the program) and closes through its drop function
(`torb_file_drop`): a binding of one, and a value that holds one, is released at the end of its scope like any other
`Close` object (`ir/closing.trb`, `mayHoldClose` in `ir/lower/scope.trb`). Open: `Child` of `std/process`, which the
back end does not lower yet. Tests: `tests/conformance/destructor-*.trb`, the group "The ends of scope" of
`compiler/tests/ownership.test.trb`, and `runtime/tests/memory_test.c` and `list_test.c`.* `Close` becomes real: `semantics/checker` adds the restriction that only a
`shared type` may implement it (section 5), rejects a direct call (section 3), and rejects keeping `self` inside
`close()` (section 2a); `ir` computes "may contain `Close`" per layout to a fixpoint and marks a `Close`-implementing
layout so the ownership pass knows to add the call; the ownership pass releases the slots of such types at the end of
their scope in reverse declaration order instead of at their last use, releases temporaries at the end of their
statement, and clears a slot on a move; `D_<layout>` releases fields in reverse declaration order and a collection's
elements from the last to the first; a slice of a list whose element type may contain `Close` copies instead of
sharing; `backend/c` emits the one static call inside `D_<layout>` (section 8); `runtime/memory.c` needs no new field,
because the count and the release path are the ones already there. Gate: a conformance program per rule of section 2a
that prints from `close()` and pins the order against the program's other output (scope end, two locals in reverse,
a temporary at the end of its statement, fields in reverse, a moved slot, a slice); every existing conformance program
still passes; the live-block counter of section 9(c) is zero after each; and `ir --statistics` shows no change in the
number of releases for a program without a `Close` type.

**R2 — `using` is a binding, and the function goes.** *Status: done in the checker (2026-09-23). `using` is a
contextual keyword - a binding only in front of a name and an `=` - so the syntax change needed no second commit: the
seed never meets the form, because the compiler's sources do not use it. Refused: a field or collection store, a
`return`, a binding to another name, a constructor, a case, a `var fn` or a member of a shared object it is handed to,
a function value it is handed to, a closure that may outlive the block or that its callee keeps, and `using` at the
top level of a file. What a call answers is not followed: a function that returns its argument can still carry the
object out, which needs the Owned/Borrowed summary of the lowering.* `using name = expression` becomes a seventh binding form next to
the six `control-structures.md` names (section 4); the checker extends the `ClosureKind.Local | .Escaping` tracking of
`docs/BACKEND.md` gap 14 to a name bound by `using`, rejecting a field store, a return, and capture by an escaping
closure. Every `using resource { body => ... }` call site in `std`, the compiler, the examples and the docs becomes a
binding, and `fn using` is deleted from `std/core/src/control.trb` — a syntax change, so it takes the two commits of
`docs/RUST-EXIT.md` 4.2. Gate: a checker test per rejected shape, with the diagnostic's exact text pinned, and no
`using` call left in the repository.

**R3 — cancellation drops.** *Status: done in the C back end (2026-09-23): a stop path releases the frame's live slots
the last declared first (`slotsOf` in `ir/suspension.trb`), a `stop` ends every open scope first, and
`tests/conformance/destructor-cancelled-task.trb` pins a cancelled task closing what it held before its waiter hears
of it.* The task's cancellation path — releasing a cancelled task's frame at the next suspension
point or back-edge check (`docs/design/CONCURRENCY.md` section 8 and gap 13) — runs `close()` on every value in it through the
ordinary release, in the order of section 2a, and never calls or awaits `end()`. Gate: a task cancelled while a
`using`-bound `Sink` is open leaves it closed and never calls `end()`; a task that writes `sink.end().await()` and then
reaches the end of the scope calls `end()` once and a `close()` that does nothing; a task cancelled inside a loop with
no `await` stops at the next turn; all three are conformance programs whose output is compared byte for byte, in C and
later in the VM.

## 11. Open, for the owner

Nothing is open in this document. `weak`, a cycle collector, and calling `close()` from user code were all
considered in section 9 and section 3 and answered there, not left for later. The review of 2026-09-22 found the
first version underspecified — when exactly a release happens, what `self` may do in `close()`, whether `using` awaits,
what a slice keeps alive, how many `using` forms there are — and section 2a, section 4, section 7 and the slices of
section 10 carry the answers the lead decided.
