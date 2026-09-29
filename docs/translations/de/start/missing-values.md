---
title: Wenn ein Wert fehlen könnte
summary: Wenn ein Wert vielleicht nicht da ist - das vierte Element einer Liste mit drei -, sagt die Antwort das, und ?? oder match entscheiden, was dann passiert.
kind: lesson
status: stable
order: 10
translates: 43f1c6483ccd
---

Eine Liste mit drei Namen hat keinen vierten. `winners[3]` würde das Programm anhalten, weil es nichts gibt, das es
zurückgeben könnte. `winners.get(3)` fragt stattdessen höflich: Es antwortet entweder mit dem Wert oder mit nichts,
und du entscheidest, was dann passiert.

`??` ist der einfachste Weg zu entscheiden: `winners.get(3) ?? "nobody"` ist der Wert, wenn es einen gibt, und
`"nobody"`, wenn nicht.

```trb run
const winners = ["Ada", "Alan", "Grace"]
const third = winners.get(2) ?? "nobody"
const fourth = winners.get(3) ?? "nobody"
print "Third place: {third}"
print "Fourth place: {fourth}"
// prints Third place: Grace
// prints Fourth place: nobody
```

## Etwas oder nichts

Die Antwort von `get` ist einer von zwei Fällen, wie das Wetter der letzten Lektion: `Some(name)`, wenn es einen Wert
gibt, und `None`, wenn nicht. `match` kann sie auseinandernehmen und muss beide behandeln.

```trb run
const winners = ["Ada", "Alan", "Grace"]
match winners.get(0) {
  Some(name) => print "The winner is {name}!"
  None => print "Nobody won."
}
// prints The winner is Ada!
```

Viele Sprachen haben einen besonderen Wert für "nichts", der überall passt, und ein Programm stürzt ab, wenn es
vergisst, danach zu fragen. In TorbScript ist "vielleicht nichts" ein eigener Typ, ein [Option](../glossary.md#option),
sodass der Computer merkt, wenn eine Prüfung fehlt.

## Übung

Gib den dritten Gast aus, oder `nobody`, wenn es keinen dritten gibt. Bei zwei Gästen gibt das Programm `nobody` aus.

```trb exercise
const guests = ["Ada", "Alan"]
print guests[0]
```

<details>
<summary>Tipp</summary>

Frag mit `guests.get(2)` und benutze `?? "nobody"` für den Fall, dass nichts zurückkommt. Gib dem Ergebnis einen
Namen, bevor du es ausgibst.

</details>

<details>
<summary>Lösung</summary>

```trb run
const guests = ["Ada", "Alan"]
const third = guests.get(2) ?? "nobody"
print third
// prints nobody
```

</details>

## Zusammenfassung

Ein Wert, der fehlen könnte, ist `Some(value)` oder `None`, und `??` oder `match` entscheiden, was passiert, wenn er
`None` ist.
