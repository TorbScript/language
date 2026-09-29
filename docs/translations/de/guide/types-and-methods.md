---
title: Typen und Methoden
summary: Wie du einen Typ deklarierst, ihm Methoden hinzufügst, und ein Verb, das ihn ändert, von dem Partizip unterscheidest, das eine Kopie zurückgibt.
kind: guide
status: stable
order: 40
prerequisites:
  - functions-and-closures.md
translates: ccea4d4d7b9e
---

`type` ist das eine Schlüsselwort, das einen Datentyp deklariert: Was andere Sprachen in `struct`, `class` und einen
Record aufteilen, ist hier eine Deklaration. Diese Seite bringt dich von einem ersten `type` zu einer Methode, die
ihn an Ort und Stelle ändert.

## Ziel

Am Ende dieser Seite kannst du einen Typ mit Feldern und Methoden deklarieren und weißt, welche Methode eine `var fn`
ist und welche nicht.

## Einen Typ deklarieren

```trb
type Point {
  var x: Int
  var y: Int
}

const origin = Point 0, 0
const somewhere = Point x: 3, y: 4
print "{origin} {somewhere}"
```

Ein Feld ist öffentlich, sofern nicht `private` markiert, und `const`, sofern nicht `var` markiert. Der
[Konstruktor](../glossary.md#constructor) wird aus den Feldern in Deklarationsreihenfolge erzeugt, sodass `Point(0,
0)` und `Point(x: 3, y: 4)` beide ohne eine dafür geschriebene Zeile funktionieren. `Equals`, `Hash` und `Show` werden
ebenfalls erzeugt, weshalb `print` oben nichts Zusätzliches braucht, um einen `Point` zu zeigen. Die volle Liste
dessen, was erzeugt wird, steht in [Declaring a type](../language/types/declaring-a-type.md).

## Eine Methode hinzufügen

Eine Methode ist eine Funktion, die innerhalb des Typs deklariert ist. Sie listet ihren Empfänger nicht auf: Die
Parameterliste ist das, was der Aufrufer schreibt.

```trb
type Rectangle {
  width: Int
  height: Int

  fn area(): Int {
    width * height
  }
}

const rectangle = Rectangle 3, 4
print rectangle.area()
```

Innerhalb von `area` lösen sich `width` und `height` gegen den Empfänger auf, ohne `self.width` zu schreiben. Ein
Member, der als [`static`](../language/types/methods.md) markiert ist, gehört stattdessen zum Typ und wird über den
Typ statt über einen Wert aufgerufen:

```trb
type Circle {
  radius: Float

  fn area(): Float {
    3.14159 * radius * radius
  }

  static fn unit(): Circle {
    Circle 1.0
  }
}

print Circle.unit().area()
```

## Einen Wert an Ort und Stelle ändern: `var fn`

Eine Methode, die ihren Empfänger ändert, ist eine `var fn`, und heißt ein **Verb**.

```trb
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }

  fn incremented(): Counter {
    copy(count: count + 1)
  }
}

var counter = Counter()
counter.increment()
print counter.count
```

`increment` braucht `counter` als `var`-Bindung, weil eine Änderung immer einen
[`var`-Pfad](../language/types/var-paths.md) von der Bindung an braucht. Ihr **Partizip**, `incremented`, ist eine
gewöhnliche `fn` und gibt stattdessen eine geänderte Kopie zurück, mit `copy`, das jeder Typ kostenlos bekommt.
`increment` über eine `const`-Bindung aufzurufen ist ein Kompilierfehler:

```trb error
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

const frozen = Counter()
frozen.increment()
// error: `increment` needs a `var`
```

Wie ein neues Paar benannt wird, steht in [Verbs and participles](../language/types/verbs-and-participles.md).

## Weiter

- [Cases and matching](cases-and-matching.md) - ein Typ mit mehr als einer Form, und ihn auseinanderzunehmen.
- [Declaring a type](../language/types/declaring-a-type.md) - Felder, Sichtbarkeit und was erzeugt wird, vollständig.
- [Mutation and var paths](../language/types/var-paths.md) - was von der Bindung an `var` sein muss.
