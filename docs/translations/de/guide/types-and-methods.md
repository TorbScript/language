---
title: Typen und Methoden
summary: Einen Typ mit Feldern deklarieren, ihm Methoden geben und eine Methode, die den Wert ändert, von einer unterscheiden, die eine geänderte Kopie zurückgibt.
kind: guide
status: stable
order: 50
prerequisites:
  - functions-and-closures.md
translates: 918612f767f9
---

`type` ist das eine Schlüsselwort für deine eigenen Daten. Was andere Sprachen auf Struct, Klasse und Record
aufteilen, ist hier eine Deklaration.

## Ziel

Am Ende dieser Seite kannst du einen Typ mit Feldern und Methoden deklarieren, und du weißt, wann eine Methode eine
`var fn` ist.

## Einen Typ deklarieren

```trb run
type Point {
  var x: Int
  var y: Int
}

const origin = Point 0, 0
const somewhere = Point x: 3, y: 4
print "{origin} {somewhere}"
print(origin == somewhere)
// prints Point(x: 0, y: 0) Point(x: 3, y: 4)
// prints false
```

Ein Feld ist `const`, außer es sagt `var`, und öffentlich, außer es sagt `private`. Einen Konstruktor schreibst du
nicht: Er nimmt die Felder der Reihe nach, nach Position oder nach Namen. Vergleichen mit `==`, Hashing, Ausgabe und
`copy` gibt es ebenfalls gratis.

## Methoden hinzufügen

```trb run
type Circle {
  radius: Float

  fn area(): Float {
    3.0 * radius * radius
  }

  static fn unit(): Circle {
    Circle 1.0
  }
}

print Circle(2.0).area()
print Circle.unit().radius
// prints 12.0
// prints 1.0
```

Eine Methode ist eine Funktion im Typ. Sie führt `self` nicht auf: In ihr ist `radius` das Feld des Werts, auf dem sie
aufgerufen wurde. Eine `static fn` gehört zum Typ, nicht zu einem Wert, und wird auf dem Typ aufgerufen:
`Circle.unit()`.

## Eine Methode, die den Wert ändert

```trb run
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
const next = counter.incremented()
print "{counter.count} {next.count}"
// prints 1 2
```

Eine Methode, die den Wert ändert, ist eine `var fn`, und sie lässt sich nur auf einem `var` aufrufen. Ihr Zwilling
`incremented` gibt eine mit `copy` gemachte geänderte Kopie zurück und funktioniert auf allem. Der Name unterscheidet
sie: Ein Verb ändert an Ort und Stelle, ein Partizip gibt eine Kopie zurück.

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

## Weiter

- [Fälle und Pattern Matching](cases-and-matching.md) - ein Typ, der eine von mehreren Formen ist.
- [Declaring a type](../language/types/declaring-a-type.md) - Felder, Standardwerte und was es gratis gibt,
  vollständig.
- [Verbs and participles](../language/types/verbs-and-participles.md) - wie ein Paar von Methodennamen gewählt wird.
