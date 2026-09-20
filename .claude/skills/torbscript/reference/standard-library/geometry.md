---
title: std/geometry
summary: The shapes of the plane and of space, with the half-open rule that makes a row of rectangles a tiling and the ray tests that answer a distance.
kind: package
status: stable
order: 106
keywords:
  - std/geometry
  - Rectangle
  - Circle
  - Box
  - Ray2
  - intersection
source:
  - std/geometry/src/lib.trb
  - docs/LINEAR.md
---

`std/geometry` is the shapes and what they answer about each other: what contains a point, what two of them share, how
far apart they are, which axis-aligned box holds one, and what a ray hits. In the plane: `Rectangle`, `Circle`,
`Segment`, `Ray2`, `Triangle2`, `Polygon`. In space: `Box`, `Sphere`, `Ray3`, `Plane`, `Triangle3`.

Every shape is generic over its scalar the way `std/linear`'s vectors are: a `Rectangle<Int>` is the pixel or tile
rectangle and a `Rectangle` is the continuous one, and the tests that need a square root or an angle live under `Real`
while everything else works with whole numbers. It is a pure value library and depends on `std/linear` and nothing else.

## Import

```trb fragment
use Rectangle, Circle, Segment, Ray2, Triangle2, Polygon from "std/geometry"
use Box, Sphere, Ray3, Plane, Triangle3 from "std/geometry"
```

```trb check
use Vector2 from "std/linear"
use Rectangle from "std/geometry"

const tile = Rectangle Vector2(0, 0), Vector2(8, 8)
print "{tile.contains(Vector2(0, 0))} {tile.contains(Vector2(8, 0))} {tile.area()}"
```

## The half-open rule

**The minimum edge of a `Rectangle` or a `Box` is inside it and the maximum edge is outside.** A point is in a rectangle
when `minimum.x <= point.x` and `point.x < maximum.x`, on every axis and for every scalar. That is what makes a row of
them a tiling: `Rectangle(Vector2(0, 0), Vector2(8, 8))` and `Rectangle(Vector2(8, 0), Vector2(8, 8))` share the column
`x == 8` in neither of them, so a pixel belongs to exactly one tile and a hit test never answers twice.

A `Circle` and a `Sphere` are **closed** instead - their boundary is inside - because they do not tile: nothing is lost by
counting a boundary twice, and a great deal is lost by leaving a touching point out of both of two touching circles.

`bounds` and `covering` answer the **closed** hull of what they are given, so a corner of a shape can lie exactly on the
maximum edge of that shape's own bounding rectangle and `contains` then answers `false` for it. A bound is a bound; the
half-open rule is about tiling.

## Declarations

<!-- torb:declarations:begin -->

### `Rectangle`, `Box`

```trb fragment
public type Rectangle<Scalar: Numeric = Float>
public type Box<Scalar: Numeric = Float>
```

An axis-aligned box: where it starts (`origin`, the corner with the smaller coordinates) and how big it is (`size`). A
size with a negative or zero component describes no points, and `isEmpty` says so - `intersection` answers an empty one
rather than `None`, so that a chain of them stays a rectangle.

`between` (from two corners), `minimum`, `maximum`, `area`/`volume`, `contains`, `encloses`, `intersects`,
`intersection`, `combined`, `covering`, `translated`, `grown`, `withSize`, `closestPoint`, `bounds` and
`manhattanDistanceTo` need only `Numeric`; `centered`, `center`, `distanceTo` and `distanceSquaredTo` halve or take a
root and therefore need `Real`.

Nothing here says `top` or `bottom`. Whether a viewer sees the minimum corner as the top or the bottom is the program's
own business.

### `Circle`, `Sphere`

```trb fragment
public type Circle<Scalar: Real = Float>
public type Sphere<Scalar: Real = Float>
```

A disc or a ball: a `center` and a `radius`, with the boundary inside. `contains`, `encloses`, `intersects`,
`intersectsRectangle`/`intersectsBox` (a test against the nearest point of the box), `closestPoint`, `distanceTo`,
`translated`, `grown`, `bounds`, `combined`, and for a circle `area` and `circumference`. `Sphere.around(box)` is the ball
through the corners of a box.

### `Segment`, `Ray2`, `Ray3`

```trb fragment
public type Segment<Scalar: Numeric = Float>
public type Ray2<Scalar: Real = Float>
public type Ray3<Scalar: Real = Float>
```

A `Segment` is the straight piece of line between two points, with both ends belonging to it: `step`, `sideOf`, `bounds`
and `intersects` under `Numeric`, and `length`, `direction`, `center`, `at`, `closestPoint`, `distanceTo` and
`intersection` (the crossing point) under `Real`.

A ray is a half-line, and **every hit test answers how far along the ray the hit is**, measured in the length of
`direction` - not the point. That is what a caller compares to find the nearest of several hits, and `ray.at(distance)`
turns it back into a point. A ray with a `direction` of length one therefore answers distances. `Ray2` hits a `Circle`, a
`Rectangle` (the slab test) and a `Segment`; `Ray3` hits a `Sphere`, a `Plane`, a `Box` and a `Triangle3`. A ray that
starts inside a shape answers `0`, and a ray with no direction answers `None`.

### `Triangle2`, `Triangle3`

```trb fragment
public type Triangle2<Scalar: Numeric = Float>
public type Triangle3<Scalar: Numeric = Float>
```

Three corners. The winding is not fixed: `signedArea` is positive where the corners run from the first axis towards the
second and negative the other way, and `contains` works for both. In space the normal follows the right-handed order of
the corners, `plane` answers the plane they lie in, and `weightsOf` writes a point as a mix of the three corners - which
is what says whether a crossing of the plane is a crossing of the *face*.

### `Polygon`

```trb fragment
public type Polygon<Scalar: Numeric = Float>
```

A closed chain of `corners`, the last joined back to the first. The **convex** tests are the ones to reach for -
`containsConvex` is a side test per edge and `intersectsConvex` is the separating axis test - and `isConvex` is what a
program establishes once instead of per query. `contains` works for any polygon, including one with a dent, by counting
the crossings of a ray. Fewer than three corners describe no area, and every test answers `false` for such a polygon.

### `Plane`

```trb fragment
public type Plane<Scalar: Real = Float>
```

A flat surface in space, as the `normal` that points away from its front and the `distance` along that normal.
`signedDistanceTo` is positive in front and negative behind, and which of the two a program calls "outside" is the
program's own business. `through`, `ofPoints`, `distanceTo`, `isInFront`, `closestPoint`, `mirrored` and `flipped`.

<!-- torb:declarations:end -->

## Related

- [std/linear](linear.md) - the vectors every shape is made of, and `Fixed`, the scalar that makes these tests
  bit-identical everywhere.
- [std/number](number.md) - `Numeric` and `Real`, the two bounds the shapes layer along.
