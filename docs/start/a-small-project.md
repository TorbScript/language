---
title: A small project
summary: A reading list that puts every lesson together - a type with cases, a type with fields, a list, a loop and functions - in one program.
kind: lesson
status: stable
order: 12
---

Time to put everything together. The program below keeps a reading list: every book has a title, a number of pages
and a status - not started, being read up to some page, or finished.

The status is one of three cases, so it is a type with `case`. A book bundles three values, so it is a type with fields.
The books are a list, a loop goes through them, and a function with a `match` describes each one.

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

Read it from the bottom up: the loop prints what `describe` answers for each book, and `describe` picks its line by the
status. Inside a list, a book is made with parentheses, `Book("Momo", 304, Status.Finished)`, because it is part of
something bigger.

## Exercise

How many pages have you read? A finished book counts all its pages, a book you are reading counts up to its page, and
an unread one counts 0. Finish `pagesRead` and add up all books, so the program prints `Pages read: 424`.

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
<summary>Hint</summary>

Replace the `0` in `pagesRead` with a `match book.status { ... }` that answers `0`, `page` or `book.pages` for the
three cases.

</details>

<details>
<summary>Solution</summary>

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

## Recap

A real program is the pieces of this course together: types for your data, a list of them, and small functions that
each do one thing.
