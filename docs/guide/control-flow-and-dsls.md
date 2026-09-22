---
title: Control flow and your own constructs
summary: if, for, while and loop as you would expect, and why unless is an ordinary function you could have written yourself.
kind: guide
status: stable
order: 90
prerequisites:
  - collections-and-pipelines.md
keywords:
  - if
  - for
  - while
  - loop
  - receiver closure
  - property command
source:
  - CONCEPT.md#blocks-and-control-flow
  - CONCEPT.md#configuration-dsl
  - examples/tour/src/08-control-flow.trb
  - examples/tour/src/09-dsl.trb
---

`if`, `for`, `while` and `loop` look the way they look everywhere. What is different here is what happens once you run
out of built-in ones: a new control structure is a function, not a new piece of syntax.

## Goal

At the end of this page you can read the built-in control flow, write your own control structure as a function, and
read a configuration block for what it is: ordinary code.

## An if is an expression

```trb
const temperature = 23

const feeling = if temperature < 10 {
  "cold"
} else if temperature < 25 {
  "pleasant"
} else {
  "hot"
}

print feeling
```

Every branch of an `if` used this way has to produce a value of the same type, exactly as every arm of a `match` does.

## Loops: for, while and loop

```trb run
for i in 0..3 {
  print i
}

var attempts = 0
while attempts < 10 {
  attempts = attempts + 1
  if attempts > 3 {
    break
  }
  print "attempt {attempts}"
}
// prints 0
// prints 1
// prints 2
// prints attempt 1
// prints attempt 2
// prints attempt 3
```

`continue` and `break` work as expected inside both. `0..3` is a [range](../language/values-and-types/ranges.md); the
end is excluded, so this prints `0`, `1` and `2`.

The third one is `loop`, for a loop that does not end by itself - a server, a read loop, a state machine:

```trb
var line = "first"
loop {
  print line
  line = ""
  if line.isEmpty() {
    break
  }
}
```

`while true` is a compile error that says `A loop that never ends is written `loop``, so there is exactly one spelling
for it. Without a `break` a `loop` has the type `Never`, which is what lets a function whose whole body is one get away
without a result; with a `break` it is `Void` like any other loop. There is no `break value`. See
[Loops](../language/execution/loops.md).

## A control structure is a function

`unless` is not a keyword. It is a function whose second parameter is a closure, called with a trailing closure so
that it reads like one of the built-in ones:

```trb
fn unless(condition: Bool, body: () => Void) {
  if !condition {
    body()
  }
}

const items: List<Int> = []
unless items.isEmpty() { print "not empty" }
```

Nothing about `unless` is special to the compiler - `do`, `retry` and `using` from the standard library are written
the same way, as ordinary functions with a closure or a
[`lazy`](../language/functions/parameter-modes.md) parameter. See
[Control structures are functions](../language/extensibility/control-structures.md) for `retry` and `using`, and how a
function like this names its closure's implicit parameter.

## A configuration block is a receiver closure

A [receiver closure](../language/configuration/receiver-closures.md) is a closure whose first parameter is called
`self`, so names inside it resolve against that receiver - exactly like inside a method.

```trb
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
  host "0.0.0.0"
  port 8443
}

print options
```

`host "0.0.0.0"` is not a call on a method named `host` - `host` is a field, so this is a
[property command](../language/types/property-commands.md): it writes the field. Together, command calls, trailing
closures and property commands are what makes `serve { ... }` above read like a piece of built-in syntax while being
nothing but a function call. See [Builders and DSLs](../language/configuration/builders.md).

## Next

- [Modules and packages](modules-and-packages.md) - splitting a program into files and a project.
- [Control structures are functions](../language/extensibility/control-structures.md) - `do`, `retry`, `using`, and how
  to add your own.
- [Receiver closures](../language/configuration/receiver-closures.md) - the exact rule for how a name resolves inside
  one.
