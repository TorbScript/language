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

`std/number` declares every numeric type, the traits their operators go through (`Numeric`, `Signed`, `Integer`, `Bits`, `Real`),
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

`Numeric` is the arithmetic and comparison every number has; `Signed` adds `absolute()` and the unary `-`. The bit
operators `&`, `|`, `^`, `~`, `<<` and `>>` are the members of `Bits` - `&` binds like `*`, `|` and `^` like `+`, a shift
between `*` and `**` - and the members stay the named form, so
masking and shifting are named methods on `Bits` instead: `value.bitwiseAnd(0xFF)`,
`seed.bitwiseExclusiveOr(byte).shiftedLeft(by: 5)`. The shift of a signed type is arithmetic (it keeps the sign), the
shift of an unsigned type is logical, and a shift by a negative amount or by the width of the type or more panics, like
every other operation that leaves its range.

```trb fragment
public trait Real with Signed, Power, Power<Int64> {
  static pi: Self
  static tau: Self
  static e: Self
  static epsilon: Self
  fn squareRoot(): Self
  fn sine(): Self
  fn cosine(): Self
  fn tangent(): Self
  fn arcSine(): Self
  fn arcCosine(): Self
  fn arcTangent(): Self
  fn arcTangentDivided(by: Self): Self
  fn exponential(): Self
  fn naturalLogarithm(): Self
  fn logarithm(base: Self): Self
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

`Real` is also the scalar that has a power by a value of its own kind and by an `Int` (`x ** 0.5`, `x ** 3`), an
`exponential()`, a `naturalLogarithm()` and a `logarithm(base:)` - so a body that is generic over its scalar writes
`(Scalar.e ** exponent)` or `value.logarithm(base: two)` exactly as a `Float64` body does, and a `Fixed` answers the
same bits on every machine.

`pi`, `tau`, `e` and `epsilon` are required constants: every implementor carries them under those names (`Float64.pi`,
`Fixed.tau`, `Float.e`), and a body that is generic over its scalar reads them through its parameter - `Scalar.pi` - where it
could not write the digits. `epsilon` is the gap between one and the next value the scalar holds: the machine epsilon
of a `Float64`, one part of a `Fixed`. The one such a body needs is `Scalar.one`, which every `Numeric` has, and
`halved` and `doubled` are the two pieces of arithmetic it would otherwise write with a literal.

### Integer

```trb fragment
public trait Integer with Numeric {
  static minimum: Self
  static maximum: Self

  fn addedChecked(other: Self): Self?
  fn subtractedChecked(other: Self): Self?
  fn multipliedChecked(other: Self): Self?
  fn dividedChecked(other: Self): Self?
  fn remainderChecked(other: Self): Self?
}
```

Every integer type, `Int8` to `UInt64`, is an `Integer`: a `Numeric` with a range, and the total twins of its
operators. `a.addedChecked(b)` is `a + b` where the sum fits the type and `None` where `+` would panic, and the same
for `-`, `*`, `/` and `%` - the division and the remainder answer `None` for a zero divisor and for the smallest value
of a signed type divided by `-1` as well. All five are defaults of the trait, written over the operators with a
comparison in front, so a body that is generic over `Value: Integer` has them too.

```trb run
print Int.maximum.multipliedChecked(2)
print(100.subtractedChecked(1) ?? 0)
// prints None
// prints 99
```

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
public native type Int8 with Signed, Hash, Bits, Power<Int64> {}
public native type Int16 with Signed, Hash, Bits, Power<Int64> {}
public native type Int32 with Signed, Hash, Bits, Power<Int64> {}
public native type Int64 with Signed, Hash, Bits, Power<Int64> {}

public native type UInt8 with Numeric, Hash, Bits, Power<Int64> {}
public native type UInt16 with Numeric, Hash, Bits, Power<Int64> {}
public native type UInt32 with Numeric, Hash, Bits, Power<Int64> {}
public native type UInt64 with Numeric, Hash, Bits, Power<Int64> {}

public native type Float32 with Signed {}
public native type Float64 with Signed {}

public type Int = Int64
public type UInt = UInt64
public type Float = Float64
```

