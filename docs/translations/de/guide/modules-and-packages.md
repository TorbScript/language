---
title: Module und Pakete
summary: Ein Programm mit public und use auf Dateien aufteilen, aus der Standardbibliothek importieren und ein Projekt so anlegen, dass seine Tests seinen Code erreichen.
kind: guide
status: stable
order: 110
prerequisites:
  - control-flow-and-dsls.md
translates: ad8eefcd8a88
---

Jede Datei ist ein Modul. Ein Name gehört seiner Datei, bis er `public` sagt, und eine andere Datei holt ihn mit `use`
herein.

## Ziel

Am Ende dieser Seite kannst du ein Projekt auf Dateien aufteilen, aus der Standardbibliothek importieren und eine
eigene Funktion testen.

## Einen Namen zwischen Dateien teilen

`src/greeting.trb`:

```trb
public fn greeting(name: String): String {
  "Hello, {name}!"
}
```

`src/main.trb`:

```trb
use greeting from "./greeting"

print greeting("World")
```

`use` nennt eine Datei über ihren Pfad relativ zur importierenden Datei, ohne `.trb`. Ein Name ohne `public` lässt sich
nicht importieren: Die `use`-Zeile ist ein Fehler.

## Die Standardbibliothek

```trb check
use File, IoError from "std/fs"

fn readSettings(path: String): Result<String, IoError> {
  File.readText path
}
```

Die Standardbibliothek ist eine Reihe von Paketen namens `std/...`, und sie kommen mit TorbScript. Die Grundlagen wie
`print`, `List`, `Option` und `Result` sind in jeder Datei ohne `use` da. Alles, was aus dem Programm hinausreicht, wie
Dateien oder das Netzwerk, braucht eines - so zeigt der Kopf einer Datei, was sie berührt.

## Ein Projekt

```text
hello/
├ src/
├─ main.trb             das Programm, das torb run startet
├─ greeting.trb         ein Modul
├ tests/
├─ greeting.test.trb    ein Test
└ project.trb           das Manifest
```

Die Namen entscheiden, was jede Datei ist. `src/main.trb` ist das Programm, und nichts importiert es. Also liegt der
Code, den Programm und Tests teilen, in einem Modul wie `src/greeting.trb`, und beide importieren es.

## Deinen Code testen

`tests/greeting.test.trb`:

```trb
use test from "std/test"
use greeting from "../src/greeting"

test "greets by name" {
  assert(greeting("World") == "Hello, World!")
}
```

```console
$ torb test
tests/greeting.test.trb
  ok      greets by name

1 passed, 0 failed (1 file)
```

`torb test` führt jede Datei aus, deren Name auf `.test.trb` endet. `group "name" { ... }` fasst mehrere Tests unter
einem Namen zusammen. Die Klammern in `assert(...)` sind nötig, weil das Argument auf oberster Ebene einen Operator hat.

## Weiter

- [Alles zusammensetzen](a-small-program.md) - ein Programm, das alles bisherige benutzt.
- [use](../language/modules-and-packages/use.md) - jede Form eines Imports, auch das Umbenennen.
- [Packages](../language/modules-and-packages/packages.md) - was ein Paket ist, und wie ein anderes Projekt davon
  abhängt.
