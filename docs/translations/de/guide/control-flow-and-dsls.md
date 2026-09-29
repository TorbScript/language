---
title: Kontrollfluss und eigene Konstrukte
summary: if, for, while und loop wie erwartet, und warum unless eine gewöhnliche Funktion ist, die du selbst hättest schreiben können.
kind: guide
status: stable
order: 90
prerequisites:
  - collections-and-pipelines.md
translates: 61f1bfbf5d3e
---

`if`, `for`, `while` und `loop` sehen so aus, wie sie überall aussehen. Anders ist hier, was passiert, sobald dir die
eingebauten ausgehen: Eine neue Kontrollstruktur ist eine Funktion, kein neues Stück Syntax.

## Ziel

Am Ende dieser Seite kannst du den eingebauten Kontrollfluss lesen, deine eigene Kontrollstruktur als Funktion
schreiben, und einen Konfigurationsblock für das lesen, was er ist: gewöhnlicher Code.

## Ein if ist ein Ausdruck

```trb
const temperature = 23

const feeling = if temperature < 10 {
  "cold"
} else if temperature < 25 {
  "pleasant"
} else {
  "hot"
}

print feeling
```

Jeder Zweig eines so benutzten `if` muss einen Wert desselben Typs liefern, genau wie jeder Zweig eines `match`.

## Schleifen: for, while und loop

```trb run
for i in 0..3 {
  print i
}

var attempts = 0
while attempts < 10 {
  attempts = attempts + 1
  if attempts > 3 {
    break
  }
  print "attempt {attempts}"
}
// prints 0
// prints 1
// prints 2
// prints attempt 1
// prints attempt 2
// prints attempt 3
```

`continue` und `break` funktionieren in beiden wie erwartet. `0..3` ist ein
[Range](../language/values-and-types/ranges.md); das Ende ist ausgeschlossen, also gibt dies `0`, `1` und `2` aus.

Das dritte ist `loop`, für eine Schleife, die nicht von selbst endet – ein Server, eine Leseschleife, eine
Zustandsmaschine:

```trb
var line = "first"
loop {
  print line
  line = ""
  if line.isEmpty() {
    break
  }
}
```

`while true` ist ein Kompilierfehler mit der Meldung `A loop that never ends is written `loop``, sodass es genau eine
Schreibweise dafür gibt. Ohne `break` hat ein `loop` den Typ `Never`, was einer Funktion, deren ganzer Rumpf einer
ist, erlaubt, ohne Ergebnis auszukommen; mit `break` ist es `Void` wie jede andere Schleife. Es gibt kein `break
value`. Siehe [Loops](../language/execution/loops.md).

## Eine Kontrollstruktur ist eine Funktion

`unless` ist kein Schlüsselwort. Es ist eine Funktion, deren zweiter Parameter eine Closure ist, aufgerufen mit einer
Trailing Closure, sodass es sich wie eine der eingebauten liest:

```trb
fn unless(condition: Bool, body: () => Void) {
  if !condition {
    body()
  }
}

const items: List<Int> = []
unless items.isEmpty() { print "not empty" }
```

Für den Compiler ist an `unless` nichts Besonderes – `do` und `retry` aus der Standardbibliothek sind genauso
geschrieben, als gewöhnliche Funktionen mit einer Closure oder einem
[`lazy`](../language/functions/parameter-modes.md)-Parameter. Siehe
[Control structures are functions](../language/extensibility/control-structures.md) für `retry`.

## Ein Konfigurationsblock ist eine Empfänger-Closure

Eine [Empfänger-Closure](../language/configuration/receiver-closures.md) ist eine Closure, deren erster Parameter
`self` heißt, sodass sich Namen darin gegen diesen Empfänger auflösen – genau wie innerhalb einer Methode.

```trb
type ServerOptions {
  var host: String = "localhost"
  var port: Int = 8080
}

fn serve(configure: (var self: ServerOptions) => Void): ServerOptions {
  var options = ServerOptions()
  configure options
  options
}

const options = serve {
  host = "0.0.0.0"
  port = 8443
}

print options
```

`host = "0.0.0.0"` ist kein Aufruf einer Methode namens `host` – `host` ist ein Feld, und ein Feld wird nur mit `=`
geschrieben. Zusammen sind Command Calls, Trailing Closures und diese Regel es, was `serve { ... }` oben wie ein
Stück eingebaute Syntax lesen lässt, obwohl es nichts als ein Funktionsaufruf ist. Siehe
[Builders and DSLs](../language/configuration/builders.md).

## Weiter

- [Modules and packages](modules-and-packages.md) - ein Programm in Dateien und ein Projekt aufteilen.
- [Control structures are functions](../language/extensibility/control-structures.md) - `do`, `retry`, und wie du
  eigene hinzufügst.
- [Receiver closures](../language/configuration/receiver-closures.md) - die genaue Regel, wie sich ein Name darin
  auflöst.
