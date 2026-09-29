---
title: Eine Tour durch TorbScript
summary: Die ganze Sprache in fünfzehn Minuten für alle, die schon programmieren - Bindungen, Aufrufe, Funktionen, Typen, Fälle, Fehler, Traits und Pipelines, je ein kurzes Beispiel.
kind: guide
status: stable
order: 10
translates: f1287491c49c
---

Du kannst schon programmieren. Diese Tour zeigt, wie TorbScript aussieht und wo es anders ist als das, was du kennst.
Jedes Beispiel läuft direkt auf der Seite: Ändere es und führe es noch einmal aus.

## Ziel

Nach dieser Tour kannst du TorbScript lesen und ein kleines Programm darin schreiben, und du weißt, welche Gewohnheiten
aus anderen Sprachen nicht kompilieren.

## Bindungen

```trb run
const fixed = [1, 2]
var buffer = fixed
buffer.append 3
print "{fixed} {buffer}"
// prints [1, 2] [1, 2, 3]
```

`const` ändert sich nie, `var` schon. Gibst du einem Wert einen zweiten Namen, entsteht eine Kopie: `buffer` zu ändern
lässt `fixed` in Ruhe. Das gilt auch für Listen, Maps und deine eigenen Typen: Zwei Namen teilen sich nie einen Wert,
und ein Wert ändert sich nur dort, wo du ihn änderst. Die Kopie ist billig, weil der Speicher geteilt bleibt, bis eine
Seite schreibt.

`const` wirkt bis ganz unten: Über ein `const` kannst du kein Feld ändern und keine Methode aufrufen, die den Wert
ändert. Deshalb gibt es keine `MutableList` - eine `var`-Liste ist eine. Die Ausnahme ist ein `shared type`, etwa eine
Datei oder ein Socket: Er hat eine Identität, und ein zweiter Name zeigt auf dieselbe.

## Aufrufe ohne Klammern

```trb run
print "Hello"
print "one", "two"
const count = [1, 2, 3].length()
print(count + 1)
// prints Hello
// prints one two
// prints 4
```

Ein Aufruf am Anfang einer Zeile, nach `=`, nach `return` und nach `=>` steht ohne Klammern: `print "Hello"`,
`return Fail problem`. Die Klammern bleiben, wo sie nötig sind:

- der Aufruf hat keine Argumente: `list.length()`
- der Aufruf steht in einem anderen Aufruf: `print area(3, 4)`
- ein Argument hat auf oberster Ebene einen Operator: `print(count + 1)`
- der Aufruf ist die Bedingung eines `if`, `for`, `while` oder `match`

Merken musst du dir das nicht: `torb format` schreibt es für dich. Eine Anweisung endet am Ende ihrer Zeile. Es gibt
keine Semikolons, und zwei Anweisungen teilen sich nie eine Zeile.

## Funktionen und Closures

```trb run
fn area(width: Int, height: Int): Int {
  width * height
}

const numbers = [1, 2, 3]
const total = numbers.fold 0 { sum, number => sum + number }
print area(3, 4)
print numbers.map({ _ * 2 }).toList()
print total
// prints 12
// prints [2, 4, 6]
// prints 6
```

Jeder Parameter hat einen Typ. Der Ergebnistyp ergibt sich aus der letzten Zeile des Rumpfs, und eine
`public`-Funktion schreibt ihn aus. Eine Closure ist immer `{ parameters => body }`, und `_` und `_2` stehen für den
ersten und zweiten Parameter, wenn du die Liste weglässt. Ist die Closure das letzte Argument, folgt sie dem Aufruf:
`numbers.fold 0 { ... }`.

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

  static fn square(size: Int): Self {
    Self size, size
  }
}

