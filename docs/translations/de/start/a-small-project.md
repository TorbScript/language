---
title: Ein kleines Projekt
summary: Eine Leseliste, die jede Lektion zusammenbringt - einen Typ mit Fällen, einen Typ mit Feldern, eine Liste, eine Schleife und Funktionen - in einem Programm.
kind: lesson
status: stable
order: 12
translates: 2a66ca115023
---

Zeit, alles zusammenzubringen. Das Programm unten führt eine Leseliste: Jedes Buch hat einen Titel, eine Seitenzahl
und einen Status – nicht angefangen, gelesen bis zu einer bestimmten Seite, oder fertig.

Der Status ist einer von drei Fällen, also ein Typ mit `case`. Ein Buch bündelt drei Werte, also ein Typ mit Feldern.
Die Bücher sind eine Liste, eine Schleife geht sie durch, und eine Funktion mit einem `match` beschreibt jedes davon.

```trb run
type Status {
  case Unread
  case Reading(page: Int)
  case Finished
}

type Book {
  title: String
  pages: Int
  status: Status
}

fn describe(book: Book): String {
  match book.status {
    .Unread => "{book.title}: not started"
    .Reading(page) => "{book.title}: page {page} of {book.pages}"
    .Finished => "{book.title}: finished"
  }
}

const books = [
  Book("Momo", 304, Status.Finished),
  Book("Dune", 612, Status.Reading(120)),
  Book("Emma", 474, Status.Unread),
]

for book in books {
  print describe(book)
}
// prints Momo: finished
// prints Dune: page 120 of 612
// prints Emma: not started
```

Lies es von unten nach oben: Die Schleife gibt aus, was `describe` für jedes Buch antwortet, und `describe` wählt
seine Zeile nach dem Status. Innerhalb einer Liste wird ein Buch mit Klammern erzeugt, `Book("Momo", 304,
Status.Finished)`, weil es Teil von etwas Größerem ist.

## Übung

Wie viele Seiten hast du gelesen? Ein fertiges Buch zählt alle seine Seiten, ein Buch, das du liest, zählt bis zu
seiner Seite, und ein ungelesenes zählt 0. Vervollständige `pagesRead` und addiere alle Bücher, sodass das Programm
`Pages read: 424` ausgibt.

```trb exercise
type Status {
  case Unread
  case Reading(page: Int)
  case Finished
}

type Book {
  title: String
  pages: Int
  status: Status
}

fn pagesRead(book: Book): Int {
  0
}

const books = [
  Book("Momo", 304, Status.Finished),
  Book("Dune", 612, Status.Reading(120)),
  Book("Emma", 474, Status.Unread),
]

var total = 0
for book in books {
  total = total + pagesRead(book)
}
print "Pages read: {total}"
```

<details>
<summary>Tipp</summary>

Ersetze die `0` in `pagesRead` durch ein `match book.status { ... }`, das für die drei Fälle `0`, `page` oder
`book.pages` zurückgibt.

</details>

<details>
<summary>Lösung</summary>

```trb run
type Status {
  case Unread
  case Reading(page: Int)
  case Finished
}

type Book {
  title: String
  pages: Int
  status: Status
}

fn pagesRead(book: Book): Int {
  match book.status {
    .Unread => 0
    .Reading(page) => page
    .Finished => book.pages
  }
}

const books = [
  Book("Momo", 304, Status.Finished),
  Book("Dune", 612, Status.Reading(120)),
  Book("Emma", 474, Status.Unread),
]

var total = 0
for book in books {
  total = total + pagesRead(book)
}
print "Pages read: {total}"
// prints Pages read: 424
```

</details>

## Zusammenfassung

Ein echtes Programm ist die Teile dieses Kurses zusammen: Typen für deine Daten, eine Liste davon, und kleine
Funktionen, die jede eine Sache tun.
