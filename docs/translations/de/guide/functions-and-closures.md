---
title: Funktionen und Closures
summary: Wie du eine Funktion deklarierst, wann sie ihren Rückgabetyp ausschreiben muss, und die eine Closure-Form, die die Sprache hat.
kind: guide
status: stable
order: 30
prerequisites:
  - values-and-bindings.md
translates: 68d903db27c7
---

Eine Funktion wird mit `fn` deklariert, und eine Closure ist die eine Stelle, an der ein Wert ein Stück Code ist statt
Daten. Diese Seite bringt dich von einer ersten `fn` dahin, eine Closure so zu übergeben, wie es der Rest dieser
Sprache erwartet.

## Ziel

Am Ende dieser Seite kannst du eine Funktion mit Standardwerten und Labels deklarieren, eine Closure schreiben und
sie einer anderen Funktion als Trailing Closure übergeben.

## Eine Funktion deklarieren

```trb
fn distance(x: Int, y: Int): Int {
  const dx = x * x
  const dy = y * y
  dx + dy
}

print distance(3, 4)
```

Jeder Parameter braucht einen Typ; der Rückgabetyp nicht, den leitet der Compiler aus dem letzten Ausdruck des
Rumpfs ab. Eine `fn` wird [gehoisted](../language/functions/declaring-a-function.md): Sie kann oberhalb der Zeile
aufgerufen werden, in der sie deklariert ist, was zwei Funktionen erlaubt, sich gegenseitig aufzurufen.

```trb
fn isEven(n: Int): Bool {
  if n == 0 { true } else { isOdd(n - 1) }
}

fn isOdd(n: Int): Bool {
  if n == 0 { false } else { isEven(n - 1) }
}

print isEven(10)
```

Eine `public`-Funktion ist der eine Fall, in dem der Rückgabetyp ausgeschrieben werden muss, selbst wenn er sich
ableiten ließe: Ein Aufrufer außerhalb der Datei soll nicht den Rumpf lesen müssen, um zu wissen, was zurückkommt.

## Standardwerte und Labels

```trb
fn connect(host: String, port: Int = 5432, timeout: Int = 30): String {
  "{host}:{port} (timeout {timeout}s)"
}

print connect("localhost")
print connect("localhost", 3306)
print connect("localhost", timeout: 10)
```

`port` und `timeout` werden aus ihren Standardwerten gefüllt, wenn der Aufruf sie weglässt, und ein Aufruf kann jeden
späteren Parameter über sein [Label](../language/functions/arguments.md) statt über seine Position benennen. Ein
Standardwert wird bei jedem Aufruf, der ihn braucht, neu ausgewertet, also läuft `limits(memory: Int =
64.megabytes())` `64.megabytes()` jedes Mal erneut aus. Den genauen Gültigkeitsbereich dafür beschreibt
[Default values](../language/functions/default-values.md).

Ein abschließender `...name: Type`-Parameter ist ein
[variadischer Parameter](../language/functions/variadics.md): Er sammelt jedes verbleibende positionale Argument in
eine `List`.

```trb
fn sumAll(...numbers: Int): Int {
  numbers.fold 0 { a, b => a + b }
}

print sumAll(1, 2, 3)
```

Eine Kollektion wird nie von selbst in ihn entpackt; sie mit `...` zu spreaden ist es, was das tut
(`sumAll(1, ...someList)`).

## Die eine Closure-Form

Ein `{` in Ausdrucksposition ist immer eine Closure, nie ein Block. Sie erfasst den Scope, in dem sie geschrieben
steht.

```trb
const double = { x: Int => x * 2 }
const triple: (Int) => Int = { _ * 3 }

print double(21)
print triple(7)
```

`double` schreibt den Typ seines Parameters aus; `triple` lässt ihn weg, weil die Typangabe der Bindung selbst schon
sagt, was ein an die Closure übergebener Wert sein muss, und dann ist `_` der implizite erste Parameter. Beide Formen
sind dieselbe [Closure](../language/functions/closures.md). `return` innerhalb einer Closure kehrt aus der Closure
zurück, nie aus der Funktion darum herum.

Wo ein Rückgabetyp ausgeschrieben werden muss oder die Closure sich selbst aufrufen können muss, ist eine lokale `fn`
stattdessen das richtige Werkzeug – sie ist dieselbe Deklaration wie eine `fn` auf oberster Ebene, kann also mit
ihrem Namen übergeben werden:

```trb
fn fib(n: Int): Int {
  if n < 2 { n } else { fib(n - 1) + fib(n - 2) }
}

print([1, 2, 3, 4, 5].map(fib).toList())
```

## Eine Closure als letztes Argument übergeben

Ist der letzte Parameter eines Aufrufs eine Funktion, kann die Closure dem Aufruf folgen, statt in seinen Klammern zu
stehen – eine [Trailing Closure](../language/functions/trailing-closures.md).

```trb
const numbers = [1, 2, 3, 4]
const doubled = numbers.map { _ * 2 }
const total = numbers.fold 0 { sum, number => sum + number }

print doubled.toList()
print total
```

`map` benennt seinen impliziten Closure-Parameter nach dem Parameter seiner eigenen Signatur, also kompiliert auch
`numbers.map { value * 2 }`. Ein Aufruf ohne Argumente braucht trotzdem `()`: Ein bloßer Name wie `distance`
bezeichnet die Funktion selbst, statt sie aufzurufen.

## Weiter

- [Types and methods](types-and-methods.md) - einen Typ deklarieren und ihm Verhalten geben.
- [Declaring a function](../language/functions/declaring-a-function.md) - die genauen Regeln, samt Hoisting und
  Inferenz.
- [Parameter modes](../language/functions/parameter-modes.md) - `var`, `lazy`, Empfänger-Closures und
  `Expression<Value>`-Parameter.
