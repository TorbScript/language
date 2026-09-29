---
title: TorbScript in 15 Minuten
summary: Die schnellste ehrliche Tour durch TorbScript für einen arbeitenden Programmierer, ein kurzes Beispiel und ein paar Sätze pro Idee.
kind: guide
status: stable
order: 5
translates: ccd34b5332a2
---

Du weißt schon, wie man programmiert. Diese Seite ist alles an TorbScript, was nicht einfach "das, was du schon
kennst, mit anderen Schlüsselwörtern" ist – lies sie einmal, dann schreib echten Code und schau Dinge nach, während
du weitermachst.

## Ziel

Am Ende dieser Seite kannst du ein gewöhnliches TorbScript-Programm schreiben – Bindungen, Funktionen, einen Typ, ein
`match`, ein `Result`, eine Pipeline – und du weißt, welchen Befehl du wofür ausführst.

## Bindungen und Mutation

```trb run
const answer = 42
var counter = 0
counter = counter + 1

var list = [1, 2]
const frozen = list
list.append 3
print "{counter} {list} {frozen}"
// prints 1 [1, 2, 3] [1, 2]
```

`const` ändert sich nie; `var` kann. Das ist die ganze Geschichte – es gibt keinen zweiten `MutableList`-Typ, nach
dem du greifen müsstest. Einen Wert zuzuweisen, zu übergeben oder zu erfassen **kopiert** ihn immer
([Wertsemantik](../glossary.md#value-semantics)), also sieht `frozen` oben das `append` nie. Es gibt keinen Borrow
Checker, den du zufriedenstellen müsstest, und keine Referenz, die sich versehentlich teilt: Der Preis ist, dass bei
jeder Zuweisung eine Kopie passiert, auch wenn der Compiler das eigentliche Kopieren dort überspringt, wo nichts den
Unterschied merken könnte.

## Funktionen und Command Calls

```trb run
fn area(width: Int, height: Int): Int {
  width * height
}

print area(3, 4)
print "{[1, 2, 3].map({ _ * 2 }).toList()}"
// prints 12
// prints [2, 4, 6]
```

Ein Parameter hat immer einen Typ; der Rückgabetyp wird aus dem letzten Ausdruck abgeleitet, außer die Funktion ist
`public`. Der größere Unterschied ist, wie ein Aufruf geschrieben wird: `print area(3, 4)` hat keine Klammern um
`print`s eigenen Aufruf, weil ein Aufruf sie überall dort ablegt, wo die Grammatik es erlaubt – das erzwingt `torb
format`, keine Geschmacksfrage. Ein verschachtelter Aufruf, ein Argument, das mit `[` beginnt, oder eine Trailing
Closure brauchen ihre Klammern trotzdem, wie die zweite Zeile zeigt.

## Typen und Methoden

```trb run
type Rectangle {
  var width: Int
  height: Int

  fn area(): Int {
    width * height
  }

  var fn grow(by: Int) {
    width = width + by
  }
}

var box = Rectangle 3, 4
box.grow 1
print box.area()
// prints 16
```

Ein Schlüsselwort, `type`, ist Struct, Class und Record zugleich – es gibt keine eigene `class`. Eine Methode listet
nie ihren Empfänger auf (keinen `self`-Parameter); zwei Wörter vor `fn` sagen, was für ein Member es ist: nichts für
eine schlichte Methode, `var fn` für eine, die den Empfänger an Ort und Stelle ändert, `static fn` für eine, die zum
Typ selbst gehört. Felder und Methoden erzeugen `Equals`, `Hash` und `Show` kostenlos, weshalb `print` oben keinen
zusätzlichen Code braucht.

## Fälle und match

```trb run
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
}

fn area(shape: Shape): Float {
  match shape {
    .Circle(radius) => 3.14159 * radius * radius
    .Rectangle(width, height) => width * height
  }
}

print area(Shape.Circle(2.0))
// prints 12.56636
```

Ein `type` kann `case`s tragen, statt Feldern oder neben ihnen – eine Deklaration deckt ab, was andere Sprachen in ein
`enum` und eine Sealed Hierarchy aufteilen. `match` ist ein Ausdruck und muss jeden Fall abdecken: Füg später
`.Triangle` hinzu, und jedes `match` auf `Shape` wird zu einem Kompilierfehler an genau der Zeile, die einen weiteren
Zweig braucht, statt zu einer still falschen Antwort. Ein Fall wird mit seinem Typ geschrieben, oder als `.Circle`, wo
der Typ schon bekannt ist – nie nackt, außer die Datei importiert ihn.

## Fehler als Werte, und `?`

```trb run
fn parsePort(text: String): Result<Int, String> {
  const port = Int.tryFrom(text).mapError({ _ => "{text} is not a number" })?
  if port < 1 || port > 65535 {
    return Fail "{port} is not a port"
  }
  port
}

print parsePort("8080")
print parsePort("nope")
// prints Ok(8080)
// prints Fail("nope is not a number")
```

Es gibt keine Exceptions. Eine Funktion, die scheitern kann, liefert `Result<Value, Failure>`, dessen Fälle `Ok` und
`Fail` sind. `?` allein ist die ganze Geschichte, um eins weiterzureichen: Es entpackt ein `Ok`, oder gibt sofort das
`Fail` zurück, wobei es den Fehlertyp unterwegs umwandelt, wenn der Zieltyp sagt, wie. `panic "..."` gibt es auch,
aber das ist für einen Bug, nicht für schlechte Eingaben – es beendet den Prozess und lässt sich nicht abfangen.

## Kein null: Option

```trb run
type User {
  id: Int
  name: String
}

const users = [User(1, "Ada"), User(2, "Grace")]

fn findUser(id: Int): User? {
  users.find({ _.id == id })
}

print(findUser(2)?.name ?? "nobody")
print(findUser(9)?.name ?? "nobody")
// prints Grace
// prints nobody
```

`Value?` ist `Option<Value>`, und das ist der einzige Weg, "vielleicht nichts" zu sagen – es gibt kein `null` und
keinen Pointer, der zufällig leer ist. `?.` bildet über die `Option` ab, statt sie zu entpacken, und `??` liefert den
Ersatzwert für `None`.

## Kollektionen und Pipelines

```trb run
const employees = [("Ada", 36), ("Alan", 41), ("Grace", 45)]

const names = employees
  .filter({ _.1 >= 40 })
  .map({ _.0 })
  .toList()

print names
// prints ["Alan", "Grace"]
```

`List`, `Map` und `Set` sind Werte wie alles andere, also gilt die Mutationsregel von oben unverändert für sie: ein
Verb (`append`, `insert`, `set`) ändert an Ort und Stelle und braucht ein `var`, sein Partizip (`appended`,
`inserted`) liefert eine geänderte Kopie. `map`, `filter` und der Rest einer Pipeline sind faul – sie bauen ein
`Iterate` und laufen nicht, bis eine terminale Operation wie `toList()` die Werte durchzieht, sodass sich Stufen ohne
versteckte Zwischenliste zusammensetzen.

## Traits

```trb run
trait Area {
  fn area(): Float
}

type Square {
  side: Float
}

extend Square with Area {
  fn area(): Float {
    side * side
  }
}

const shapes: List<Area> = [Square(2.0)]
print shapes[0].area()
// prints 4.0
```

Es gibt keine Vererbung. Eine Fähigkeit ist ein `trait`, den ein Typ bei seiner Deklaration bekommt (`type Square
with Area`) oder nachträglich mit `extend`, wie oben. Ein Trait mit genau einer erforderlichen Methode wird nach
dieser Methode benannt – `Hash`, `Equals`, `Show` –, und auch Operatoren sind Traits: `+` ist `Add.add`, `==` ist
`Equals.equals`. Ein Trait kann überall stehen, wo ein Typ stehen kann (`List<Area>`), womit eine Kollektion mehrere
konkrete Typen hinter einer Fähigkeit hält.

## Nebenläufigkeit, kurz gefasst

Eine Funktion, die wartet, liefert `Task<Value>`; `.await()` holt den Wert zurück, und eine Cancellation stoppt
denjenigen, der wartet, statt als Fehler abgefangen zu werden. `numbers.parallel()` führt eine gewöhnliche Pipeline
über die Worker-Threads der Maschine aus und liefert die Ergebnisse trotzdem in Eingabereihenfolge zurück – so viel
läuft schon heute. `Channel` und ein vollständiger asynchroner `Stream` sind entworfen (siehe
[Concurrency and streams](../language/concurrency-and-streams/index.md)), aber noch kein Backend führt sie aus, also
sind `Task`, `await()` und `parallel()` vorerst die ganze Geschichte.

## Module und project.trb

```trb
public fn greeting(name: String): String {
  "Hello, {name}!"
}
```

```trb skip a second file's declaration, shown only to name the import that reaches it
use greeting from "./greeting"

print greeting("World")
```

Eine Deklaration auf oberster Ebene ist privat zu ihrer Datei, sofern nicht `public` markiert; `use` holt einen Namen
aus einem relativen Pfad oder aus der Standardbibliothek herein (`use File from "std/fs"`), nie einen nackten,
unqualifizierten Import von allem. `project.trb` ist das Manifest, und es ist TorbScript selbst, ausgeführt in einer
Sandbox, die nur Dateien unterhalb ihres eigenen Verzeichnisses lesen kann:

```trb fragment
name = "hello"
version = "0.1.0"
```

## Die Toolchain

Eine Binärdatei, `torb`, macht das alles:

| Befehl | Was er tut |
|---|---|
| `torb run <file>` | Prüft und führt eine Datei sofort in der VM aus - kein C-Compiler nötig |
| `torb run --native`, `torb build` | Kompiliert zuerst zu einer nativen Binärdatei (`build` tut das immer; `run` nur mit `--native`) |
| `torb test` | Führt jede `*.test.trb`-Datei unter den angegebenen Pfaden aus |
| `torb format`, `torb format --check` | Schreibt das eine Layout der Sprache, oder meldet, was nicht darin ist |
| `torb lsp` | Der Language Server: Diagnosen, Hover, Sprung zur Definition, Vervollständigung |
| `torb debug` | Der Debugger: Breakpoints, Stepping, lokale Variablen, Auswerten |

`torb check` und `torb format --check` sind die zwei Befehle, die du ausführst, bevor du irgendetwas für fertig
hältst. Die vollständige Liste und die Reihenfolge, in der du sie ausführst, steht in
[Verify your work](../tooling/verifying-your-work.md).

## Weiter

- [Values and bindings](values-and-bindings.md) und der Rest dieses Pfads - dieselben Ideen, eine Seite pro Stück,
  langsamer und mit den genauen Regeln.
- [Syntax cheat sheet](../language/syntax/cheat-sheet.md) - jede Form der Sprache, auf einen Blick, während du
  schreibst.
- [Coming from Rust, TypeScript, Python, Go, Kotlin or Swift](coming-from/index.md) - was direkt aus deiner Sprache
  übertragbar ist und was dich überraschen wird.
- [What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md) - die Gewohnheiten,
  die richtig aussehen und es nicht sind.
