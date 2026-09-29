---
title: Cases and matching
summary: Declare a type whose value is one of several cases, and take it apart with a match that has to handle every case.
kind: guide
status: stable
order: 60
prerequisites:
  - types-and-methods.md
keywords:
  - case
  - match
  - exhaustive
  - pattern
  - enum
source:
  - CONCEPT.md#algebraic-data-types-and-pattern-matching
  - examples/tour/src/04-adts-and-matching.trb
---

A `type` can list `case`s: the shapes its value can take. This covers what other languages call an enum, a sealed
class or a tagged union.

## Goal

At the end of this page you can declare a type with cases and write a `match` that handles all of them.

## Declare cases

```trb run
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
  case Empty

  fn area(): Float {
    match self {
      .Circle(radius) => 3.0 * radius * radius
      .Rectangle(width, height) => width * height
      .Empty => 0.0
    }
  }
}

const shapes = [Shape.Circle(2.0), Shape.Rectangle(2.0, 3.0), Shape.Empty]
print shapes.map({ _.area() }).toList()
// prints [12.0, 6.0, 0.0]
```

A case can carry values, like `Circle(radius: Float)`, or none, like `Empty`. Write it with its type, `Shape.Circle`,
or with a dot, `.Circle`, where the type is already clear. A case is never bare unless the file imports it, and every
file imports `Some`, `None`, `Ok` and `Fail`.

## A match handles every case

```trb error
type Shape {
  case Circle(radius: Float)
  case Empty
}

fn area(shape: Shape): Float {
  match shape {
    .Circle(radius) => 3.0 * radius * radius
  }
}
// error: `match` does not handle `.Empty`
```

A `match` is an expression, and it must handle every case. So when you add a case later, the compiler lists every
`match` that has to learn about it.

## More than the case

```trb run
fn describe(value: Int): String {
  match value {
    0 => "zero"
    1 | 2 | 3 => "small"
    4..=9 => "medium"
    number if number < 0 => "negative"
    _ => "large"
  }
}

print describe(-5)
print describe(7)
// prints negative
// prints medium
```

A pattern can be a literal, several joined with `|`, a range, or a name with a condition after `if`. A lowercase name
takes the value, and `_` matches anything and keeps nothing. A name that the arm never uses is an error: write `_`.

## Next

- [Traits](traits.md) - what a type can do, instead of what it is.
- [Cases and match](../language/pattern-matching/cases-and-match.md) - the exact rules, and importing a case.
- [Pattern forms](../language/pattern-matching/pattern-forms.md) - every pattern, including lists and tuples.
