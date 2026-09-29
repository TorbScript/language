---
title: Installieren und ausführen
summary: TorbScript mit einem Befehl installieren, eine Datei ausführen und ein Projekt mit einem Test anlegen - die fünf Befehle, die du jeden Tag brauchst.
kind: guide
status: stable
order: 20
translates: e60b0531db45
---

TorbScript ist ein Programm, `torb`. Es führt deinen Code aus, testet ihn, prüft ihn und formatiert ihn.

## Ziel

Am Ende dieser Seite ist TorbScript installiert, du hast eine Datei ausgeführt, und du hast ein Projekt mit einem Test,
der besteht.

## Installieren

Führe den einen Befehl für dein System von der [Installationsseite](../site/install.md) aus, öffne dann ein neues
Terminal und prüfe:

```console
$ torb --version
torb 0.1.0
```

## Eine Datei ausführen

Speichere das als `hello.trb`:

```trb run
const name = "World"
print "Hello, {name}!"
// prints Hello, World!
```

Und führe es aus:

```console
$ torb run hello.trb
Hello, World!
```

`{name}` setzt einen Wert in den Text ein. Eine Datei wie diese darf Code auf oberster Ebene enthalten; sie braucht
kein `main`.

## Ein Projekt anlegen

```console
$ torb new hello --offline
wrote hello/project.trb, hello/src/main.trb, hello/tests/main.test.trb
$ cd hello
$ torb run
Hello, hello
```

`--offline` schreibt sofort ein kleines Startprojekt. Ohne das stellt `torb new` ein paar Fragen und beginnt mit einer
Vorlage ([torb new](../tooling/torb-new.md)). Das Projekt besteht aus drei Dateien:

```text
hello/
├ src/
├─ main.trb           das Programm, das torb run startet
├ tests/
├─ main.test.trb      ein Test, weil sein Name auf .test.trb endet
└ project.trb         das Manifest: name = "hello", version = "0.1.0"
```

Auch `project.trb` ist TorbScript. Die Dateinamen entscheiden den Rest, also braucht das Manifest keine weiteren
Zeilen.

## Testen

`tests/main.test.trb` enthält einen Test:

```trb check
use test from "std/test"

test "hello runs" {
  assert(1 + 1 == 2)
}
```

```console
$ torb test
tests/main.test.trb
  ok      hello runs

1 passed, 0 failed (1 file)
```

`test` nimmt einen Namen und einen Block. `assert` nimmt eine Bedingung, und wenn sie nicht hält, gibt es die
Bedingung und die Werte darin aus - es gibt also nichts weiter zu lernen.

## Prüfen und formatieren

```console
$ torb check .
3 files, no problems
$ torb format --check .
0 of 3 files would change
```

`torb check` findet Fehler, ohne etwas auszuführen, und zeigt auf die Zeile. `torb format` schreibt deine Dateien im
einen Layout der Sprache; `--check` meldet nur. Führe beide aus, bevor du etwas fertig nennst.

## Ausliefern

`torb run` startet sofort. Willst du eine Programmdatei, die ohne TorbScript läuft, bau eine:

```console
$ torb build --output hello
wrote hello
$ ./hello
Hello, hello
```

`torb build` braucht einen C-Compiler auf deinem Computer; die [Installationsseite](../site/install.md) sagt, welchen.

## Weiter

- [Werte und Bindungen](values-and-bindings.md) - `const`, `var` und was eine Kopie bedeutet.
- [Module und Pakete](modules-and-packages.md) - mehr als eine Datei, und wie ein Test deinen Code erreicht.
- [The torb command](../tooling/the-torb-command.md) - jeder Befehl und jede Option.
