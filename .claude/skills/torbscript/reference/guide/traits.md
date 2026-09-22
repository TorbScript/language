---
title: Traits
summary: How to declare a capability, give it to a type, and use the trait itself as a type that hides which concrete type it is.
kind: guide
status: stable
order: 60
prerequisites:
  - cases-and-matching.md
keywords:
  - trait
  - with
  - extend
  - supertrait
source:
  - CONCEPT.md#traits
  - examples/tour/src/05-traits.trb
---

There is no inheritance in this language. What a type can do is a [trait](../glossary.md#trait), and a type comes with
one either at its own declaration or afterwards.

## Goal

At the end of this page you can declare a trait, implement it two ways, and use the trait itself as a type.

## Declaring a trait and implementing it

```trb
trait Shape {
  fn area(): Float

  fn describe(): String {
    "area {area()}"
  }
}

type Square with Shape {
  side: Float

  fn area(): Float {
    side * side
  }
}

const square = Square 2.0
print square.describe()
```

`area` has no body, so every type that says `with Shape` must supply one; `describe` has a default body, so `Square`
gets it for free and could still override it. A trait with exactly one required method is named after that method
(`Hash`, `Equals`, `Show`) - `Shape` here has two members, so it keeps its own name.

## Implementing a trait afterwards

`extend` adds a trait implementation to a type that already exists, including one you did not declare:

```trb
type Circle {
  radius: Float
}

extend Circle with Shape {
  fn area(): Float {
    3.14159 * radius * radius
  }
}

print Circle(1.0).area()
```

See [extend](../language/traits/extend.md) for what else it can add, and
[Coherence and blanket implementations](../language/traits/coherence.md) for which package is allowed to write one.

## Using a trait as a type

A trait can stand wherever a type can. A value coerces to it automatically, so a single `List` can hold several
concrete types behind one trait.

```trb
const shapes: List<Shape> = [Square(2.0), Circle(1.0)]
for shape in shapes {
  print shape.describe()
}
```

Inside the loop, `shape` only offers what `Shape` declares - calling a method that only `Square` has would not
compile, because the concrete type is hidden. See [Traits as types](../language/traits/trait-types.md).

## Operators are traits, too

`+`, `==`, `<` and the other operators are ordinary trait methods, so a type gets one by implementing the trait behind
it.

```trb
type Vector2 with Add {
  x: Float
  y: Float

  fn add(other: Vector2): Vector2 {
    Vector2(x + other.x, y + other.y)
  }
}

print(Vector2(1.0, 2.0) + Vector2(3.0, 4.0))
```

See [Operators are traits](../language/traits/operators.md) for the full table of operators and the methods behind
them.

## Next

- [Errors](errors.md) - how a function says it can fail.
- [Traits](../language/traits/traits.md) - the exact rules, including why a single-method trait is named after its
  method.
- [Trait intersections](../language/traits/intersections.md) - `Show & Encode` as one type.

