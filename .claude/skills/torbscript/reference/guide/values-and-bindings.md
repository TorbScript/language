---
title: Values and bindings
summary: Why const and var are the whole mutation story, what a copy costs, and the one trap that catches everybody coming from a language with references.
kind: guide
status: stable
order: 20
prerequisites:
  - installing-and-running.md
keywords:
  - const
  - var
  - copy
  - value semantics
source:
  - CONCEPT.md#bindings
  - CONCEPT.md#values
  - examples/tour/src/01-bindings-and-values.trb
---

Most languages need two answers about a value: is the *binding* changeable, and is the *thing* changeable. TorbScript
has one. The binding decides, and it decides everything below it.

## Goal

At the end of this page you can predict, for any line, whether it compiles and what changes.

## Two words

```trb
const answer = 42
var counter = 0
counter = counter + 1
print "{answer} {counter}"
```

`const` is a binding that never changes. `var` is a binding that can. A binding always has an initializer: `var x` and
`var x: Int` are both errors, because there is no implied default value anywhere in the language.

A type annotation is optional and goes after the name. A literal adapts to the type that is expected of it:

```trb
const ratio: Float = 1
const byte: UInt8 = 0xFF
const million = 1_000_000
```

`1` became a `Float64` because that is what was expected. Without an annotation an integer literal is `Int64` and a
decimal literal is `Float64`.

## The binding decides about the value too

This is the part that is different from nearly every other language:

```trb
var list = [1, 2]
list.append 3
const fixed = list
print "{list} {fixed}"
```

`list.append 3` works because `list` is a `var`. `fixed.append 3` would not compile, and not because the *binding* cannot be
reassigned - because `const` is **deep**. Through a `const` binding you cannot reassign, cannot assign a field, and
cannot call a method that is a `var fn`.

```trb error
const fixed = [1, 2]
fixed.append 3
// error: `append` needs a `var`
```

The diagnostic names the other half of the rule. A method that changes its receiver in place is a **verb** and declares
a `var fn`; the method that returns a changed copy instead is its **participle**. So `list.sort { _ }` sorts in place and
`list.sorted { _ }` answers a new list, `append` and `appended`, `remove` and `removed`.

There is no `MutableList`, no `ImmutableList` and no read-only view. A `const` binding *is* the immutable list.

## Assigning is copying

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

That prints `Point(x: 99, y: 2) Point(x: 1, y: 2)`. `second` is a copy, so nothing that happens through `first` can be
seen through it. The same holds for passing a value to a function and for capturing one in a closure.

What a copy *costs* is the implementation's business and is never observable. A small value like a `Point` is really
copied; a `List` or a `String` shares its storage until somebody writes to it, and then the writer copies. So the model
is "always a copy" and the cost is "only when it matters".

## The copy trap

This is the one mistake everybody makes once, and it is the price of value semantics:

```trb run
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
var first = counters[0]
first.increment()
print "{first.count} {counters[0].count}"
// prints 1 0
```

That prints `1 0`. `var first = counters[0]` took a **copy** out of the list, so incrementing it left the list
untouched. A change that is never read afterwards is a compile error (`This change has no effect: `first` is never
read again`, with the note that `first` is a copy and the path it came from). This program reads `first.count` in the
last line, so the compiler has nothing to report: the copy is used, it is only not what was meant. Reach through the
path instead:

```trb check
type Counter {
  var count: Int = 0

  var fn increment() {
    count = count + 1
  }
}

var counters = [Counter(), Counter()]
counters[0].increment()
print counters[0].count
```

`counters[0]` is a **`var` path**: the value is taken out, changed and put back, without a copy. A path may go through
fields and indices as deep as you like (`world.entities[id].health = 5`), and every step of it has to be `var`.

## Mutation needs a var path, from the binding down

Three things have to line up for a change to be legal:

1. the **binding** is a `var`, or you are inside a `var` parameter or a `var fn` method,
2. every **field** on the way is declared `var`,
3. the value you reach is reached through that path and not through a copy of it.

```trb
type Engine {
  var running: Bool = false
}

type Car {
  var engine: Engine = Engine()
  wheels: Int = 4
}

var car = Car()
car.engine.running = true
print car.engine.running
```

`wheels` has no `var`, so it never changes after construction - not even through a `var` binding. That is how a value
says "this part of me is fixed".

## Next

- [Bindings](../language/values-and-types/bindings.md) - the exact rules, including shadowing and dead changes.
- [Declaring a type](../language/types/declaring-a-type.md) - fields, methods and what is generated.
- [Why values instead of references](../explanation/why-values-instead-of-references.md) - the argument, and what it
  costs.
- [Coming from Rust](../explanation/coming-from-rust.md) - if `&mut` is what you reach for.

