---
title: Kontrollfluss und eigene Konstrukte
summary: Ifs und Schleifen funktionieren wie erwartet, und eine neue Kontrollstruktur oder ein Konfigurationsblock ist eine gewöhnliche Funktion, die du selbst schreiben kannst.
kind: guide
status: stable
order: 100
prerequisites:
  - collections-and-pipelines.md
translates: e5fa4aac7438
---

`if`, `for`, `while` und `loop` sehen aus wie überall. Neu ist, dass du eigene hinzufügen kannst: Eine
Kontrollstruktur ist eine Funktion, keine neue Syntax.

## Ziel

Am Ende dieser Seite kannst du den eingebauten Kontrollfluss benutzen, eine eigene Kontrollstruktur schreiben und
einen Konfigurationsblock als den Funktionsaufruf lesen, der er ist.

## Ein if gibt einen Wert

```trb run
const temperature = 23
const feeling = if temperature < 10 {
  "cold"
} else if temperature < 25 {
  "pleasant"
} else {
  "hot"
}
print feeling
// prints pleasant
```

Ein `if` kann einen Wert geben, wie ein `match`. Dann gibt jeder Zweig einen Wert desselben Typs.

## Schleifen

```trb run
for index in 0..3 {
  print index
}

var attempts = 0
while attempts < 10 {
  attempts = attempts + 1
  if attempts > 2 {
    break
  }
  print "attempt {attempts}"
}
// prints 0
// prints 1
// prints 2
// prints attempt 1
// prints attempt 2
```

`0..3` zählt von 0 bis 3 ohne die 3; `0..=3` schließt sie ein. `break` und `continue` funktionieren wie gewohnt. Eine
Schleife, die nicht von selbst endet, etwa die eines Servers, ist `loop { ... }`: `while true` ist ein Fehler, der dir
das sagt.

## Eine eigene Kontrollstruktur schreiben

```trb run
fn unless(condition: Bool, body: () => Void) {
  if !condition {
    body()
  }
}

const items = [1, 2]
unless items.isEmpty() {
  print "{items.length()} items"
}
// prints 2 items
```

`unless` ist eine gewöhnliche Funktion, deren letzter Parameter eine Closure ist. Ein Aufruf ohne Klammern und eine
Closure dahinter lassen sie wie ein Schlüsselwort lesen. `test` aus der Standardbibliothek ist genauso geschrieben.

## Ein Konfigurationsblock

```trb run
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
// prints ServerOptions(host: "0.0.0.0", port: 8443)
```

Der Parameter der Closure heißt `self`, also bedeuten `host` und `port` im Block die Felder von `options`, so wie in
einer Methode. Der Block sieht aus wie eine Konfigurationsdatei und ist gewöhnlicher Code: Er wird typgeprüft, und ein
Tippfehler in einem Feldnamen ist ein Fehler.

## Weiter

- [Module und Pakete](modules-and-packages.md) - ein Programm in mehr als einer Datei.
- [Loops](../language/execution/loops.md) - die genauen Regeln von `for`, `while` und `loop`.
- [Builders and DSLs](../language/configuration/builders.md) - Konfigurationsblöcke im Detail.
