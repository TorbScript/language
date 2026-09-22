---
title: Receiver closures
summary: A receiver closure is a closure whose first parameter is called self, so names inside it resolve against that receiver first, exactly as inside a method.
kind: reference
status: stable
order: 10
keywords:
  - receiver closure
  - self
  - name resolution
  - builder
source:
  - CONCEPT.md#configuration-dsl
---

> **Not built natively yet.** A closure that captures a `var` parameter or `var self` is not built by the native back
> end yet, so `torb run` refuses the examples here that use it. `torb check` accepts them, and the rules are the
> language's.

Inside a method, a bare name can mean a field or another method of `self` without writing `self.` in front of it. A
[receiver closure](../../glossary.md#receiver-closure) gets the same treatment: a closure whose first parameter is
named `self` reads and writes that receiver the same way a method body does.

## Example

```trb check
type Counter {
  var value: Int = 0

  var fn add(amount: Int) {
    value = value + amount
  }
}

fn build(configure: (var self: Counter) => Void): Counter {
  var counter = Counter()
  configure counter
  counter
}

const counter = build {
  add 3
  add 4
}

print counter.value
```

## Syntax

```text
(self: <Receiver>) => <Type>          a receiver closure, read-only
(var self: <Receiver>) => <Type>      a receiver closure that may change the receiver
```

## Rules

1. **A closure whose function type names its first parameter `self` is a receiver closure.** `add 3` inside the
   trailing closure above is `counter.add(3)`, reached the same way it would be inside a method of `Counter`.

2. **Name resolution inside a closure or a method tries the local scope first, then the innermost receiver, then the
   module.** A local binding always wins over a field of the same name, and a field of the receiver always wins over
   a top-level name of the module.

   A **position is not a name**, so a receiver never makes one implicit: `.0` and `.1` always stand behind a dot
   (`entry.0`, `_.0`, `self.0`), and a bare `0` is always the number. What a receiver makes implicit are the *members*
   of a type, and a tuple position is not one.

3. **Exactly one receiver is implicit at a time.** A method has exactly one `self`, and a receiver closure has
   exactly one receiver in scope this way - nesting one receiver closure inside another does not add the outer one
   to what a bare name can mean inside the inner one.

4. **Naming a receiver closure's parameter is what reaches it from inside a nested receiver closure**, the way
   `server { s => s.database { url "{s.host}/db" } }` reads `s.host` from inside the nested `database` block. Naming
   the parameter makes it an ordinary `var` parameter, and a nested closure that only reads it and runs immediately -
   never escaping the call it was passed to - does not capture it past that call.

   ```trb check
   type DatabaseConfig {
     var url: String = ""
   }

   type ServerConfig {
     var host: String = "localhost"
     var database: DatabaseConfig = DatabaseConfig()
   }

   fn server(configure: (var self: ServerConfig) => Void): ServerConfig {
     var config = ServerConfig()
     configure config
     config
   }

   const config = server { s =>
     s.database { url "{s.host}/db" }
   }

   print config.database.url
   ```

5. **A closure that stores the named parameter instead of reading it immediately is still rejected**, because storing
   it - assigning it into a field, the way `onStart` below keeps a closure for later - is exactly the kind of outliving
   the call that rule 4's nested closure avoids by running at once.

   ```trb error
   type ServerConfig {
     var host: String = "localhost"
     var onStart: () => Void = {}
   }

   fn server(configure: (var self: ServerConfig) => Void): ServerConfig {
     var config = ServerConfig()
     configure config
     config
   }

   const config = server { s =>
     s.onStart { print s.host }
   }
   // error: This closure captures the `var` parameter `s` and may outlive the call
   ```

6. **A receiver closure is an ordinary closure value, passed as an argument.** The receiver type never declares it,
   so two functions that both take a `(var self: Counter) => Void` can build completely different things out of the
   same receiver - what a receiver closure does is decided by the function it is passed to, not by the receiver type.

## What this is not

**A receiver closure is not a method `Receiver` declares.** `Row` above has no member that looks like this closure;
the closure exists only as a value at the call site of `grid`, and `Row` itself never changes because of it.

```trb check
type Row {
  var cells: List<Int> = []

  var fn cell(value: Int) {
    cells.add value
  }
}

fn describe(build: (var self: Row) => Void): Row {
  var row = Row()
  build row
  row
}

const row = describe { cell 1 }
print row
```

```trb error
type Row {
  var cells: List<Int> = []
}

var row = Row()
row.build { cell 1 }
// error: `Row` has no member `build`
```

## Related

- [Parameter modes](../functions/parameter-modes.md) - a receiver closure as one of the shapes a parameter can take.
- [Builders and DSLs](builders.md) - the function around a receiver closure that makes a configuration block work.
- [Property commands](../types/property-commands.md) - what a command call does when the name is a field, not a method.
