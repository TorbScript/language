---
title: Traits
summary: Declare what a type can do as a trait, give it to a type now or later, and use the trait as a type that holds any of them.
kind: guide
status: stable
order: 70
prerequisites:
  - cases-and-matching.md
keywords:
  - trait
  - with
  - extend
  - interface
  - operator
source:
  - CONCEPT.md#traits
  - examples/tour/src/05-traits.trb
---

There are no classes and no inheritance. What a type can do is a trait, much like an interface or a protocol.

## Goal

At the end of this page you can declare a trait, give it to a type, and use the trait as a type.

## Declare a trait and give it to a type

```trb run
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

print Square(2.0).describe()
// prints area 4.0
```

`area` has no body, so every type `with Shape` must write one. `describe` has a body, so `Square` gets it for free. A
trait with one method is named after that method: `Hash`, `Equals`, `Show`, never `Hashable`.

## Give it later with extend

```trb run
trait Shape {
  fn area(): Float
}

type Circle {
  radius: Float
}

extend Circle with Shape {
  fn area(): Float {
    3.0 * radius * radius
  }
}

const shapes: List<Shape> = [Circle(1.0)]
for shape in shapes {
  print shape.area()
}
// prints 3.0
```

`extend` gives a trait to a type that already exists, even one from the standard library. A trait works as a type:
`List<Shape>` holds any value whose type has `Shape`. Through it, you can call only what `Shape` declares.

## Operators are traits

```trb run
type Vector with Add {
  x: Int
  y: Int

  fn add(other: Vector): Vector {
    Vector(x + other.x, y + other.y)
  }
}

print(Vector(1, 2) + Vector(3, 4))
// prints Vector(x: 4, y: 6)
```

`+` calls `add` of the trait `Add`, `==` calls `equals` of `Equals`, and `<` calls `compare` of `Compare`. A type
gets an operator by having its trait.

## Next

- [Errors](errors.md) - how a function says that it can fail.
- Traits (skill `torbscript-language`: `references/language/traits/traits.md`) - the exact rules.
- Operators are traits (skill `torbscript-language`: `references/language/traits/operators.md`) - every operator and its trait.

