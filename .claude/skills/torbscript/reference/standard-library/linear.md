---
title: std/linear
summary: Vectors, matrices, quaternions and angles over one generic scalar, plus Fixed, the fixed-point scalar whose answers are the same bits everywhere.
kind: package
status: stable
order: 105
keywords:
  - std/linear
  - Vector2
  - Matrix4
  - Quaternion
  - Angle
  - Fixed
source:
  - std/linear/src/lib.trb
  - docs/design/LINEAR.md
---

`std/linear` is the vector arithmetic every geometric package builds on: `Vector2`, `Vector3`, `Vector4`, `Matrix2`,
`Matrix3`, `Matrix4`, `Quaternion`, `Angle`, and `Fixed`, the deterministic scalar. There is **one** vector type per
width and not two - `Vector2` is the float vector, `Vector2<Int>` the pixel or tile vector, `Vector2<Fixed>` the
deterministic one - and what each of them can do follows from the scalar's bound: arithmetic under `Numeric`, a sign
under `Signed`, and everything that needs a root or an angle under `Real` (`std/number`). It is a pure value library and
knows nothing about entities, rendering or files.

## Import

```trb fragment
use Vector2, Vector3, Vector4, Angle from "std/linear"
use Matrix2, Matrix3, Matrix4, Quaternion, Fixed from "std/linear"
```

```trb check
use Vector2 from "std/linear"

const step = Vector2 3.0, 4.0
const tile = Vector2 3, 4
print "{step.length()} {step.normalized()} {tile.manhattanLength()}"
```

## Declarations

### `Vector2`, `Vector3`, `Vector4`

```trb fragment
public type Vector2<Scalar: Numeric = Float> with Add, Subtract, Multiply<Scalar>, Divide<Scalar>
public type Vector3<Scalar: Numeric = Float> with Add, Subtract, Multiply<Scalar>, Divide<Scalar>
public type Vector4<Scalar: Numeric = Float> with Add, Subtract, Multiply<Scalar>, Divide<Scalar>
```

A point, a direction or a size, over whatever scalar the program counts in. `Vector2(1, 2)` is a `Vector2<Int>` and
`Vector2(1.0, 2.0)` is a `Vector2<Float>`: the literals decide, and the two do not mix - `toFloat()`, `toFixed()`,
`rounded()`, `floored()` and `ceiling()` cross the boundary on purpose.

Under `Numeric`: `add`, `subtract`, `multiply` and `divide` by a scalar, `dot`, `lengthSquared`, `cross` (a number in the
plane, a vector in space), `scaled(by:)` and `divided(by:)` component by component, `min`, `max`, `clamped`, `filled`,
`isZero`, `withX`/`withY`/`withZ`, `sum`, `largestComponent`, `smallestComponent`. Under `Signed`: `negate`, `absolute`,
`manhattanLength`, `manhattanDistanceTo`, and `perpendicular` in the plane - a quarter turn that is exact for integers
and needs no trigonometry. Under `Real`: `length`, `distanceTo`, `normalized`, `withLength`, `angle`, `angleTo`,
`rotated`, `rotatedAround`, `interpolated`, `projectedOnto`, `reflected`, `isCloseTo`.

Nothing here knows which way is up. `y` is the second component and that is all it is.

### `Angle`

```trb fragment
public type Angle<Scalar: Real = Float> with Add, Subtract, Negate, Multiply<Scalar>, Compare
```

A rotation, held in radians and constructed by the unit it is written in: `Angle(1.5)` is radians - the field says so -
and `Angle.degrees 90.0` is the other unit, spelled out. That wrapper is the whole point: a quarter turn written as `90`
can never be read as `90` radians. `sine`, `cosine`, `tangent`, `toDegrees`, `halved` and `normalized` (the same rotation
in `(-pi, pi]`) round it out; `Angle<Float>.zero`, `.quarterTurn`, `.halfTurn` and `.fullTurn` are the four constants.

Angles grow from the first axis towards the second: a quarter turn takes `unitX` onto `unitY`. Whether a viewer calls
that clockwise depends on which way the program draws its second axis, and the library does not decide it.

### `Matrix2`, `Matrix3`, `Matrix4`

```trb fragment
public type Matrix2<Scalar: Numeric = Float> with Add, Subtract, Multiply
public type Matrix3<Scalar: Numeric = Float> with Add, Subtract, Multiply
public type Matrix4<Scalar: Numeric = Float> with Add, Subtract, Multiply
```

A transformation, held as the vectors its basis lands on: the fields **are** the columns (`xAxis`, `yAxis`, `zAxis`,
`wAxis`), so reading a matrix is reading two, three or four vectors and `matrix.xAxis` is `matrix.applied(to: unitX)`.
Vectors are columns and a transformation is applied on the left, so `a * b` is "`a` after `b`" and a chain reads right to
left as it does in mathematics.

`Matrix2` is the linear part in the plane, `Matrix3` is both the linear transformation of space and the affine
transformation of the plane (`affine`, `linearPart`, `translationPart`, `transformedPoint`, `transformedDirection`), and
`Matrix4` is the affine transformation of space. `transposed`, `determinant`, `at`, `column`, `row`, `scaling` and
`applied(to:)` are everywhere; `rotation` (`rotationAroundX`/`Y`/`Z` for `Matrix3`), `inverse` and `isCloseTo` need a
`Real` scalar. `Matrix4` answers both a general `inverse` - the adjugate over the determinant, for a matrix of any
shape - and `inverseAffine`, the short way for the matrices a scene graph is made of.

**The matrix-vector product is `applied(to:)` and not `*`**: a type has one namespace of members, and `multiply` is
already the composition of two matrices.

There is no projection matrix here. A projection depends on the clip space of the API that consumes it, and this package
knows nothing about an API.

### `Quaternion`

```trb fragment
public type Quaternion<Scalar: Real = Float> with Multiply, Negate
```

A rotation of space that composes and interpolates without shearing and without the gimbal lock three angles in a row
have. `Quaternion.rotation(around:by:)` builds one from an axis and an `Angle`, `applied(to:)` turns a point with it,
`multiply` composes, `conjugate` undoes, `toMatrix3` hands it to a shader, and `interpolated` walks between two.

### `Fixed`

```trb fragment
public type Fixed with Signed, Real, Hash, Show, TryFrom<String, NumberParseError>
```

A number held as a whole number of `1/65536` parts - the Q16.16 scalar - and the only `Real` whose answers are the same
bits on every platform and out of every back end. Every operation on it, the square root and the trigonometry included,
is integer arithmetic: the square root is Newton's method on integers, and `sine`, `cosine` and the arc functions are
CORDIC, sixteen rotations of an addition and a division by a power of two each.

That is what a lockstep simulation, a replay and a checksum over a world state need, and it is why `Vector2<Fixed>`,
`Rectangle<Fixed>` and every intersection test of `std/geometry` accept it in place of `Float` with no other change to
the code.

`Fixed.from(anInt)` is exact, `Fixed.tryFrom(text)` rounds to the nearest part, `Fixed.approximating(aFloat)` is the bridge
out of floating point, and `show` writes the exact decimal the value is - so `Fixed.tryFrom("0.1")` shows as
`0.100006103515625`, because that is the number.

## Related

- [std/geometry](geometry.md) - the shapes built out of these vectors.
- [std/number](number.md) - `Numeric`, `Signed` and `Real`, the three bounds this package layers along.
- [std/math](math.md) - the functions on `Float` that `Float64`'s `Real` is written over.
- [Bounds](../language/generics/bounds.md) - the conditional `extend` that decides which member exists for which scalar.

