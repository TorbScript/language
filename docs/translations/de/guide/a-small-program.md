---
title: Alles zusammensetzen
summary: Ein kleines Programm - ein Typ mit Fällen, eine Funktion, die scheitern kann, und eine Pipeline -, das alles benutzt, was dieser Pfad gelehrt hat.
kind: guide
status: stable
order: 120
prerequisites:
  - tests-and-tooling.md
translates: 06fc30b3e8bb
---

Jede Idee dieses Pfads taucht unten in einem Programm auf: ein `type` mit Feldern und Fällen, eine Funktion, die ein
`Result` liefert, `match`, und eine Pipeline, die in einem Collector endet. Nichts hier ist neu: Diese Seite
verdrahtet es nur miteinander.

## Ziel

Am Ende dieser Seite hast du ein Programm ausgeführt, das einen Typ deklariert, Eingaben in ein `Result` validiert,
und über eine Kollektion durch eine Pipeline berichtet.

## Eine Aufgabe modellieren

```trb check
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

fn priorityLabel(priority: Priority): String {
  match priority {
    .Low => "low"
    .Medium => "medium"
    .High => "high"
  }
}
```

`Priority` ist ein Typ mit drei Fällen und ohne Felder; `Task` ist ein Typ mit Feldern, einem davon mit Standardwert.
`completed` ist ein Partizip: Es liefert eine geänderte Kopie, statt `self` zu ändern, mit dem erzeugten `copy`.

## Eingaben validieren

Eine Aufgabe mit leerem Titel ist ein Fehler, den der Aufrufer sehen muss, also liefert das Erzeugen einer ein
`Result`:

```trb check
type Priority {
  case Low
  case Medium
  case High
}

type Task {
  title: String
  priority: Priority
  done: Bool = false
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

match newTask("", Priority.Low) {
  Ok(task) => print "created {task.title}"
  Fail(error) => print "rejected: {error}"
}
```

`Fail TaskError.EmptyTitle` und `Ok Task(title, priority)` sind beides Command Calls: Das erste Argument ist ein
Wert, das zweite ein verschachtelter Aufruf, und keines braucht eigene Klammern, um eindeutig zu sein.

## Die Pipeline laufen lassen

Setz die zwei Teile über eine Liste von Versuchen zusammen, behalte nur die erfolgreichen, und berichte über sie:

```trb check
type Priority {
  case Low
  case Medium
  case High
}

type Task {
  title: String
  priority: Priority
  done: Bool = false
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

fn priorityLabel(priority: Priority): String {
  match priority {
    .Low => "low"
    .Medium => "medium"
    .High => "high"
  }
}

const attempts = [
  newTask("Write the guide", Priority.High),
  newTask("", Priority.Low),
  newTask("Review the PR", Priority.Medium),
]

const tasks = attempts.filterMap { _.ok() }.toList()

for task in tasks {
  print "{task.title} ({priorityLabel(task.priority)})"
}

const highPriority = tasks.filter { _.priority == Priority.High }.count()
print "High priority: {highPriority}"
```

`filterMap` verwirft jedes `Fail` und entpackt jedes `Ok`, sodass der leere Titel die Schleife nie erreicht. Das
gibt aus:

```text
Write the guide (high)
Review the PR (medium)
High priority: 1
```

## Weiter

- [The language reference](../language/index.md) - eine Seite pro Konstrukt, für die genaue Regel hinter allem oben.
- [Task recipes](../how-to/index.md) - ein Rezept pro Aufgabe, sobald du die Sprache kennst.
- [Why the language is like this](../explanation/index.md) - die Argumente hinter den Entscheidungen, von denen
  dieser Pfad dir nur die Oberfläche gezeigt hat.
