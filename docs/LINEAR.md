# Linear Algebra and Geometry

One vector type per width, over whatever number the program counts in. This is the specification of `std/linear` and
`std/geometry`, of the scalar tower they stand on, and of the rule that decides which member exists for which scalar.
Nothing here is a new feature of the language: the types are ordinary `type`s, the layering is ordinary conditional
`extend`, and the operators are the traits they already were.

```text
            Numeric            Signed              Real                      the bound
  + - * / % == <          unary -, absolute    squareRoot, sine, ...       what it adds
  ────────────────────────────────────────────────────────────────
  dot, lengthSquared      negate, absolute     length, normalized          Vector2, Vector3, Vector4
  cross, min, max         manhattanLength      angle, rotated, lerp
  Rectangle, Box          perpendicular        Circle, Sphere, Ray, Plane
  ────────────────────────────────────────────────────────────────
  Int, Int32, UInt, ...   Int, Int32, Float    Float64, Fixed              who carries it
```

- **[1. The scalar tower](#1-the-scalar-tower)** — `Numeric`, `Signed`, `Real`, and `Fixed`
- **[2. The types](#2-the-types)**
- **[3. Which member lives under which bound](#3-which-member-lives-under-which-bound)** — the table
- **[4. Naming](#4-naming)**
- **[5. Literals and inference](#5-literals-and-inference)**
- **[6. Operators](#6-operators)** — and the three products that are methods
- **[7. Conventions](#7-conventions)** — handedness, columns, angles, and what the library must not assume
- **[8. Numeric policy](#8-numeric-policy)** — overflow, comparison, determinism
- **[9. What is deliberately not in](#9-what-is-deliberately-not-in)**
- **[10. `Fixed`, the deterministic scalar](#10-fixed-the-deterministic-scalar)**
- **[11. Where this comes from](#11-where-this-comes-from)** — glam, nalgebra, cgmath, euclid, Godot, Unity, simd, GLM
- **[12. What the language and the compiler must provide](#12-what-the-language-and-the-compiler-must-provide)**
- **[13. The package cut for what follows](#13-the-package-cut-for-what-follows)**
- **[14. Open questions](#14-open-questions)**

---

## 1. The scalar tower

**There is one `Vector2`, and its scalar is a type parameter with a bound and a default.**

```trb fragment
public type Vector2<Scalar: Numeric = Float> with Add, Subtract, Multiply<Scalar>, Divide<Scalar>
```

`Vector2` is the float vector, `Vector2<Int>` the pixel or tile vector, `Vector2<Fixed>` the deterministic one. There is
no `Vector2i` and no `Vector2Int`: without macros a twin type is a hand-maintained copy, and an abbreviation in a name is
not a name.

What each instantiation *can do* is decided by three bounds, and the bounds are the library's whole structure:

| Bound | Declared in | What it adds | Who carries it |
|-------|-------------|--------------|----------------|
| `Numeric` | `std/number` | `+ - * / %`, `==`, `<`, `Show`, `TryFrom<String, _>` | every integer type, `Float32`, `Float64`, `Decimal`, `Fixed` |
| `Signed` | `std/number` | unary `-`, `absolute` | the signed integers, the floats, `Fixed` |
| `Real` | `std/number` (new) | `squareRoot`, the eight trigonometric functions, `floor`/`ceiling`/`round`, `halved`, `unit`, the two degree conversions | `Float64`, `Fixed` |

`Real` is the line this library is built along. Everything that is arithmetic alone — `dot`, `lengthSquared`, `cross`,
`min`, `max`, `clamped`, a Manhattan length, a rectangle intersection, a tile lookup — lives under `Numeric`, so a grid
vector has it. Everything that needs a root or an angle — `length`, `normalized`, `distanceTo`, `angle`, `rotated`,
interpolation, circles, matrices with rotations, quaternions — lives under `Real`, so a grid vector **cannot** reach it
and never silently rounds.

The full shape of `Real`:

```trb fragment
public trait Real with Signed {
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
  fn unit(): Self
  fn doubled(): Self { self + self }
}
```

Three of those need a word.

**`halved`, `unit` and `doubled` are not mathematics, they are a workaround, and they are named so that they read as
members rather than as a hack.** A body that is generic over its scalar cannot write a numeric literal at all: a literal
has a type, and inside `extend<Scalar: Real> Vector2<Scalar>` that type is `Scalar`, which no back end substitutes (see
[section 12](#12-what-the-language-and-the-compiler-must-provide)). So `size / 2` cannot be written, `1` cannot be
written, and the midpoint of a rectangle, the bottom row of a transformation matrix and a "full" interpolation factor all
need one of the two constants. `unit(self)` does not read `self`; it is the scalar type asked for its own one, through a
value of it, because a static member cannot be reached through a type parameter either. **Both disappear the day a back
end substitutes the type of a literal.** `zeroOf(value)` is the same thing for the zero, and it is a free function rather
than a trait member because `value - value` needs nothing a `Numeric` does not already promise.

**`radiansOfDegrees` and `degreesOfRadians` are members of the scalar and not functions of `std/linear`** for the same
reason: the conversion needs the value of pi *in this scalar's type*, and `180` cannot be written in a generic body
either. Each implementor carries its own exact factor, which for `Fixed` is a precomputed count of parts rather than a
division.

`Real` deliberately does **not** carry the constants `pi`, `tau` or `epsilon`. A trait cannot require a `const` at all
("A binding needs a value: there are no uninitialized bindings"), and requiring them as `static fn`s would clash with
the `const pi: Float64` that `Float64` already has and is the nicer spelling. They stay `const`s on each scalar —
`Float64.pi`, `Fixed.pi`, `Fixed.tau` — under exactly those names, so that a future trait constant can absorb them
without renaming anything. Where generic code needs pi, it takes `arcCosine` of minus one, which is pi by definition;
`std/geometry`'s `halfTurnOf` is that one line.

## 2. The types

`std/linear`:

| Type | Fields | Default scalar | Bound |
|------|--------|----------------|-------|
| `Fixed` | `parts: Int64` | — | carries `Real` |
| `Angle<Scalar>` | `radians` | `Float` | `Real` |
| `Vector2<Scalar>` | `x`, `y` | `Float` | `Numeric` |
| `Vector3<Scalar>` | `x`, `y`, `z` | `Float` | `Numeric` |
| `Vector4<Scalar>` | `x`, `y`, `z`, `w` | `Float` | `Numeric` |
| `Matrix2<Scalar>` | `xAxis`, `yAxis` (each a `Vector2`) | `Float` | `Numeric` |
| `Matrix3<Scalar>` | `xAxis`, `yAxis`, `zAxis` (each a `Vector3`) | `Float` | `Numeric` |
| `Matrix4<Scalar>` | `xAxis`, `yAxis`, `zAxis`, `wAxis` (each a `Vector4`) | `Float` | `Numeric` |
| `Quaternion<Scalar>` | `x`, `y`, `z`, `w` | `Float` | `Real` |

`std/geometry`:

| Type | Fields | Bound | Notes |
|------|--------|-------|-------|
| `Rectangle<Scalar>` | `origin`, `size` | `Numeric` | half-open |
| `Circle<Scalar>` | `center`, `radius` | `Real` | closed |
| `Segment2<Scalar>` | `from`, `to` | `Numeric` | both ends belong to it |
| `Ray2<Scalar>` | `origin`, `direction` | `Real` | hits answer a distance |
| `Triangle2<Scalar>` | `first`, `second`, `third` | `Numeric` | winding not fixed |
| `Polygon<Scalar>` | `corners: List<Vector2<Scalar>>` | `Numeric` | convex tests first |
| `Box<Scalar>` | `origin`, `size` | `Numeric` | half-open |
| `Sphere<Scalar>` | `center`, `radius` | `Real` | closed |
| `Ray3<Scalar>` | `origin`, `direction` | `Real` | hits answer a distance |
| `Plane<Scalar>` | `normal`, `distance` | `Real` | normal of length one |
| `Triangle3<Scalar>` | `first`, `second`, `third` | `Numeric` | `normal` needs `Real` |

**`Matrix2` is in.** It is the linear part of a 2D transformation — rotation, scale, shear, no translation — it is what
`Matrix3.affine` takes and `Matrix3.linearPart` answers, and it costs two vectors.

**A matrix stores its columns as vectors, not as a flat array.** `Array<Item, const Size: Int>` was the alternative and a
probe settled it: `Array` is implemented by **no back end** — the interpreter answers `Unknown name Array`, and the C back
end's manifest marks every member of it planned — so nothing that uses one runs today. Beyond that, columns as vectors is
the better design: a column *is* where a basis vector lands, so `matrix.xAxis` is the answer to a question a caller
actually asks, `matrix.applied(to: unitX)` and `matrix.xAxis` are the same value, and every column operation is vector
arithmetic that already exists. `at(row, column)` is there for the loop that wants it, and `column(index)` and
`row(index)` for the two directions.

## 3. Which member lives under which bound

Short form; the docblocks are the full list.

| Under `Numeric` | Under `Signed` | Under `Real` |
|-----------------|----------------|--------------|
| `add`, `subtract`, `multiply`, `divide` (by a scalar) | `negate` | `length`, `distanceTo`, `distanceSquaredTo` |
| `dot`, `lengthSquared`, `cross` | `absolute` | `normalized`, `withLength` |
| `scaled(by:)`, `divided(by:)` (component-wise) | `manhattanLength`, `manhattanDistanceTo` | `angle`, `angleTo`, `rotated`, `rotatedAround` |
| `min`, `max`, `clamped`, `withX`/`withY`/`withZ` | `perpendicular` (2D) | `interpolated`, `projectedOnto`, `reflected`, `isCloseTo` |
| `filled`, `isZero`, `sum`, `largestComponent` | | |
| `Matrix.transposed`, `determinant`, `multiply`, `applied(to:)`, `at`, `column`, `row`, `scaling` | `Matrix.negate` | `Matrix.rotation`, `inverse`, `affine`, `translation`, `transformedPoint`, `isCloseTo` |
| `Rectangle`/`Box`: `contains`, `encloses`, `intersects`, `intersection`, `combined`, `covering`, `translated`, `grown`, `closestPoint`, `bounds`, `area`/`volume`, `isEmpty` | `manhattanDistanceTo` | `center`, `centered`, `distanceTo`, `distanceSquaredTo` |
| `Segment2`: `step`, `sideOf`, `bounds`, `intersects` | `manhattanLength` | `length`, `direction`, `center`, `at`, `closestPoint`, `distanceTo`, `intersection` |
| `Triangle2`: `signedArea`, `bounds`, `edges`, `contains`, `translated` | `doubledArea` | `area`, `center`, `closestPoint`, `distanceTo` |
| `Polygon`: `edges`, `signedDoubledArea`, `bounds`, `isConvex`, `contains`, `containsConvex`, `intersectsConvex` | `doubledArea` | `area` |
| `Triangle3`: `firstEdge`, `secondEdge`, `doubledAreaVector`, `bounds` | | `normal`, `area`, `plane`, `center`, `weightsOf`, `contains` |
| — (the whole type needs `Real`) | | `Circle`, `Sphere`, `Ray2`, `Ray3`, `Plane`, `Quaternion`, `Angle` |

Two entries in that table are compromises and say so in their `# Open`:

- **`Matrix3.affine` and `Matrix4.translation` need `Real`**, although an affine transformation of a tile grid is
  ordinary whole-number arithmetic. Their bottom row ends in a one, and a generic body cannot write that literal. A
  `Matrix3<Int>` is built by writing its three columns out.
- **`Rectangle.center` needs `Real`** because it halves, and the midpoint of a whole-number rectangle is not a whole
  number. The library does not pick a rounding for a program.

## 4. Naming

- Full words. `squareRoot`, not `sqrt`. `lengthSquared`, not `lengthSq`. `arcTangentDivided(by:)`, not `atan2` — and it
  takes a label, because `y` and `x` in that order is the one signature everybody gets wrong.
- A method that answers a changed copy is a participle or a noun: `normalized`, `rotated`, `translated`, `grown`,
  `transposed`, `floored`, `rounded`, `clamped`, `interpolated`, `combined`, `covering`, `flipped`, `mirrored`.
  `perpendicular`, `determinant`, `bounds`, `center`, `area`, `ceiling` are nouns.
- A question is `is...`/`has...`: `isEmpty`, `isZero`, `isConvex`, `isInFront`, `isCloseTo`.
- A transition between scalar kinds is explicit and named after its target: `Vector2<Int>.toFloat()`,
  `Vector2<Int>.toFixed()`, `Vector2<Float>.rounded()`, `.floored()`, `.ceiling()`, `Fixed.toFloat()`, `Fixed.toInt()`,
  `Fixed.approximating(aFloat)`. There is no implicit conversion anywhere.
- `Segment2` carries the digit that `Ray2`/`Ray3` and `Triangle2`/`Triangle3` carry, because a segment of space has no
  word of its own the way `Rectangle`/`Box` and `Circle`/`Sphere` have one. A `Segment3` therefore lands beside it
  rather than renaming it.
- **A conversion is never given the same name in two instantiations of one type.** `toFloat` exists on
  `Vector2<Int>` and nowhere else, `toFixed` on `Vector2<Int>` and nowhere else, `rounded`/`floored`/`ceiling` on
  `Vector2<Float>` and nowhere else. The reason is the interpreter, which has no types and therefore cannot tell
  `extend Vector2<Int>` from `extend Vector2<Fixed>`; two `toFloat`s would make one of them silently answer the other's
  arithmetic. The four constants (`zero`, `one`, `unitX`, `unitY`) do share their names across instantiations, because a
  constant is worth it — and that is the one place where a program that runs on the interpreter has to write the
  components out instead.

## 5. Literals and inference

`Vector2(1, 2)` is a `Vector2<Int>`; `Vector2(1.0, 2.0)` is a `Vector2<Float>`. The literals decide, through ordinary
inference, and the two do not mix:

```trb fragment
const tile = Vector2(3, 4)          // Vector2<Int>
const step = Vector2(3.0, 4.0)      // Vector2<Float>
const exact = Vector2(Fixed.from(3), Fixed.from(4))
const crossed = tile.toFloat().add(step)
```

Three inference rules are worth knowing, and two of them are sharp edges:

1. **A literal adapts to a declared scalar.** `const size: Vector2<Float> = Vector2(1, 2)` works, because the annotation
   says `Float64` and `1` adapts to it.
2. **A constant of a concrete `extend` needs its scalar written out.** `Vector2<Float>.zero`, not `Vector2.zero` — the
   type parameter's *default* is not consulted for a member of an `extend` of one instantiation, and neither is the
   expected type. This is a gap and not a decision; see [section 12](#12-what-the-language-and-the-compiler-must-provide).
3. **A `const` binding of a generic result may need an annotation.** Inside `extend<Scalar: Real> Vector2<Scalar>`,
   `const size = self.length()` does not infer, and `const size: Scalar = self.length()` does. A result position infers
   fine; only a bare binding does not.

## 6. Operators

The operators are the traits they already are, and the vectors and matrices come `with` them:

| Written | Trait | On |
|---------|-------|-----|
| `a + b` | `Add` | vector + vector, matrix + matrix, angle + angle |
| `a - b` | `Subtract` | the same three |
| `-a` | `Negate` | a vector, a matrix or a quaternion of a **`Signed`** scalar, conditionally |
| `a * s` | `Multiply<Scalar>` | vector times scalar, angle times scalar |
| `a / s` | `Divide<Scalar>` | vector by scalar |
| `a * b` | `Multiply` | matrix times matrix, quaternion times quaternion — the composition |
| `a == b` | `Equals` | every type, generated from the fields |
| `a < b` | `Compare` | `Fixed` and `Angle` |

`Negate` is the pattern worth copying: it is **not** on the type, it is a conditional implementation
`extend<Scalar: Signed> Vector2<Scalar> with Negate`, because `Vector2<UInt>` has no unary minus and saying so in the
bound is better than a body that panics.

Three products are **methods and not operators**, each for a reason:

- **`matrix.applied(to: vector)`.** A type has one namespace of members, and `multiply` is already the composition of two
  matrices. Two `Multiply` implementations with different `Other` are legal as declarations and coherent, and neither the
  member lookup at a call nor the operator resolution finds the second one — so writing both would be an API nothing can
  reach. `applied(to:)` also reads better in a chain: `projection.multiply(view).applied(to: point)`.
- **`vector.scaled(by: otherVector)`** for the component-wise product, because `*` on two vectors means the dot product
  to half of the world and the component-wise product to the other half, and neither half is wrong.
- **`scalar * vector` does not exist.** It would be `extend Float64 with Multiply<Vector2<Float64>, Vector2<Float64>>`,
  and coherence forbids it: `std/linear` owns neither `Float64` nor `Multiply`. Write `vector * scalar`. This is the
  right answer rather than a limitation — the alternative is every numeric package reaching into `Float64`.

**In a compiled program the operator symbols on these types do not work yet**, because the back end does not lower an
operator whose implementation belongs to a generic type. The trait method is the same call and does work
(`a.add(b)`, `a.multiply(factor)`, `a.negate()`), the checker and the interpreter accept both, and the native gate
programs are written in the method form for exactly this reason. It is the first item of
[section 12](#12-what-the-language-and-the-compiler-must-provide).

## 7. Conventions

**Nothing here knows which way is up.** `y` is the second component and `z` is the third. A program that draws with `y`
growing downwards and one that draws with `y` growing upwards use the same vectors, the same rectangles and the same
rotations; only what a *viewer* sees differs. Concretely, the library never has a member called `up`, `down`, `left`,
`right`, `top` or `bottom`, and `Rectangle` says `minimum`/`maximum` and `origin`/`size` rather than `top`/`bottom`.

| Question | Answer | Why |
|----------|--------|-----|
| Handedness in 3D | **right-handed**: `unitX.cross(unitY) == unitZ` | the convention of mathematics, of OpenGL's classic pipeline and of glam/nalgebra/cgmath; a left-handed world is one negated axis away |
| Vectors | **columns** | `matrix * vector`, so a composition reads right to left as it does in mathematics: `a * b` is "`a` after `b`" |
| Matrix storage | **the columns, each as a vector** | a column is where a basis vector lands; this is also what a graphics API means by "column-major", so a buffer upload is the fields in order |
| Angles | **radians**, always, wrapped in `Angle` | one unit inside, and the two places where the other unit appears are `Angle.degrees` and `toDegrees` |
| Rotation direction | from the first axis **towards the second**: a quarter turn takes `unitX` onto `unitY` | it is the only statement that does not need to know which way `y` points; whether a viewer calls it clockwise is the program's own business |
| Rectangles and boxes | **half-open**: the minimum edge is inside, the maximum edge is outside | so that a row of them tiles: `Rectangle(Vector2(0, 0), Vector2(8, 8))` and `Rectangle(Vector2(8, 0), Vector2(8, 8))` share no cell, and a hit test never answers twice |
| Circles and spheres | **closed**: the boundary is inside | they do not tile, so nothing is lost by counting a boundary twice, and a great deal is lost by leaving a touching point out of both |
| Bounding volumes | **closed hulls** | `bounds` and `covering` answer the box whose *corners* hold the shape, so a corner can lie exactly on a maximum edge and `contains` then answers `false` for it. A bound is a bound; the half-open rule is about tiling |
| Rays | a hit answers **how far along the ray**, not the point | that is what a caller compares to find the nearest of several hits; `ray.at(distance)` turns it back into a point |

## 8. Numeric policy

- **Overflow panics**, here as everywhere in the language. `lengthSquared` on a `Vector2<Int>` squares both components,
  so a pair of coordinates above about three billion leaves the range of an `Int` although the vector itself is ordinary.
  That is a bug in the program and the panic says so. `Fixed` multiplies before it scales back down, so its real limit is
  about 46341 and not `Fixed.largest`; both are in the docblocks.
- **Integer division truncates towards zero** and a division by zero panics, because that is what `Divide` says.
  `Rectangle<Int>` therefore has no `center`.
- **Floats are compared with a tolerance.** `Float64.isCloseTo(other, tolerance:)` already exists in `std/number` with a
  default of `0.000001`, and every shape and vector here carries `isCloseTo(other, tolerance:)` with the tolerance
  **required** — a tolerance that is right for a unit vector is wrong for a world coordinate, and a default would hide
  which one was meant. `Fixed.isCloseTo` defaults to 66 parts, about a thousandth.
- **A zero-length vector normalizes to itself** rather than panicking or answering `nan`. There is no direction to keep,
  and the honest answer is the one the caller can test.
- **No `nan` reaches a comparison here.** `Float64` has a total `compare` with `nan` above everything, and `Fixed` has no
  `nan` at all — which is one more reason a lockstep simulation runs on it.
- **`Fixed` is bit-identical everywhere.** Every operation on it, the square root and the trigonometry included, is
  integer arithmetic. The native gate programs `linear.trb`, `geometry.trb` and `grid-vectors.trb` compare the
  interpreter against the compiled binary byte for byte, and that is the guarantee written down as a test.

## 9. What is deliberately not in

- **No SIMD intrinsics and no alignment padding.** `Vector4<Float>` is four doubles and nothing else. A `Vector4` that
  was secretly 32 bytes wide would make `Vector3` a lie, and vectorization is the back end's job — the C back end can
  recognise the loops, and until it does the cost is honest rather than hidden.
- **No swizzles.** There is no `.xy`, `.xzy` or `.rgba`. Each of them is a name per permutation (fifty for four
  components) with no way to generate them, and `Vector2(v.x, v.z)` says the same thing in the same number of characters.
  `toVector2()`, `toVector3()` and `withX`/`withY`/`withZ` cover what is actually used.
- **No implicit conversions.** `Vector2<Int>` and `Vector2<Float>` do not mix, and no arithmetic widens on its own.
- **No `From` between two instantiations.** `extend<Target, Source> Vector2<Target> with From<Vector2<Source>> where
  Target: From<Source>` type checks and does not collide with the blanket `Into`, which answers the coherence question
  the plan asked — but the static member of a trait cannot be called through a type parameter, so nothing could reach it.
  The named conversions ([section 4](#4-naming)) are also the better API: `toFloat()` says which direction it goes.
- **No projection matrices.** A projection depends on the clip space of the API that consumes it — how deep it is and
  which way it points — and this package knows nothing about an API. They belong where that convention is known.
- **No triangulation and no convex decomposition.** Both are algorithms rather than geometry, both want a scratch buffer,
  and a mesh library is where they earn their place.
- **No colour.** `std/color` is its own package: a colour is not a vector, it has a space, and `Vector4` as a colour is
  the mistake every graphics library regrets.

## 10. `Fixed`, the deterministic scalar

**Q16.16 in an `Int64`.** The field is `parts`, a count of `1/65536`, so `Fixed(65536)` is one and `Fixed(1)` is the
smallest step. Values run to about ±1.4e14 with a resolution of 1.5e-5.

**Why not Q32.32.** A Q32.32 multiplication needs `(a * b) >> 32` over a 128-bit intermediate, and the language has no
128-bit integer. It can be emulated with `UInt64.multipliedWrapping` on split halves, which is thirty lines of
sign-careful bit work that no test in the repository exercises — for a type whose entire purpose is being trustworthy,
that is the wrong trade. Q16.16 multiplies as `parts * other.parts / 65536` in one `Int64`, which is exact until the
intermediate overflows at about 46341, and an overflow panics rather than wrapping. A world of 46000 units at 1.5e-5
resolution is a large game.

**No bit operations.** Every shift is written as a division or a multiplication by a power of two. That is not
aesthetics: the interpreter has **no** `Bits` member at all (`shiftedLeft` on an `Int` answers "has no method"), so a
`Fixed` built on shifts would run in no test and in no gate program. Truncating division and an arithmetic shift differ
for negative values, and the library uses division consistently, which is what makes both stages agree.

**`squareRoot`** is Newton's method on integers over `parts * 65536`, which answers the largest value whose square is at
most the input — so it is exact for a square and one part low at worst. It panics above about 2147483648, where the
scaling up overflows.

**`sine` and `cosine`** are CORDIC: sixteen rotations by a fixed angle each, where every rotation is an addition and a
division by a power of two. No table of sines, no series, no floating point. The angle is first reduced into
`[-pi/2, pi/2]` with a sign flip for the cosine, the vector starts at the reciprocal of the rotation's gain (39797
parts) and ends at one, and the sixteen step angles are `arcTangent(2^-i)` in parts. The divisions round to nearest
rather than towards zero, because sixteen truncations pull every vector towards the origin and add up to a visible
error; with rounding the answers are within about two parts of the true value, which the tests pin down.
**`arcTangentDivided(by:)`** is the same machine run the other way: the rotations that drive the vertical component to
zero add up to the angle. `arcSine` goes through it with `squareRoot(1 - x²)` as the other component, and `arcCosine` is
a quarter turn minus that.

**`show` writes the exact decimal.** A `Fixed` is a multiple of `1/65536`, so its expansion always ends, after at most
sixteen digits — and `Fixed.tryFrom("0.1")` shows as `0.100006103515625`, because that is the number. `parse` reads at most
nine digits after the point and rounds to the nearest part.

**What this buys.** `Vector2<Fixed>`, `Rectangle<Fixed>`, `Circle<Fixed>`, `Ray3<Fixed>`, `Matrix4<Fixed>` and
`Quaternion<Fixed>` all work with no change to a single line of the generic code — the three native gate programs are
the proof, and they are byte-identical between the interpreter and the compiled binary. It also buys the only
trigonometry this repository can currently *run at all* in the interpreter, since `Float64`'s implementation of `Real`
lives in `std/number`, which the interpreter does not load.

## 11. Where this comes from

| Library | What it got right | What hurt | What we take |
|---------|-------------------|-----------|--------------|
| **glam** (Rust) | columns as `Vec4` fields; `Affine3A` separate from `Mat4`; one obvious type per job | `Vec3`/`Vec3A` twins for alignment; feature flags decide `f32` vs `f64`; no generic scalar, so `DVec3` is a macro-generated copy | columns as vector fields; the affine form as its own constructor rather than its own type |
| **nalgebra** (Rust) | one `Matrix<T, R, C, S>` for everything, statically sized; `Unit<T>` for "this is normalized" | the type signatures are unreadable; compile times; a beginner cannot spell the type of a 3-vector | the idea of encoding "normalized" in the type is noted and **not** taken: it doubles the API for a property one `normalized()` call establishes |
| **cgmath** (Rust) | `Rad`/`Deg` newtypes for angles — the bug it prevents is real | two angle types plus an `Angle` trait is one too many; `InnerSpace`/`VectorSpace`/`MetricSpace` traits are more layering than payoff | one `Angle` wrapper, radians inside, degrees at the boundary |
| **euclid** (Rust, Servo) | units as a phantom type parameter, so a `Point2D<f32, ScreenSpace>` cannot be added to a world point; `Point` and `Vector` are different types | two type parameters on every signature; the space parameter infects every function | the units idea is the strongest thing on this list and is **deferred to `std/transform`**, where a coordinate space is the subject |
| **Godot** (`Vector2`/`Vector2i`) | a first-class integer vector, which is what a tilemap and a pixel actually need | twin types, hand-maintained, and the method sets drift apart | the integer vector is first class here too — as `Vector2<Int>`, one declaration |
| **Unity.Mathematics** | shader-like `float2`/`float3`, swizzles, SIMD-backed | lowercase type names fight every other convention; fifty swizzle properties; `float3` is not `Vector3` and the two coexist forever | nothing, except the confirmation that swizzles are a cost |
| **Swift `simd`** | operators on tuples-of-floats with hardware backing; `simd_quatf` separate from vectors | fixed widths only; the type names leak the register width | the quaternion as its own type, never four loose floats |
| **GLM** (C++) | mirrors GLSL exactly, so shader code and host code read alike | `glm::vec3` is `float` only unless you reach for `tvec3`; the GLSL mirror brings swizzles and implicit conversions | the "host and shader read alike" goal, to be paid for in `std/gpu` by *generating* the shader rather than by imitating it |

Two things nobody on that list has and this design does: **one generic type instead of twin types**, paid for by the
scalar tower rather than by macros, and **a fixed-point scalar that carries the same trait as the float**, which makes
every line of geometry deterministic by changing one type argument.

## 12. What the language and the compiler must provide

In the order it hurt, each with a reproduction. The ones that are still open are in `examples/generic-scalar/`, which
type checks and which `torb build` rejects; the rest quote the diagnostic that is the reproduction.

1. ~~An operator on a generic type does not lower.~~ **Closed.** `a + b` is `Add.add`, and which `add` runs is decided
   by the **operand** and not by the implementation that declares it: `extend<Scalar: Signed> Vector2<Scalar> with
   Negate` names a target that still holds a parameter, while the value in front of the operator carries the enclosing
   instance's arguments. `operandTypeOf` in `compiler/src/ir/lower/call.trb` therefore prefers the written operand
   wherever the target is not closed, and the operator form and the method form of one call are the same function -
   inside a generic body as well. `tests/conformance/generic-operators.trb` is the gate.
2. **A numeric literal in a generic body is never adapted to the parameter.** `fn oneOf<Scalar: Numeric>(): Scalar
   { 1 }` type checks, the checker records `Int64` for the literal, and the back end then reports "the function returns
   `Float64` and `return` carries `Int64`" for the `Float64` instance. The substitution is not what is missing - there
   is no parameter in the recorded type to substitute - so this is the **checker's** half of item 14, and it closes
   there: the literal has to adapt to the parameter, and the back end then does with it exactly what it does with
   `const x: Float = 1`, which compiles today. Everything in [section 1](#1-the-scalar-tower) about `unit`, `halved` and
   `zeroOf` exists only because of this. Reproduction: `examples/generic-scalar/src/main.trb`, `oneOf` and `doubled`.
3. ~~A `const` member of a generic type is not instantiated per type argument.~~ **Closed in the back end.**
   `Box<Int>.empty` and `Box<String>.empty` are one declaration and two values, and a binary keeps one cell per
   instance, named after the arguments the read decided. The interpreter still answers the same value for every scalar,
   which is item 6 seen from another side and is why the constants of this library live in concrete `extend`s.
   `tests/conformance/generic-constants.trb` is the gate.
4. **A type parameter's default is not used to reach a member of a concrete `extend`.** `Vector2.zero` where `zero` is
   declared in `extend Vector2<Float>` reports "Cannot infer `Scalar` of `Vector2`" *even with the annotation*
   `const origin: Vector2<Float> = Vector2.zero`; `Vector2<Float>.zero` works. Since the declaration says
   `Scalar: Numeric = Float`, the bare name has an answer. **Smallest change:** apply the declared defaults where a
   member access on a bare type name has nothing else to go on. Reproduction:
   `examples/generic-scalar/src/main.trb`, `extend Pair<Float>`.
5. **The interpreter resolves no package import.** `use Vector2 from "std/linear"` answers `Unknown name Vector2` there,
   because stage 0 replaces `std/` with its own natives. Everything of `std/` that has to run in the interpreter — a test
   under `torb test`, a program of `tests/conformance/` — therefore imports **by path**, and a `std/` package can
   only call functions from its own directory. That is why `zeroOf` is one line in `std/linear/src/scalar.trb` and one
   line in `std/geometry/src/scalar.trb` instead of living once in `std/number`, and why `std/geometry` reaches
   `std/linear` as `"../../linear/src/vector2"`.
6. **The interpreter cannot tell two instantiations of one `extend` apart.** `Vector2<Int>.unitX` answers
   `Vector2(x: 1.0, y: 0.0)` there, because `extend Vector2<Float>` was declared first. The library keeps this to the
   four constants and gives every conversion a name of its own ([section 4](#4-naming)); a compiled program is correct
   either way.
7. **A static member cannot be reached through a type parameter.** `Scalar.zero()` inside a generic body answers
   `Unknown name Scalar` in the interpreter, and `Vector2.from(other)` through `From` reports "a call of a trait member
   without a receiver" in the back end. This is what makes `Real.unit()` an instance member that ignores `self`, and
   what makes a `From` between two instantiations unreachable.
8. **`Float32` cannot carry `Real`.** It has no `squareRoot`, the C back end marks all eleven of its arithmetic members
   planned, and there is **no conversion from a `Float64` down to a `Float32`** at all, so no body can be written for one
   even through `Float64`. **Smallest change, in order:** a `Float32 with TryFrom<Float64, NumberRangeError>` native, then
   `Float32` arithmetic in the C back end.
9. **`Array<Item, const Size: Int>` runs nowhere.** The interpreter answers `Unknown name Array` and the C back end marks
   every member planned. It is the reason a matrix is fields and not storage, and it will be the reason `std/tensor` waits
   for `Buffer<Item>`.
10. **A trait cannot require a constant.** `const pi: Self` inside a trait reports "A binding needs a value: there are no
    uninitialized bindings and no default values". With it, `Real` would carry `pi`, `tau` and `epsilon` under the names
    the scalars already use, and `Scalar.pi` would work in a generic body.
11. **A member-level `where` clause on a method of a generic `type` adds nothing.** `fn manhattanLength(): Scalar
    where Scalar: Signed` inside `type Vector2<Scalar: Numeric>` reports "`Scalar` has no member `absolute`" in its own
    body, although the documentation describes exactly that form for a trait member. The library uses a conditional
    `extend` instead, which is the better spelling anyway.
12. **Two `Multiply` implementations on one type are unreachable.** `extend<Scalar> Matrix2<Scalar> with
    Multiply<Vector2<Scalar>, Vector2<Scalar>>` next to `with Multiply` on the type is accepted and coherent, and then
    `matrix.multiply(vector)` reports "Expected `Matrix2<Float64>`, found `Vector2<Float64>`" while `matrix * vector`
    reports "`Matrix2<Float64>` does not implement `Multiply`". Overloading by a trait parameter is one of the two
    overload forms the decision log keeps, so a call should find it.
13. **Two small checker reports.** `const size = self.length()` inside a conditional `extend` does not infer the
    parameter although the annotation form does; and `print(x).round()` answers "The checker did not work out the type of
    this expression — this is a bug of the compiler" where it means "`Void` has no member `round`".
14. **A type parameter accepts a value of any type at all.** `fn oneOf<Item: Numeric>(): Item { 1 }` type checks, and so
    does `fn textOf<Item: Numeric>(): Item { "x" }` and `fn boundless<Item>(): Item { 1 }`. The literal is not adapted to
    the parameter - the checker records `Int64` for it - and the return is then accepted against `Item` although nothing
    makes it one. That is the real cause of item 2: the back end has no fact to substitute, and the IR verifier is what
    catches the program. The checker has to record the parameter as the literal's adapted type and reject the others;
    what an integer literal means at a **user** implementor of `Numeric` such as `Fixed` is a language question that has
    no answer yet, so that instance stays a finding of the back end until it has one.
15. **`==` on a generic type records no member for some instantiations.** `Vector2<Float>.equals` is not found by
    `lookupMember`, so `recordTraitCall` records nothing and the lowering has no resolution to read; `Vector2<Int>` has
    one. The smallest reproduction is a generic type plus a concrete `extend` of it whose body constructs the type. The
    lowering answers it the way it answers a tuple's structural comparison - it asks `dispatchedOn` itself - so no
    program is blocked by it, and the missing record is still a record the checker owes.
16. **Two `From` implementations on one type collide in the back end.** `extend Path with From<String>` next to
    `extend Path with From<Name>` are two declarations with one mangled name (`Path.from` carries no arguments of its
    own), and the C compiler rejects the second prototype. It is the same shape as item 12 seen from the emitter's end,
    and `compiler/src/ir/mangle.trb` is where a name would have to carry the implementation's own arguments.

## 13. The package cut for what follows

**`std/transform`.** `Transform2` and `Transform3` as a *decomposed* transformation — translation, rotation
(`Angle` or `Quaternion`), scale — rather than a matrix, because that is the form an animation interpolates and a scene
graph edits, and `toMatrix3`/`toMatrix4` is the one-way door to the shader. This is also where **coordinate spaces**
belong: euclid's phantom unit parameter is the strongest idea in the research table, and a `Point<Space>` that cannot be
added to another space's point is worth a type parameter *here*, where a space is the subject, and not in `std/linear`,
where it would infect every signature. Hierarchies are values: a parent chain is a `List<Transform3>` folded, and
nothing needs an identity.

**`std/collision`.** Broad phase (a sweep over `bounds`, then a grid or a bounding-volume hierarchy over `Rectangle` and
`Box`), narrow phase (the separating axis test `Polygon.intersectsConvex` already sketches, and the Gilbert-Johnson-
Keerthi distance algorithm for convex shapes in space), and swept tests — a moving `Circle` against a static world,
which is what a character controller is. It answers *contacts* — a point, a normal, a penetration depth — where
`std/geometry` answers `Bool` and a distance, and that is the whole difference between the two packages. It is where a
`trait Bounds` and a `trait Contains` earn their place, because a broad phase wants to hold shapes of different kinds in
one list.

**`std/curve`.** Polylines, quadratic and cubic Bézier curves, Catmull-Rom and B-splines, arcs; length by subdivision,
the point and the tangent at a parameter, and flattening to a polyline within a tolerance. The same curves serve a canvas
or an SVG *and* a motion along one, which is why it is one package and not two. `Fixed` matters here: a curve followed
in lockstep has to land on the same point on both machines. The name is `std/curve` and not `std/path`, because
`std/path` is the package of **file paths** ([docs/PATH.md](PATH.md)) and one word may mean one thing.

**`std/animation`.** Easing functions, keyframes, tracks, tweens and a state machine, over a `trait Interpolate` that
`Vector2`, `Quaternion`, `Angle`, `Fixed` and a colour all carry — `interpolated(toward:by:)` is already spelled that way
in `std/linear` on purpose, so the trait can adopt the existing members without renaming anything. Time is `Duration`
from `std/time`, which is a value and needs no clock.

**`std/color`.** `Color` as a value with a *space* — linear sRGB, sRGB with its transfer function, Oklab, HSL — because a
colour that does not say its space is the bug behind every wrong gradient, and a `Vector4` as a colour cannot say it.
Interpolation happens in a space the caller names, conversion is explicit, and `Color` carries `Interpolate` for
`std/animation`.

## 14. Open questions

1. **`isCloseTo` or `isNear`?** The plan said `isNear`; `std/number` already has `Float64.isCloseTo` with a default
   tolerance, and this design used that name everywhere for consistency. Renaming `Float64.isCloseTo` to `isNear` across
   `std/` is a small mechanical change if `isNear` is preferred.
2. **`interpolated(toward:by:)` or `lerp`?** `lerp` is what every other library calls it and an abbreviation this
   language does not otherwise allow. `interpolated` is the full word and reads well at a call
   (`here.interpolated(toward: there, by: 0.5)`).
3. ~~**`Segment` or `Segment2`?**~~ Decided: `Segment2`. `Ray2`/`Ray3` and `Triangle2`/`Triangle3` carry the digit,
   and a segment of space has no word of its own the way a `Box` and a `Sphere` have one.
4. **Should the four vector constants stay on every instantiation?** They are the one place where a program that runs on
   the interpreter reads the wrong scalar's value ([section 12](#12-what-the-language-and-the-compiler-must-provide),
   item 6). Keeping them on `Float` alone would remove the trap and cost `Vector2<Int>.zero`.
5. ~~**Should `Matrix4` carry a general inverse?**~~ Decided: yes. `Matrix4.inverse` is the general one by cofactors
   and `inverseAffine` stays beside it as the short way for the matrices a scene graph is made of.
6. **`Quaternion.interpolated` takes the straight path and renormalizes**, not the constant-speed path along the sphere.
   The difference shows in the middle of a long rotation. A constant-speed version needs an arc cosine whose accuracy
   near a zero angle should be measured on `Fixed` before it is written.
7. **Does `std/linear` belong in the prelude?** It is an explicit import today, which the plan asked for. A game project
   would import it in every file.
