---
title: Funktionen
summary: Eine Funktion gibt ein paar Zeilen einen Namen, damit sie mit jeweils anderen Werten erneut laufen, und sie kann ein Ergebnis zurückgeben.
kind: lesson
status: stable
order: 7
translates: acb63cd894e9
---

Eine Funktion ist ein paar Zeilen mit einem Namen. `fn greet(name: String)` macht eine Funktion namens `greet`, die
einen Wert braucht, einen `String`, den sie `name` nennt. `greet "Ada"` führt ihre Zeilen aus, wobei `name` für
`"Ada"` steht.

Eine Funktion aufzurufen sieht aus wie `print`, und das ist kein Zufall: `print` ist auch eine Funktion.

```trb run
fn greet(name: String) {
  print "Hello, {name}!"
}

greet "Ada"
greet "Alan"
// prints Hello, Ada!
// prints Hello, Alan!
```

## Eine Funktion, die antwortet

Eine Funktion kann einen Wert zurückgeben. `: Int` nach den Klammern sagt, was für ein Wert zurückkommt, und die
letzte Zeile der Funktion ist dieser Wert. Ein Aufruf, der Teil von etwas Größerem ist – wie `double(21)` innerhalb
von `print` –, behält seinen Wert in Klammern.

```trb run
fn double(number: Int): Int {
  number * 2
}

print double(21)
print(double(5) + 1)
// prints 42
// prints 11
```

## Übung

Schreib eine Funktion `square`, die einen `Int` nimmt und die Zahl mal sich selbst zurückgibt, sodass das Programm
`25` ausgibt.

```trb exercise incomplete
// Write the function square here

print square(5)
```

<details>
<summary>Tipp</summary>

Fang wie `double` oben an: `fn square(number: Int): Int { ... }`, mit `number * number` als letzter Zeile.

</details>

<details>
<summary>Lösung</summary>

```trb run
fn square(number: Int): Int {
  number * number
}

print square(5)
// prints 25
```

</details>

## Zusammenfassung

`fn name(value: Type) { ... }` macht eine Funktion, `: Type` nach den Klammern sagt, was sie zurückgibt, und ihre
letzte Zeile ist die Antwort.
