---
title: Traits as types
summary: A trait can stand wherever a type can, a value coerces to it automatically, and that coercion is the only subtyping the language has, with no variance for the types built from it.
kind: reference
status: stable
order: 40
keywords:
  - trait type
  - coercion
  - subtyping
  - variance
  - dynamic dispatch
source:
  - CONCEPT.md#traits
  - examples/tour/src/05-traits.trb
---

A trait is a type, so it can stand anywhere a type can: a parameter, a field, a `List<Item>`. A value coerces to a
trait it implements automatically, which is the one place the language has subtyping.

## Example

```trb check
trait Shape {
  fn area(): Float
}

type Square with Shape {
  side: Float

  fn area(): Float {
    side * side
  }
}

type Circle with Shape {
  radius: Float

  fn area(): Float {
    Float.pi * radius * radius
  }
}

fn draw(shape: Shape) {
  print "area {shape.area()}"
}

const shapes: List<Shape> = [Square(2.0), Circle(1.0)]
for shape in shapes {
  draw shape
}
```

## Syntax

```text
fn <name>(<param>: <Trait>)                a trait-typed parameter
const <name>: List<<Trait>> = [...]        a collection of a trait type
```

## Rules

1. **A trait can be used as a type wherever a type is expected**: a parameter, a field, a type argument
   (`List<Shape>`). `draw(shape: Shape)` accepts any value whose type implements `Shape`.

2. **A value coerces to a trait it implements automatically, only where a type is expected.** `Square(2.0)` becomes a
   `Shape` in `const shapes: List<Shape> = [Square(2.0), Circle(1.0)]` without a cast or a wrapper - the list is built
   holding the trait type from the start.

3. **Coercion to a trait type is the only subtyping the language has, and it never solves an inference variable.** It
   only fires against an expected type that is already known.

4. **There is no variance.** `List<Square>` is not a `List<Shape>`, even though `Square` is a `Shape`: a collection
   already built with one type argument does not become a collection of another one.

   ```trb error
   trait Shape {
     fn area(): Float
   }

   type Square with Shape {
     side: Float

     fn area(): Float {
       side * side
     }
   }

   const squares: List<Square> = [Square(2.0)]
   const shapes: List<Shape> = squares
   // error: `List<Square>` does not implement `List<Shape>`
   ```

5. **Whether a call on a trait-typed value is dispatched statically or dynamically is the implementation's business
   and is not observable.** Nothing about `shape.area()` above says which shape ran; see
   [Witness tables](../generics/witnesses.md) for how a generic member reaches a trait-typed value at all.

6. **Not every member of a trait can be called on a trait-typed value.** A member whose signature mentions `Self`, or
   that is `static`, is rejected at the call - the type stays a legal type even so. See
   [Object safety](object-safety.md).

## What this is not

**A `List` of a trait type is not the same list as a `List` of the concrete type, in either direction.** Coercion
happens once, while the list is being built; nothing about the finished `List<Shape>` says it happens to hold only
`Square`s, so it cannot be handed back as a `List<Square>`.

```trb check
trait Shape {
  fn area(): Float
}

type Square with Shape {
  side: Float

  fn area(): Float {
    side * side
  }
}

const squares: List<Square> = [Square(2.0)]
const shapes: List<Shape> = [Square(3.0)]
```

```trb error
trait Shape {
  fn area(): Float
}

type Square with Shape {
  side: Float

  fn area(): Float {
    side * side
  }
}

const shapes: List<Shape> = [Square(2.0)]
const squares: List<Square> = shapes
// error: `List<Shape>` does not implement `List<Square>`
```

## Related

- [Traits](traits.md) - `with` and `extend`, before a trait is used as a type.
- [Trait intersections](intersections.md) - `Show & Encode` as one trait type built from several.
- [Object safety](object-safety.md) - which members a trait-typed value cannot call.
- [Witness tables](../generics/witnesses.md) - how a generic member is called on a trait-typed value.