var box = Rectangle 3, 4
box.grow 1
print box
print Rectangle.square(2).area()
// prints Rectangle(width: 4, height: 4)
// prints 4
```

`type` ist das eine Schlüsselwort für Daten: Struct, Klasse und Record in einem. Ein Feld ist `const`, außer es sagt
`var`. Der Konstruktor, `==`, Hashing, Ausgabe und `copy` gibt es gratis. Eine Methode führt `self` nicht auf: Das Wort
vor `fn` sagt, was sie ist - nichts für eine Methode, die nur liest, `var fn` für eine, die den Wert ändert,
`static fn` für eine, die zum Typ gehört.

## Fälle und match

```trb run
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)

  fn area(): Float {
    match self {
      .Circle(radius) => 3.0 * radius * radius
      .Rectangle(width, height) => width * height
    }
  }
}

const shapes = [Shape.Circle(1.0), Shape.Rectangle(2.0, 3.0)]
for shape in shapes {
  print shape.area()
}
// prints 3.0
// prints 6.0
```

Ein `type` kann `case`s aufzählen: die Formen, die ein Wert annehmen kann. Das deckt ab, was andere Sprachen Enum oder
Sealed Class nennen. Ein Fall wird mit seinem Typ geschrieben, `Shape.Circle`, oder mit einem Punkt, `.Circle`, wo der
Typ schon klar ist. Er steht nie allein, außer `Some`, `None`, `Ok` und `Fail`, die jede Datei hat.

Ein `match` muss jeden Fall behandeln. Kommt später ein `.Triangle` dazu, wird jedes `match` über `Shape` zu einem
Kompilierfehler an der Zeile, die einen neuen Zweig braucht. In einem Muster nimmt ein kleingeschriebener Name wie
`radius` den Wert auf, und ein großgeschriebener ist ein Fall. Ein Name, den der Zweig nie benutzt, ist ein Fehler:
Schreib stattdessen `_`.

## Kein null: Option

```trb run
type User {
  id: Int
  name: String
}

const users = [User(1, "Ada"), User(2, "Grace")]

fn findUser(id: Int): User? {
  users.find { _.id == id }
}

print(findUser(2)?.name ?? "nobody")
print(findUser(9)?.name ?? "nobody")
// prints Grace
// prints nobody
```

Es gibt kein `null`. Ein Wert, der fehlen kann, hat den Typ `User?`, und der Compiler lässt dich den fehlenden Fall
behandeln. `?.` greift in den Wert, wenn es einen gibt, und `??` liefert den Ersatz, wenn es keinen gibt.

## Keine Exceptions: Result und ?

```trb run
fn parsePort(text: String): Result<Int, String> {
  const port = Int.tryFrom(text).mapError({ _ => "{text} is not a number" })?
  if port < 1 || port > 65535 {
    return Fail "{port} is not a port"
  }
  port
}

print parsePort("8080")
match parsePort("nope") {
  Ok(port) => print "listening on {port}"
  Fail(problem) => print "cannot start: {problem}"
}
// prints Ok(8080)
// prints cannot start: nope is not a number
```

Eine Funktion, die scheitern kann, sagt es in ihrem Typ: `Result<Int, String>` ist entweder `Ok` mit einem `Int` oder
`Fail` mit einem `String`. Das `?` nach einem Aufruf gibt ein `Fail` direkt an den Aufrufer zurück, also ersetzt ein
Zeichen die Fehlerprüfung nach jedem Aufruf. Scheitert der Aufrufer mit einem anderen Fehlertyp, wandelt `?` den Fehler
über `From` um. Der Rumpf endet mit `port`, nicht mit `Ok(port)`: Der Wert wird für dich eingepackt.

`panic "..."` ist für Bugs, nicht für schlechte Eingaben. Es beendet das Programm mit Exit-Code 101, und nichts kann es
abfangen.

## Traits statt Vererbung

```trb run
trait Area {
  fn area(): Float
}

type Square with Area {
  side: Float

  fn area(): Float {
    side * side
  }
}

type Circle {
  radius: Float
}

extend Circle with Area {
  fn area(): Float {
    3.0 * radius * radius
  }
}

