---
title: Idiomatisches TorbScript
summary: Die Gewohnheiten, die TorbScript wie TorbScript lesen lassen - Namen, Mutation, Aufrufe, Typen, Fehler, Closures, Ressourcen und Tasks - je als eine Regel, ein lauffähiges Beispiel und der Grund dahinter.
kind: guide
status: stable
order: 130
prerequisites:
  - a-small-program.md
translates: 8c3696da2619
---

**Ein Programm kann kompilieren und trotzdem nicht wie TorbScript lesen.** Diese Seite sammelt die Gewohnheiten, denen
die Standardbibliothek und der Compiler folgen, eine Regel pro Abschnitt: die Regel fett, das kleinste Programm, das
sie zeigt, und ein Satz Begründung. Die erste Hälfte ist das, wonach du täglich greifst; der Rest steckt in
`<details>` und ist es wert, einmal überflogen zu werden. Jede Regel verlinkt die Referenzseite mit den vollen
Einzelheiten.

Die Fehler, die ein Programmierer aus Rust, Swift, Kotlin oder TypeScript mitbringt, wiederholen sich hier nicht – sie
stehen in [What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md), jeder mit der
falschen Zeile und der Diagnose. Diese Seite ist die andere Hälfte: was zu schreiben ist, sobald das Programm
kompiliert.

## Ziel

Am Ende dieser Seite schreibst du TorbScript, das ein Leser der Standardbibliothek als seinesgleichen erkennt: die
Namen, die Aufrufe, die Typen und die Fehlerbehandlung alle in der einen Form, die die Sprache dafür gewählt hat.

## Inhalt

