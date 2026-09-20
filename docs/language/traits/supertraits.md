---
title: Supertraits
summary: A trait declared with a supertrait requires every implementing type to also implement that supertrait, and a default member can call the supertrait's members directly.
kind: reference
status: stable
order: 30
keywords:
  - supertrait
  - with
  - requirement
source:
  - CONCEPT.md#traits
  - std/core/src/compare.trb
---

A supertrait is a trait that comes bundled with another one: a type that implements `Compare` has to implement
`Equals` too, because `Compare` says so at its own declaration.

## Example

```trb check
trait Named {
  fn name(self): String
}

trait Aged {
  fn age(self): Int
}

trait Greeter with Named, Aged {
  fn greet(self): String {
    "Hello, {name()}. You are {age()}."
  }
}

type Person with Greeter {
  fn name(self): String {
    "Ada"
  }

  fn age(self): Int {
    36
  }
}

print Person().greet()
```

## Syntax

```text
trait <Name> with <Supertrait>, <Supertrait> { ... }
type <Name> with <Trait> { ... }        satisfies <Trait> and every one of its supertraits
```

## Rules

1. **`with` after a trait's own name lists its supertraits.** `trait Compare with Equals` says that every type with
   `Compare` also has `Equals`; `trait Greeter with Named, Aged` lists two.

2. **A type that implements a trait has to satisfy every supertrait too**, whether by a member it writes itself or one
   that is generated. `Person` above never writes `with Named` or `with Aged` - providing `name` and `age` is enough,
   because `Greeter` already requires them.

3. **A default member of a trait can call the members of its supertraits directly**, with no qualification. `greet`
   calls `name()` and `age()` even though `Greeter` itself requires neither; they come from `Named` and `Aged`.

4. **A missing supertrait member is reported against the trait that asked for it, not the one the type wrote
   `with`.** The message names both traits, so the fix is clear even when the supertrait was never written out.

## What this is not

**A supertrait is not satisfied by writing `with` alone.** The trait still needs every one of the supertrait's
members from somewhere - the declaration itself, an `extend`, or generation.

```trb check
type Money with Compare {
  cents: Int

  fn compare(self, other: Money): Ordering {
    if cents < other.cents { .Less } else if cents > other.cents { .Greater } else { .Equal }
  }
}
```

```trb error
trait Loud {
  fn shout(self): String
}

type Box with Compare {
  loud: Loud

  fn compare(self, other: Self): Ordering {
    .Equal
  }
}
// error: `Compare` requires `Equals`, and `Box` has no `equals`
```

`Money`'s `Equals` comes for free because every field of `Money` supports it; `Box` holds a `Loud`, which has no
`Equals` to derive from, so its `Compare` has nothing to stand on until `equals` is written by hand.

## Related

- [Traits](traits.md) - `with` at the declaration, and the naming rule for a single-method trait.
- [Coherence and blanket implementations](coherence.md) - the other rule a `with` or `extend` has to satisfy.
- [Declaring a type](../types/declaring-a-type.md) - when `Equals`, `Hash` and `Show` are generated.
