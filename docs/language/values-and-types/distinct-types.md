---
title: Distinct types
summary: A distinct type is an ordinary single-field type, and `by` forwards specific traits to that field so the wrapper costs no boilerplate - there is no separate opaque-alias feature.
kind: reference
status: stable
order: 22
keywords:
  - by
  - single field
  - opaque alias
  - delegation
source:
  - CONCEPT.md#distinct-types-opaque-aliases
---

> **Not built natively yet.** `by` delegation is not built by the native back end yet, so `torb run` refuses the
> examples here that use it. `torb check` accepts them, and the rules are the language's.

There is no separate concept for "a type that wraps another type but is not interchangeable with it" - that is a
`type` with one field. `by` forwards specific traits to that field, one decision per trait, so the wrapper costs
nothing to use.

## Example

```trb check
use Add, Compare from "std/core"

type Seconds with Show, Add by value, Compare by value {
  value: Int
}

const total = Seconds(5) + Seconds(2)
print total

const raw = total.value
print raw
```

## Syntax

```text
type Name with Trait, Trait & Trait by field, Trait by field { ... }
```

## Rules

1. **A distinct type is a `type` with a single field**, unlike [a type alias](type-aliases.md), which is
   interchangeable with its target. `Seconds` and `Int` above are never interchangeable: `Seconds(5) + 2` does not
   type check.

   ```trb error
   type Seconds with Add by value {
     value: Int
   }

   const total = Seconds(5) + 2
   // error: Expected `Seconds`, found `Int64`
   ```

2. **`by` binds to the one element of the `with` list directly in front of it**, which may be an `&` group, never to
   the whole list. A mixed line therefore says exactly what happens to each trait: `Show` above is derived the usual
   way, `Add` and `Compare` are each delegated to `value`.

3. **`by` needs a type with exactly one field, and the name after `by` names that field.** A type with more than one
   field has no single answer for what a delegated `Add` would even do.

   ```trb error
   type Money with Add by amount {
     amount: Int
     currency: String
   }
   // error: `by` needs a type with a single field: `Money` has `amount` and `currency`
   // error: `Money` implements `Add<Money, Money>` but has no `add`
   ```

4. **Where a forwarded signature mentions `Self`, arguments are unwrapped and results wrapped again.** `add(other:
   Self): Self` on `Int` becomes `add(other: Seconds): Seconds` on `Seconds`, so the caller never sees
   the underlying `Int`.

5. **A trait without `Self` in its signature can be delegated by a type of any field count**, because there is
   nothing to unwrap or rewrap. `Iterate<User>` delegated by a multi-field `Team` is this case.

6. **`by` forwards the required members; the default members still come from the trait.** A default written in terms
   of the required ones - `Compare.max` in terms of `compare` - stays correct without being forwarded itself, and
   returns the wrapper type, not the field's type.

7. **A trait that is neither derived, delegated nor written by hand does not exist on the type.** `Seconds * Seconds`
   does not compile unless `Multiply` is added to the `with` list, which is deliberate: multiplying two `Seconds`
   would be square seconds, a type this design does not want to produce silently.

8. **A single-field type has the representation of its field: no allocation, no indirection.** This follows from
   there being nothing else to store; it is not something a program can observe.

## What this is not

**A distinct type is not the same feature as a private field with a `parse` factory**, even though the two are often
combined. `by` forwards *existing* traits from the field; a private field with its own `parse` is how a type
enforces an invariant (a well-formed email address) rather than delegating arithmetic.

```trb check
use Show from "std/core"

type ParseError with Show {
  reason: String

  fn show(): String {
    reason
  }
}

type Email with Show by value, TryFrom<String, ParseError> {
  private value: String

  static fn tryFrom(text: String): Result<Email, ParseError> {
    if text.contains("@") {
      Ok Email(text)
    } else {
      Fail ParseError("missing @")
    }
  }
}

match Email.tryFrom("ada@example.test") {
  Ok(email) => print email
  Fail(problem) => print problem
}
```

**A type with cases cannot use `by` at all**, having no field of its own to name - `by` needs exactly one field, and
a case-bearing type has none outside its cases.

## Related

- [Type aliases](type-aliases.md) - the interchangeable form `by` deliberately is not.
- [Traits](../traits/traits.md) - what `with` declares in general.
- [Declaring a type](../types/declaring-a-type.md) - fields, visibility, and what every `type` generates.
