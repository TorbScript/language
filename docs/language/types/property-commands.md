---
title: Property commands
summary: The command form of a call on a field - no parentheses - writes the field instead of calling it, which is what lets a configuration block read like plain data without a single hand-written setter; parentheses always call.
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
field instead. Only the command form does that - `name value`, or `name { ... }` with the closure as the one argument:
parentheses always call, and a field that holds no function has nothing to call.

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
value.field { ... }               // Writes a closure into the field, or configures the value it holds in place
value.field()                     // Calls the function the field holds
value.field                       // Reads the field
```

## Rules

1. **A command on a field writes it.** `server.port 9090` is exactly `server.port = 9090`; the command form and `=`
   reach the same field the same way.

2. **A command whose argument is a closure assigns that closure to the field.** `server.onStart { print "started" }`
   replaces whatever `onStart` held, the same as `server.onStart = { print "started" }` would.

3. **Parentheses always call.** `server.onStart()` runs the closure that is stored in the field; without the
   parentheses, `server.onStart` only reads it. A field that holds no function has nothing to call, so
   `server.port(9090)` is an error and not a second way to write the field - and neither is
   `server.onStart({ ... })`, which hands the closure to a function that takes none.

   ```trb error
   type Server {
     var port: Int = 8080
   }

   var server = Server()
   server.port(9090)
   print server.port
   // error: `port` is a field, and parentheses call a function
   ```

   ```trb error
   type Button {
     var onClick: () => Void = {}
   }

   var button = Button()
   button.onClick({ print "clicked" })
   // error: `onClick` holds a function: `onClick { ... }` assigns it, `onClick()` calls it
   ```

4. **A value the command form cannot take is written with `=`.** The first argument of a command may not start with
   `(`, `[`, `-`, `!` or `.`, and one with an operator at its top level puts the call in parentheses - which calls. So
   `options.tls = port == 8443` and `logging.level = .Debug` are the property writes of those values.

   ```trb check
   type Options {
     var port: Int = 80
     var tls: Bool = false
   }

   var options = Options()
   options.port 8443
   options.tls = options.port == 8443
   print options.tls
   ```

5. **A property command still needs a `var` path to the field**, exactly like an assignment does. A command on a
   `const` field is rejected the same way `field = value` would be.

   ```trb error
   type Server {
     port: Int = 8080
   }

   var server = Server()
   server.port 9090
   print server
   // error: `port` never changes after construction
   ```

6. **There is no line that could mean two things.** A command on a method calls it; a command on a field writes it,
   assigning a closure when the field holds one and configuring the value in place otherwise; parentheses call. Which
   one happens is decided by what the name refers to and by whether the call has parentheses, never by anything else.

## What this is not

**A command on a field is not calling it with an implicit `()`.** `tls true` writes the field and calls nothing, and
`tls(true)` is not the same line with parentheses: it calls, and a `Bool` field has nothing to call.

```trb
type Options {
  var tls: Bool = false
}

var options = Options()
options.tls true
print options.tls
```

A command carries no permission of its own either: a field another file cannot name is not written by one, and one it
may only read is not written by one. `private` reaches as far as the file that declares it
([Fields](fields.md), rule 5).

```trb error
use Workspace from "std/project"

var workspace = Workspace()
workspace.memberPatterns = ["packages/*"]
print workspace.memberPatterns
// error: `memberPatterns` can only be written by `Workspace`
```

## Related

- [Fields](fields.md) - `var`, `private` and `private(var)`, which decide whether a command may reach the field.
- [Methods and `static fn`s](methods.md) - what the same command syntax does when the name is a method instead.
- [Declaring a type](declaring-a-type.md) - the one namespace a property command and a method call share.
