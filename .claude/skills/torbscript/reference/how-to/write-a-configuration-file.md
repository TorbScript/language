---
title: Write a configuration file
summary: Declare a type for the configuration, write the file as TorbScript against it, and load it through the sandbox with the capabilities you grant.
kind: how-to
status: stable
order: 20
keywords:
  - configuration
  - receiver closure
  - project.trb
  - Sandbox
  - property command
source:
  - CONCEPT.md#configuration-dsl
  - CONCEPT.md#receiver-scripts-and-the-sandbox
  - examples/config-dsl
---

TorbScript is its own configuration format. A configuration file is an ordinary `.trb` file that runs as the body of a
receiver closure against a type you declared, so it is statically type checked, autocompleted in an editor, and sandboxed
when it runs.

## Steps

1. **Declare a type for the configuration.** Every field that the file may set is a `var` field with a default. A field
   without a default is a value the file has to provide.

   ```trb fragment
   type ServerConfig {
     var host: String = "localhost"
     var port: Int = 8080
     var database: DatabaseConfig = DatabaseConfig()
   }
   ```

2. **Add a method for anything that is more than "set a field".** A field is set by a command on its name, and a nested
   field is configured in place by a command with a block. Only a real operation needs a method.

   ```trb fragment
   var fn route(path: String, to: String) {
     routes.add Route(path, to)
   }
   ```

3. **Write the configuration file against that type.** Names inside it resolve against the receiver, so no prefix is
   needed. A command on a field writes the field; a command with a block configures the field's value in place.

   ```trb skip this file is the body of a receiver closure, so its names come from the receiver and not from the file
   host "0.0.0.0"
   port 8080
   database {
     url "postgres://localhost:5432/app"
   }
   for name in ["users", "orders"] {
     route "/api/{name}", to: name
   }
   ```

   It is a program, so a loop, a `const` and an `if` all work. There is no template language.

4. **Load it and apply it.** Loading and running are two steps with different failures: `Sandbox.load` reports what is
   wrong with the file - a syntax error, a type error against the receiver, a module it may not import - and
   `Script.apply` reports what went wrong while it ran, including a step, memory or time limit and a panic inside the
   script.

   ```trb fragment
   const script = Sandbox.load<ServerConfig>("./config.trb")?
   var config = ServerConfig()
   script.apply(config)?
   ```

5. **Grant capabilities at the call site, never in a project file.** Without a block the script has no file system, no
   network, no clock, no environment and no foreign functions. Whoever loads it decides what it may reach.

   ```trb fragment
   const script = Sandbox.load<ServerConfig>("./config.trb") {
     modules "std/text", "std/time"
     files readOnly: "./config"
     limits steps: 1_000_000
   }?
   ```

6. **Use the same mechanism for a project manifest.** `project.trb` is exactly this with the receiver `Project`, which is
   why `name "acme/shop"` works and why reading the metadata of a package is safe.

## Pitfalls

- **Only the innermost receiver is implicit.** Inside `database { ... }` the names resolve against `DatabaseConfig` and not
  against `ServerConfig`. To reach the outer one, name the parameter: `server { s => s.database { url "{s.host}/db" } }`.
- **A command on a field writes it and never calls it.** `onStart { ... }` assigns the closure to the field; `onStart()`
  calls what is in it. That is the whole rule, and it has no exception.
- **A field the file sets has to be `var`.** A `const` field is fixed after construction, so a configuration file cannot
  write it.
- **`Sandbox.load` resolves its path against the project directory**, not against the file that calls it. An import
  (`use Name from "./x"`) is the other way round, because that is a question about the source tree.
- **A field with an operator in its value needs parentheses.** `tls(port == 8443)` is the only way to write that, because
  an operator at the top level of an argument is one of the places the canon requires them.

## Full example

The receiver type, and a program that configures a value of it in place through a receiver closure. This is the same
mechanism a file uses, without the sandbox.

```trb check
type DatabaseConfig {
  var url: String = ""
  var poolSize: Int = 10
}

type Route {
  path: String
  handler: String
}

type ServerConfig {
  var host: String = "localhost"
  var port: Int = 8080
  var database: DatabaseConfig = DatabaseConfig()
  private(var) routes: List<Route> = []

  /** More than setting a field, so it is a method. */
  var fn route(path: String, to: String) {
    routes.add Route(path, to)
  }
}

/** Takes a receiver closure over a fresh configuration and answers the configured value. */
fn server(configure: (var self: ServerConfig) => Void): ServerConfig {
  var config = ServerConfig()
  configure config
  config
}

const config = server {
  host "0.0.0.0"
  port 9000
  database {
    url "postgres://localhost:5432/app"
    poolSize 20
  }
  route "/health", to: "health"
}

print "{config.host}:{config.port} {config.database.url} {config.routes.length()}"
```

## Related

- [Command calls](../language/syntax/command-calls.md) - why `port 9000` is written without parentheses.
- [Declaring a type](../language/types/declaring-a-type.md) - `var` fields, `private(var)` and methods.
- [Read a file](read-a-file.md) - the plain way to read text when no receiver is involved.
- [Run your first program](../guide/installing-and-running.md) - the `project.trb` of a new project.

