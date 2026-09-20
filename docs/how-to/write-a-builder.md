---
title: Write a builder
summary: Write a function that creates a value, hands it to a receiver closure, and returns it - three lines that make every property command, nested block and method call in the closure statically typed.
kind: how-to
status: stable
order: 130
keywords:
  - builder
  - receiver closure
  - property command
  - DSL
source:
  - CONCEPT.md#configuration-dsl
---

A builder is a function, not a special declaration: it creates a value, hands it to a closure whose first parameter is
`self`, and returns what the closure configured. Once the receiver type has fields and methods, the block that calls
the builder reads like a small language of its own - checked like ordinary code, because it is ordinary code.

## Steps

1. **Declare the type the block will configure**, with `var` on every field the block may set.

   ```trb fragment
   type ServerOptions {
     var host: String = "localhost"
     var port: Int = 8080
   }
   ```

2. **Write the builder: create, configure, return.** The parameter is a receiver closure over the type from step 1.

   ```trb fragment
   fn serve(configure: (var self: ServerOptions) => Void): ServerOptions {
     var options = ServerOptions()
     configure options
     options
   }
   ```

3. **Call it with a trailing closure.** A property command inside writes a field; nothing else has to change for
   `host "0.0.0.0"` to mean `self.host = "0.0.0.0"`.

   ```trb fragment
   const options = serve {
     host "0.0.0.0"
     port 8443
   }
   ```

4. **Give a field its own nested block instead of a second builder function**, by making the field's type support the
   same mechanism. `database { ... }` needs no field of function type - it reads the field and configures the value
   it already holds, in place.

   ```trb fragment
   type ServerOptions {
     var host: String = "localhost"
     var database: DatabaseOptions = DatabaseOptions()
   }
   ```

5. **Add a method only for what a plain field cannot do.** A method that appends to a private collection, validates
   an argument, or takes more than one value belongs on the receiver type, not in the builder function.

   ```trb fragment
   fn route(var self, path: String, to: String) {
     routes.add Route(path, to)
   }
   ```

## Pitfalls

- **Only the innermost receiver is implicit.** Inside a nested block, a bare name resolves against the nested type,
  not the outer one - reaching the outer receiver needs its parameter named, which is a design `CONCEPT.md`
  describes and today's checker does not yet accept for a closure that reads it (see
  [Receiver closures](../language/configuration/receiver-closures.md)).
- **A command on a field writes it; calling it always needs parentheses.** `onStart { ... }` assigns the closure,
  `onStart()` calls what is in it - the same line never means both.
- **A name the receiver does not have is a compile error at that line**, with the receiver's own members offered.
  There is no silent fallback the way an unknown key in a map or a string-keyed configuration format would have.
- **A field the block sets has to be `var`.** A `const` field is fixed after construction, so the builder's caller
  has no way to reach it from inside the block.

## Full example

```trb check
type DatabaseOptions {
  var url: String = ""
  var poolSize: Int = 10
}

type Route {
  path: String
  handler: String
}

type ServerOptions {
  var host: String = "localhost"
  var port: Int = 8080
  var database: DatabaseOptions = DatabaseOptions()
  private(var) routes: List<Route> = []

  fn route(var self, path: String, to: String) {
    routes.add Route(path, to)
  }
}

fn serve(configure: (var self: ServerOptions) => Void): ServerOptions {
  var options = ServerOptions()
  configure options
  options
}

const options = serve {
  host "0.0.0.0"
  port 8443
  database {
    url "postgres://localhost:5432/app"
    poolSize 20
  }
  route "/health", to: "health"
}

print "{options.host}:{options.port} {options.database.url} {options.routes.length()}"
```

## Related

- [Builders and DSLs](../language/configuration/builders.md) - the same mechanism, with every rule spelled out.
- [Receiver closures](../language/configuration/receiver-closures.md) - name resolution inside the block, in full.
- [Property commands](../language/types/property-commands.md) - what a command on a field does and does not do.
- [Write a configuration file](write-a-configuration-file.md) - the same builder loaded from a file, through the sandbox.
