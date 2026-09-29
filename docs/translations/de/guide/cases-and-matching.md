---
title: Fälle und Pattern Matching
summary: Einen Typ deklarieren, dessen Wert einer von mehreren Fällen ist, und ihn mit einem match auseinandernehmen, das jeden Fall behandeln muss.
kind: guide
status: stable
order: 60
prerequisites:
  - types-and-methods.md
translates: 8a2b362494d8
---

Ein `type` kann `case`s aufzählen: die Formen, die sein Wert annehmen kann. Das deckt ab, was andere Sprachen Enum,
Sealed Class oder Tagged Union nennen.

## Ziel

Am Ende dieser Seite kannst du einen Typ mit Fällen deklarieren und ein `match` schreiben, das alle behandelt.

## Fälle deklarieren

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

Ein Fall kann Werte tragen, wie `Circle(radius: Float)`, oder keine, wie `Empty`. Schreib ihn mit seinem Typ,
`Shape.Circle`, oder mit einem Punkt, `.Circle`, wo der Typ schon klar ist. Ein Fall steht nie allein, außer die Datei
importiert ihn, und jede Datei importiert `Some`, `None`, `Ok` und `Fail`.

## Ein match behandelt jeden Fall

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

Ein `match` ist ein Ausdruck, und es muss jeden Fall behandeln. Kommt später ein Fall dazu, listet der Compiler also
jedes `match` auf, das davon erfahren muss.

## Mehr als der Fall

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

Ein Muster kann ein Literal sein, mehrere mit `|` verbunden, ein Bereich oder ein Name mit einer Bedingung nach `if`.
Ein kleingeschriebener Name nimmt den Wert auf, und `_` passt auf alles und behält nichts. Ein Name, den der Zweig nie
benutzt, ist ein Fehler: Schreib `_`.

## Weiter

- [Traits](traits.md) - was ein Typ kann, statt was er ist.
- [Cases and match](../language/pattern-matching/cases-and-match.md) - die genauen Regeln, und einen Fall importieren.
- [Pattern forms](../language/pattern-matching/pattern-forms.md) - jedes Muster, auch für Listen und Tupel.