const shapes: List<Area> = [Square(2.0), Circle(1.0)]
for shape in shapes {
  print shape.area()
}
// prints 4.0
// prints 3.0
```

Es gibt keine Klassen und keine Vererbung. Was ein Typ kann, ist ein `trait`, und ein Typ bekommt ihn mit `with` oder
später mit `extend`. Ein Trait mit einer Methode heißt wie sie: `Hash`, `Equals`, `Show`, nie `Hashable`. Auch
Operatoren sind Traits: `+` ist `Add`, `==` ist `Equals`. Ein Trait funktioniert als Typ, also kann eine Liste ein
`Square` und einen `Circle` halten, und `&` verbindet zwei davon: `Show & Hash`.

## Listen, Maps und Pipelines

```trb run
const names = ["Ada", "Alan"]
const more = names.appended "Grace"
var ages = ["Ada": 36, "Grace": 45]
ages["Alan"] = 41
print "{names} {more}"
print ages
print more.filter({ _.byteLength() > 3 }).map({ _.toUpperCase() }).toList()
// prints ["Ada", "Alan"] ["Ada", "Alan", "Grace"]
// prints ["Ada": 36, "Grace": 45, "Alan": 41]
// prints ["ALAN", "GRACE"]
```

Eine Methode, die einen Wert an Ort und Stelle ändert, ist ein Verb und braucht ein `var`: `append`, `sort`, `remove`.
Ihr Zwilling, der eine geänderte Kopie zurückgibt, ist ein Partizip und funktioniert auf einem `const`: `appended`,
`sorted`, `removed`. `map`, `filter` und die anderen Schritte einer Pipeline sind faul: Nichts läuft, bis ein letzter
Schritt wie `toList()` die Werte durchzieht. `map` und `flatMap` bedeuten dasselbe auf einer `Option`, einem `Result`
und einem `Task`.

## Dateien und Pakete

```trb
use File from "std/fs"
use greeting from "./greeting"

print greeting("World")
```

Eine Deklaration auf oberster Ebene gehört ihrer Datei, außer sie sagt `public`. `use` holt einen Namen aus einer
anderen Datei oder aus der Standardbibliothek. `print`, `List`, `Option`, `Result` und die anderen Grundlagen brauchen
kein `use`. Ein Projekt ist ein Ordner mit einer `project.trb`, und auch dieses Manifest ist TorbScript.

## Gewohnheiten, die brechen

Was du aus anderen Sprachen mitbringst und hier nicht kompiliert:

- `let` und `let mut` sind `const` und `var`. Es gibt kein `null` und kein `throw`: Nimm `Option` und `Result`.
- `Err(e)` ist `Fail(e)`. Ein Fall ist `Shape.Circle` oder `.Circle`, nie `Shape::Circle` oder ein nacktes `Circle`.
- `class` und `extends` sind `type` und ein Trait. `fn area(self)` ist `fn area()`, und `mutating func` ist `var fn`.
- `list.add(x)` ist `list.append x`: Eine Liste hängt an, eine Menge fügt ein, eine Map setzt.
- `MAX_SIZE` ist `maxSize`: Der erste Buchstabe eines Namens ist eine Regel, groß nur für Typen, Traits und Fälle.
- Ein Semikolon ist ein Fehler, und ein Aufruf am Zeilenanfang lässt seine Klammern weg, wo er kann: `print "hi"`.

Jede davon, mit der Meldung, die der Compiler ausgibt, steht auf
[What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md).

## Weiter

- [Installieren und ausführen](installing-and-running.md) - TorbScript auf deinem eigenen Computer, und dein erstes
  Projekt.
- [Coming from another language](coming-from/index.md) - eine Tabelle für Rust, TypeScript, Python, Go, Kotlin oder
  Swift.
- [Syntax cheat sheet](../language/syntax/cheat-sheet.md) - jede Form der Sprache auf einer Seite, beim Schreiben.
