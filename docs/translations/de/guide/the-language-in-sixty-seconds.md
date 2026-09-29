---
title: Die Sprache in sechzig Sekunden
summary: "Das mentale Modell von TorbScript auf einem Bildschirm: Werte, Bindungen, kein null, keine Exceptions, Traits, und Aufrufe, die als Commands geschrieben werden."
kind: guide
status: stable
order: 0
translates: 4ac53036d198
---

TorbScript sieht aus wie Rust, Swift und Kotlin und ist keines davon. Sechs Ideen erklären fast jede Zeile davon. Lies
das, bevor du TorbScript schreibst; jede Idee verlinkt zu der Seite mit den genauen Regeln.

## Ziel

Nach dieser Seite kannst du TorbScript lesen, und du weißt, welche deiner Gewohnheiten aus anderen Sprachen Code
erzeugen, der nicht kompiliert.

## Alles ist ein Wert, und die Bindung entscheidet

Es gibt einen `Point`, nicht einen `Point` und einen `MutablePoint`. Es gibt eine `List`, nicht `List` und
`MutableList`. Eine `const`-Bindung ändert sich nie, und nichts darunter ändert sich; eine `var`-Bindung kann an Ort
und Stelle geändert werden.

```trb run
const fixed = [1, 2]
var buffer = fixed
buffer.append 3
print "{fixed} {buffer}"
// prints [1, 2] [1, 2, 3]
```

Das gibt `[1, 2] [1, 2, 3]` aus. Zuweisen, Übergeben und Erfassen eines Werts ist eine **Kopie**, sodass zwei
Bindungen nie auf dasselbe zeigen und eine Änderung genau dort passiert, wo sie geschrieben steht. Mutation braucht
ein `var` bis ganz nach unten: eine `var`-Bindung, einen `var`-Parameter oder einen `var fn`-Empfänger, dann
`var`-Felder. Siehe [Bindings](../language/values-and-types/bindings.md) und
[Why values instead of references](../explanation/why-values-instead-of-references.md).

Die eine Ausnahme ist ein `shared type`, der eine Identität hat: Ihn zuzuweisen kopiert nicht. Dateien, Sockets und
Channels sind Shared Types; fast nichts anderes ist es.

## Es gibt kein null und keine Exceptions

Abwesenheit ist `Option<Value>`, geschrieben `Value?`. Scheitern ist `Result<Value, Failure>`, dessen Fälle `Ok` und
`Fail` sind. Das nachgestellte `?` entpackt ein `Ok` oder gibt das `Fail` aus der umgebenden Funktion zurück, wobei es
den Fehlertyp unterwegs über `From` umwandelt.

```trb
use File from "std/fs"

fn firstLine(path: String): Result<String, Error> {
  const text = File.readText(path)?
  const lines = text.lines()
  match lines.first() {
    Some(line) => line
    None => ""
  }
}
```

`??` ist der Ersatzwert (`findUser(1)?.name ?? "anonymous"`), `?.` bildet über eine `Option` ab. `panic "..."` ist für
Bugs: Es gibt auf der Standardfehlerausgabe aus und beendet sich mit Code 101, und nichts anderes läuft danach noch.
Siehe [Result](../language/errors/result.md).

## Ein Schlüsselwort deklariert jeden Datentyp

`type` ist Struct, Class, Enum und algebraischer Datentyp zugleich. Felder sind `const`, sofern nicht `var` markiert;
Member sind öffentlich, sofern nicht `private` markiert. `Equals`, `Hash`, `Show` und `copy` werden erzeugt.

Ein Member sagt mit zwei Wörtern vor `fn`, was es ist: nichts für eine Methode, die ihren Empfänger nicht auflistet;
`var fn` für eine, die ihn ändert; `static` für eine, die zum Typ gehört (`static fn square(size: Int): Self`,
`static origin = Point(0, 0)`). Siehe [Methods and `static fn`s](../language/types/methods.md).

```trb
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)

  fn area(): Float {
    match self {
      .Circle(radius) => Float.pi * radius ** 2
      .Rectangle(width, height) => width * height
    }
  }
}

const shape = Shape.Circle 2.0
print shape.area()
```

Ein Fall wird als `Shape.Circle` geschrieben, oder als `.Circle`, wo der erwartete Typ schon sagt, welcher Typ gemeint
ist. Er ist **niemals nackt**, außer die Datei importiert ihn (`use Option.Some from "std/core"`), und die Prelude
importiert `Some`, `None`, `Ok` und `Fail` für dich. Ein `match` ist ein Ausdruck und muss jeden Fall abdecken. Siehe
[Cases and match](../language/pattern-matching/cases-and-match.md).

