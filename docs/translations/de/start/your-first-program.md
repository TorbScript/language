---
title: Dein erstes Programm
summary: Ein Programm ist eine Liste von Anweisungen, die der Computer von oben nach unten befolgt, und print ist die Anweisung, die eine Zeile Text zeigt.
kind: lesson
status: stable
order: 1
translates: d65f10b0d871
---

Ein Programm ist eine Liste von Anweisungen. Der Computer liest sie von oben nach unten und führt eine nach der
anderen aus, genau wie sie dastehen.

Die Anweisung `print` zeigt eine Zeile Text. Der Text steht zwischen doppelten Anführungszeichen, damit der Computer
weiß, wo er anfängt und wo er aufhört.

Zeilen, die mit `//` beginnen, sind Kommentare: Notizen für Menschen, die der Computer überspringt. In diesen
Lektionen zeigt ein Kommentar mit `prints`, was das Programm ausgibt. Probier es aus: Ändere den Text und lass das
Programm noch einmal laufen.

```trb run
print "Hello!"
print "I am learning to program."
// prints Hello!
// prints I am learning to program.
```

## Übung

Ändere das Programm so, dass es zwei Zeilen ausgibt: zuerst `Hello, Ada!`, dann `Nice to meet you.`

```trb exercise
print "Hello, World!"
```

<details>
<summary>Tipp</summary>

Ändere den Text zwischen den Anführungszeichen und füge eine zweite Zeile hinzu, die mit `print` beginnt.

</details>

<details>
<summary>Lösung</summary>

```trb run
print "Hello, Ada!"
print "Nice to meet you."
// prints Hello, Ada!
// prints Nice to meet you.
```

</details>

## Zusammenfassung

Ein Programm läuft von oben nach unten, eine Zeile nach der anderen, und `print "..."` zeigt eine Zeile Text.
