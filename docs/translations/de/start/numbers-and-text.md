---
title: Zahlen und Text
summary: Programme rechnen mit + - * und /, fügen Text mit + zusammen, und jeder Wert hat einen Typ, der sagt, was für ein Wert es ist.
kind: lesson
status: stable
order: 3
translates: 21967d6f522b
---

Ein Programm kann rechnen: `+` addiert, `-` subtrahiert, `*` multipliziert und `/` dividiert. Wenn das, was du
ausgibst, eine Rechnung ist, setze sie in Klammern, damit der Computer weiß, dass sie ganz zu `print` gehört.

Ganze Zahlen bleiben ganz: `7 / 2` ist `3`, der Rest fällt weg. Für ein Ergebnis mit Komma schreibst du die Zahlen mit
einem Punkt: `7.0 / 2.0` ist `3.5`.

```trb run
const price = 4
const count = 3
print(price * count)
print(7 / 2)
print(7.0 / 2.0)
// prints 12
// prints 3
// prints 3.5
```

## Text

Text wird mit `+` zusammengefügt, und er kann Dinge, die hinter einem Punkt stehen: `full.toUpperCase()` gibt denselben
Text in Großbuchstaben zurück.

```trb run
const first = "Ada"
const last = "Lovelace"
const full = first + " " + last
print full
print full.toUpperCase()
// prints Ada Lovelace
// prints ADA LOVELACE
```

## Jeder Wert hat einen Typ

Eine ganze Zahl wie `12` ist ein `Int`, eine Zahl mit Komma wie `3.5` ist ein `Float`, und Text wie `"Ada"` ist ein
`String`. Diese Art von Wert heißt sein [Typ](../glossary.md#type).

Der Typ entscheidet, was funktioniert: Text und eine Zahl lassen sich nicht mit `+` verbinden, weil unklar ist, was
das bedeuten soll. Um eine Zahl in Text einzusetzen, benutze geschweifte Klammern: `"Age: {age}"`.

<details>
<summary>Was passiert, wenn du Text und eine Zahl addierst?</summary>

Der Computer findet das, bevor das Programm läuft:

```trb error
print "Age: " + 36
// error: Expected `String`, found `Int64`
```

`Int64` ist der volle Name von `Int`: eine ganze Zahl, die in 64 Bit passt.

</details>

## Übung

Ein Ticket kostet 12, und du kaufst 3. Lass das Programm `Total: 36` ausgeben, indem du die Summe berechnest statt 0
auszugeben.

```trb exercise
const pricePerTicket = 12
const tickets = 3
print "Total: 0"
```

<details>
<summary>Tipp</summary>

Gib der Summe mit `const total = ...` einen Namen und setze dann `{total}` in den Text ein.

</details>

<details>
<summary>Lösung</summary>

```trb run
const pricePerTicket = 12
const tickets = 3
const total = pricePerTicket * tickets
print "Total: {total}"
// prints Total: 36
```

</details>

## Zusammenfassung

Zahlen rechnen mit `+ - * /`, Text fügt sich mit `+` zusammen, und jeder Wert hat einen Typ: `Int`, `Float` oder
`String`.
