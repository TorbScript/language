---
title: Property commands
summary: A field is written only with `=`, everywhere; the one command call left is a trailing block on a field whose value is a record, which configures that value in place rather than replacing it - no hand-written setters.
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

A [command call](../../glossary.md#command-call) on a method calls it. **A field is written only with `=`**
(2026-09-25), everywhere, including inside a receiver closure. The one command call left on a field is a trailing
block on a value that is a *record*, never a function: it configures that value in place instead of replacing it.

## Example

```trb
type Server {
  var port: Int = 8080
  var onStart: () => Void = {}
}

var server = Server()
server.port = 9090
server.onStart = { print "started" }
print "Listening on {server.port}"
server.onStart()
```

## Syntax

```text
value.field = <expression>        Writes the field
value.field { ... }               Configures the value the field holds in place - only when it is a record type
value.field()                     Calls the function the field holds
value.field                       Reads the field
```

## Rules

1. **A field is written with `=`, never with a command.** `server.port 9090` is an error: "`port` is a field: write
   `port = 9090`", the fix reconstructed from what was written. `server.onStart { print "started" }` is an error the
   same way: "`onStart` is a field: write `onStart = { print \"started\" }`".

   ```trb error
   type Server {
     var port: Int = 8080
   }

   var server = Server()
   server.port 9090
   print server.port
   // error: `port` is a field: write `port = 9090`
   ```

   ```trb error
   type Server {
     var onStart: () => Void = {}
   }

   var server = Server()
   server.onStart { print "started" }
   // error: `onStart` is a field: write `onStart = { print "started" }`
   ```

2. **A trailing block on a field whose value is a record configures it in place, and is not an assignment.**
   `server.database { url = "..." }` reads the field and runs the block as a receiver closure over the value it
   already holds - the value is changed where it lies, never replaced. Inside the block a field is still written with
   `=`: `url = "..."` is itself an ordinary field write, resolved against the block's own receiver.

   ```trb
   type DatabaseConfig {
     var url: String = ""
   }

   type Server {
     var database: DatabaseConfig = DatabaseConfig()
   }

   var server = Server()
   server.database {
     url = "postgres://..."
   }
   print server.database.url
   ```

3. **Parentheses always call.** `server.onStart()` runs the closure that is stored in the field; without the
   parentheses, `server.onStart` only reads it. A field that holds no function has nothing to call, so
   `server.port(9090)` is an error - parentheses call, and a field is written with `=` - and neither is
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
   // error: `onClick` holds a function: `onClick = { ... }` assigns it, `onClick()` calls it
   ```

4. **Both forms need a `var` path to the field**, exactly like an assignment does. A block on a `const` record field
   is rejected the same way `field = value` would be, with a note that names the syntax.

   ```trb error
   type Database {
     var url: String = ""
   }

   type Server {
     database: Database = Database()
   }

   var server = Server()
   server.database {
     url = "postgres://..."
   }
   print server
   // error: `database` never changes after construction
   ```

5. **There is no line that could mean two things.** A command on a method calls it; the one command left on a field -
   a trailing block on a record value - configures it in place; parentheses call. Which one happens is decided by what
   the name refers to and by whether the call has parentheses, never by anything else.

## What this is not

**A trailing block on a field is not calling it with an implicit `()`.** `server.database { url = "..." }` configures
the field and calls nothing; `server.database(...)` is not the same line with parentheses, it calls, and a record
field has nothing to call.

A command carries no permission of its own either: a field another file cannot name is not configured by one, and one
it may only read is not configured by one. `private` reaches as far as the file that declares it
([Fields](fields.md), rule 5).

```trb error
use Workspace from "std/project"

var workspace = Workspace()
workspace.memberPatterns = ["packages/*"]
print workspace.memberPatterns
// error: `memberPatterns` is write-protected: only the file that declares `Workspace` writes it
```

## Related

- [Fields](fields.md) - `var`, `private` and `protected var`, which decide whether a write may reach the field.
- [Methods and `static fn`s](methods.md) - what the same command syntax does when the name is a method instead.
- [Declaring a type](declaring-a-type.md) - the one namespace a property command and a method call share.

