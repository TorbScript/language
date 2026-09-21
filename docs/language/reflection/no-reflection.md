---
title: There is no reflection
summary: A type never flows as a value, so there is no Type type, no typeof and no Class.forName - only four syntactic bridges connect a type to a value, all resolved at compile time.
kind: reference
status: stable
order: 10
keywords:
  - reflection
  - typeof
  - Class.forName
  - constructor
source:
  - CONCEPT.md#types-values-and-reflection
---

Types and values are two worlds that meet in exactly four places, and every one of them is ordinary syntax the
checker resolves before a program runs - never a value a program can hold, inspect or branch on at runtime.

## Example

```trb check
type Shape {
  case Circle(radius: Int)
  case Square(side: Int)

  static unit = Shape.Circle 1

  fn area(): Int {
    match self {
      .Circle(radius) => radius * radius
      .Square(side) => side * side
    }
  }
}

const shapes = [Shape.Circle(2), Shape.Square(3), Shape.unit]
print shapes.map({ _.area() }).toList()
```

## Syntax

```text
Point(1, 2)          the constructor
Point.origin          a static member
Point.tryFrom(text)     a static member
Shape.Circle          a case
Point.area            a method reference
```

## Rules

1. **A type never flows as a value.** There is no `Type` type to hold one in, no `typeof` operator to produce one, no
   `value is Value` on a generic `Value`, and no `Class.forName` to look one up by name. A type name appears only in
   a type position: after `:`, inside `<>`, after `with` or `where`, or on the right of `type X =`.

2. **The values of a built-in type are lowercase literals, never the type's name.** `true`, `false` and `void` are
   values; `Bool` and `Void` are types, and neither can stand where the other is expected.

3. **Exactly four syntactic forms bridge a type and a value, and all four are resolved at compile time.** A
   constructor call (`Point(1, 2)`), a static member (`Point.origin`, `Point.tryFrom(text)`), a case
   (`Shape.Circle`), and a method reference (`Point.area`, the unbound function value - see
   [Methods and `static fn`s](../types/methods.md)). None of them look a name up while the program runs.

4. **There is no runtime reflection**, because it could not mean the same thing in every back end: monomorphized and
   boxed generics would stop being interchangeable the moment a program could ask what a type parameter was, and
   every type's metadata would have to stay alive in a compiled binary whether or not anything used it.

## What this is not

**Matching on a case is not reflection.** `match self { .Circle(radius) => ... }` reads data that is already there;
it never asks what type `self` is at runtime; the case was chosen when the value was constructed, and the compiler
already knows the exhaustive list.

```trb check
type Shape {
  case Circle(radius: Int)
  case Square(side: Int)
}

fn isRound(shape: Shape): Bool {
  match shape {
    .Circle(_) => true
    .Square(_) => false
  }
}

print isRound(Shape.Circle(1))
```

```trb error
fn describe<Value>(value: Value): String {
  Value.show()
}
// error: `Value` has no member `show`
```

`Value` is a type parameter: it names a type in `fn describe<Value>`, and a type is never a value to call a member
on, whatever the member is called.

## Related

- [Methods and `static fn`s](../types/methods.md) - `Type.member` as a call and as a value.
- [Cases and match](../pattern-matching/cases-and-match.md) - matching a case without ever asking for a type.
- [Encode and Decode](encode-and-decode.md) - what covers serialization instead of reflection.
- [Type parameters](../generics/type-parameters.md) - where a name stands for a type instead of a value.
