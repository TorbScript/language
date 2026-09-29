---
title: Values and bindings
summary: A binding is const or var, a second name is always a copy, and a change goes through the path where the value lives.
kind: guide
status: stable
order: 30
prerequisites:
  - installing-and-running.md
keywords:
  - const
  - var
  - copy
  - value semantics
  - var path
source:
  - CONCEPT.md#bindings
  - CONCEPT.md#values
  - examples/tour/src/01-bindings-and-values.trb
---

In most languages you ask two questions: can this name change, and can the thing behind it change? In TorbScript there
is one answer, and the name gives it.

## Goal

At the end of this page you can tell for any line whether it compiles and what it changes.

## Two kinds of binding

```trb run
const answer = 42
var counter = 0
counter = counter + 1
const ratio: Float = 1
print "{answer} {counter} {ratio}"
// prints 42 1 1.0
```

`const` never changes, `var` can. A binding always gets a value when it is declared: there is no empty `var x`. A type
after the name is optional, and a number takes the type that is expected, so `1` became a `Float` here.

## A const goes all the way down

```trb error
const fixed = [1, 2]
fixed.append 3
// error: `append` needs a `var`
```

Through a `const` nothing changes: no new value, no field, and no method that changes the value. So a `const` list is
a list that never changes, and there is no separate `ImmutableList`.

A method that changes a value in place is a verb, such as `append` or `sort`. Its twin that returns a changed copy is a
participle, such as `appended` or `sorted`, and it works on a `const`:

```trb run
const fixed = [1, 2]
const longer = fixed.appended 3
print "{fixed} {longer}"
// prints [1, 2] [1, 2, 3]
```

## A second name is a copy

```trb run
type Point {
  var x: Int
  var y: Int
}

var first = Point 1, 2
const second = first
first.x = 99
print "{first} {second}"
// prints Point(x: 99, y: 2) Point(x: 1, y: 2)
```

`second` is a copy, so a change through `first` never shows up in it. The same is true when you pass a value to a
function or store it in a list. Copies are cheap: a list or a string shares its storage until one side writes to it.

## Change a value where it lives

This is the one mistake everybody makes once:

```trb error
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
var first = counters[0]
first.increment()
// error: This change has no effect: `first` is never read again
```

`var first = counters[0]` takes a copy out of the list, so the list never sees the change. The compiler notices when a
change is lost like this. Change the value where it lives instead:

```trb run
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
counters[0].increment()
print counters[0].count
// prints 1
```

`counters[0]` is a path to the value, not a copy of it. A path can go as deep as you like,
`world.players[id].health = 5`, and every step on it has to be changeable: the binding is a `var`, and every field on
the way is a `var` field. A field without `var` never changes after the value is built.

## Next

- [Functions and closures](functions-and-closures.md) - declaring a function and passing code as a value.
- [Bindings](../language/values-and-types/bindings.md) - the exact rules, including shadowing.
- [Why values instead of references](../explanation/why-values-instead-of-references.md) - why the language works this
  way.
