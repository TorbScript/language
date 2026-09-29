---
title: Entscheidungen treffen
summary: Mit if und else tut ein Programm das eine oder das andere, je nachdem, wie eine Frage beantwortet wird - wahr oder falsch.
kind: lesson
status: stable
order: 4
translates: c1d2570e7673
---

Ein Programm muss oft entscheiden: eine Jacke mitnehmen oder nicht. `if` stellt eine Frage und führt die Zeilen in
seinen geschweiften Klammern nur aus, wenn die Antwort Ja ist. `else` hält das, was läuft, wenn die Antwort Nein ist.

Die Frage vergleicht zwei Werte: `<` ist kleiner als, `>` größer als, `<=` und `>=` schließen den Wert selbst ein,
`==` heißt gleich und `!=` ungleich. Achte auf die zwei Gleichheitszeichen: Ein `=` gibt einem Namen einen Wert, zwei
vergleichen.

```trb run
const temperature = 8
if temperature < 10 {
  print "Take a jacket."
} else {
  print "No jacket needed."
}
// prints Take a jacket.
```

## Mehr als zwei Wege

`else if` stellt die nächste Frage, wenn die davor mit Nein beantwortet wurde. Der Computer geht von oben nach unten
und nimmt den ersten Weg, dessen Antwort Ja ist.

```trb run
const hour = 14
if hour < 12 {
  print "Good morning"
} else if hour < 18 {
  print "Good afternoon"
} else {
  print "Good evening"
}
// prints Good afternoon
```

Auch die Antwort auf eine Frage ist ein Wert: `true` oder `false`. Ihr Typ ist `Bool`, und ein Name kann ihn halten
wie jeden anderen Wert: `const isCold = temperature < 10`.

## Übung

Lass das Programm `Adult` ausgeben, wenn `age` 18 oder mehr ist, sonst `Child`. Bei einem Alter von 20 gibt es
`Adult` aus.

```trb exercise
const age = 20
print "Child"
```

<details>
<summary>Tipp</summary>

Frage `if age >= 18`, gib in seinen Klammern `Adult` aus, und verschiebe `print "Child"` in die Klammern eines
`else`.

</details>

<details>
<summary>Lösung</summary>

```trb run
const age = 20
if age >= 18 {
  print "Adult"
} else {
  print "Child"
}
// prints Adult
```

</details>

## Zusammenfassung

`if` führt seine Zeilen aus, wenn die Antwort auf seine Frage `true` ist, `else` wenn sie `false` ist, und `==`
vergleicht, während `=` einen Wert gibt.
