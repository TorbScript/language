---
title: Module und Pakete
summary: Wie use einen Namen aus einer anderen Datei oder der Standardbibliothek hereinholt, und was public für eine Deklaration auf oberster Ebene bedeutet.
kind: guide
status: stable
order: 100
prerequisites:
  - control-flow-and-dsls.md
translates: 2b9c0354ba2f
---

Ein Programm wächst schnell über eine Datei hinaus. Diese Seite teilt eines in zwei, holt einen Namen mit `use`
herüber, und benennt das Paket, in dem die Standardbibliothek lebt.

## Ziel

Am Ende dieser Seite kannst du Code mit `use` und `public` auf Dateien aufteilen und einen Import aus der
Standardbibliothek für das lesen, was er benennt.

## Öffentliche Deklarationen und use

Eine Deklaration auf oberster Ebene ist privat zu ihrer Datei, sofern nicht `public` markiert. Gegeben
`src/greeting.trb`:

```trb
public fn greeting(name: String): String {
  "Hello, {name}!"
}
```

Eine andere Datei desselben Projekts erreicht sie mit `use`, wobei sie die Datei über einen relativen Pfad ohne
Endung benennt:

```trb
use greeting from "./greeting"

print greeting("World")
```

Ein Name, der nicht `public` ist, lässt sich überhaupt nicht importieren – nicht per Konvention verborgen, sondern
ein Kompilierfehler an der `use`-Zeile, die ihn nennt. Siehe
[Visibility](../language/modules-and-packages/visibility.md) und
[use](../language/modules-and-packages/use.md).

## Aus der Standardbibliothek importieren

Die Standardbibliothek ist eine Sammlung von Paketen namens `std/<name>`, und sie brauchen keinen Eintrag in den
Abhängigkeiten eines Projekts – sie kommen mit der Toolchain.

```trb
use File from "std/fs"

fn readConfiguration(path: String): Result<String, IoError> {
  File.readText path
}
```

`use File from "std/fs"` am Anfang einer Datei ist auch die Aussage "diese Datei rührt an Dateien": `std/fs` ist
nicht Teil der [Prelude](../language/modules-and-packages/the-prelude.md), des Pakets, dessen öffentliche Namen –
`Option`, `Result`, `List`, `print` und der Rest dessen, was jede Datei schon hat – überall ohne Import im Sichtbereich
sind.

## Ein Fall kommt über seinen Typ herein

Ein Fall eines Typs mit Fällen wird über diesen Typ importiert, und nur ein Fall kann das:

```trb
use Option, Option.Some, Option.None from "./option"
```

Danach sind `Some` und `None` in einem Pattern und in einem Ausdruck unqualifiziert, genau wie es die eigenen `Some`
und `None` der Prelude schon sind – siehe [Importing cases](../language/pattern-matching/importing-cases.md).

## Ein Paket ist ein Verzeichnis

```text
hello/
├ src/
├─ main.trb
├─ lib.trb
├─ greeting.trb
├ tests/
├─ greeting.test.trb
└ project.trb
```

Die Namen der Dateien sagen, was jede ist. `src/main.trb` ist das Programm, das `torb run` ausführt, `src/lib.trb`
ist das, was ein anderes Paket importiert, `src/greeting.trb` ist ein Modul, und eine Datei, deren Name auf
`.test.trb` endet, ist ein Test, den `torb test` ausführt, egal wo er liegt. `project.trb` benennt das Paket
`owner/name` und braucht für keine davon eine Zeile.

Das Programm wird nie importiert – eine Datei, die Code auf oberster Ebene enthalten darf, kann das nicht sein –,
also lebt das, was sich das Programm und ein Test teilen, in einem Modul wie `src/greeting.trb`, und beide
importieren es. Nur ein als Abhängigkeit aufgeführtes Paket lässt sich von einem anderen aus erreichen, über seinen
Namen und nie über einen relativen Pfad – siehe [Packages](../language/modules-and-packages/packages.md).

Ein Layout wie dieses – `src/main.trb` neben einem testbaren `src/greeting.trb` – ist genau das, wovon
[`torb new --template app`](../tooling/torb-new.md) ausgeht; eine Bibliothek mit einem `src/lib.trb` und nichts zum
Ausführen ist `torb new`s eigene Standardvorlage, `package`.

## Weiter

- [Tests and the toolchain](tests-and-tooling.md) - einen Test schreiben und die Prüfungen ausführen.
- [use](../language/modules-and-packages/use.md) - jede Import-Form, einschließlich Umbenennen und
  Namensraum-Importen.
- [The prelude](../language/modules-and-packages/the-prelude.md) - was überall im Sichtbereich ist, und warum
  Capabilities nicht dazugehören.
