---
title: Integers
summary: Eight sized integer types with fixed ranges on every platform, an unannotated literal is always Int64, and overflow is a compile error when it is written and a panic when it happens at runtime.
kind: reference
status: stable
order: 11
keywords:
  - Int8
  - UInt64
  - overflow
  - truncating division
  - Bits
source:
  - CONCEPT.md#built-in-types
  - std/number/src/lib.trb
---

An integer type always carries its width: `Int8` through `Int64` signed, `UInt8` through `UInt64` unsigned. There is
no platform-dependent "the native integer size" - the same program overflows at the same value on every platform.

## Example

```trb check
const count: Int = 5
const byte: UInt8 = 200
const smallest: Int8 = Int8.minimum
const largest: Int8 = Int8.maximum

print "{count} {byte} {smallest} {largest}"
```

## Syntax

```text
Int8 Int16 Int32 Int64                   signed, in increasing width
UInt8 UInt16 UInt32 UInt64                unsigned, in increasing width
Int UInt                                  aliases for the 64-bit width
Type.minimum  Type.maximum                the smallest and largest value of a type
```

## Rules

1. **`Int` is an alias for `Int64`, and `UInt` is an alias for `UInt64`.** Write `Int` for "a number" and the sized
   name when the width itself is the point - a binary format, FFI, memory layout.

2. **An integer literal without an expected type is `Int64`.** This is fixed and does not follow the `Int` alias, so
   a file that shadows `Int` still has `Int64` literals. See [Literals](../syntax/literals.md).

3. **Every integer type carries `minimum` and `maximum` as its own constants**, so a bound is named instead of
   repeated: `Int8.minimum` is `-128`, `Int8.maximum` is `127`, `UInt64.maximum` is `18446744073709551615`.

4. **A literal that does not fit its target type is a compile error at the literal**, not at some later use.

   ```trb error
   const small: Int8 = 300
   // error: `300` does not fit into `Int8`
   ```

5. **Overflow at runtime panics; overflow written directly in the source is caught where it is written.** Rule 4 is
   the compile-time half of this rule; the runtime half is the same panic every other bug produces (see
   [what a panic does](../../../CONCEPT.md#error-handling)).

6. **Integer division truncates toward zero, and the remainder takes the sign of the dividend.** `-7 / 2` is `-3`,
   `-7 % 2` is `-1`, so `a` is always `(a / b) * b + a % b`. Dividing by zero panics, and so does dividing the
   smallest value of a signed type by `-1` - an overflow like any other.

7. **There are no implicit numeric conversions.** Converting between two integer types, or between an integer and a
   `Float`, always goes through `From`, `TryFrom` or `.into()`.

   ```trb error
   const whole: Int64 = 5
   const asFloat: Float = whole
   // error: There are no implicit conversions
   ```

   ```trb check
   const whole: Int64 = 5
   const asFloat = Float.from(whole)
   print asFloat
   ```

8. **A widening conversion is `From` and never fails; a narrowing one is `TryFrom` and returns a `Result`.** Every
   signed type widens into a larger signed type, and every unsigned type up to `UInt32` widens into `Int64`.
   Narrowing (`Int64` into `Int8`, `Int64` into `Int32`, `Float64` into `Int64`) is `TryFrom<..., NumberRangeError>`.

   ```trb check
   const big: Int64 = 300
   const narrowed = Int8.tryFrom(big)
   match narrowed {
     Ok(value) => print value
     Fail(problem) => print problem
   }
   ```

9. **There are no bit operators; the integer types come `with Bits` instead.** `bitwiseAnd`, `bitwiseOr`,
   `bitwiseExclusiveOr`, `bitwiseNot`, `shiftedLeft(by:)` and `shiftedRight(by:)` replace `&`, `|`, `^`, `<<` and `>>`.
   A shift by a negative amount or by the width of the type or more panics.

   ```trb check
   const flags: UInt8 = 0xFF
   const masked = flags.bitwiseAnd 0x0F
   const shifted = flags.shiftedLeft(by: 2)
   print "{masked} {shifted}"
   ```

10. **`UInt64` alone has `addedWrapping` and `multipliedWrapping`, the only arithmetic that does not panic on
    overflow.** They exist for hash functions, which need the wrap; every other numeric type panics on overflow the
    way `+` always does.

## What this is not

**`&` is not bitwise AND on an integer.** `&` combines traits (`Show & Encode`) and appears nowhere in an expression
on numbers; writing it there is not a different operator, it is not an operator at all in that position.

```trb check
const flags: UInt8 = 0b1010
const masked = flags.bitwiseAnd 0b0110
print masked
```

```trb error
const flags: UInt8 = 0b1010
const masked = flags & 0b0110
// error: Expected the end of the statement, found `&`
```

**`^` is not written for exclusive-or, with or without a fallback meaning.** It is not an operator of the language at
all, unlike `&`, which at least means something in a type position.

```trb error
const flipped = 0b1010 ^ 0b0110
// error: There is no `^` operator
```

## Related

- [Built-in types](built-in-types.md) - the numeric types in the context of every other built-in type.
- [Floating-point numbers](floating-point.md) - `Float32`, `Float64`, and why `==` and `compare` disagree.
- [Literals](../syntax/literals.md) - the four ways to write an integer literal.
