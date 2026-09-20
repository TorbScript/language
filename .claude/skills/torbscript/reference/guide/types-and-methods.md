---
title: Types and methods
summary: How to declare a type, add methods to it, and tell a verb that changes it from the participle that answers a copy.
kind: guide
status: stable
order: 40
prerequisites:
  - functions-and-closures.md
keywords:
  - type
  - method
  - self
  - var self
  - static function
source:
  - CONCEPT.md#types
  - examples/tour/src/03-types.trb
---

`type` is the one keyword that declares a data type: what other languages split into `struct`, `class` and a record is
one declaration here. This page takes you from a first `type` to a method that changes it in place.

## Goal

At the end of this page you can declare a type with fields and methods, and know which method needs `var self` and
which does not.

## Declaring a type

```trb
type Point {
  var x: Int
  var y: Int
}

const origin = Point 0, 0
const somewhere = Point x: 3, y: 4
print "{origin} {somewhere}"
```

A field is public unless marked `private`, and `const` unless marked `var`. The
[constructor](../glossary.md#constructor) is generated from the fields in declaration order, so `Point(0, 0)` and
`Point(x: 3, y: 4)` both work without a line of code written for them. `Equals`, `Hash` and `Show` are generated too, which is why `print` above needs nothing extra to show a
`Point`. See [Declaring a type](../language/types/declaring-a-type.md) for the full list of what is generated.

## Adding a method

A method is a function declared inside the type that names `self` as its receiver.

```trb
type Rectangle {
  width: Int
  height: Int

  fn area(self): Int {
    width * height
  }
}

const rectangle = Rectangle 3, 4
print rectangle.area()
```

Inside `area`, `width` and `height` resolve against `self` without writing `self.width`. A function declared on a type
without `self` is a [static function](../language/types/methods.md), called through the type itself rather than
through a value:

```trb
type Circle {
  radius: Float

  fn area(self): Float {
    3.14159 * radius * radius
  }

  fn unit(): Circle {
    Circle 1.0
  }
}

print Circle.unit().area()
```

## Changing a value in place: var self

A method that changes its receiver declares `var self`, and is called a **verb**.

```trb
type Counter {
  var count: Int = 0

  fn increment(var self) {
    count = count + 1
  }

  fn incremented(self): Counter {
    copy(count: count + 1)
  }
}

var counter = Counter()
counter.increment()
print counter.count
```

`increment` needs `counter` to be a `var` binding, because a change always needs a
[`var` path](../language/types/var-paths.md) from the binding down. Its **participle**, `incremented`, takes a plain
`self` and answers a changed copy instead, using `copy`, which every type gets for free. Calling `increment` through a
`const` binding is a compile error:

```trb error
type Counter {
  var count: Int = 0

  fn increment(var self) {
    count = count + 1
  }
}

const frozen = Counter()
frozen.increment()
// error: `increment` needs a `var`
```

See [Verbs and participles](../language/types/verbs-and-participles.md) for how a new pair is named.

## Next

- [Cases and matching](cases-and-matching.md) - a type with more than one shape, and taking it apart.
- [Declaring a type](../language/types/declaring-a-type.md) - fields, visibility and what is generated, in full.
- [Mutation and var paths](../language/types/var-paths.md) - what has to be `var` from the binding down.