In einem Pattern entscheidet der erste Buchstabe: ein kleingeschriebener Name **bindet**, ein großgeschriebener ist
ein Fall. Deshalb ist der erste Buchstabe jeder Deklaration eine Regel, die der Compiler meldet (`type Point`, `fn
distance`, `const maxSize` – es gibt kein `MAX_SIZE`), und deshalb ist eine Bindung eines `match`-Zweigs, eines `if
const` oder eines `while const`, die der Zweig nie liest, ein Fehler: Schreib `_`, oder `_name`, um den Namen zu
behalten. Siehe [Naming](../language/syntax/naming.md) und
[Pattern forms](../language/pattern-matching/pattern-forms.md).

## Capabilities sind Traits, und ein Typ kommt mit ihnen `with`

Es gibt keine Vererbung. Ein Trait mit einer erforderlichen Methode wird nach dieser Methode benannt – `Hash`,
`Equals`, `Compare`, `Show`, `Add`, `Close` –, und ein Typ sagt `with Hash`. Operatoren sind Traits: `+` ist
`Add.add`, `==` ist `Equals.equals`, `a[i]` ist `Index.at`.

```trb
trait Area {
  fn area(): Float
}

type Square with Area {
  side: Float

  fn area(): Float {
    side * side
  }
}
```

`&` schneidet Traits (`fn audit(entry: Show & Encode)`), `where Item: Hash` grenzt einen Typparameter ein, und
`extend` fügt einem bereits bestehenden Typ Member hinzu. Siehe [Traits](../language/traits/traits.md).

## Ein Aufruf wird als Command geschrieben, wo immer es geht

`Ok value`, `print "hello"`, `return Fail problem`, `names.map Role`. Klammern erscheinen, wo die Grammatik sie
braucht: verschachtelte Aufrufe (`Ok Some(x)`), keine Argumente (`list.length()`), ein Operator auf oberster Ebene
eines Arguments (`assert(sum == 3)`), mehrere Zeilen, und der Kopf eines `if`, `for`, `while` oder `match`. Das ist
keine Vorliebe, das ist der Formatter-Kanon, und `torb format --check` erzwingt es. Siehe
[Command calls](../language/syntax/command-calls.md).

Anweisungen enden am Ende der Zeile. Es gibt keine Semikolons, und zwei Anweisungen teilen sich nie eine Zeile.

## Pipelines sind faul, und ein Vokabular wird geteilt

`map`, `filter`, `flatMap`, `take`, `sorted` und der Rest geben ein `Iterate` zurück und laufen nicht, bis eine
terminale Operation die Werte durchzieht.

```trb
type Employee {
  name: String
  age: Int
}

const employees = [Employee("Ada", 36), Employee("Alan", 41)]
const names = employees.filter({ _.age >= 40 }).map({ _.name }).toList()
print names
```

`map` und `flatMap` bedeuten dasselbe auf `Option`, `Result`, `Task` und `Iterate`, per Konvention statt über
Higher-Kinded Types. Eine Closure ist immer `{ parameters => body }`, und `_`, `_2` sind ihre impliziten Parameter.
Siehe [the language reference](../language/index.md).

## Was du aus anderen Sprachen übernehmen kannst, und was nicht

Die Gewohnheiten, die nicht funktionieren, sind eine Liste, jede mit der Diagnose, die sie erzeugt:
[What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md). Kurz: `var` statt `let
mut` und `mutating func`, eine Kopie oder ein `var`-Parameter statt eines Borrows, `Option` statt `null`, `Result`
und `?` statt Exceptions, `Shape.Circle` statt eines nackten Falls, `type` und Traits statt Classes, `f a, b` statt
`f(a, b)`, und keine Semikolons.

## Weiter

- [Run your first program](installing-and-running.md) - von nichts bis zur Ausgabe.
- [Values and bindings](values-and-bindings.md) - die Mutationsregeln vollständig.
- [What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md) - die Fehler, mit den
  Diagnosen, die sie erzeugen.
- [Idiomatic TorbScript](idiomatic-torbscript.md) - die Gewohnheiten, denen die Standardbibliothek folgt, sobald der
  Code kompiliert: Namen, Verben und Partizipien, Kapseln, `using`, Tasks.
- [The language reference](../language/index.md) - eine Seite pro Konstrukt.
