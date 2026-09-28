---
title: TorbScript
summary: Eine Programmiersprache für Skripte, Werkzeuge und Server. Sie findet Fehler, bevor dein Programm läuft, startet sofort und wird schnell, wenn du es auslieferst.
kind: site
status: stable
order: 10
translates: 6b145ab9e15e
---

## Wo willst du anfangen?

- [Neu im Programmieren?](../start/index.md) Lerne von null an, in dreizehn kurzen Lektionen mit Code, den du direkt auf der Seite ausführst.
- [Du programmierst schon?](../guide/index.md) Eine schnelle Tour für alle, die schon eine andere Sprache kennen, und was hier anders ist.
- [In die Tiefe](../index.md) Jede Regel der Sprache, jedes Paket der Standardbibliothek, jeder Befehl der Werkzeuge.

## Nur ändern, was du ändern willst

Gibst du einer Liste einen zweiten Namen, entsteht eine Kopie. Änderst du die Kopie, bleibt das Original, wie es war:
Ein Wert ändert sich nie hinter deinem Rücken, nur dort, wo du ihn änderst. Siehe
[Wertsemantik](../glossary.md#value-semantics).

```trb run
const original = [1, 2]
var copy = original
copy.append 3
print "{original} {copy}"
// prints [1, 2] [1, 2, 3]
```

## Fehler zeigen sich, bevor das Programm läuft

Wenn etwas fehlen oder scheitern kann, sagt sein Typ das, und der [Compiler](../glossary.md#compiler) sorgt dafür,
dass du damit umgehst, bevor das Programm läuft. Ein Fehlschlag ist eine Antwort wie jede andere: Es gibt kein
[null](../glossary.md#null) und keine [Exception](../glossary.md#exception), die man vergessen kann.

```trb run
match Int.tryFrom("zweiundvierzig") {
  Ok(number) => print number
  Fail(_) => print "Das ist keine Zahl."
}
// prints Das ist keine Zahl.
```

## Sofort beim Schreiben, schnell beim Ausliefern

`torb run` startet ein Programm sofort. `torb build` macht aus demselben Programm eine eigene Programmdatei: schnell,
und sie läuft ohne TorbScript. Dasselbe eine Werkzeug testet, formatiert und veröffentlicht auch. Siehe
[den Befehl torb](../tooling/the-torb-command.md).

```console
$ torb run hello.trb
Hello, World!
$ torb build hello.trb --output hello
wrote hello
$ ./hello
Hello, World!
```
