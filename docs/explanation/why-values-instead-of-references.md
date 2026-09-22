---
title: Why values instead of references
summary: Every type is a value and the binding decides about mutation, which removes every mutable-and-immutable type pair at the price of one local, lintable trap.
kind: explanation
status: stable
order: 10
keywords:
  - value semantics
  - mutable value semantics
  - copy trap
  - aliasing
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#values
  - CONCEPT.md#execution-model
---

Most languages answer two questions about a value: can the binding be changed, and can the thing be changed. TorbScript
answers one. This page is the argument for that, and for the one cost it has.

## The decision

**Every `type` is a value, and the binding decides whether it can be changed.** This is mutable value semantics, as in
Swift's structs and in Hylo.

- Assigning, passing and capturing a value is a copy. Two bindings never refer to the same value.
- `const` is deep: through a `const` binding nothing changes.
- Mutation needs a `var` path - a `var` binding, `var` parameter or a `var fn` receiver, then `var` fields all the way down.
- References are second-class: they exist only as a `var` parameter or a `var fn` receiver, for the duration of one call. They
  cannot be stored in a field, returned, or captured by a closure that escapes.
- Identity is the marked exception: a `shared type` is not copied when it is assigned.

## Why

**Because the alternative tripled every concept.** The design started with an immutable `type` plus a mutable reference
`var type`. That produced `List`, `MutableList` and `ListView`; builders next to the things they build; snapshots with
`toList()`; and "is this a `type` or a `var type`?" as the first question about every type anybody wrote. With one rule -
no `var` path, no mutation - all of that disappears. There is one `Point` and one `List`, and a `const` binding *is* the
immutable one.

**Because a change then happens exactly where it is written.** The hardest class of bug in a language with references is
a change that arrives through a name you were not looking at. That bug cannot be written here: nothing is aliased, so
nothing changes behind your back. The compiler does not need a borrow checker for that, and you do not need defensive
copies.

**Because it makes concurrency uninteresting.** A task gets copies, so there is nothing to race for. Data races are
impossible by construction rather than by discipline, and the only things that cross a task boundary and are *not* copied
are `Channel` and `Task`. A value that contains a shared object is confined to the task that created it, and the compiler
derives that without an annotation.

**Because references can stay second-class.** Since a reference only exists for the duration of a call, there are no
lifetimes, no borrow checker, no `'a`, and nothing can dangle. A mutable slice - the thing Rust needs `&mut [T]` and Go
needs slices for - is a `var` path to a range: `samples[0..100].sort { _ }`.

**Because the cost is not what it looks like.** "Always a copy" is what the *model* says; what the implementation does is
something else and is not observable. A small value like a `Point` is really copied. A `List`, a `Map` or a `String`
shares its storage until somebody writes to it, and then the writer copies - copy on write, implemented once, in the
native collections. A type built from those gets it for free. `var` parameters and `var` paths are passed as references,
and their "copy in, copy out" meaning is never executed literally.

### What was rejected

- **Immutable types plus a mutable reference type** (`var type`). Rejected for the reason above: three names for every
  concept, and a question to answer about every declaration.
- **Lifetimes and a borrow checker**, as in Rust. They buy first-class references, which the language does not need once
  mutation needs a `var` path and every value is copied. They cost a concept that every reader and every error message has
  to carry.
- **A `weak` reference, and a cycle collector.** Values cannot form cycles, and a closure that captures a `var` binding
  may not escape its scope, so only `shared type` objects can. There is no cycle collector for them either: trees and
  graphs hold handles instead of references, a stored callback takes its owner as a receiver, and a leaked cycle is
  reported by type at the end of a test ([the destructors design record](../DESTRUCTORS.md), section 9).
- **Destructors that run on any value.** A destructor exists, and only a `shared type` may have one: `close()`, run
  exactly once by the last release (planned, [the destructors design record](../DESTRUCTORS.md)). A value is copied on
  assignment and has no identity for a destructor to belong to.

## Consequences

**The copy trap is the price.** Taking an element out of a collection takes a copy, so the program below prints `1 0`:
the copy changed and the list did not.

```trb run
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
var first = counters[0]
first.increment()
print "{first.count} {counters[0].count}"
// prints 1 0
```

The rule that catches this is general: **a change that cannot have an effect is a compile error.** With value semantics
such a change is always a mistake and never a defensive line, so it is not a lint. A copy that is changed and then read
is not reported, because the change has an effect - on the copy. The fix is to reach through the path:

```trb check
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
counters[0].increment()
print counters[0].count
```

**A method that changes and a method that returns a copy are different words.** `sort` and `sorted`, `add` and `added`,
`remove` and `removed`. The verb is a `var fn`; the participle reads its receiver and answers a new value. The
participles are default members of the collection traits, so an implementation writes only the verbs.

**There are no getters and no defensive copies.** `private(var)` hands an outsider a `const` path to a field, and `const`
is deep, so `config.routes` can be read and iterated from outside while `config.routes.add(...)` is an error. What somebody
takes out of it is a copy anyway.

**Exclusivity replaces the borrow checker, and it is conservative.** While a `var` access to a path is running, the same
path cannot be accessed another way. The access of a call begins once all of its arguments have been evaluated, so
`items.removeAt(items.length() - 1)` is ordinary code; what is left is what really overlaps - two `var` accesses of the
same call, and a closure argument that reaches the path the call is changing. What the compiler cannot prove is an error,
and there is no check at runtime.

**One thing does escape.** A `var` binding captured by a closure is a shared box, reference counted like a `shared type`
object. It is the only sharing of a variable in the language, and it is exempt from the dead-change rule because the read
can be anywhere.

## Related

- [Bindings](../language/values-and-types/bindings.md) - the rules, with the diagnostics.
- [Declaring a type](../language/types/declaring-a-type.md) - `var` fields, `private(var)` and verbs.
- [Values and bindings](../guide/values-and-bindings.md) - the same material as a learning step.
- [Coming from Rust](coming-from-rust.md) - what to do instead of `&mut`.
- [What a model trained on other languages gets wrong](mistakes-models-make.md) - the copy trap as a diagnostic.
