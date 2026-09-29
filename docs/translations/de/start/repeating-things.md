---
title: Dinge wiederholen
summary: Eine for-Schleife lässt dieselben Zeilen einmal für jede Zahl eines Bereichs laufen, eine while-Schleife so lange, wie eine Frage mit Ja beantwortet wird.
kind: lesson
status: stable
order: 5
translates: 1886e1d301f9
---

Computer sind gut darin, dasselbe viele Male zu tun. Eine `for`-Schleife führt die Zeilen in ihren geschweiften
Klammern einmal für jede Zahl von einem Start bis zu einem Ende aus und gibt der Zahl dabei jedes Mal einen Namen.

`1..=3` steht für die Zahlen von 1 bis einschließlich 3. `1..3` hört vor der 3 auf.

```trb run
for round in 1..=3 {
  print "Round {round}"
}
print "Done!"
// prints Round 1
// prints Round 2
// prints Round 3
// prints Done!
```

## Solange

Eine `while`-Schleife stellt vor jeder Runde eine Frage und hört auf, sobald die Antwort Nein ist. Etwas in der
Schleife muss die Antwort ändern, sonst endet sie nie.

```trb run
var countdown = 3
while countdown > 0 {
  print countdown
  countdown = countdown - 1
}
print "Liftoff!"
// prints 3
// prints 2
// prints 1
// prints Liftoff!
```

## Übung

Addiere mit einer `for`-Schleife alle Zahlen von 1 bis 10, sodass das Programm `55` ausgibt.

```trb exercise
var sum = 0
print sum
```

<details>
<summary>Tipp</summary>

Setze eine Schleife `for number in 1..=10 { ... }` zwischen die beiden Zeilen und schreibe darin
`sum = sum + number`.

</details>

<details>
<summary>Lösung</summary>

```trb run
var sum = 0
for number in 1..=10 {
  sum = sum + number
}
print sum
// prints 55
```

</details>

## Zusammenfassung

`for number in 1..=10` wiederholt für jede Zahl eines Bereichs, und `while` wiederholt, solange seine Frage mit Ja
beantwortet wird.
