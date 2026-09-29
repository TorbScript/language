---
title: Traits
summary: Als Trait deklarieren, was ein Typ kann, ihn einem Typ jetzt oder später geben und den Trait als Typ benutzen, der jeden davon hält.
kind: guide
status: stable
order: 70
prerequisites:
  - cases-and-matching.md
translates: 3b1587169b64
---

Es gibt keine Klassen und keine Vererbung. Was ein Typ kann, ist ein Trait, ähnlich einem Interface oder einem
Protocol.

## Ziel

Am Ende dieser Seite kannst du einen Trait deklarieren, ihn einem Typ geben und den Trait als Typ benutzen.

## Einen Trait deklarieren und einem Typ geben

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

`area` hat keinen Rumpf, also muss jeder Typ `with Shape` einen schreiben. `describe` hat einen Rumpf, also bekommt
`Square` ihn gratis. Ein Trait mit einer Methode heißt wie diese Methode: `Hash`, `Equals`, `Show`, nie `Hashable`.

## Später geben mit extend

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

`extend` gibt einem Typ, den es schon gibt, einen Trait, sogar einem aus der Standardbibliothek. Ein Trait funktioniert
als Typ: `List<Shape>` hält jeden Wert, dessen Typ `Shape` hat. Darüber kannst du nur aufrufen, was `Shape`
deklariert.

## Operatoren sind Traits

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

`+` ruft `add` des Traits `Add` auf, `==` ruft `equals` von `Equals` auf, und `<` ruft `compare` von `Compare` auf. Ein
Typ bekommt einen Operator, indem er dessen Trait hat.

## Weiter

- [Fehler](errors.md) - wie eine Funktion sagt, dass sie scheitern kann.
- [Traits](../language/traits/traits.md) - die genauen Regeln.
- [Operators are traits](../language/traits/operators.md) - jeder Operator und sein Trait.
