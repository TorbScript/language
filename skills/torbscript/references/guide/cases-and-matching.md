---
title: Cases and matching
summary: How to declare a type with more than one shape, and take it apart with a match that has to cover every case.
kind: guide
status: stable
order: 50
prerequisites:
  - types-and-methods.md
keywords:
  - case
  - match
  - exhaustive
  - pattern
source:
  - CONCEPT.md#algebraic-data-types-and-pattern-matching
  - examples/tour/src/04-adts-and-matching.trb
---

A `type` is not only fields: it can also have `case`s, one per shape a value can take. This page declares one and
takes it apart with `match`.

## Goal

At the end of this page you can declare a type with cases and write a `match` that the compiler accepts as covering
every one of them.

## Declaring a type with cases

```trb
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
  case Empty

  fn area(): Float {
    match self {
      .Circle(radius) => 3.14159 * radius * radius
      .Rectangle(width, height) => width * height
      .Empty => 0.0
    }
  }
}

const shapes = [Shape.Circle(2.0), Shape.Rectangle(2.0, 3.0), Shape.Empty]
print(shapes.map { _.area() }.toList())
```

A case (skill `torbscript-language`: `references/glossary.md`) is written `Shape.Circle` in an expression, or `.Circle` where the expected type already
says which type is meant - inside the `match` above, the subject is `self`, so every arm can drop the type name.

## A match has to cover everything

`match` is an expression, and the compiler checks that its arms cover every case. Removing the `.Empty` arm above does
not compile:

```trb error
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
  case Empty

  fn area(): Float {
    match self {
      .Circle(radius) => 3.14159 * radius * radius
      .Rectangle(width, height) => width * height
    }
  }
}
// error: `match` does not handle `.Empty`
```

This is what makes adding a case later safe: every existing `match` on the type turns into a compile error instead of
a silently wrong answer at the one call site nobody remembered to update. See
Exhaustiveness (skill `torbscript-language`: `references/language/pattern-matching/exhaustiveness.md`).

## Matching on more than the case

A pattern can carry a literal, a range, alternatives joined with `|`, and a guard:

```trb
fn describe(value: Int): String {
  match value {
    0 => "zero"
    1 | 2 | 3 => "small"
    4..=9 => "medium"
    n if n < 0 => "negative ({n})"
    _ => "large"
  }
}

print describe(-5)
print describe(7)
```

`_` is the wildcard (skill `torbscript-language`: `references/glossary.md`): it matches anything and binds nothing, and it is what makes the `match`
above cover every remaining `Int`. See Pattern forms (skill `torbscript-language`: `references/language/pattern-matching/pattern-forms.md`) for every form a
pattern can take, including list patterns and nested patterns over tuples.

## Next

- [Traits](traits.md) - giving a type a capability instead of a case.
- Cases and match (skill `torbscript-language`: `references/language/pattern-matching/cases-and-match.md`) - the exact rules for writing and importing a
  case.
- Patterns in bindings and conditions (skill `torbscript-language`: `references/language/pattern-matching/patterns-in-bindings.md`) - `const Point(x, y) =`,
  `if const` and `while const`.

