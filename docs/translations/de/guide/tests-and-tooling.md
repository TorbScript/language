---
title: Tests und die Toolchain
summary: Wie du mit test, group und assert einen Test schreibst, und die zwei Befehle, die prüfen, ob das Geschriebene korrekt ist.
kind: guide
status: stable
order: 110
prerequisites:
  - modules-and-packages.md
translates: c7d03ae4dab1
---

Ein Test hier ist ein gewöhnlicher Funktionsaufruf, und der Compiler, der dein Programm typprüft, ist dasselbe
Werkzeug, das deine Tests ausführt. Diese Seite schreibt einen Test und führt die zwei Befehle aus, die du für den
Rest dieses Pfads benutzt.

## Ziel

Am Ende dieser Seite kannst du eine Testdatei mit `test` und `assert` schreiben und die zwei Befehle benennen, die
ein Programm prüfen, bevor du es aus der Hand gibst.

## Einen Test schreiben

Eine Testdatei endet auf `.test.trb`, und der Name allein macht sie dazu: Sie darf überall im Paket liegen, und
`tests/` ist, wo sie per Konvention liegt. Sie ist ein
[Skript](../language/modules-and-packages/top-level-code.md) aus `test`- und `group`-Aufrufen: Nichts importiert sie,
also darf sie diesen Code auf oberster Ebene enthalten, so wie es eine Einstiegsdatei kann. `torb test` im
Verzeichnis des Pakets führt jede davon aus.

```trb check
use test from "std/test"

fn greeting(name: String): String {
  "Hello, {name}!"
}

test "greets by name" { assert(greeting("World") == "Hello, World!") }
```

`test` nimmt einen Namen und eine Closure, deren Rumpf die Prüfung ist; `group` benennt eine Closure aus
`test`-Aufrufen, um eine Suite zu organisieren. `assert` nimmt die Bedingung als
[`Expression<Bool>`](../language/functions/quoted-expressions.md): Ein Fehlschlag gibt den Quelltext der Bedingung
und die darin erfassten Werte aus, sodass es kein Matcher-Vokabular zu lernen gibt. Siehe
[std/test](../standard-library/test.md).

Beachte die Klammern um `greeting("World") == "Hello, World!"`: Ein Operator auf oberster Ebene eines Arguments ist
eine der Stellen, an denen der Formatter-Kanon sie verlangt, selbst innerhalb des eigenen Arguments eines Command
Call.

## Ein Programm prüfen

`torb check` typprüft ein ganzes Projekt oder eine einzelne Datei und antwortet mit `no problems`, oder zeigt auf die
genaue Zeile:

```console
$ torb check examples/tour
14 files, no problems
```

Führ es vom Wurzelverzeichnis des Repositorys aus – siehe [Run your first program](installing-and-running.md).
`torb check` ist das Gate: Ein Fehlalarm davon ist ein Bug im Checker, nie ein Grund, ein korrektes Programm zu
ändern.

## Formatieren

`torb format` schreibt das eine Layout der Sprache über den Syntaxbaum – ein Aufruf wird überall zum Command, wo die
Grammatik es erlaubt, und bekommt sonst Klammern, zwei Leerzeichen pro Ebene, ein Leerzeichen um einen Operator –, und
`--check` meldet die Dateien, die nicht darin sind, ohne etwas zu schreiben:

```console
$ torb format --check examples/tour
0 of 14 files would change
```

Eine Änderung ist fertig, wenn beide Befehle grün sind. Die vollständige Liste, samt Testrunner, steht in
[Verify your work](../tooling/verifying-your-work.md).

## Weiter

- [Put it together](a-small-program.md) - ein Programm, das alles auf diesem Pfad benutzt.
- [The torb command](../tooling/the-torb-command.md) - jeder Unterbefehl.
- [Verify your work](../tooling/verifying-your-work.md) - die Befehle, die du der Reihe nach ausführst, bevor du
  fertig bist.
