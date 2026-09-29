---
title: Fälle und Pattern Matching
summary: Wie du einen Typ mit mehr als einer Form deklarierst und ihn mit einem match auseinandernimmst, das jeden Fall abdecken muss.
kind: guide
status: stable
order: 50
prerequisites:
  - types-and-methods.md
translates: 2dc33a3b84cf
---

Ein `type` ist nicht nur Felder: Er kann auch `case`s haben, einen pro Form, die ein Wert annehmen kann. Diese Seite
deklariert einen und nimmt ihn mit `match` auseinander.

## Ziel

Am Ende dieser Seite kannst du einen Typ mit Fällen deklarieren und ein `match` schreiben, das der Compiler als
vollständig für jeden davon akzeptiert.

## Einen Typ mit Fällen deklarieren

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

Ein [Case](../glossary.md#case) wird in einem Ausdruck als `Shape.Circle` geschrieben, oder als `.Circle`, wo der
erwartete Typ schon sagt, welcher Typ gemeint ist – innerhalb des `match` oben ist das Subjekt `self`, also kann
jeder Zweig den Typnamen weglassen.

## Ein match muss alles abdecken

`match` ist ein Ausdruck, und der Compiler prüft, dass seine Zweige jeden Fall abdecken. Den `.Empty`-Zweig oben zu
entfernen kompiliert nicht:

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

Das macht es sicher, später einen Fall hinzuzufügen: Jedes bestehende `match` auf dem Typ wird zu einem
Kompilierfehler statt zu einer still falschen Antwort an der einen Aufrufstelle, die niemand aktualisiert hat. Siehe
[Exhaustiveness](../language/pattern-matching/exhaustiveness.md).

## Auf mehr als den Fall matchen

Ein Pattern kann ein Literal tragen, einen Bereich, mit `|` verbundene Alternativen und eine Guard-Bedingung:

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

`_` ist der [Wildcard](../glossary.md#wildcard): Er matcht alles und bindet nichts, und er ist es, der das `match`
oben jeden verbleibenden `Int` abdecken lässt. Jede Form, die ein Pattern annehmen kann, einschließlich Listenmustern
und verschachtelten Mustern über Tupel, steht in
[Pattern forms](../language/pattern-matching/pattern-forms.md).

## Weiter

- [Traits](traits.md) - einem Typ eine Fähigkeit geben statt eines Falls.
- [Cases and match](../language/pattern-matching/cases-and-match.md) - die genauen Regeln zum Schreiben und
  Importieren eines Falls.
- [Patterns in bindings and conditions](../language/pattern-matching/patterns-in-bindings.md) - `const Point(x, y) =`,
  `if const` und `while const`.
