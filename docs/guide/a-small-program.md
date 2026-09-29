---
title: Put it together
summary: One small program - a type with cases, a type with fields, a function that can fail and a pipeline - that uses what the guide taught.
kind: guide
status: stable
order: 120
prerequisites:
  - modules-and-packages.md
keywords:
  - example program
source:
  - examples/tour
---

This page adds nothing new. It puts the pieces of the guide into one program you can run and change.

## Goal

At the end of this page you have run a program that models tasks, checks its input and reports on a list.

## The program

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

## What each part does

- `Priority` is a type with three cases and no fields, `Task` a type with fields, one of them with a default.
- `completed` is a participle: it returns a changed copy, made with `copy`, and leaves the task alone.
- `newTask` can fail, so it returns a `Result`. The empty title becomes a `Fail` the caller has to handle.
- `label` is a `match` that handles every case of `Priority`.
- `filterMap({ _.ok() })` keeps the `Ok` values and drops every `Fail`, so the empty title never reaches the loop.
- `tasks[0] = tasks[0].completed()` changes the list where the value lives, which is why `tasks` is a `var`.

## Next

- [Idiomatic TorbScript](idiomatic-torbscript.md) - the habits that make code read like the standard library.
- [Task recipes](../how-to/index.md) - one page per task, once you know the language.
- [The language reference](../language/index.md) - the exact rule behind anything on the way here.
