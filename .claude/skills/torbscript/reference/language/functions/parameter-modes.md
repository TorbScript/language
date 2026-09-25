---
title: Parameter modes
summary: A parameter is an ordinary value unless it says otherwise; var hands over a path to mutate, lazy defers evaluation once, a self-named closure resolves names against a receiver, and Expression also hands over the typed tree.
kind: reference
status: stable
order: 70
keywords:
  - var parameter
  - lazy parameter
  - receiver closure
  - parameter mode
source:
  - CONCEPT.md#parameter-modes
  - CONCEPT.md#quoted-expressions-expressionvalue
---

Most parameters are ordinary values, evaluated once at the call site before the function runs. A parameter can ask
for something else instead: a path to mutate, an expression evaluated only if it is needed, or a closure whose names
resolve against a receiver.

## Example

```trb check
fn reset(var counter: Int): Int {
  counter = 0
  counter
}

fn orElseComputed(value: Int?, fallback: lazy Int): Int {
  value ?? fallback
}

var n = 5
print reset(n)
print n
print orElseComputed(Some(1), 999)
```

## Syntax

```text
<name>: <Type>                        an ordinary parameter
var <name>: <Type>                    a var path
<name>: () => <Type>                  a closure
<name>: (self: <Receiver>) => <Type>  a receiver closure
<name>: lazy <Type>                   deferred, at most once
<name>: Expression<<Type>>            deferred and quoted
```

## Rules

1. **A plain parameter is a value, evaluated once at the call site, before the body runs.** This is the default and
   needs no keyword.

2. **`var name: Type` asks for a var path, and the function works on the caller's value.** The caller passes a
   binding, field or index that can be changed, not a copy; changing `name` inside the function changes what the
   caller holds (see [Bindings](../values-and-types/bindings.md)).

   ```trb check
   fn increment(var target: Int) {
     target = target + 1
   }

   var count = 0
   increment count
   increment count
   print count
   ```

3. **A parameter typed as a function is a closure**, and one written after the call as a trailing closure if it is
   the last parameter (see [Closures](closures.md) and [Trailing closures](trailing-closures.md)).

4. **A closure parameter whose function type names its first parameter `self` is a receiver closure.** Inside its
   body, names resolve against that receiver first, exactly as inside a method, which is what makes a configuration
   block or a builder read as its own small language.

   ```trb check
   type ServerConfig {
     var host: String = "localhost"
     var port: Int = 8080
   }

   fn server(configure: (var self: ServerConfig) => Void): ServerConfig {
     var config = ServerConfig()
     configure config
     config
   }

   const config = server {
     host = "0.0.0.0"
     port = 8080
   }

   print config
   ```

5. **`lazy Type` accepts any expression of that type and evaluates it at most once, the first time the parameter is
   read.** A parameter that is never read never runs its argument at all, which is what makes a fallback
   (`value ?? expensiveDefault()`) cheap on the common path.

   ```trb check
   fn expensive(): Int {
     print "computing"
     99
   }

   fn orElseComputed(value: Int?, fallback: lazy Int): Int {
     value ?? fallback
   }

   print orElseComputed(Some(1), expensive())
   print orElseComputed(None, expensive())
   ```

6. **`Expression<Type>` type checks the argument as an ordinary `Type` and additionally hands over its typed
   expression tree.** The call site looks exactly like a call to `Type` itself; see
   [Quoted expressions](quoted-expressions.md) for what the tree contains and what can stand in it.

## What this is not

**`var` on a parameter is not a reference type, and it is not `inout` you can store.** It exists only for the
duration of the call: it cannot be put in a field, returned, or captured by a closure that is stored (see
[Closures](closures.md)), which is why there are no lifetimes to annotate and nothing can dangle.

```trb check
fn increment(var target: Int) {
  target = target + 1
}
```

```trb error
fn makeIncrementer(var target: Int): () => Void {
  {
    target = target + 1
  }
}
// error: This closure captures the `var` parameter `target` and may outlive the call
```

**A receiver closure is not a method added to `Receiver`.** It is an ordinary closure value passed as an argument;
`Receiver` never declares it, and two receiver closures for the same type can disagree freely, one per builder
function.

## Related

- [Closures](closures.md) - the closure form every function-typed parameter takes.
- [Trailing closures](trailing-closures.md) - writing the last closure parameter after the call.
- [Quoted expressions](quoted-expressions.md) - what `Expression<Type>` hands the function beyond the value.
- [Declaring a function](declaring-a-function.md) - the rest of a parameter's declaration.

