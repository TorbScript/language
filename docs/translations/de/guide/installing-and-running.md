---
title: Dein erstes Programm ausführen
summary: Die Toolchain bauen, eine einzelne Datei ausführen, und ein Projekt mit Manifest, Quelldatei und Test erstellen.
kind: guide
status: stable
order: 10
translates: 97858038fbd7
---

Die Toolchain ist eine einzige Binärdatei namens `torb`. `torb run` prüft eine Datei und führt sie sofort in der VM
innerhalb von `torb` aus, wofür kein C-Compiler nötig ist, und `torb build` kompiliert sie stattdessen zu einer
nativen ausführbaren Datei – `torb run --native` macht beides in einem Schritt. Beide führen dasselbe Programm mit
derselben Ausgabe aus.

## Ziel

Am Ende dieser Seite hast du eine `.trb`-Datei ausgeführt, ein Projekt mit Manifest und Test erstellt, und die zwei
Befehle gesehen, die du am meisten benutzen wirst.

## Die Toolchain bauen

TorbScript ist selbst-hostend: Der Compiler ist in TorbScript geschrieben und kompiliert sich selbst aus einem Seed –
einem `torb`, das schon existiert. Leg eines unter `seed/` ab und bau es einmal, vom Wurzelverzeichnis des
Repositorys aus:

```console
$ sh tools/bootstrap.sh
seed: seed/torb
step 1: the seed builds the compiler
step 2: that compiler builds the compiler again

the fixpoint holds: both steps emitted the same C.
torb: build/release/torb
```

`build/release/torb` ist der Compiler, der dabei herauskommt, und `torb` unten ist diese Binärdatei. Ein
C-Compiler im `PATH` ist, was es braucht (`$TORB_CC`, oder `clang`, `gcc`, `cc`).

## Eine einzelne Datei ausführen

Schreib das in `hello.trb`:

```trb
const name = "World"
print "Hello, {name}!"
```

Und führ es aus:

```console
$ torb run hello.trb
Hello, World!
```

Zwei Dinge sind dabei passiert, die es wert sind, benannt zu werden. `print` ist eine gewöhnliche Funktion aus der
Prelude, aufgerufen als **Command**: `print "..."` statt `print("...")`, weil ein Aufruf überall dort ohne Klammern
geschrieben wird, wo es die Grammatik erlaubt. Und `{name}` innerhalb der Zeichenkette ist Interpolation, die mit
jedem Ausdruck funktioniert.

Eine Datei, die nichts importiert, darf Code auf oberster Ebene wie diesen enthalten. Eine Datei, die importiert
wird, enthält nur Deklarationen, weshalb es keine Reihenfolge der Modulinitialisierung in der Sprache gibt.

## Ein Projekt erstellen

