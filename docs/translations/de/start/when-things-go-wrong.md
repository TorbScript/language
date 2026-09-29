---
title: Wenn etwas schiefgeht
summary: Eine Funktion, die scheitern kann, sagt das und antwortet mit Ok und ihrem Ergebnis oder mit Fail und dem Grund, sodass jeder Fehlschlag dort behandelt wird, wo er auftritt.
kind: lesson
status: stable
order: 11
translates: ad4dd7f6df7b
---

Manche Dinge können scheitern: Text, der eine Zahl sein sollte, ist keine, eine Datei ist nicht da. Eine Funktion,
die scheitern kann, sagt das und antwortet mit einem von zwei Fällen: `Ok` mit ihrem Ergebnis oder `Fail` mit dem
Grund.

`Int.tryFrom("42")` macht aus Text eine ganze Zahl. Für `"42"` antwortet es `Ok(42)`, für `"forty-two"` `Fail`. Ein
`match` behandelt beide.

```trb run
for typed in ["42", "forty-two"] {
  match Int.tryFrom(typed) {
    Ok(number) => print "{typed} is the number {number}."
    Fail(_) => print "{typed} is not a number."
  }
}
// prints 42 is the number 42.
// prints forty-two is not a number.
```

`_` ist ein Name für einen Wert, den du nicht brauchst, hier die Einzelheiten des Fehlschlags.

## Warum keine versteckten Sprünge

In vielen Sprachen springt ein Fehlschlag aus der Mitte des Programms an eine weit entfernte Stelle, die ihn auffängt
– oder beendet das Programm, wenn keine das tut. Dieser Sprung heißt Exception, und der Code zeigt nicht, wo er
passieren kann.

TorbScript hat keine Exceptions. Ein Fehlschlag ist ein ganz normaler Wert, der Typ der Funktion sagt, dass sie
scheitern kann, und du siehst jede Stelle, an der etwas schiefgehen kann. Dieser Typ ist ein
[Result](../glossary.md#result).

## Deine eigene Funktion, die scheitern kann

`Result<Int, String>` nach den Klammern sagt: Die Antwort ist ein `Int`, wenn es klappt, und ein `String` mit dem
Grund, wenn nicht. `return Fail "..."` beendet die Funktion vorzeitig mit einem Fehlschlag.

```trb run
fn share(total: Int, people: Int): Result<Int, String> {
  if people == 0 {
    return Fail "there is nobody to share with"
  }
  Ok(total / people)
}

match share(12, 0) {
  Ok(each) => print "Each gets {each}."
  Fail(reason) => print "Cannot share: {reason}."
}
// prints Cannot share: there is nobody to share with.
```

## Übung

Ein Ticket kostet 10, aber ein Alter unter 0 ist ein Fehler. Lass `ticketPrice` für so ein Alter mit dem Grund
`an age cannot be negative` scheitern, sodass das Programm `Error: an age cannot be negative` ausgibt.

```trb exercise
fn ticketPrice(age: Int): Result<Int, String> {
  Ok 10
}

match ticketPrice(-3) {
  Ok(price) => print "Price: {price}"
  Fail(reason) => print "Error: {reason}"
}
```

<details>
<summary>Tipp</summary>

Frag vor `Ok 10` `if age < 0 { ... }` und darin `return Fail "an age cannot be negative"`.

</details>

<details>
<summary>Lösung</summary>

```trb run
fn ticketPrice(age: Int): Result<Int, String> {
  if age < 0 {
    return Fail "an age cannot be negative"
  }
  Ok 10
}

match ticketPrice(-3) {
  Ok(price) => print "Price: {price}"
  Fail(reason) => print "Error: {reason}"
}
// prints Error: an age cannot be negative
```

</details>

## Zusammenfassung

Was scheitern kann, antwortet mit `Ok(result)` oder `Fail(reason)`, es gibt keine Exceptions, und `match` behandelt
beide dort, wo es passiert.
