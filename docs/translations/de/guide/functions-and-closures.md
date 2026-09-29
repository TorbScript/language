---
title: Funktionen und Closures
summary: Eine Funktion mit typisierten Parametern, Standardwerten und Labels deklarieren, eine Closure schreiben und sie als letztes Argument eines Aufrufs übergeben.
kind: guide
status: stable
order: 40
prerequisites:
  - values-and-bindings.md
translates: 5e2a3402a5ba
---

Eine Funktion wird mit `fn` deklariert. Eine Closure ist ein Stück Code, das du in einer Bindung ablegen oder einer
anderen Funktion übergeben kannst.

## Ziel

Am Ende dieser Seite kannst du eine Funktion mit Standardwerten und Labels deklarieren, eine Closure schreiben und
eine an einen Aufruf übergeben.

## Eine Funktion deklarieren

```trb run
fn distance(x: Int, y: Int): Int {
  const dx = x * x
  const dy = y * y
  dx + dy
}

print distance(3, 4)
// prints 25
```

Jeder Parameter hat einen Typ. Das Ergebnis ist die letzte Zeile des Rumpfs, und seinen Typ kannst du weglassen: Der
Compiler ermittelt ihn. Eine `public`-Funktion schreibt ihn aus, damit ein Aufrufer in einer anderen Datei ihn sieht,
ohne den Rumpf zu lesen. Eine Funktion kann oberhalb der Zeile aufgerufen werden, in der sie deklariert ist.

## Standardwerte und Labels

```trb run
fn connect(host: String, port: Int = 5432, timeout: Int = 30): String {
  "{host}:{port}, timeout {timeout}s"
}

print connect("localhost")
print connect("localhost", 3306)
print connect("localhost", timeout: 10)
// prints localhost:5432, timeout 30s
// prints localhost:3306, timeout 30s
// prints localhost:5432, timeout 10s
```

Ein Parameter mit Standardwert kann wegfallen. Ein Aufruf kann einen Parameter beim Namen nennen, `timeout: 10`, um die
davor zu überspringen.

## Closures

```trb run
const double = { x: Int => x * 2 }
const triple: (Int) => Int = { _ * 3 }

print double(21)
print triple(7)
// prints 42
// prints 21
```

Eine Closure wird immer `{ parameters => body }` geschrieben. Ist der Typ klar aus der Stelle, an die die Closure geht,
kannst du die Parameter weglassen und `_` für den ersten schreiben. Ein `{`, wo ein Wert erwartet wird, ist immer eine
Closure, nie ein Block. `return` in einer Closure verlässt die Closure, nicht die Funktion um sie herum.

## Das letzte Argument kann dem Aufruf folgen

```trb run
const numbers = [1, 2, 3, 4]
const doubled = numbers.map { _ * 2 }
const total = numbers.fold 0 { sum, number => sum + number }

print doubled.toList()
print total
// prints [2, 4, 6, 8]
// prints 10
```

Ist der letzte Parameter eine Funktion, kann die Closure hinter dem Aufruf stehen statt in den Klammern. So lesen sich
`test "name" { ... }` und deine eigenen Kontrollstrukturen wie eingebaute Syntax. Ein Aufruf ohne Argumente braucht
trotzdem `()`: `distance` allein ist die Funktion selbst, kein Aufruf.

## Weiter

- [Typen und Methoden](types-and-methods.md) - deine eigenen Typen, und Funktionen, die zu ihnen gehören.
- [Declaring a function](../language/functions/declaring-a-function.md) - die genauen Regeln.
- [Closures](../language/functions/closures.md) - was eine Closure erfasst, und wann.
