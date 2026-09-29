---
title: Alles zusammensetzen
summary: Ein kleines Programm - ein Typ mit Fällen, ein Typ mit Feldern, eine Funktion, die scheitern kann, und eine Pipeline -, das benutzt, was der Guide gezeigt hat.
kind: guide
status: stable
order: 120
prerequisites:
  - modules-and-packages.md
translates: 14e4b5d74859
---

Diese Seite bringt nichts Neues. Sie setzt die Teile des Guides zu einem Programm zusammen, das du ausführen und
ändern kannst.

## Ziel

Am Ende dieser Seite hast du ein Programm ausgeführt, das Aufgaben modelliert, seine Eingabe prüft und über eine Liste
berichtet.

## Das Programm

```trb run
type Priority {
  case Low
  case Medium
  case High
}

type Task {
  title: String
  priority: Priority
  done: Bool = false

  fn completed(): Task {
    copy done: true
  }
}

type TaskError {
  case EmptyTitle
}

fn newTask(title: String, priority: Priority): Result<Task, TaskError> {
  if title.isEmpty() {
    return Fail TaskError.EmptyTitle
  }
  Task title, priority
}

fn label(priority: Priority): String {
  match priority {
    .Low => "low"
    .Medium => "medium"
    .High => "high"
  }
}

const attempts = [
  newTask("Write the guide", Priority.High),
  newTask("", Priority.Low),
  newTask("Review the change", Priority.Medium),
]

var tasks = attempts.filterMap({ _.ok() }).toList()
tasks[0] = tasks[0].completed()

for task in tasks {
  const mark = if task.done { "x" } else { " " }
  print "[{mark}] {task.title} ({label(task.priority)})"
}

const open = tasks.filter({ !_.done }).count()
print "{open} open"
// prints [x] Write the guide (high)
// prints [ ] Review the change (medium)
// prints 1 open
```

## Was jeder Teil tut

- `Priority` ist ein Typ mit drei Fällen und ohne Felder, `Task` ein Typ mit Feldern, eines davon mit Standardwert.
- `completed` ist ein Partizip: Es gibt eine mit `copy` gemachte geänderte Kopie zurück und lässt die Aufgabe in Ruhe.
- `newTask` kann scheitern, also gibt es ein `Result` zurück. Der leere Titel wird zu einem `Fail`, das der Aufrufer
  behandeln muss.
- `label` ist ein `match`, das jeden Fall von `Priority` behandelt.
- `filterMap({ _.ok() })` behält die `Ok`-Werte und verwirft jedes `Fail`, also erreicht der leere Titel die Schleife
  nie.
- `tasks[0] = tasks[0].completed()` ändert die Liste dort, wo der Wert liegt, deshalb ist `tasks` ein `var`.

## Weiter

- [Idiomatisches TorbScript](idiomatic-torbscript.md) - die Gewohnheiten, mit denen Code wie die Standardbibliothek
  liest.
- [Task recipes](../how-to/index.md) - eine Seite pro Aufgabe, sobald du die Sprache kennst.
- [The language reference](../language/index.md) - die genaue Regel hinter allem auf dem Weg hierher.
