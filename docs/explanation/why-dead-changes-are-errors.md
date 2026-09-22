---
title: Why a change that cannot be seen is an error
summary: A var that is changed and never read again, or the discarded result of a method that takes self, is a compile error rather than a lint, because with value semantics such a change is always a mistake and never a defensive copy.
kind: explanation
status: stable
order: 90
keywords:
  - dead change
  - discarded value
  - copy trap
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#var-paths-and-var-parameters
---

Most languages let a program write to a place and never look at it again, and call it merely wasteful. TorbScript
calls it a compile error. This page is the argument for treating a class of bug most linters merely warn about as one
the compiler refuses to accept.

## The decision

**A change that cannot have an effect is a compile error, at the top level of a script exactly as inside a
function.** Two shapes of it are caught:

- A `var` binding that is changed and never read afterwards.
- The discarded result of a method that reads its receiver and returns a value - the call ran for its result, and nothing
  used it.

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

## Why

**Because under value semantics a change nobody reads is never a defensive copy, it is always a mistake.** In a
language with references, writing to a place and not reading it back can still matter - something else might hold the
same reference and observe the change. Under [mutable value semantics](why-values-instead-of-references.md) nothing
is aliased, so if a change is not read through the same binding, it is not read at all, anywhere, ever. There is no
legitimate reason left for it, which is exactly what turns a lint into a compile error: a lint is for a pattern that is
often a mistake, a compile error is for one that always is.

**Because this is the same rule the copy trap needs to be catchable at all.** Taking an element out of a collection
copies it (`var first = counters[0]`), and changing the copy compiles under every rule that only looks at the
assignment itself. What makes the mistake visible is the read: `first.increment()` followed by nothing that reads
`first` again is a dead change, and the compiler reports it at the line where the value stopped mattering rather than
letting the program run and print the wrong number silently.

```trb check
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
```

This program is *not* rejected: `first` is read on the last line, so the change has an effect - on the copy. The
mistake is that the copy was the wrong thing to change, and the fix is to reach through the path
(`counters[0].increment()`), not to add a read.

**Because a discarded method result is the same mistake from the other direction.** A method that reads its receiver and
returns a value either produces information the caller asked for or it does not need to be called at all; discarding
its result silently is the same "wrote something nobody looks at" shape as an unread `var`. An expression statement
must have type `Void` or `Never` unless the call has a `var` receiver or a `var` argument - which is exactly the
condition under which a call can be effectful without returning anything. `parser.bump()` is fine as a statement
because `bump` is a `var fn`; `numbers.sorted { _ }` as a bare statement is not, because `sorted` changes nothing
and its whole point is the value it returns.

**Because an unreachable `match` arm is rejected for the identical reason.** Value semantics rule out the class of bug
where a later branch depends on state a hidden earlier branch already changed, so an arm nothing can reach is not a
defensive fallback either - it is dead code the same way a change nothing reads is a dead write. See
[Why every match is exhaustive](why-exhaustive-matches.md).

### What was rejected

- **A lint instead of a compile error.** Rejected because, unlike in a language with aliasing, there is no case where
  the pattern is intentional: a `var` that is never read again can never be observed through another binding, so
  warning instead of rejecting would keep a bug compiling for no benefit.
- **Requiring every call to use its result** (as an unconditional rule, without the `var`-receiver exception). Rejected
  because it would make `parser.bump()`, `list.add(1)` and every other mutating call that returns nothing but changes
  the receiver into ordinary, useful statements that a blanket rule would have no reason to reject - the rule is about
  values wasted, not about results ignored.

## Consequences

**The fix a dead change points at is almost always "reach through the path instead of taking a copy."**
`items[index].increment()`, `world.entities[id].health = 5`, and `items.update(index) { ... }` all change the original
in place; none of them introduce a `var` binding that would need to be read back.

**A `var` binding captured by a closure is the one exception, and it needs to be.** The closure shares the binding
instead of copying it, so the read that justifies a change to it can happen anywhere the call runs the closure - there
is no single line after which "never read again" could be decided. See
[Why values instead of references](why-values-instead-of-references.md#consequences) for why such a closure never
outlives the binding.

**A temporary is never a valid base for a change, which is a related but different error.** `iterator().next()` fails
before the dead-change rule is even relevant, because the change would be lost with the temporary it was made in - see
[Mutation and var paths](../language/types/var-paths.md) for the rule about the base of a path, as opposed to this
page's rule about what happens after the path is used.

## Related

- [Why values instead of references](why-values-instead-of-references.md) - the copy trap this rule catches.
- [Mutation and var paths](../language/types/var-paths.md) - what has to be a `var` path before this rule even
  applies.
- [Exclusivity](../language/types/exclusivity.md) - the other compiler check that comes from the same value model.
- [What a model trained on other languages gets wrong](mistakes-models-make.md) - the copy trap as a diagnostic, in
  full.
