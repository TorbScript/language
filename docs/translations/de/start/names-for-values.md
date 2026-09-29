---
title: Namen für Werte
summary: Gib einem Wert mit const einen Namen und benutze den Namen statt des Werts; schreibe var, wenn sich der Wert später ändern soll.
kind: lesson
status: stable
order: 2
translates: eea9161984c8
---

Ein Wert ist eine Information: ein Wort, eine Zahl. Braucht ein Programm denselben Wert mehrmals, gibt es ihm einen
Namen und benutzt von da an den Namen.

`const name = "Ada"` heißt: Von jetzt an steht `name` für `"Ada"`. In Text wird ein Name in geschweiften Klammern
durch seinen Wert ersetzt, sodass aus `"Hello, {name}!"` `Hello, Ada!` wird.

```trb run
const name = "Ada"
print "Hello, {name}!"
print "Goodbye, {name}!"
// prints Hello, Ada!
// prints Goodbye, Ada!
```

## Ein Name, dessen Wert sich ändert

`const` bedeutet, dass der Wert für immer derselbe bleibt. Muss sich der Wert ändern können – ein Punktestand, ein
Zähler –, schreibst du stattdessen `var` und gibst dem Namen mit `=` einen neuen Wert.

`score = score + 10` liest sich so: nimm den Wert von `score`, addiere 10, und mach das zum neuen Wert von `score`.

```trb run
var score = 0
score = score + 10
score = score + 5
print "Score: {score}"
// prints Score: 15
```

<details>
<summary>Was passiert, wenn du eine const änderst?</summary>

Das Programm läuft nicht. Der Computer prüft das ganze Programm, bevor er auch nur eine Zeile ausführt, und sagt dir,
wo der Fehler liegt:

```trb error
const name = "Ada"
name = "Alan"
print name
// error: `name` is a `const`. Only a `var` binding can be changed
```

Diese Prüfung ist eine gute Sache: Ein Wert, von dem du gesagt hast, dass er gleich bleibt, tut das auch wirklich.
Ein Name für einen Wert heißt [Binding](../glossary.md#binding), das Wort, das die Fehlermeldung benutzt.

</details>

## Übung

Du hast 3 Münzen und findest 4 weitere. Füge eine Zeile hinzu, damit das Programm `Coins: 7` ausgibt.

```trb exercise
var coins = 3
print "Coins: {coins}"
```

<details>
<summary>Tipp</summary>

Gib `coins` vor der `print`-Zeile einen neuen Wert: seinen alten Wert plus 4.

</details>

<details>
<summary>Lösung</summary>

```trb run
var coins = 3
coins = coins + 4
print "Coins: {coins}"
// prints Coins: 7
```

</details>

## Zusammenfassung

`const` gibt einem Wert einen Namen, der sich nie ändert, `var` einen, der es kann, und `{name}` setzt einen Wert in
Text ein.