`torb new <name>` ist der Anfang: Es schreibt dir ein Projekt, in der Form, die [`torb run`](../tooling/torb-run.md)
und [`torb test`](../tooling/torb-test.md) schon zu bauen wissen. Ohne `--offline` startet es von einer Vorlage von
[git.torb.dev](https://git.torb.dev) und stellt zuerst ein paar Fragen – siehe
[`torb new`](../tooling/torb-new.md) für all das. Diese Seite benutzt das schlichte, eingebaute Gerüst, das
`--offline` sofort schreibt, weil es das ist, worauf der Rest der Seite Zeile für Zeile aufbaut:

```console
$ torb new hello --offline
wrote hello/project.trb, hello/src/main.trb, hello/tests/main.test.trb
```

```text
hello/
├ src/
├─ main.trb          print "Hello, hello"
├ tests/
├─ main.test.trb      use test from "std/test", one passing test
└ project.trb          name = "hello", version = "0.1.0"
```

Führ es aus seinem Verzeichnis aus:

```console
$ cd hello
$ torb run
Hello, hello
```

`project.trb` ist eine TorbScript-Datei, keine Konfigurationssprache. Es läuft gegen einen eingebauten `Project`-Wert
in einer Sandbox, die Dateien unterhalb ihres eigenen Verzeichnisses lesen darf und nichts außerhalb davon, sodass ein
Werkzeug es sicher lesen kann:

```trb fragment
name = "hello"
version = "0.1.0"
```

`name = "hello"` schreibt das Feld `name` dieses `Project`, und das ist das ganze Manifest: `src/main.trb` ist das
Programm wegen seines Namens, und `tests/main.test.trb` ist ein Test wegen seines Namens, also braucht keines von
beiden eine Zeile. `torb new` schreibt den nackten Namen, den es bekommen hat; ein Paket, das veröffentlicht werden
soll, benutzt stattdessen `owner/name`, weil ein Owner ein verifizierter Namensraum einer Registry ist. Was `torb
new` sonst noch schreibt und warum, steht in [torb new](../tooling/torb-new.md).

## Es erweitern

`src/main.trb` ist das, was läuft, und ein Programm wird nie importiert – eine Datei, die Code auf oberster Ebene
enthalten darf, kann das nicht sein –, also kommt die Funktion, die der Test unten aufruft, in ein eigenes Modul,
`src/greeting.trb`:

```trb
public fn greeting(name: String): String {
  "Hello, {name}!"
}
```

`public` ist es, was einer anderen Datei erlaubt, `greeting` zu importieren: Eine Deklaration ist privat zu ihrer
Datei, sofern sie nichts anderes sagt. Ersetze die eine Zeile, die `torb new` in `src/main.trb` geschrieben hat,
durch einen Aufruf davon:

```trb skip it imports the 'src/greeting.trb' of the project this page creates, which one snippet of this documentation cannot provide
use greeting from "./greeting"

print greeting("World")
```

```console
$ torb run
Hello, World!
```

## Einen Test schreiben

Eine Testdatei ist ein Skript aus `test`- und `group`-Aufrufen, und die einzige Prüfung ist `assert`. Ersetze den
Platzhaltertest, den `torb new` in `tests/main.test.trb` geschrieben hat, durch einen, der `greeting` aufruft:

```trb skip it imports the 'src/greeting.trb' of the project this page creates, which one snippet of this documentation cannot provide
use test from "std/test"
use greeting from "../src/greeting"

test "greets by name" {
  assert(greeting("World") == "Hello, World!")
}
```

`torb test` führt jede Datei des Pakets aus, deren Name auf `.test.trb` endet:

```console
$ torb test
tests/main.test.trb
  ok      greets by name

1 passed, 0 failed (1 file)
```

`test` ist eine gewöhnliche Funktion aus `std/test`, deren letzter Parameter eine Closure ist, weshalb der Block der
Zeichenkette folgen kann. `assert` nimmt einen `Expression<Bool>`: Es bekommt die Bedingung **und ihren Quelltext**,
sodass ein Fehlschlag den Ausdruck und die Werte darin ausgibt, ohne ein Matcher-Vokabular, das man lernen müsste.
Beachte die Klammern um Bedingungen wie `sum == 3`: Ein Operator auf oberster Ebene eines Arguments ist eine der
Stellen, an denen der Kanon sie verlangt.

## Prüfen und formatieren

Die zwei Befehle, die du am häufigsten ausführen wirst:

```console
$ torb check .
4 files, no problems
$ torb format --check .
0 of 4 files would change
```

`check` typprüft alles und antwortet mit `no problems` oder zeigt auf eine Zeile. `format` schreibt das eine Layout
der Sprache über den Syntaxbaum, und `--check` meldet die Dateien, die nicht darin sind. Die vollständige Liste steht
in [Verify your work](../tooling/verifying-your-work.md).

## Weiter

- [Values and bindings](values-and-bindings.md) - `const`, `var`, und warum das die ganze Mutationsgeschichte ist.
- [The torb command](../tooling/the-torb-command.md) - jeder Unterbefehl.
- [The language reference](../language/index.md) - eine Seite pro Konstrukt.
