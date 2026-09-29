---
title: Decimal
summary: Decimal is designed for exact base-ten arithmetic such as money, and a decimal literal adapts to it the way it adapts to Float - but no back end implements it yet.
kind: reference
status: planned
order: 13
keywords:
  - Decimal
  - exact arithmetic
  - money
source:
  - CONCEPT.md#built-in-types
  - std/number/src/lib.trb
---

> **Planned.** This feature is designed but not implemented. Nothing on this page works today.

`Decimal` is the type for money and anything else where base-ten rounding has to be exact - `0.1 + 0.2` has to be
`0.3`, which no binary floating-point type can promise. The type is declared in `std/number`, and a decimal literal
adapts to it the same way an integer literal adapts to `Int8`; no back end gives it a value at runtime yet.

## Example

```trb check
const price: Decimal = 19.99
print price
```

## Syntax

```text
Decimal                                  the type
const price: Decimal = 19.99             a literal, adapted like any other numeric literal
```

## Rules

1. **`Decimal` is declared `native type Decimal with Signed, Hash`, next to the other numeric types in
   `std/number`.** It has the same trait list a signed integer type has, so the same arithmetic and comparison
   operators are written the same way once it runs.

2. **A decimal literal adapts to `Decimal` the way any numeric literal adapts to its expected type, and this type
   checks today.** The example above passes `torb docs check`'s type checker; it is what happens after that which is
   missing.

3. **`Decimal` is `Hash`, unlike `Float32` and `Float64`.** Exact base-ten arithmetic has no `nan` and no `-0.0` to
   complicate equality, so a `Decimal` can be a `Map` key once the type has values to compare.

4. **No back end gives `Decimal` a value.** `Decimal` type-checks like any other numeric type; what is missing is a
   back end that can run it, not a diagnostic. No compile error is planned for using it.

## What this is not

**`Decimal` is not `Float64` with more digits.** `Float64` is binary floating point and is not `Hash`; `Decimal` is
base-ten and is `Hash`, because it does not have the two IEEE-754 exceptions that make hashing a float unsound. See
[Floating-point numbers](floating-point.md) for what those exceptions are.

**`Decimal` is not usable today even though it type checks.** A page that only reads the `## Example` above could
conclude the type works; it does not, until a back end implements it.

## Related

- [Floating-point numbers](floating-point.md) - why `Float32`/`Float64` are not `Hash`, and `Decimal` is.
- [Integers](integers.md) - the sized integer types `Decimal` sits next to in `std/number`.
- [Built-in types](built-in-types.md) - `Decimal` in the context of every other numeric type.

