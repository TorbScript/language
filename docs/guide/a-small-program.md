---
title: Put it together
summary: One small program - a type with cases, a function that can fail, and a pipeline - that uses everything this path taught.
kind: guide
status: stable
order: 120
prerequisites:
  - tests-and-tooling.md
keywords:
  - example program
source:
  - examples/tour
---

Every idea on this path shows up in one program below: a `type` with fields and cases, a function that answers a
`Result`, `match`, and a pipeline that ends in a collector. Nothing here is new: this page only wires it together.

## Goal

At the end of this page you have run one program that declares a type, validates input into a `Result`, and reports on
a collection through a pipeline.

## Modeling a task

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

`Priority` is a type with three cases and no fields; `Task` is a type with fields, one of them defaulted. `completed`
is a participle: it answers a changed copy rather than changing `self`, using the generated `copy`.

## Validating input

A task with an empty title is a mistake the caller has to see, so creating one answers a `Result`:

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

`Fail TaskError.EmptyTitle` and `Ok Task(title, priority)` are both command calls: the first argument is a value, the
second a nested call, and neither needs its own parentheses to be unambiguous.

## Running the pipeline

Put the two pieces together over a list of attempts, keep only the ones that succeeded, and report on them:

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

`filterMap` drops every `Fail` and unwraps every `Ok`, so the empty title never reaches the loop. This prints:

```text
Write the guide (high)
Review the PR (medium)
High priority: 1
```

## Next

- [The language reference](../language/index.md) - one page per construct, for the exact rule behind anything above.
- [Task recipes](../how-to/index.md) - a recipe for one task at a time, once you know the language.
- [Why the language is like this](../explanation/index.md) - the arguments behind the decisions this path only showed
  you the surface of.
