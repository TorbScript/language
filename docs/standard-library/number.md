---
title: std/number
summary: Every numeric type of the language, the traits their arithmetic and bit operations go through, and Real.
kind: package
status: stable
order: 30
keywords:
  - std/number
  - Int
  - Float
  - Decimal
  - Bits
  - Real
  - overflow
source:
  - std/number/src/lib.trb
---

`std/number` declares every numeric type, the traits their operators go through (`Numeric`, `Signed`, `Bits`, `Real`),
and the two error types a numeric conversion fails with. Every name below is already in scope through the prelude.

## Import

```trb fragment
use Int8, Int16, Int32, Int64, UInt8, UInt16, UInt32, UInt64 from "std/number"
use Float32, Float64, Numeric, Signed, Bits from "std/number"
```

```trb check
const value: Int8 = 100
print value.absolute()
```

## Declarations

### Numeric, Signed, Bits, Real

```trb fragment
public trait Numeric
  with Add, Subtract, Multiply, Divide, Remainder, Equals, Compare, Show, TryFrom<String, NumberParseError> {}

public trait Signed
  with Numeric, Negate {
  fn absolute(): Self
}

public trait Bits {
  fn bitwiseAnd(other: Self): Self
  fn bitwiseOr(other: Self): Self
  fn bitwiseExclusiveOr(other: Self): Self
  fn bitwiseNot(): Self
  fn shiftedLeft(by: Int64): Self
  fn shiftedRight(by: Int64): Self
}
```

`Numeric` is the arithmetic and comparison every number has; `Signed` adds `absolute()` and the unary `-`. The language
has no bit operators - `|` is the union of literal types, and a method needs no precedence rule and no new token - so
masking and shifting are named methods on `Bits` instead: `value.bitwiseAnd(0xFF)`,
`seed.bitwiseExclusiveOr(byte).shiftedLeft(by: 5)`. The shift of a signed type is arithmetic (it keeps the sign), the
shift of an unsigned type is logical, and a shift by a negative amount or by the width of the type or more panics, like
every other operation that leaves its range.

```trb fragment
public trait Real with Signed {
  static pi: Self
  static tau: Self
  static epsilon: Self
  fn squareRoot(): Self
  fn sine(): Self
  fn cosine(): Self
  fn tangent(): Self
  fn arcSine(): Self
  fn arcCosine(): Self
  fn arcTangent(): Self
  fn arcTangentDivided(by: Self): Self
  fn floor(): Self
  fn ceiling(): Self
  fn round(): Self
  fn halved(): Self
  fn radiansOfDegrees(): Self
  fn degreesOfRadians(): Self
  fn doubled(): Self
}
```

`Real` is `Signed` plus everything that needs a root or an angle, and it is the bound a library that is generic over its
scalar separates its two halves along: `dot` and `lengthSquared` are arithmetic and need only `Numeric`, while `length`,
`normalized` and every rotation need a `Real`. Angles are in radians throughout, and `arcTangentDivided(by:)` is the
two-argument arc tangent that uses the sign of both to pick the quadrant - it takes a label because the order of the two
is the one thing everybody gets wrong.

Two implementors. `Float64` is the fast one and answers whatever the platform's mathematics library answers, which is not
the same bits on every machine. `Fixed` (`std/linear`) is the deterministic one: every operation on it, the square root
and the trigonometry included, is integer arithmetic, so a lockstep simulation and a replay run on it.

`pi`, `tau` and `epsilon` are required constants: every implementor carries them under those names (`Float64.pi`,
`Fixed.tau`), and a body that is generic over its scalar reads them through its parameter - `Scalar.pi` - where it
could not write the digits. `epsilon` is the gap between one and the next value the scalar holds: the machine epsilon
of a `Float64`, one part of a `Fixed`. The one such a body needs is `Scalar.one`, which every `Numeric` has, and
`halved` and `doubled` are the two pieces of arithmetic it would otherwise write with a literal.

### NumberParseError, NumberRangeError

```trb fragment
public type NumberParseError with Error {
  text: String
}

public type NumberRangeError with Error {
  message: String
}
```

What a conversion of a number fails with: `NumberParseError` where text is not a number, `NumberRangeError` where a
value does not fit the target width.

### The integer and floating-point types

```trb fragment
public native type Int8 with Signed, Hash, Bits {}
public native type Int16 with Signed, Hash, Bits {}
public native type Int32 with Signed, Hash, Bits {}
public native type Int64 with Signed, Hash, Bits {}

public native type UInt8 with Numeric, Hash, Bits {}
public native type UInt16 with Numeric, Hash, Bits {}
public native type UInt32 with Numeric, Hash, Bits {}
public native type UInt64 with Numeric, Hash, Bits {}

public native type Float32 with Signed {}
public native type Float64 with Signed {}

public type Int = Int64
public type UInt = UInt64
public type Float = Float64
```

Every type carries its width in its name. `Int8` through `UInt64` are `Hash` and each has a `minimum` and a `maximum`
constant of its own type. `Int64` is the type of an integer literal and additionally has `parseDigits(text, radix:)` for
reading digits in another base (`Int.parseDigits("ff", radix: 16)`, no sign, no prefix, `_` allowed between digits) and
`addedWrapping`/`multipliedWrapping` on `UInt64`, the only arithmetic in the language that does not panic on overflow -
it exists for hash functions that need the wrap. `Float64` is the type of a decimal literal and additionally has `pi`,
`e`, `squareRoot()`, `floor()`, `ceiling()`, `round()`, `isNaN()` and `isCloseTo(other, tolerance:)`, because `==` on a
float is IEEE-754 (`nan != nan`, `0.0 == -0.0`) while `compare` is a total order (`nan` is above everything, `-0.0`
compares equal to `0.0`) - which is also why `Float32` and `Float64` are deliberately not `Hash`, so a float can never
be a `Map` key and the `nan` key does not exist. `Int`, `UInt` and `Float` are plain aliases for the default width,
nothing the language treats specially.

### Decimal {#decimal}

```trb fragment
public native type Decimal with Signed, Hash {}
```

Exact base-10 arithmetic: a decimal literal adapts to `Decimal` the same way it adapts to `Float64`
(`const price: Decimal = 19.99`). See [Decimal](../language/values-and-types/decimal.md): the type checks today, but no
back end gives it a value yet.

### Conversions

Widening between integer types, and from an integer or `Float32` to `Float64`, is `From` and therefore infallible
(`Int64.from(byte)` for a `UInt8`). Narrowing - `Int64` down to `Int8` or `Int32`, or `Float64` to `Int64` - is
`TryFrom<Source, NumberRangeError>` instead. `Int64.from(character)` is the code point of a `Char`; the other direction
is `std/text`'s `Char.tryFrom`.

## Related

- [Integers](../language/values-and-types/integers.md) - the widths, the defaults, and what overflow does.
- [Floating-point numbers](../language/values-and-types/floating-point.md) - why `==` and `compare` disagree, and why
  floats are not `Hash`.
- [Decimal](../language/values-and-types/decimal.md) - exact base-ten arithmetic, and that it is planned.
- [std/core](core.md) - `Add`, `Subtract`, `Negate` and the other operator traits `Numeric` and `Signed` build on.
