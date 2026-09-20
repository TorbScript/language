---
title: Delegation with by
summary: by forwards a trait's required members to the one field of a single-field type, binding only to the trait or & group written directly in front of it.
kind: reference
status: stable
order: 60
keywords:
  - by
  - delegation
  - distinct type
  - wrapper
source:
  - CONCEPT.md#distinct-types-opaque-aliases
  - CONCEPT.md#traits
---

`by` forwards a trait to the one field of a type, so a wrapper around a single value does not have to write out
members that only ever call through to it.

## Example

```trb check
type Seconds with Show, Add & Subtract by value, Compare by value {
  value: Int
}

const total = Seconds(5) + Seconds(2)
print total
print total.max(Seconds(10))
```

## Syntax

```text
type <Name> with <Trait> by <field> { ... }
type <Name> with <Trait>, <Trait> & <Trait> by <field> { ... }   `by` binds to the group right before it
```

## Rules

1. **`by <field>` forwards the trait written directly in front of it to that field.** In `Add & Subtract by value`,
   `value` is the field that provides `add` and `subtract`.

2. **`by` binds to one element of the `with` list - the trait or the `&` group directly before it - never to the
   whole list.** `Show, Add & Subtract by value, Compare by value` derives `Show`, delegates `Add` and `Subtract` to
   `value` in one group, and delegates `Compare` to `value` on its own; nothing about `by` reaches back to `Show`.

   ```trb error
   trait Loud {
     fn shout(self): String
   }

   type Wrapper with Loud, Add by value {
     value: Int
   }
   // error: `Wrapper` implements `Loud` but has no `shout`
   ```

3. **`by` needs a type with exactly one field**, and the name after `by` names that field. A type with two fields has
   no single field `Add` could mean, and a type with cases has no field of its own to name at all.

4. **`by` forwards the required members; the default members still come from the trait.** `total.max(Seconds(10))` is
   `Compare.max`, a default that is written in terms of `compare` - the forwarded member - so it returns a `Seconds`
   and nothing has to be rewrapped by hand.

5. **A trait that is neither derived, delegated nor written by hand is not available on the wrapper.** `Seconds` has
   no `Multiply`, so `Seconds(5) * Seconds(2)` does not type check - which is the point, since multiplying two
   durations is not another duration.

## What this is not

**`by` is not a way to expose the field.** `value` still follows the usual field rules - `private value: Int` stays
unreadable from outside - `by` only wires the trait's members to it internally.

```trb check
type Email with Show by value {
  private value: String

  fn parse(text: String): Result<Email, String> {
    if !text.contains("@") {
      return Fail "'{text}' is not an email address"
    }
    Ok Self(text)
  }
}

const email = Email.parse "info@example.test"
print email
```

```trb error
type Email with Show by value {
  private value: String

  fn parse(text: String): Result<Email, String> {
    if !text.contains("@") {
      return Fail "'{text}' is not an email address"
    }
    Ok Self(text)
  }
}

const email = Email.parse("info@example.test").expect("bad email")
print email.value
// error: `value` is private to `Email`
```

## Related

- [Traits](traits.md) - `with` at the declaration, and where `by` sits inside a list of traits.
- [Coherence and blanket implementations](coherence.md) - what still applies to a delegated implementation.
