---
title: Werte und Bindungen
summary: Eine Bindung ist const oder var, ein zweiter Name ist immer eine Kopie, und eine Änderung geht über den Pfad, an dem der Wert liegt.
kind: guide
status: stable
order: 30
prerequisites:
  - installing-and-running.md
translates: 43ab8f57a4f1
---

In den meisten Sprachen stellst du zwei Fragen: Kann sich dieser Name ändern, und kann sich das Ding dahinter ändern?
In TorbScript gibt es eine Antwort, und der Name gibt sie.

## Ziel

Am Ende dieser Seite kannst du für jede Zeile sagen, ob sie kompiliert und was sie ändert.

## Zwei Arten von Bindung

```trb run
const answer = 42
var counter = 0
counter = counter + 1
const ratio: Float = 1
print "{answer} {counter} {ratio}"
// prints 42 1 1.0
```

`const` ändert sich nie, `var` schon. Eine Bindung bekommt immer einen Wert, wenn sie deklariert wird: Es gibt kein
leeres `var x`. Ein Typ nach dem Namen ist optional, und eine Zahl nimmt den Typ an, der erwartet wird, also wurde `1`
hier zu einem `Float`.

## Ein const wirkt bis ganz unten

```trb error
const fixed = [1, 2]
fixed.append 3
// error: `append` needs a `var`
```

Über ein `const` ändert sich nichts: kein neuer Wert, kein Feld und keine Methode, die den Wert ändert. Eine
`const`-Liste ist also eine Liste, die sich nie ändert, und es gibt keine eigene `ImmutableList`.

Eine Methode, die einen Wert an Ort und Stelle ändert, ist ein Verb, etwa `append` oder `sort`. Ihr Zwilling, der eine
geänderte Kopie zurückgibt, ist ein Partizip, etwa `appended` oder `sorted`, und funktioniert auf einem `const`:

```trb run
const fixed = [1, 2]
const longer = fixed.appended 3
print "{fixed} {longer}"
// prints [1, 2] [1, 2, 3]
```

## Ein zweiter Name ist eine Kopie

```trb run
type Point {
  var x: Int
  var y: Int
}

var first = Point 1, 2
const second = first
first.x = 99
print "{first} {second}"
// prints Point(x: 99, y: 2) Point(x: 1, y: 2)
```

`second` ist eine Kopie, also zeigt sich eine Änderung über `first` dort nie. Dasselbe gilt, wenn du einen Wert an eine
Funktion übergibst oder in einer Liste ablegst. Kopien sind billig: Eine Liste oder ein String teilt sich den Speicher,
bis eine Seite hineinschreibt.

## Einen Wert dort ändern, wo er liegt

Das ist der eine Fehler, den jeder einmal macht:

```trb error
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
var first = counters[0]
first.increment()
// error: This change has no effect: `first` is never read again
```

`var first = counters[0]` nimmt eine Kopie aus der Liste, also sieht die Liste die Änderung nie. Der Compiler merkt,
wenn eine Änderung so verloren geht. Ändere den Wert stattdessen dort, wo er liegt:

```trb run
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
counters[0].increment()
print counters[0].count
// prints 1
```

`counters[0]` ist ein Pfad zum Wert, keine Kopie davon. Ein Pfad kann beliebig tief gehen,
`world.players[id].health = 5`, und jeder Schritt darauf muss änderbar sein: Die Bindung ist ein `var`, und jedes Feld
auf dem Weg ist ein `var`-Feld. Ein Feld ohne `var` ändert sich nie, nachdem der Wert gebaut ist.

## Weiter

- [Funktionen und Closures](functions-and-closures.md) - eine Funktion deklarieren und Code als Wert übergeben.
- [Bindings](../language/values-and-types/bindings.md) - die genauen Regeln, samt Shadowing.
- [Why values instead of references](../explanation/why-values-instead-of-references.md) - warum die Sprache so
  funktioniert.
