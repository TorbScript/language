---
title: Listen
summary: Eine Liste hält mehrere Werte in Reihenfolge, eine Schleife geht sie einzeln durch, und ein zweiter Name für eine Liste erzeugt eine Kopie.
kind: lesson
status: stable
order: 6
translates: a6ae19c017d9
---

Eine Liste hält mehrere Werte hintereinander, zwischen eckigen Klammern. Die Werte sind von 0 an nummeriert, sodass
`fruits[0]` der erste ist. `fruits.length()` sagt, wie viele es sind, und eine `for`-Schleife geht sie nacheinander
durch.

```trb run
const fruits = ["apple", "banana", "cherry"]
print fruits[0]
print fruits.length()
for fruit in fruits {
  print "I like {fruit}."
}
// prints apple
// prints 3
// prints I like apple.
// prints I like banana.
// prints I like cherry.
```

## Zu einer Liste hinzufügen

`append` fügt am Ende einen Wert hinzu. Es ändert die Liste, deshalb braucht die Liste ein `var`, genau wie eine
Zahl, die du änderst.

```trb run
var shopping = ["bread"]
shopping.append "milk"
shopping.append "eggs"
print shopping
// prints ["bread", "milk", "eggs"]
```

## Zwei Namen, zwei Listen

Gibst du einer Liste einen zweiten Namen, bekommst du eine Kopie. Änderst du die Kopie, bleibt die erste Liste
unberührt: Nichts ändert sich hinter deinem Rücken, ein Wert ändert sich nur dort, wo du ihn änderst.

```trb run
const original = ["bread"]
var copy = original
copy.append "milk"
print original
print copy
// prints ["bread"]
// prints ["bread", "milk"]
```

Viele Sprachen teilen sich stattdessen eine Liste zwischen beiden Namen, und eine Änderung über den einen Namen zeigt
sich auch beim anderen. TorbScript nicht: Jeder Wert verhält sich hier wie eine Zahl. Die Referenz nennt das
[Wertsemantik](../glossary.md#value-semantics).

## Übung

Addiere die Preise mit einer `for`-Schleife, sodass das Programm `Total: 16` ausgibt.

```trb exercise
const prices = [3, 8, 5]
var total = 0
print "Total: {total}"
```

<details>
<summary>Tipp</summary>

Geh die Liste mit `for price in prices { ... }` durch und addiere jeden `price` zu `total`.

</details>

<details>
<summary>Lösung</summary>

```trb run
const prices = [3, 8, 5]
var total = 0
for price in prices {
  total = total + price
}
print "Total: {total}"
// prints Total: 16
```

</details>

## Zusammenfassung

Eine Liste hält Werte ab Position 0 in Reihenfolge, `append` fügt zu einer `var`-Liste hinzu, und ein zweiter Name
ist eine eigene Kopie.
