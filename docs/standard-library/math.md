---
title: std/math
summary: The functions on Float that read as an operation rather than a method, under the math namespace import.
kind: package
status: stable
order: 100
keywords:
  - std/math
  - pi
  - power
  - logarithm
  - trigonometry
source:
  - std/math/src/lib.trb
---

`std/math` is the usual functions on `Float` that are not already methods, kept under a namespace import so that
`math.power(a, 2.0)` reads as a function of a value rather than one of `Float64`'s own operations. `Float64`'s own
methods (`squareRoot`, `floor`, `ceiling`, `round`, `absolute`, `min`, `max`, ...) cover what reads as "a number's own
operation"; what is here instead is exponentiation, logarithms, trigonometry, and the two constants everything else is
expressed in terms of. `std/math` is in the prelude under its own namespace.

## Import

```trb fragment
use * as math from "std/math"
```

```trb check
const sumOfSquares = math.power(3.0, 2.0) + math.power(4.0, 2.0)
print sumOfSquares.squareRoot()
```

## Declarations

### `pi`, `e`

```trb fragment
public const pi: Float64 = 3.141592653589793
public const e: Float64 = 2.718281828459045
```

The ratio of a circle's circumference to its diameter, and the base of the natural logarithm.

### `power`, `exponential`, `naturalLog`, `logarithm`

```trb fragment
public native fn power(base: Float64, exponent: Float64): Float64
public native fn exponential(value: Float64): Float64
public native fn naturalLog(value: Float64): Float64
public native fn logarithm(value: Float64, base: Float64): Float64
```

`power(base, exponent)` is `base` raised to `exponent`. `exponential(value)` is `e` raised to `value`. `naturalLog` is
the logarithm base `e`; `logarithm(value, base:)` is the logarithm of `value` in the given `base`
(`math.logarithm(8.0, base: 2.0)` is `3.0`).

### `sine`, `cosine`, `tangent`, `arcSine`, `arcCosine`, `arcTangent`, `arcTangent2`

```trb fragment
public native fn sine(value: Float64): Float64
public native fn cosine(value: Float64): Float64
public native fn tangent(value: Float64): Float64
public native fn arcSine(value: Float64): Float64
public native fn arcCosine(value: Float64): Float64
public native fn arcTangent(value: Float64): Float64
public native fn arcTangent2(y: Float64, x: Float64): Float64
```

`sine`, `cosine` and `tangent` take radians; `arcSine`, `arcCosine` and `arcTangent` answer radians. `arcTangent2(y, x)`
is `arcTangent(y / x)` using the sign of both arguments to pick the correct quadrant.

## Related

- [Floating-point numbers](../language/values-and-types/floating-point.md) - `Float64`'s own methods, next to the
  functions here.
- [The standard library](index.md) - the other packages.
