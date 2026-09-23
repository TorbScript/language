---
title: Why traits instead of inheritance
summary: A type comes with a trait instead of extending a base class, so composition and delegation replace an inheritance hierarchy, and a value can still be typed by capability without carrying a class it did not ask for.
kind: explanation
status: stable
order: 140
keywords:
  - inheritance
  - composition
  - delegation
  - base class
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#traits
---

Class inheritance answers two questions with one mechanism: what data a type has, and what it can do. TorbScript
answers them separately - fields for the first, traits for the second - and this page argues for why splitting them
removes a whole family of design mistakes rather than merely renaming them.

## The decision

**A type comes with a trait; it does not extend a base type.** There is no `class`, no `extends`, and no shared state
inherited from a parent.

- A trait is a list of members a type provides, given at the declaration (`type Square with Shape`) or afterwards
  (`extend Point with Shape`).
- A single-field type can delegate a trait's required members to that field with `by`, instead of inheriting an
  implementation.
- `Show`, `Equals`, `Compare`, `Add` and the rest are traits, not a base class every value inherits from.

```trb check
trait Shape {
  fn area(): Float
}

type Square with Shape {
  side: Float

  fn area(): Float {
    side * side
  }
}

extend Square with Compare {
  fn compare(other: Square): Ordering {
    side.compare other.side
  }
}

fn describe(shape: Shape): String {
  "area {shape.area()}"
}

print describe(Square(side: 2.0))
```

## Why

**Because inheritance forces a choice - one parent - onto a problem that is often "this type does several unrelated
things."** A `Duck` that is both `Swimmable` and `Flyable` needs multiple inheritance, an interface plus a class, or a
mixin, depending on the language; a type with `with Show, Equals, Hash` needs none of those, because a trait is not a
place data lives and there is no diamond to resolve. `&` builds an intersection of traits as an ordinary type
(`Show & Encode`) exactly because traits, unlike base classes, are meant to be combined freely.

**Because inheriting an implementation couples a type to decisions its parent might change.** The fragile base class
problem - a subclass breaking because its parent's internals changed underneath it, even though the public interface
did not - only exists where a child reuses a parent's *implementation* by construction. Delegation (`with Add &
Subtract by value`) reuses an implementation the same way inheritance would, but explicitly and only for the traits
named: nothing is inherited that was not asked for, and nothing is coupled beyond the field the delegation names.

**Because a trait-typed value stays a value, not a pointer to something with an unknown, possibly larger shape.** A
class hierarchy needs a reference (or a `Box`) the moment a base-typed variable might hold a subclass, because the
subclass may be bigger; `fn describe(shape: Shape)` above takes a value that coerces into the trait, and the
receiver's actual size is a witness table lookup, not an allocation - see
[Why values instead of references](why-values-instead-of-references.md) for the value model this depends on and
[Witness tables](../language/generics/witnesses.md) for the mechanism.

**Because coherence gives every trait implementation exactly one home.** In a language with inheritance, "does this
type support `Show`-like behavior" can be answered by any file that subclasses it and overrides a method; here,
`extend X with Trait` is legal only where the package owns `X` or `Trait`, so exactly one implementation of a trait
exists for a type and there is nowhere else to look for a second one.

### What was rejected

- **Class inheritance with a single base type.** Rejected because it bundles "what data this has" with "what this can
  do" into one mechanism and one hierarchy, forcing types that need several unrelated capabilities into interfaces
  and mixins anyway.
- **Mixins with implicit field access into the type they are mixed into.** Rejected in favor of `by`, which names the
  exact field a trait's members are forwarded to, so nothing is inherited that is not visible in the line that grants
  it.
- **Uniform function call syntax** (`value.f(x)` meaning `f(value, x)` for any function `f`), which would have let any
  free function act like an inherited method. Rejected because it turns every function name into a possible member,
  which fights the one-namespace rule and the visibility rules of `extend` at once - see
  [Why a method is a constant](why-one-member-namespace.md).

## Consequences

**A capability that several unrelated types share is a trait, not a common ancestor.** `Iterate<Item>` is
implemented by `List`, `Set`, a `Range`, and anything else that can be pulled through a pipeline, none of which share
a base type or any data at all.

**Reusing an implementation is explicit, one field and one trait group at a time**, through `by` rather than through
an inherited method table:

```trb check
type Seconds with Show, Add & Subtract by value {
  value: Int
}

const total = Seconds(5) + Seconds(2)
print total
```

**A type can gain a trait after the fact, in its own package or in another one that owns the trait, without touching
the type's declaration.** `extend String { fn shout(): String { "{toUpperCase()}!" } }` adds a capability to a
type this package does not own, the way inheritance never could without wrapping it in a new subclass first.

## Related

- [Traits](../language/traits/traits.md) - the full syntax, naming rule and required members.
- [Delegation with by](../language/traits/delegation.md) - reusing an implementation without inheriting one.
- [Coherence and blanket implementations](../language/traits/coherence.md) - who may implement what, and why overlap
  is rejected.
- [Trait intersections](../language/traits/intersections.md) - `&`, the mechanism that replaces multiple inheritance.
- [Why a method is a constant](why-one-member-namespace.md) - the one-namespace rule that rules out an inheritance-like
  call syntax.
