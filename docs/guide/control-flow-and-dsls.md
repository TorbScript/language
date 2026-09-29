---
title: Control flow and your own constructs
summary: Ifs and loops work as you expect, and a new control structure or a configuration block is an ordinary function you can write yourself.
kind: guide
status: stable
order: 100
prerequisites:
  - collections-and-pipelines.md
keywords:
  - if
  - for
  - while
  - loop
  - receiver closure
  - DSL
source:
  - CONCEPT.md#blocks-and-control-flow
  - CONCEPT.md#configuration-dsl
  - examples/tour/src/08-control-flow.trb
  - examples/tour/src/09-dsl.trb
---

`if`, `for`, `while` and `loop` look the way they look everywhere. What is new is that you can add your own: a control
structure is a function, not new syntax.

## Goal

At the end of this page you can use the built-in control flow, write a control structure of your own, and read a
configuration block as the function call it is.

## An if gives a value

```trb run
const temperature = 23
const feeling = if temperature < 10 {
  "cold"
} else if temperature < 25 {
  "pleasant"
} else {
  "hot"
}
print feeling
// prints pleasant
```

An `if` can give a value, like a `match`. Then every branch gives a value of the same type.

## Loops

```trb run
for index in 0..3 {
  print index
}

var attempts = 0
while attempts < 10 {
  attempts = attempts + 1
  if attempts > 2 {
    break
  }
  print "attempt {attempts}"
}
// prints 0
// prints 1
// prints 2
// prints attempt 1
// prints attempt 2
```

`0..3` counts from 0 up to 3 without the 3; `0..=3` includes it. `break` and `continue` work as usual. A loop that
does not end by itself, like a server's, is `loop { ... }`: `while true` is an error that tells you so.

## Write your own control structure

```trb run
fn unless(condition: Bool, body: () => Void) {
  if !condition {
    body()
  }
}

const items = [1, 2]
unless items.isEmpty() {
  print "{items.length()} items"
}
// prints 2 items
```

`unless` is an ordinary function whose last parameter is a closure. A call without parentheses and a closure after it
make it read like a keyword. `test` from the standard library is written the same way.

## A configuration block

```trb run
type ServerOptions {
  var host: String = "localhost"
  var port: Int = 8080
}

fn serve(configure: (var self: ServerOptions) => Void): ServerOptions {
  var options = ServerOptions()
  configure options
  options
}

const options = serve {
  host = "0.0.0.0"
  port = 8443
}
print options
// prints ServerOptions(host: "0.0.0.0", port: 8443)
```

The closure's parameter is named `self`, so inside the block `host` and `port` mean the fields of `options`, as they
would inside a method. The block looks like a configuration file and is plain code: it is type checked, and a typo in
a field name is an error.

## Next

- [Modules and packages](modules-and-packages.md) - a program in more than one file.
- [Loops](../language/execution/loops.md) - the exact rules of `for`, `while` and `loop`.
- [Builders and DSLs](../language/configuration/builders.md) - configuration blocks in depth.