Every type carries its width in its name. `Int8` through `UInt64` are `Hash` and `Integer`, and each has a `minimum` and
a `maximum` constant of its own type. `Int64` is the type of an integer literal and additionally has `parseDigits(text, radix:)` for
reading digits in another base (`Int.parseDigits("ff", radix: 16)`, no sign, no prefix, `_` allowed between digits) and
`addedWrapping`/`multipliedWrapping` on `UInt64`, the only arithmetic in the language that does not panic on overflow -
it exists for hash functions that need the wrap. `Float64` is the type of a decimal literal and additionally has `pi`,
`e`, `squareRoot()`, `floor()`, `ceiling()`, `round()`, `isNaN()` and `isCloseTo(other, tolerance:)`, because `==` on a
float is IEEE-754 (`nan != nan`, `0.0 == -0.0`) while `compare` is a total order (`nan` is above everything, `-0.0`
compares equal to `0.0`) - which is also why `Float32` and `Float64` are deliberately not `Hash`, so a float can never
be a `Map` key and the `nan` key does not exist. `Int`, `UInt` and `Float` are plain aliases for the default width,
nothing the language treats specially.

Both `Float32` and `Float64` also carry `nan`, `infinity` and `negativeInfinity`, and `isInfinite()` and `isFinite()`
alongside `isNaN()` (`Float64.nan`, `Float32.infinity.isInfinite()`). `nan` and `infinity` are named the way `pi` and
`e` are, but a constant expression that *computes* `nan` - `0.0 / 0.0` written anywhere else - stays a compile error at
that expression (Floating-point numbers (skill `torbscript-language`: `references/language/values-and-types/floating-point.md`), rule 7): `Float64.nan` and
`Float32.nan` are the only two declarations the constant evaluator lets fold to it, because that is what the name of
each of them already says. An infinity has no such exception to make - it is an ordinary IEEE-754 value like any
other, which is why `1.0 / 0.0` needs none to be a constant.

Every integer type is raised by an `Int`: `2 ** 10` is exact, a power that leaves the range of the type panics the way
`*` does (`arithmetic overflow in \`**\``), and so does a negative exponent, because its result is no whole number. A
`Float64` is raised by a `Float64` (C's `pow`) or by an `Int` (the same `pow`, which rounds once where repeated
squaring would round at every step), and never panics.

`Float32` is not a `Real` yet: it has no arithmetic that a compiled program runs - every operator of it is a planned
row of the manifest of natives - and no conversion down from a `Float64`, so no body could be run for it. `<math.h>`
has the `float` functions, and once `Float32` has arithmetic, `Real` is one `extend` over them.

```trb run
print(2 ** 10)
print(2.0 ** 0.5)
print Float.e.naturalLogarithm()
print((8.0).logarithm(base: 2.0))
// prints 1024
// prints 1.4142135623730951
// prints 1.0
// prints 3.0
```

### There is no std/math {#no-math}

`std/math` held `pi`, `e`, `power`, `exponential`, `naturalLog`, `logarithm` and the trigonometry as free functions
of `Float64`. It is gone, because every one of them was a second way to write something the number already has: the
trigonometry is `Real`'s (`angle.sine()`, `y.arcTangentDivided(by: x)` for the old `arcTangent2(y, x)`), `pi` and `e`
are `Float.pi` and `Float.e`, the power is the operator `**`, and `exponential()`, `naturalLogarithm()` and
`logarithm(base:)` became members of `Real`. As members they are generic: a body over `Scalar: Real` computes with a
`Fixed` exactly as with a `Float`, which free functions of `Float64` never allowed.

### Decimal {#decimal}

```trb fragment
public native type Decimal with Signed, Hash {}
```

Exact base-10 arithmetic: a decimal literal adapts to `Decimal` the same way it adapts to `Float64`
(`const price: Decimal = 19.99`). See Decimal (skill `torbscript-language`: `references/language/values-and-types/decimal.md`): the type checks today, but no
back end gives it a value yet.

### Conversions

Widening between integer types, and from an integer or `Float32` to `Float64`, is `From` and therefore infallible
(`Int64.from(byte)` for a `UInt8`). Narrowing - `Int64` down to `Int8` or `Int32`, or `Float64` to `Int64` - is
`TryFrom<Source, NumberRangeError>` instead. `Int64.from(character)` is the code point of a `Char`; the other direction
is `std/text`'s `Char.tryFrom`.

## Related

- Integers (skill `torbscript-language`: `references/language/values-and-types/integers.md`) - the widths, the defaults, and what overflow does.
- Floating-point numbers (skill `torbscript-language`: `references/language/values-and-types/floating-point.md`) - why `==` and `compare` disagree, and why
  floats are not `Hash`.
- Decimal (skill `torbscript-language`: `references/language/values-and-types/decimal.md`) - exact base-ten arithmetic, and that it is planned.
- [std/core](core.md) - `Add`, `Subtract`, `Negate` and the other operator traits `Numeric` and `Signed` build on.
- Operators are traits (skill `torbscript-language`: `references/language/traits/operators.md`) - `**`, its precedence, and why `^` is no operator.

