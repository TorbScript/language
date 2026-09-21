---
title: Property commands
summary: A command call on a field writes it instead of calling it, which is what lets a configuration block read like plain data without a single hand-written setter.
kind: reference
status: stable
order: 120
keywords:
  - property command
  - command call
  - configuration
source:
  - CONCEPT.md#members-a-method-is-a-constant-that-holds-a-closure
  - examples/tour/src/03-types.trb
---

A [command call](../../glossary.md#command-call) on a method calls it. The same command written on a field writes the
field instead - there is no third meaning, because a field is never callable on its own.

## Example

```trb
type Server {
  var port: Int = 8080
  var onStart: () => Void = {}
}

var server = Server()
server.port 9090
server.onStart { print "started" }
print "Listening on {server.port}"
server.onStart()
```

## Syntax

```text
value.field <expression>          // Writes the field: value.field = <expression>
value.field { ... }               // Writes a closure into the field
value.field()                     // Calls the function the field holds
value.field                       // Reads the field
```

## Rules

1. **A command on a field writes it.** `server.port 9090` is exactly `server.port = 9090`; the command form and `=`
   reach the same field the same way.

2. **A command whose argument is a closure assigns that closure to the field.** `server.onStart { print "started" }`
   replaces whatever `onStart` held, the same as `server.onStart = { print "started" }` would.

3. **Calling a function a field holds always takes parentheses.** `server.onStart()` runs the closure that is stored
   in the field; without the parentheses, `server.onStart` only reads it.

4. **A property command still needs a `var` path to the field**, exactly like an assignment does. A command on a
   `const` field is rejected the same way `field = value` would be.

   ```trb error
   type Server {
     port: Int = 8080
   }

   var server = Server()
   server.port 9090
   // error: `port` never changes after construction
   ```

5. **There is no line that could mean two things.** A command on a method calls it; a command on a field writes it,
   assigning a closure when the field holds one and configuring the value in place otherwise. Which one happens is
   decided by what the name refers to, not by how the call is written.

## What this is not

**A command on a field is not calling it with an implicit `()`.** `tls true` and `tls(true)` write the same field, and
neither one calls anything - a field is never callable, whichever way the command is written.

```trb
type Options {
  var tls: Bool = false
}

var options = Options()
options.tls true
print options.tls
```

```trb error
type Options {
  private tls: Bool = false
}

var options = Options()
options.tls true
// error: `tls` is private to `Options`
```

## Related

- [Fields](fields.md) - `var`, `private` and `private(var)`, which decide whether a command may reach the field.
- [Methods and `static fn`s](methods.md) - what the same command syntax does when the name is a method instead.
- [Declaring a type](declaring-a-type.md) - the one namespace a property command and a method call share.
