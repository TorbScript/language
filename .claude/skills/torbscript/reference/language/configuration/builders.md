---
title: Builders and DSLs
summary: A builder is a function that creates a value, hands it to a receiver closure to configure, and returns it, which is what makes a configuration block a statically typed value instead of a string to parse.
kind: reference
status: stable
order: 20
keywords:
  - builder
  - DSL
  - configuration
  - property command
source:
  - CONCEPT.md#configuration-dsl
  - examples/tour/src/09-dsl.trb
---

A [receiver closure](receiver-closures.md) needs something on the other end of it: a function that creates the
value, runs the closure against it, and returns what came out. That function is the builder, and it is the whole
mechanism behind every configuration block in the language.

## Example

```trb check
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

## Syntax

```text
fn <builder>(configure: (var self: <Receiver>) => Void): <Receiver> {
  var value = <Receiver>()
  configure value
  value
}
```

## Rules

1. **A builder is three lines: create, configure, return.** Everything a configuration block can do - property
   commands, nested builders, method calls, ordinary control flow - comes from what `<Receiver>` declares, not from
   the builder function itself.

2. **A property command inside the block writes a field of the receiver; a call without a receiving field calls a
   method.** `host "0.0.0.0"` is `self.host = "0.0.0.0"`; a section such as `database { ... }` both reads the field
   and configures the value it already holds, in place - see [Property commands](../types/property-commands.md).

3. **A field that is itself built by a nested block needs no field of function type.** `var database: DatabaseConfig
   = DatabaseConfig()` is enough; the nested `database { ... }` block is exactly this page's mechanism applied one
   level down, driven by the field's own type rather than by a second builder function.

4. **Only what is more than "set a field" needs a method.** A field covers a setting; a method exists for what a
   plain assignment cannot do, such as validating an argument or appending to a private collection.

5. **A configuration block is ordinary code, not a restricted grammar.** A loop, a condition or a local binding
   inside `{ ... }` runs exactly as it would inside a method body, because a receiver closure is a closure first and
   a configuration syntax second.

   ```trb check
   type Menu {
     private var items: List<String> = []

     var fn item(name: String) {
       items.append name
     }
   }

   fn menu(build: (var self: Menu) => Void): Menu {
     var created = Menu()
     build created
     created
   }

   const names = ["soup", "salad", "bread"]
   const dinner = menu {
     for name in names {
       item name
     }
   }

   print dinner
   ```

6. **The block is checked against `<Receiver>` before the program runs.** A name the receiver does not have is a
   compile error at the line it is written on, with the receiver's own members offered - a configuration block is a
   value the checker already verified, not a string a builder parses at runtime.

## What this is not

**A builder is not a template engine that reads a string.** Every name inside `{ ... }` is resolved and type checked
like any other code; there is no runtime lookup of `"host"` as text, so a name that does not exist is a compile
error rather than an option silently ignored.

```trb check
type Options {
  var retries: Int = 3
}

fn configured(configure: (var self: Options) => Void): Options {
  var options = Options()
  configure options
  options
}

const options = configured { retries 5 }
print options
```

```trb error
type Options {
  var retries: Int = 3
}

fn configured(configure: (var self: Options) => Void): Options {
  var options = Options()
  configure options
  options
}

const options = configured { retryCount 5 }
// error: Cannot find `retryCount` here
```

## Related

- [Receiver closures](receiver-closures.md) - the closure form a builder's parameter takes.
- [Property commands](../types/property-commands.md) - what a command call does to a field inside the block.
- [Receiver scripts](receiver-scripts.md) - the same mechanism with a whole file as the closure body.

