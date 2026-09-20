---
title: Why every match is exhaustive
summary: A public ADT is a promise about every case it has today, so a match must cover all of them and a new case is a breaking change, while a library that wants room to grow hides its ADT behind a type instead.
kind: explanation
status: stable
order: 160
keywords:
  - exhaustiveness
  - open type
  - breaking change
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#algebraic-data-types-and-pattern-matching
---

Rust's `match` needs a `_` arm the moment an enum might grow a case somewhere else in the dependency tree; Kotlin's
`when` over a `sealed class` does not, until the class stops being sealed. TorbScript never gives a public type that
choice, and this page argues for why closing it is worth the migration cost it creates.

## The decision

**`match` is an expression and must be exhaustive, on every type, with no way to declare a type "open."** There are no
non-exhaustive types.

- Every case of the subject's type has to be covered, by a case pattern, a binding, or the wildcard `_`.
- An arm that can never be reached is a compile error, for the same reason a dead change is: with value semantics it
  is always a mistake.
- A public ADT is a promise: adding a case to it later is a breaking change, and the compiler points at every `match`
  that needs a new arm.

```trb check
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
  case Empty
}

fn area(shape: Shape): Float {
  match shape {
    .Circle(radius) => Float.pi * radius * radius
    .Rectangle(width, height) => width * height
    .Empty => 0.0
  }
}

print area(Shape.Circle(2.0))
```

## Why

**Because a `_` arm on every `match` of a growing enum is worse than the migration it avoids.** In a language with
non-exhaustive matching by default, adding a case to a widely used enum compiles silently everywhere, and the new case
falls into whatever the nearest `_` arm does - often the wrong thing, discovered at runtime rather than at the point a
maintainer added the case. Exhaustive matching turns that into a compile error at every call site that needs to know
about the new case, which is strictly more information, delivered at the moment it is cheapest to act on: the
declaration of the new case itself.

**Because "the type of this value is a closed, known set of shapes" and "this API might grow" are two different
claims, and the language lets an author make only the one they mean.** Rust and Swift's `enum` conflates them: every
enum is exhaustively matchable, so a library that wants to add cases later either accepts breaking every consumer's
`match` or gives up exhaustiveness (Rust's `#[non_exhaustive]`) for the whole type at once. TorbScript instead moves
the growth option to a different construct entirely - a single-field wrapper type - so an ADT stays what it says it
is: closed.

```trb check
public type HttpError with Show {
  private kind: HttpErrorKind

  fn timeout(): HttpError {
    HttpError kind: .Timeout
  }

  fn isTimeout(self): Bool {
    match kind {
      .Timeout => true
      .ConnectionRefused => false
    }
  }
}

type HttpErrorKind {
  case Timeout
  case ConnectionRefused
}

print HttpError.timeout().isTimeout()
```

**Because an unreachable arm is the same mistake as a dead change, from the other direction.** [Why a change that
cannot be seen is an error](why-dead-changes-are-errors.md) rejects a write nothing reads; an unreachable `match` arm
is a read that nothing can reach, guarded by a condition value semantics rule out - an arm after a wildcard, or a
literal pattern that repeats one already covered. Both are rejected as errors rather than warnings, because with
value semantics there is no scenario under which either one is intentional.

**Because the rule that decides what a pattern name means also has to decide exhaustiveness, and coupling them to the
expected type would break both.** [Why a case is never bare](why-cases-are-never-bare.md) settles what an uppercase
name in a pattern means from the `use` list alone, never from what type is expected; exhaustiveness checking depends
on that same settled meaning - the compiler has to know which case a pattern names before it can know whether every
case was covered, and it cannot know that from an expected type that has not been decided yet either.

### What was rejected

- **`#[non_exhaustive]`-style open enums** (Rust), which need a `_` arm in every external `match` from the moment the
  attribute is added, defeating exhaustiveness for the whole type rather than for the API surface that actually needs
  to grow.
- **Sealed classes that can be un-sealed later** (Kotlin), which keep exhaustiveness only until the first time
  someone wants to grow the hierarchy, at which point every existing `when` breaks or needs a default anyway - the
  same migration cost as adding a case to a closed TorbScript ADT, but paid at an unpredictable time instead of by
  design from the start.

## Consequences

**A public ADT is closed by default, and staying free to add cases means not exposing the ADT.** A library wraps its
variants in a type with a private field, as `HttpError` does above, and hands out predicates and factories instead of
the raw cases - adding a third case to `HttpErrorKind` later only means updating the `match` arms inside `HttpError`
itself, never every caller's.

**Forgetting a case is a compile error, named at the `match` that needs it, never a silently wrong answer at
runtime.**

```trb error
type Shape {
  case Circle(radius: Float)
  case Empty
}

fn area(shape: Shape): Float {
  match shape {
    .Circle(radius) => Float.pi * radius * radius
  }
}
// error: `match` does not handle `.Empty`
```

**An arm that repeats coverage already given is rejected instead of silently doing nothing.**

```trb error
fn describe(value: Bool): String {
  match value {
    true => "yes"
    false => "no"
    true => "also yes"
  }
}
// error: This arm is never reached
```

## Related

- [Exhaustiveness](../language/pattern-matching/exhaustiveness.md) - the full rule, with every corner case.
- [Cases and match](../language/pattern-matching/cases-and-match.md) - the syntax a `match` arm and a case pattern
  use.
- [Why a case is never bare](why-cases-are-never-bare.md) - the rule that decides what a pattern name means, which
  exhaustiveness depends on.
- [Why a change that cannot be seen is an error](why-dead-changes-are-errors.md) - the same "this can never be
  intentional" argument, applied to writes instead of reads.
