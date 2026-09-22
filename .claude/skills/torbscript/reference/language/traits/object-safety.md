---
title: Object safety
summary: A member that mentions Self in a parameter or its result, or that has no self, cannot be called on a trait-typed value, even though the trait stays a legal type.
kind: reference
status: stable
order: 90
keywords:
  - object safety
  - Self
  - trait-typed value
  - dynamic dispatch
source:
  - CONCEPT.md#traits
  - std/core/src/compare.trb
---

Object safety is checked at the call, not at the trait's declaration. A member that needs a second value of exactly
the same concrete type cannot be called once that type has been erased to a trait, but the trait itself stays a
perfectly legal type to hold, pass and store.

## Example

```trb check
type Money with Show, Hash {
  cents: Int

  fn hash(): Int {
    cents
  }
}

const items: List<Show & Hash> = [Money(1), Money(2)]
print items.length()

fn describe(item: Show & Hash): String {
  "{item} #{item.hash()}"
}

print describe(Money(3))
```

## Syntax

```text
fn <name>(other: Self): <Type>             unsafe: needs a second value of the caller's own concrete type
fn <name>(): Self                          unsafe: promises to return the caller's own concrete type
static fn <name>(<parameters>): <Type>     unsafe: `static`, so there is no value to dispatch on
fn <name>(...): <Type>                     safe: `Self` appears nowhere but in the receiver's own position
```

## Rules

1. **A member that mentions `Self` in a parameter or in its result cannot be called on a trait-typed value.**
   `Compare.min(other: Self): Self` takes and returns a second value of the caller's exact type, which a
   trait-typed value does not carry.

   ```trb error
   fn smaller(a: Compare, b: Compare): Compare {
     a.min(b)
   }
   // error: `min` cannot be called on a `Compare` value
   ```

2. **This rule applies to a default member exactly as it applies to a requirement.** `min` has a body in `Compare`
   (`if lessThan(other) { self } else { other }`), and it is still rejected on a `Compare`-typed value, because the
   body is not what is checked - the signature is.

3. **A `static` member has nothing to dispatch on, and is never called through a value.** `TryFrom.tryFrom`
   and `From.from` are `static` requirements; they are reached through the type's own name
   (`Money.tryFrom(text)`), never through a binding whose type happens to be the trait.

4. **The trait stays a legal type regardless.** `List<Show & Hash>` and `fn describe(item: Show & Hash)` compile and
   run: `Show.show` and `Hash.hash` both take only `self`, so both are safe, and nothing about `Compare`'s unsafe
   members stops `Show & Hash` from being used.

5. **Rejection happens at the call, so adding an unsafe member to a trait never breaks an existing trait-typed
   value.** `List<Compare>` is legal today even though every one of `Compare`'s members is unsafe; it has no
   callable member yet.

## What this is not

**Object safety is not a property of the trait, checked once at its declaration.** It is checked per call, against
the type the value is known as at that point - so the same member is fine on a concrete type and rejected on the
trait it was coerced to.

```trb check
type Money with Compare, Equals {
  cents: Int

  fn compare(other: Money): Ordering {
    if cents < other.cents { .Less } else if cents > other.cents { .Greater } else { .Equal }
  }

  fn equals(other: Money): Bool {
    cents == other.cents
  }
}

print Money(3).min(Money(5))
```

```trb error
type Money with Compare, Equals {
  cents: Int

  fn compare(other: Money): Ordering {
    if cents < other.cents { .Less } else if cents > other.cents { .Greater } else { .Equal }
  }

  fn equals(other: Money): Bool {
    cents == other.cents
  }
}

fn smaller(a: Compare, b: Compare): Compare {
  a.min(b)
}
// error: `min` cannot be called on a `Compare` value
```

`Money(3).min(Money(5))` type checks because both sides are known to be `Money`; the moment either one is only known
as `Compare`, `min` has nothing to promise its result is.

## Related

- [Trait intersections](intersections.md) - combining traits without changing which members are safe.
- [Traits as types](trait-types.md) - what coercing to a trait type does and does not change.
- [Witness tables](../generics/witnesses.md) - how a generic member, as opposed to a trait-typed value, still reaches every bound.

