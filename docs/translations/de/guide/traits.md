---
title: Traits
summary: Wie du eine Fähigkeit deklarierst, sie einem Typ gibst, und den Trait selbst als Typ benutzt, der verbirgt, welcher konkrete Typ dahintersteht.
kind: guide
status: stable
order: 60
prerequisites:
  - cases-and-matching.md
translates: 86cfb98248f8
---

Diese Sprache kennt keine Vererbung. Was ein Typ kann, ist ein [Trait](../glossary.md#trait), und ein Typ bekommt
einen entweder bei seiner eigenen Deklaration oder nachträglich.

## Ziel

Am Ende dieser Seite kannst du einen Trait deklarieren, ihn auf zwei Arten implementieren und den Trait selbst als
Typ benutzen.

## Einen Trait deklarieren und implementieren

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

`area` hat keinen Rumpf, also muss jeder Typ, der `with Shape` sagt, einen liefern; `describe` hat einen
Standardrumpf, also bekommt `Square` ihn kostenlos und könnte ihn trotzdem überschreiben. Ein Trait mit genau einer
erforderlichen Methode wird nach dieser Methode benannt (`Hash`, `Equals`, `Show`) – `Shape` hat hier zwei Member,
also behält er seinen eigenen Namen.

## Einen Trait nachträglich implementieren

`extend` fügt einem bereits bestehenden Typ eine Trait-Implementierung hinzu, auch einem, den du nicht selbst
deklariert hast:

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

Was `extend` sonst noch hinzufügen kann, steht in [extend](../language/traits/extend.md), und welches Paket eine
solche Implementierung schreiben darf, in
[Coherence and blanket implementations](../language/traits/coherence.md).

## Einen Trait als Typ benutzen

Ein Trait kann überall stehen, wo ein Typ stehen kann. Ein Wert wird automatisch zu ihm koerziert, sodass eine
einzelne `List` mehrere konkrete Typen hinter einem Trait halten kann.

```trb
const shapes: List<Shape> = [Square(2.0), Circle(1.0)]
for shape in shapes {
  print shape.describe()
}
```

Innerhalb der Schleife bietet `shape` nur das, was `Shape` deklariert – eine Methode aufzurufen, die nur `Square`
hat, würde nicht kompilieren, weil der konkrete Typ verborgen ist. Siehe
[Traits as types](../language/traits/trait-types.md).

## Auch Operatoren sind Traits

`+`, `==`, `<` und die anderen Operatoren sind gewöhnliche Trait-Methoden, also bekommt ein Typ einen davon, indem er
den dahinterstehenden Trait implementiert.

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

Die vollständige Tabelle der Operatoren und der Methoden dahinter steht in
[Operators are traits](../language/traits/operators.md).

## Weiter

- [Errors](errors.md) - wie eine Funktion sagt, dass sie scheitern kann.
- [Traits](../language/traits/traits.md) - die genauen Regeln, samt dem Grund, warum ein Trait mit einer Methode nach
  dieser Methode heißt.
- [Trait intersections](../language/traits/intersections.md) - `Show & Encode` als ein Typ.
