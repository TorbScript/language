---
title: Witness tables
summary: A trait-typed value carries a witness table per trait it is known through, so a generic bound is satisfied by any trait the value's own traits require, even without knowing its concrete type.
kind: reference
status: stable
order: 50
keywords:
  - witness table
  - dynamic dispatch
  - monomorphization
  - supertrait
source:
  - CONCEPT.md#traits
  - std/core/src/error.trb
---

Calling a generic function normally compiles one copy of it per concrete type. A witness table is what makes the
same call work when the argument is only known through a trait, with the type behind it erased.

## Example

```trb check
fn describe<Value: Show>(value: Value): String {
  "{value}"
}

fn report(error: Error): String {
  describe error
}

type ConfigError with Error {
}

print report(ConfigError())
```

## Syntax

```text
fn <name><Item: <Bound>>(value: Item): ...    a generic bound, checked against whatever `Item` turns out to be
fn <name>(value: <Trait>): ...                a trait-typed parameter - `value` carries a witness table
```

## Rules

1. **A trait-typed value carries a witness table**: one function pointer per member of the trait it is known
   through. `error: Error` above never needs to know it is holding a `ConfigError` - the witness table already
   points at `ConfigError`'s members.

2. **A generic call passes one witness per bound, when the argument is trait-typed.** `describe error` fills
   `Value` with `Error` and passes `Error`'s witness table for `Show` along with it, instead of compiling a version
   of `describe` for `ConfigError`.

3. **A witness table satisfies a bound on any trait the value's own traits require**, not only the trait the value
   is spelled with. `error: Error` satisfies `Value: Show` above because `trait Error with Show` already requires
   every `Error` to carry a `Show` witness - `report` never writes `Show` anywhere.

   ```trb error
   fn describe<Value: Show>(value: Value): String {
     "{value}"
   }

   trait Loud {
     fn shout(): String
   }

   fn announce(entry: Loud): String {
     describe(entry)
   }
   // error: `Loud` does not implement `Show`
   ```

4. **`Result<Value, Failure>` is `Show` whenever both are, which is why `Result<Void, Error>` is `Show` without
   writing anything for it.** `extend<Value: Show, Failure: Show> Result<Value, Failure> with Show` and
   `trait Error with Show` compose: an error handled only as `Error` is still printable.

5. **Where no trait-typed value is involved, a back end monomorphizes as it always did.** `describe(5)` compiles a
   version of `describe` specialized to `Int`, with no witness table anywhere - witnesses exist only where a
   concrete type has already been erased to a trait.

## What this is not

**A witness table is not something you write or name.** There is no syntax for it; it exists because a value's type
is a trait, not because a declaration asks for one. Writing the trait bound is all a generic function ever does.

```trb check
fn describe<Value: Show>(value: Value): String {
  "{value}"
}

fn report(error: Error): String {
  describe error
}
```

```trb error
fn describe<Value: Show>(value: Value): String {
  "{value}"
}

fn report(error: Error): String {
  describe(error, Error)
}
// error: `describe` takes 1 argument, 2 were given
```

`describe` only ever takes the one value; there is no second, explicit argument for a witness to fill, and passing
the trait's own name does not mean what it might in a language with explicit dictionaries. The table travels with
`error` itself, invisibly.

## Related

- [Bounds](bounds.md) - what a bound checks; witnesses are how it is checked against a trait-typed value.
- [Object safety](../traits/object-safety.md) - the members no witness table can offer, whatever the bound asks.
- [Traits as types](../traits/trait-types.md) - what a value becomes once it is known only through a trait.