- [Namen werden ausgeschrieben](#names)
- [Ein Trait mit einer Methode heißt wie seine Methode](#traits)
- [Ein Feld beginnt nie mit `is`](#bool-fields)
- [Ein Verb ändert an Ort und Stelle, ein Partizip liefert eine Kopie](#verbs-and-participles)
- [Jede Kollektion hat ihre eigenen Wörter](#collection-words)
- [Ein Anweisungsaufruf ist ein Command](#command-calls)
- [Eine Anweisung pro Zeile](#one-statement-per-line)
- [Ein Member listet `self` nicht auf](#members)
- [Ein Typ ist ein Wert, außer er braucht eine Identität](#values)
- [Eine Invariante lebt in einer Kapsel](#capsules)
- [Das Feld einer Kapsel heißt `value`](#capsule-field-naming) (eingeklappt)
- [Ein Feld-Standardwert ist eine Konstante](#field-defaults) (eingeklappt)
- [Ein erwarteter Fehlschlag ist ein `Result`](#errors)
- [Der Erfolg ist der Wert](#wrapping) (eingeklappt)
- [Ein Typ mit Fällen wird mit `match` auseinandergenommen](#match)
- [Eine Closure ist kurz und folgt ihrem Aufruf](#closures)
- [Eine Closure über ein `var` entkommt nicht](#closures-over-var) (eingeklappt)
- [Ein Closure-Parameter benennt seine Form](#closure-types) (eingeklappt)
- [Eine Ressource wird mit `using` gebunden](#resources)
- [`await()` liefert den Wert, und ein Abbruch stoppt den Wartenden](#tasks) (eingeklappt)
- [`Into` kommt aus `From`](#conversions) (eingeklappt)
- [Ein Betriebssystem-Zweig ist ein `match`](#operating-system) (eingeklappt)
- [Eine Kollektion wird mit `for` oder einer Pipeline durchlaufen](#loops-and-pipelines)
- [`torb format` entscheidet das Layout](#format)
- [Gewohnheiten aus anderen Sprachen](#habits)

## Namen werden ausgeschrieben {#names}

**Ein Name ist ein ganzes Wort: `Expression`, `absolute`, `squareRoot`, `Item` – nie `Expr`, `abs`, `sqrt`, `T`.** Die
Ausnahmen sind Abkürzungen, die schon der Name selbst sind (`Html`, `Json`, `Http`, `Int64`, `min`, `max`).

```trb run
fn squareRoot(value: Float): Float {
  value.squareRoot()
}

fn firstOrDefault<Item>(items: List<Item>, fallback: Item): Item {
  items.first() ?? fallback
}

print squareRoot(16.0)             // prints 4.0
print firstOrDefault([3, 4], 0)    // prints 3
```

Warum: Ein Name wird weit häufiger gelesen als getippt, und eine Schreibweise pro Wort heißt, dass eine Suche jede
Verwendung findet. Siehe [Naming](../language/syntax/naming.md).

## Ein Trait mit einer Methode heißt wie seine Methode {#traits}

**Ein Trait mit einer erforderlichen Methode nimmt deren Namen: `Hash`, `Show`, `Close`, `Iterate`, `Equals`, `From`.
Nichts endet auf `-able`.** Ein Trait, der hauptsächlich als Typ benutzt wird, ist stattdessen ein Substantiv:
`Iterator`, `Source`, `Sink`.

```trb run
trait Describe {
  fn describe(): String
}

type Planet with Describe, Equals {
  name: String

  fn describe(): String {
    "the planet {name}"
  }

  fn equals(other: Planet): Bool {
    name == other.name
  }
}

print Planet("Mars").describe()    // prints the planet Mars
```

Warum: `type Planet with Describe` liest sich wie ein Satz, und der Trait und der Aufruf, für den er steht, können nie
auseinanderdriften. Siehe [Traits](../language/traits/traits.md).

## Ein Feld beginnt nie mit `is` {#bool-fields}

**Ein `Bool`-Feld ist ein Adjektiv oder ein Partizip: `enabled: Bool`, nicht `isEnabled: Bool`.** Eine Methode, die
einen `Bool` liefert, darf mit `is` oder `has` beginnen (`isEmpty()`), muss aber nicht – `enabled()` ist in Ordnung,
wo der Typ kein Feld dieses Namens hat, weil sich ein Feld und eine Methode einen Namensraum teilen.

```trb run
type Feature {
  name: String
  enabled: Bool = false
  var tags: List<String> = []

  fn isTagged(): Bool {
    !tags.isEmpty()
  }
}

const search = Feature "search"
print "{search.enabled} {search.isTagged()}"    // prints false false
```

Warum: Ein Feld ist Daten, und `is` liest sich wie eine Frage, also sagt das Präfix einem Leser, wo die Arbeit steckt.
Siehe [Naming](../language/syntax/naming.md), Regel 11.

## Ein Verb ändert an Ort und Stelle, ein Partizip liefert eine Kopie {#verbs-and-participles}

**Eine Methode, die ihren Empfänger ändert, ist ein Verb und eine `var fn`; ihr Zwilling, der eine geänderte Kopie
liefert, ist das Partizip und eine gewöhnliche `fn`.** `append`/`appended`, `sort`/`sorted`, `translate`/`translated`.

```trb run
type Counter {
  var value: Int = 0

  var fn increment() {
    value = value + 1
  }

  fn incremented(): Counter {
    copy(value: value + 1)
  }
}

var counter = Counter()
counter.increment()
const next = counter.incremented()
print "{counter.value} {next.value}"    // prints 1 2
```

Warum: Der Name allein sagt, ob sich der Empfänger ändert, und das Verb über ein `const` aufzurufen lässt den Compiler
das Partizip vorschlagen. Siehe [Verbs and participles](../language/types/verbs-and-participles.md).

## Jede Kollektion hat ihre eigenen Wörter {#collection-words}

**Eine Liste appended, ein Set inserted, eine Map setzt, ein Stack pusht und poppt, eine Queue enqueued und dequeued,
und alle von ihnen entfernen mit remove.** Eine leere Kollektion ist das Literal `[]` – für eine Liste, ein Set, einen
Stack oder eine Queue – und `[:]` für eine Map, mit dem Typ an der Bindung.

```trb run
var names: List<String> = []
names.append "Ada"

var seen: Set<String> = []
seen.insert "Ada"

var scores: Map<String, Int> = [:]
scores.set "Ada", 3
scores["Grace"] = 5

var undo: Stack<String> = []
undo.push "typed"

var pending: Queue<Int> = []
pending.enqueue 1

print "{names} {seen.length()} {scores.length()}"    // prints ["Ada"] 1 2
print "{undo.pop()} {pending.dequeue()}"             // prints Some("typed") Some(1)
```

Warum: Es gibt kein gemeinsames `add`, also sagt ein Aufruf, was er tut, ohne einen Blick auf den Typ des Empfängers,
und `add` bedeutet immer nur `+`. Siehe [Lists](../language/collections-and-iteration/lists.md),
[Maps and sets](../language/collections-and-iteration/maps-and-sets.md) und
[Stacks and queues](../language/collections-and-iteration/stacks-and-queues.md).

## Ein Anweisungsaufruf ist ein Command {#command-calls}

**Ein Aufruf am Anfang einer Anweisung, nach `=`, nach `return` oder nach `=>` wird ohne Klammern geschrieben.**
Klammern bleiben, wo der Aufruf verschachtelt ist, keine Argumente hat, einen Operator auf oberster Ebene eines
Arguments hat, oder im Kopf eines `if`, `for`, `while` oder `match` steht.

```trb run
fn route(path: String, to: String) {
  print "{path} -> {to}"
}

route "/health", to: "health"    // prints /health -> health
const doubled = [1, 2].map { _ * 2 }
print(doubled.toList().length() + 1)    // prints 3
print Some(doubled.toList())            // prints Some([2, 4])
```

Warum: Eine Anweisung liest sich dann wie eine eingebaute, sodass `unless`, `test` und ein eigenes DSL wie die Sprache
selbst aussehen. Siehe [Command calls](../language/syntax/command-calls.md).

## Eine Anweisung pro Zeile {#one-statement-per-line}

**Eine Anweisung endet am Ende ihrer Zeile. Es gibt keine Semikolons, und ein Rumpf aus zwei Anweisungen sind zwei
Zeilen.**

```trb run
fn greeting(name: String): String {
  const loud = name.toUpperCase()
  "Hello, {loud}"
}

print greeting("Ada")    // prints Hello, ADA
```

```trb error
const answer = 42;
// error: There are no semicolons. A statement ends at the end of its line
```

Warum: Eine Zeile ist die Einheit, auf die ein Leser, ein Diff und eine Fehlermeldung gleichermaßen zeigen. Siehe
[Lexical structure](../language/syntax/lexical-structure.md).

## Ein Member listet `self` nicht auf {#members}

**Eine Methode ist `fn area(): Int`, eine ändernde Methode ist `var fn grow(by: Int)`, und ein Member des Typs selbst
ist `static fn square(size: Int): Self`.** Der Empfänger steht nie in der Parameterliste; `self` ist trotzdem ein
Ausdruck im Rumpf.

```trb run
type Rectangle {
  width: Int
  height: Int

  fn area(): Int {
    width * height
  }

  static fn square(size: Int): Self {
    Self size, size
  }
}

print Rectangle.square(3).area()    // prints 9
```

Warum: Die Parameterliste ist genau das, was ein Aufrufer schreibt, und die zwei Wörter vor `fn` sagen, was für ein
Member es ist. Siehe [Methods and `static fn`s](../language/types/methods.md).

## Ein Typ ist ein Wert, außer er braucht eine Identität {#values}

**Schreib standardmäßig `type`. Schreib `shared type` nur für ein Ding mit Identität – eine Verbindung, eine Datei,
ein Fenster –, bei dem jeder, der es hält, dasselbe Objekt sehen muss.**

```trb run
type Point {
  var x: Int
  var y: Int
}

var start = Point 0, 0
var moved = start
moved.x = 5
print "{start.x} {moved.x}"    // prints 0 5
```

Warum: Ein Wert wird nie aliasiert, also passiert eine Änderung genau dort, wo sie geschrieben steht, und nirgends
sonst. Siehe [Copies](../language/execution/copies.md) und [Shared types](../language/types/shared-types.md).

## Eine Invariante lebt in einer Kapsel {#capsules}

**Ein Typ, dessen Werte eine Regel erfüllen müssen, hat ein `private`-Feld ohne Standardwert und eine `static
fn`-Factory, die die Regel prüft.** Das private Feld verschließt den Konstruktor, sodass die Factory der einzige Weg
hinein ist.

```trb run
type Percent {
  private value: Int

  static fn tryFrom(value: Int): Result<Percent, String> {
    if value < 0 || value > 100 {
      return Fail "{value} is not between 0 and 100"
    }
    Self value
  }

  fn percent(): Int {
    value
  }
}

print Percent.tryFrom(120)                        // prints Fail("120 is not between 0 and 100")
print Percent.tryFrom(40).map({ _.percent() })    // prints Ok(40)
```

Warum: Ein Konstruktor enthält nie Logik, also braucht eine Prüfung, die bei jedem Wert läuft, eine Tür, um die man
nicht herumkommt. Siehe [Data or capsule](../language/types/data-or-capsule.md).

## Das Feld einer Kapsel heißt `value` {#capsule-field-naming}

<details>
<summary>Die Regel, ein Beispiel, und warum</summary>

**Das eine gespeicherte Feld einer Kapsel heißt `value`; mehrere heißen nach der Methode, die jedes beantwortet, plus
`Value`, oder `Values` für einen Plural.** Ein Feld und eine Methode teilen sich nie einen Namen, also leiht sich das
Feld nie das Wort des Zugriffs.

```trb run
type Distance {
  private value: Int

  fn meters(): Int {
    value
  }
}

print Distance(5).meters()    // prints 5
```

Warum: `value` kann mit keinem Zugriff kollidieren, und `rootValue` neben `fn root()` oder `componentValues` neben
`fn components()` liest sich auf einen Blick, welches der beiden die Speicherung ist. Siehe
[Naming](../language/syntax/naming.md) und [Data or capsule](../language/types/data-or-capsule.md).

</details>

## Ein Feld-Standardwert ist eine Konstante {#field-defaults}

<details>
<summary>Die Regel, ein Beispiel, und warum</summary>

**Ein Feld-Standardwert ist ein Literal, eine Konstante, ein Konstruktor aus Konstanten oder ein leeres
Kollektions-Literal. Alles, was berechnet werden muss, kommt in eine `static fn`.**

```trb run
type Buffer {
  var slots: List<Int> = []
  capacity: Int = 16

  static fn filled(capacity: Int): Self {
    Self List.filled(capacity, 0), capacity
  }
}

print Buffer().capacity                   // prints 16
print Buffer.filled(3).slots.length()     // prints 3
```

```trb error
type Buffer {
  var slots: List<Int> = List.filled 16, 0
}
// error: A field default is a constant: `List.filled` is a call - compute it in a `static fn`, or start from `[]`
```

Warum: Der erzeugte Konstruktor hat keinen Rumpf, in dem Code laufen könnte, und einen Wert zu bauen scheitert nie.
Siehe [Construction](../language/types/construction.md), Regel 4.

</details>

## Ein erwarteter Fehlschlag ist ein `Result` {#errors}

**Eine Funktion, die scheitern kann, liefert `Result<Value, Failure>`. Ein Aufrufer reicht den Fehlschlag mit `?`
weiter, ersetzt ihn mit `??`, oder nimmt ihn mit `match` auseinander.** `panic` ist für einen Zustand, den das
Programm für unmöglich hält, nie für schlechte Eingaben.

```trb run
fn port(text: String): Result<Int, String> {
  const number = Int.tryFrom(text).mapError({ _ => "{text} is not a number" })?
  if number < 1 || number > 65535 {
    return Fail "{number} is not a port"
  }
  number
}

print port("8080")             // prints Ok(8080)
print(port("http") ?? 80)      // prints 80
```

Warum: Ein Fehlschlag in der Signatur kann nicht vergessen werden, und ein `?` ist die ganze Zeremonie, die ihn
weiterzureichen kostet. Siehe [Result](../language/errors/result.md),
[The question mark operator](../language/errors/question-mark.md) und [panic](../language/errors/panic.md).

## Der Erfolg ist der Wert {#wrapping}

<details>
<summary>Die Regel, ein Beispiel, und warum</summary>

**Ein Rumpf, der eine `Option` oder ein `Result` liefert, endet in seinem Wert, nie in `Some(value)` oder
`Ok(value)`: Ein Wert, wo eines der beiden erwartet wird, verpackt sich selbst. `None` und `Fail` werden geschrieben;
`Some` und `Ok` nur, wo der Wert eine `Option` innerhalb einer `Option` ist, oder wo sein Typ noch nicht entschieden
ist.** `torb lint --rule redundant-wrap` findet den Rest, und `--fix` entfernt ihn.

```trb run
fn find(names: List<String>, wanted: String): Int? {
  for index in 0..names.length() {
    if names[index] == wanted {
      return index
    }
  }
  None
}

const fallback: Int? = 0
print find(["ada", "alan"], "alan")    // prints Some(1)
print fallback                         // prints Some(0)
```

Warum: eine Schreibweise pro Bedeutung – der Typ sagt schon, dass der Wert der Erfolg ist, also sagte das `Some` es
zweimal. Siehe [Conversions](../language/types/conversions.md), Regel 8.

</details>

## Ein Typ mit Fällen wird mit `match` auseinandergenommen {#match}

**Ein `match` deckt jeden Fall ab, und ein Fall wird mit seinem Typ oder einem führenden Punkt geschrieben – nackt
nur, wenn die Datei ihn importiert.** `Some`, `None`, `Ok` und `Fail` sind nackt, weil die Prelude sie importiert.

```trb run
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
}

fn area(shape: Shape): Float {
  match shape {
    .Circle(radius) => 3.0 * radius * radius
    .Rectangle(width, height) => width * height
  }
}

print area(Shape.Rectangle(2.0, 3.0))    // prints 6.0
```

Warum: Ein vollständiges `match` macht aus einem neuen Fall eine Liste von Kompilierfehlern an jeder Stelle, die
davon erfahren muss. Siehe [Cases and match](../language/pattern-matching/cases-and-match.md) und
[Why cases are never bare](../explanation/why-cases-are-never-bare.md).

## Eine Closure ist kurz und folgt ihrem Aufruf {#closures}

**Eine einzeilige Closure benutzt die impliziten Parameter `_`, `_2`, `_3`, und eine Closure, die letztes Argument
ist, folgt dem Aufruf.** Es gibt kein Currying: Eine Funktion nimmt alle ihre Argumente auf einmal, und eine Funktion
mit einigen davon festgelegt ist eine Closure mit einem Platzhalter.

```trb run
fn scaled(value: Int, by: Int): Int {
  value * by
}

const numbers = [1, 2, 3]
const total = numbers.fold 0 { _ + _2 }
const doubled = numbers.map({ scaled _, by: 2 })
print total                  // prints 6
print doubled.toList()       // prints [2, 4, 6]
```

Warum: eine Closure-Form und eine Aufrufform decken ab, was anderswo Currying, Methodenreferenzen und Lambdas
abdecken. Siehe [Closures](../language/functions/closures.md) und
[Trailing closures](../language/functions/trailing-closures.md).

## Eine Closure über ein `var` entkommt nicht {#closures-over-var}

<details>
<summary>Die Regel, ein Beispiel, und warum</summary>

**Eine Closure, die eine `var`-Bindung liest oder schreibt, wird direkt an einen Aufruf übergeben, der sie nur
ausführt – `forEach`, `unless`, einen DSL-Block – und wird nie gespeichert oder zurückgegeben.** Um einen Wert
hinauszutragen, gib ihn zurück.

```trb run
fn total(numbers: List<Int>): Int {
  var sum = 0
  numbers.forEach { sum = sum + _ }
  sum
}

print total([1, 2, 3])    // prints 6
```

```trb error
fn counter(): () => Int {
  var count = 0
  {
    count = count + 1
    count
  }
}
// error: This closure captures the `var` binding `count` and may outlive it
```

Warum: Ein `var` ist die eine Variable, die die Sprache teilt, und eine Closure, die sie überlebt hätte, würde sie
mit niemandes Wissen teilen. Siehe [Closures](../language/functions/closures.md), Regel 8.

</details>

## Ein Closure-Parameter benennt seine Form {#closure-types}

<details>
<summary>Die Regel, ein Beispiel, und warum</summary>

**Ein Parameter, der eine Frage über einen Wert stellt, ist ein `Predicate<Item>`, einer, der für seinen Effekt
aufgerufen wird, ist eine `Action<Item>`, und einer, der einen Wert in einen anderen verwandelt, ist ein
`Transform<Item, Output>`.** Die drei sind Aliase in der Prelude, sodass jede Closure der Form passt, und die
Signatur sagt, wofür die Closure ist, bevor sie sagt, wie sie aussieht.

```trb run
fn countWhere(numbers: List<Int>, predicate: Predicate<Int>): Int {
  numbers.filter(predicate).count()
}

fn labels(numbers: List<Int>, transform: Transform<Int, String>): List<String> {
  numbers.map(transform).toList()
}

const numbers = [3, 8, 5]
const large = countWhere numbers { _ > 4 }
const shown = labels numbers { "#{_}" }
print large    // prints 2
print shown    // prints ["#3", "#8", "#5"]
```

Warum: `filter`, `forEach` und `map` der Standardbibliothek sind so geschrieben, und eine Signatur, die sich wie
ihre liest, braucht keinen zweiten Blick. Eine Closure ohne Parameter bleibt `() => Value`, das ist schon so kurz wie
ein Name. Siehe [Predicate, Action and Transform](../standard-library/function-types.md).

</details>

## Eine Ressource wird mit `using` gebunden {#resources}

**Eine Ressource ist ein `shared type` mit `Close`, gebunden mit `using`. Niemand ruft `close()` auf: Es läuft
einmal, wenn der letzte Halter am Ende seines Blocks verschwindet.**

```trb run
shared type Log with Close {
  name: String

  fn write(message: String) {
    print "{name}: {message}"
  }

  var fn close() {
    print "{name} closed"
  }
}

fn work() {
  using log = Log "audit"
  log.write "started"
}

work()
// prints audit: started
// prints audit closed
```

Warum: Die Zeile, an der eine Ressource freigegeben wird, ist das Ende des Blocks, also vergisst sie kein Pfad durch
die Funktion. Siehe [Destructors](../language/execution/destructors.md).

## `await()` liefert den Wert, und ein Abbruch stoppt den Wartenden {#tasks}

<details>
<summary>Die Regel, ein Beispiel, und warum</summary>

**Eine Funktion, die wartet, gibt `Task<Value>` zurück. `await()` liefert den Wert, also ist ein Task, dessen Wert
ein `Result` ist, ein `.await()?` pro Zeile – das `?` ist der eigene Fehlschlag der Arbeit. Ein Abbruch wird
weitergegeben, nie beantwortet: `cancel()` bittet einen Task zu stoppen, und wer auf ihn wartet, stoppt auch an genau
diesem `await()`. `result()` ist für den Code, der einen beobachten muss.**

```trb run
fn doubled(value: Int): Task<Result<Int, String>> {
  value * 2
}

fn sum(): Task<Result<Int, String>> {
  const first = doubled(1).await()?
  const second = doubled(2).await()?
  first + second
}

print sum().await()    // prints Ok(6)
```

Warum: Ein Aufruf, der wartet, wird nie von sich aus abgebrochen – nur der Task, der wartet, wird es, und dann hat er
für eine Antwort keine Verwendung mehr: Er stoppt, wo er wartet, seine `using`s werden geschlossen, und wer auf ihn
wartet, stoppt seinerseits. Den Abbruch als Wert zu beantworten hätte in jede Zeile IO umsonst ein zweites `Result`
verschachtelt. Ein Supervisor, der es wissen muss, schreibt `task.result()`, was `Fail(Cancelled)` liefert. Siehe
[Tasks](../language/concurrency-and-streams/tasks.md) und [std/task](../standard-library/task.md).

</details>

## `Into` kommt aus `From` {#conversions}

<details>
<summary>Die Regel, ein Beispiel, und warum</summary>

**Implementiere `From<Source>` auf dem Ziel, und `into()` existiert kostenlos. `Into` wird nie von Hand implementiert,
und es gibt keine Casts.**

```trb run
type Celsius {
  degrees: Float
}

type Fahrenheit {
  degrees: Float
}

extend Celsius with From<Fahrenheit> {
  static fn from(value: Fahrenheit): Celsius {
    Celsius((value.degrees - 32.0) * 5.0 / 9.0)
  }
}

const boiling: Celsius = Fahrenheit(212.0).into()
print boiling.degrees    // prints 100.0
```

Warum: Eine von Hand geschriebene Richtung heißt eine Stelle zum Ändern, und `?` findet dasselbe `From`, wenn es
einen Fehlschlag umwandelt. Siehe [Conversions](../language/types/conversions.md).

</details>

## Ein Betriebssystem-Zweig ist ein `match` {#operating-system}

<details>
<summary>Die Regel, ein Beispiel, und warum</summary>

**Code, der sich pro Betriebssystem unterscheidet, ist ein `match OperatingSystem.current` in der Funktion, in der er
sich unterscheidet.** Der Compiler prüft jeden Zweig auf jeder Maschine und baut nur den, den das Ziel nimmt.

> **Planned.** `OperatingSystem.current` gibt es noch nicht; [The Operating System](../design/OS.md) ist das
> entschiedene Design. Der Block unten parst und wird nicht typgeprüft.

```trb
fn searchPathSeparator(): String {
  match OperatingSystem.current {
    .Windows => ";"
    .Linux | .MacOs | .FreeBsd | .Browser => ":"
  }
}
```

Warum: Ein neues Betriebssystem wird dann zu einem Kompilierfehler an jeder Stelle, die davon erfahren muss, statt zu
einem in C oder einer Build-Datei versteckten Zweig.

</details>

## Eine Kollektion wird mit `for` oder einer Pipeline durchlaufen {#loops-and-pipelines}

**Eine Schleife, die pro Element etwas tut, ist ein `for`. Eine Berechnung von einer Kollektion zu einer anderen ist
eine Pipeline: faule Stufen, eine pro Zeile, und eine terminale Operation am Ende.** Es gibt keine Index-Schleife und
keinen `iter()`-Schritt.

```trb run
const words = ["pipeline", "for", "stage", "match"]

for word in words {
  if word.byteLength() == 3 {
    print word    // prints for
  }
}

const long = words
  .filter { _.byteLength() > 4 }
  .map { _.toUpperCase() }
  .toList()
print long    // prints ["PIPELINE", "STAGE", "MATCH"]
```

Warum: Nichts in einer Pipeline läuft, bevor die terminale Operation zieht, also setzen sich Stufen ohne
Zwischenkollektionen zusammen. Siehe [Iterating](../language/collections-and-iteration/iterating.md) und
[Pipelines](../language/collections-and-iteration/pipelines.md).

## `torb format` entscheidet das Layout {#format}

**Wo die Sprache zwei Schreibweisen erlaubt, wählt `torb format` eine, und ihr Layout ist das, worin eine Datei
geschrieben ist.** Führ es vor jedem Commit aus, und lass `--check` einen Build scheitern lassen, der nicht darin
ist.

```console
torb format src
torb format --check src
```

Warum: Ein Stil, der einmal entschieden ist, wird nie wieder diskutiert. Siehe
[torb format](../tooling/torb-format.md) und [Verify your work](../tooling/verifying-your-work.md).

## Gewohnheiten aus anderen Sprachen {#habits}

`let`, `Err`, ein nackter Fall, `print("x")`, `Hashable`, `MAX_SIZE`, `list.add(x)`, `fn area(self)` und der Rest
dessen, was Rust, Swift, Kotlin und TypeScript lehren, stehen, jeder mit der richtigen Zeile und der Diagnose, auf
[What a model trained on other languages gets wrong](../explanation/mistakes-models-make.md). Die Kontrastseiten
gehen je eine Sprache durch: [Rust](../explanation/coming-from-rust.md),
[Swift](../explanation/coming-from-swift.md), [Kotlin](../explanation/coming-from-kotlin.md) und
[TypeScript](../explanation/coming-from-typescript.md).

## Weiter

- [The language reference](../language/index.md) - die genaue Regel hinter jedem Abschnitt oben.
- [Syntax cheat sheet](../language/syntax/cheat-sheet.md) - jede Form der Sprache auf einer Seite.
- [Why the language is like this](../explanation/index.md) - die Argumente hinter diesen Gewohnheiten.
