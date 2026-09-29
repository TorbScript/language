---
title: Werte und Bindungen
summary: Warum const und var die ganze Mutationsgeschichte sind, was eine Kopie kostet, und die eine Falle, in die jeder tappt, der von einer Sprache mit Referenzen kommt.
kind: guide
status: stable
order: 20
prerequisites:
  - installing-and-running.md
translates: 95ae91b70d4b
---

Die meisten Sprachen brauchen zwei Antworten zu einem Wert: Ist die *Bindung* veränderbar, und ist das *Ding*
veränderbar? TorbScript hat eine. Die Bindung entscheidet, und sie entscheidet alles darunter.

## Ziel

Am Ende dieser Seite kannst du für jede Zeile vorhersagen, ob sie kompiliert und was sich ändert.

## Zwei Wörter

```trb
const answer = 42
var counter = 0
counter = counter + 1
print "{answer} {counter}"
```

`const` ist eine Bindung, die sich nie ändert. `var` ist eine, die es kann. Eine Bindung hat immer einen
Startwert: `var x` und `var x: Int` sind beide Fehler, weil es nirgends in der Sprache einen stillschweigenden
Standardwert gibt.

Eine Typangabe ist optional und steht nach dem Namen. Ein Literal passt sich dem erwarteten Typ an:

```trb
const ratio: Float = 1
const byte: UInt8 = 0xFF
const million = 1_000_000
```

`1` wurde zu einem `Float64`, weil das erwartet wurde. Ohne Angabe ist ein Ganzzahl-Literal ein `Int64` und ein
Dezimal-Literal ein `Float64`.

## Die Bindung entscheidet auch über den Wert

Das ist der Teil, der sich von fast jeder anderen Sprache unterscheidet:

```trb
var list = [1, 2]
list.append 3
const fixed = list
print "{list} {fixed}"
```

`list.append 3` funktioniert, weil `list` ein `var` ist. `fixed.append 3` würde nicht kompilieren, und zwar nicht,
weil die *Bindung* nicht neu zugewiesen werden kann – sondern weil `const` **tief** wirkt. Durch eine
`const`-Bindung kannst du nicht neu zuweisen, kein Feld setzen und keine Methode aufrufen, die eine `var fn` ist.

```trb error
const fixed = [1, 2]
fixed.append 3
// error: `append` needs a `var`
```

Die Fehlermeldung nennt die andere Hälfte der Regel. Eine Methode, die ihren Empfänger an Ort und Stelle ändert, ist
ein **Verb** und deklariert eine `var fn`; die Methode, die stattdessen eine geänderte Kopie zurückgibt, ist ihr
**Partizip**. Deshalb sortiert `list.sort { _ }` an Ort und Stelle, während `list.sorted { _ }` eine neue Liste
zurückgibt, `append` und `appended`, `remove` und `removed`.

Es gibt keine `MutableList`, keine `ImmutableList` und keine schreibgeschützte Sicht. Eine `const`-Bindung *ist* die
unveränderliche Liste.

## Zuweisen ist Kopieren

```trb run
type Point {
  var x: Int
  var y: Int
}

var first = Point 1, 2
const second = first
first.x = 99
print "{first} {second}"
// prints Point(x: 99, y: 2) Point(x: 1, y: 2)
```

Das gibt `Point(x: 99, y: 2) Point(x: 1, y: 2)` aus. `second` ist eine Kopie, sodass nichts, was über `first`
passiert, sich dort zeigt. Dasselbe gilt, wenn ein Wert an eine Funktion übergeben oder in einer Closure erfasst
wird.

Was eine Kopie *kostet*, ist Sache der Implementierung und niemals beobachtbar. Ein kleiner Wert wie ein `Point`
wird wirklich kopiert; eine `List` oder ein `String` teilt sich ihren Speicher, bis jemand hineinschreibt, und erst
dann kopiert der Schreibende. Das Modell ist also "immer eine Kopie", die Kosten sind "nur wenn es darauf ankommt".

## Die Kopierfalle

Das ist der eine Fehler, den jeder einmal macht, und er ist der Preis der
[Wertsemantik](../glossary.md#value-semantics):

```trb run
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
var first = counters[0]
first.increment()
print "{first.count} {counters[0].count}"
// prints 1 0
```

Das gibt `1 0` aus. `var first = counters[0]` hat eine **Kopie** aus der Liste genommen, sodass sie zu erhöhen die
Liste unberührt ließ. Eine Änderung, die danach nie gelesen wird, ist ein Kompilierfehler (`This change has no
effect: `first` is never read again`, mit dem Hinweis, dass `first` eine Kopie ist, und dem Pfad, aus dem sie stammt).
Dieses Programm liest `first.count` in der letzten Zeile, also gibt es für den Compiler nichts zu melden: Die Kopie
wird benutzt, nur eben nicht so, wie es gemeint war. Greif stattdessen über den Pfad zu:

```trb check
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
counters[0].increment()
print counters[0].count
```

`counters[0]` ist ein **`var`-Pfad**: Der Wert wird herausgenommen, geändert und zurückgelegt, ohne Kopie. Ein Pfad
kann beliebig tief durch Felder und Indizes gehen (`world.entities[id].health = 5`), und jeder Schritt davon muss
`var` sein.

## Mutation braucht einen var-Pfad, von der Bindung an

Drei Dinge müssen zusammenpassen, damit eine Änderung erlaubt ist:

1. die **Bindung** ist ein `var`, oder du bist innerhalb eines `var`-Parameters oder einer `var fn`-Methode,
2. jedes **Feld** auf dem Weg ist als `var` deklariert,
3. der Wert, den du erreichst, wird über diesen Pfad erreicht und nicht über eine Kopie davon.

```trb
type Engine {
  var running: Bool = false
}

type Car {
  var engine: Engine = Engine()
  wheels: Int = 4
}

var car = Car()
car.engine.running = true
print car.engine.running
```

`wheels` hat kein `var`, also ändert es sich nach der Konstruktion nie – auch nicht über eine `var`-Bindung. So sagt
ein Wert: "Dieser Teil von mir steht fest."

## Weiter

- [Bindings](../language/values-and-types/bindings.md) - die genauen Regeln, samt Shadowing und toten Änderungen.
- [Declaring a type](../language/types/declaring-a-type.md) - Felder, Methoden und was generiert wird.
- [Why values instead of references](../explanation/why-values-instead-of-references.md) - das Argument, und was es
  kostet.
- [Coming from Rust](../explanation/coming-from-rust.md) - falls du nach `&mut` greifen willst.
