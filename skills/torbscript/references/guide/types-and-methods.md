---
title: Types and methods
summary: Declare a type with fields, give it methods, and tell a method that changes the value from one that returns a changed copy.
kind: guide
status: stable
order: 50
prerequisites:
  - functions-and-closures.md
keywords:
  - type
  - method
  - var fn
  - static fn
  - copy
source:
  - CONCEPT.md#types
  - examples/tour/src/03-types.trb
---

`type` is the one keyword for your own data. What other languages split into struct, class and record is one
declaration here.

## Goal

At the end of this page you can declare a type with fields and methods, and you know when a method is a `var fn`.

## Declare a type

```trb run
type Point {
  var x: Int
  var y: Int
}

const origin = Point 0, 0
const somewhere = Point x: 3, y: 4
print "{origin} {somewhere}"
print(origin == somewhere)
// prints Point(x: 0, y: 0) Point(x: 3, y: 4)
// prints false
```

A field is `const` unless it says `var`, and public unless it says `private`. You write no constructor: it takes the
fields in order, by position or by name. Comparing with `==`, hashing, printing and `copy` come for free as well.

## Add methods

```trb run
type Circle {
  radius: Float

  fn area(): Float {
    3.0 * radius * radius
  }

  static fn unit(): Circle {
    Circle 1.0
  }
}

print Circle(2.0).area()
print Circle.unit().radius
// prints 12.0
// prints 1.0
```

A method is a function inside the type. It does not list `self`: inside it, `radius` is the field of the value it was
called on. A `static fn` belongs to the type, not to a value, and is called on the type: `Circle.unit()`.

## A method that changes the value

```trb run
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }

  fn incremented(): Counter {
    copy(count: count + 1)
  }
}

var counter = Counter()
counter.increment()
const next = counter.incremented()
print "{counter.count} {next.count}"
// prints 1 2
```

A method that changes the value is a `var fn`, and it can only be called on a `var`. Its twin `incremented` returns a
changed copy made with `copy`, and works on anything. The name tells them apart: a verb changes in place, a participle
returns a copy.

```trb error
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

const frozen = Counter()
frozen.increment()
// error: `increment` needs a `var`
```

## Next

- [Cases and matching](cases-and-matching.md) - a type that is one of several shapes.
- Declaring a type (skill `torbscript-language`: `references/language/types/declaring-a-type.md`) - fields, defaults and what comes for free, in full.
- Verbs and participles (skill `torbscript-language`: `references/language/types/verbs-and-participles.md`) - how a pair of method names is chosen.

