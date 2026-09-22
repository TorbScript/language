---
title: Arguments and labels
summary: An argument is passed positionally or by label, positional arguments always come first, and a label matches a parameter by name rather than by position.
kind: reference
status: stable
order: 20
keywords:
  - label
  - positional argument
  - arity
  - argument order
source:
  - CONCEPT.md#arguments
  - examples/tour/src/02-functions.trb
---

A call passes each parameter of a function either by position or by label. A label is the parameter's name followed
by `:`, and it lets a call read like a sentence instead of a list of values.

## Example

```trb check
fn connect(host: String, port: Int, timeout: Int): String {
  "{host}:{port} (timeout {timeout}s)"
}

print connect("localhost", 5432, 30)
print connect("localhost", port: 5432, timeout: 30)
```

## Syntax

```text
<callee>(<positional>, ..., <label>: <value>, ...)
<callee> <positional>, ..., <label>: <value>, ...
```

## Rules

1. **A positional argument fills the next parameter that has not been filled yet, left to right.** `connect("localhost",
   5432, 30)` fills `host`, `port` and `timeout` in that order.

2. **A labelled argument matches the parameter of that name, wherever it stands in the parameter list.** The two calls
   of the example above pass the same three values.

3. **Every positional argument of a call comes before every labelled one.** A labelled argument that is followed by a
   positional one is a compile error at the positional argument, because reading left to right would otherwise no
   longer tell a parameter's position from its name.

   ```trb error
   fn connect(host: String, port: Int, timeout: Int): String {
     "{host}:{port} (timeout {timeout}s)"
   }

   print connect(host: "localhost", 5432, 30)
   // error: Positional arguments come first, labeled arguments follow them: `connect(host, timeout: 10)`
   ```

4. **A label that names no parameter is a compile error**, and so is a label that repeats a parameter a positional
   argument already filled.

   ```trb error
   fn connect(host: String, port: Int): String {
     "{host}:{port}"
   }

   print connect("localhost", ports: 5432)
   // error: `connect` has no parameter `ports`
   ```

   ```trb error
   fn connect(host: String, port: Int): String {
     "{host}:{port}"
   }

   print connect("localhost", 5432, port: 80)
   // error: `port` already has an argument
   ```

5. **A parameter with no default and no argument is a compile error, named with how many arguments were expected and
   how many were given.** A parameter with a default may be left out (see [Default values](default-values.md)), and a
   variadic parameter that receives nothing becomes an empty list (see
   [Variadic parameters](variadics.md)).

   ```trb error
   fn connect(host: String, port: Int, timeout: Int): String {
     "{host}:{port} (timeout {timeout}s)"
   }

   print connect("localhost")
   // error: `connect` takes 3 arguments, 1 was given
   ```

6. **The name a call uses is the parameter's name in the declaration, not the type.** Renaming a parameter changes
   every call site that labels it; there is no separate external name as in Swift.

7. **A collection passed where a single value is expected is an ordinary type mismatch, not an arity error.** Only a
   [variadic parameter](variadics.md) collects several arguments into one; an ordinary parameter's type is checked
   exactly as any other argument's is.

   ```trb error
   fn takesInt(value: Int): Int {
     value
   }

   const numbers = [1, 2, 3]
   print takesInt(numbers)
   // error: Expected `Int64`, found `List<Int64>`
   ```

## What this is not

**A label is not a keyword argument dictionary.** There is no way to bundle several labelled arguments into one `Map`
and pass it in their place; a call always names one argument per label, so the compiler can check every one of them.

```trb
fn connect(host: String, port: Int, timeout: Int): String {
  "{host}:{port} ({timeout}s)"
}

print connect("localhost", port: 5432, timeout: 30)
```

```trb error
fn connect(host: String, port: Int, timeout: Int): String {
  "{host}:{port} ({timeout}s)"
}

const options = ["port": 5432, "timeout": 30]
print connect("localhost", options)
// error: `connect` takes 3 arguments, 2 were given
// error: Expected `Int64`, found `Map<String, Int64>`
```

**Argument order is not evaluation order.** Every argument is evaluated once, in the order it is written in the
call - labelled arguments included - and only afterwards moved into the parameter it belongs to. A labelled argument
that reads a `var` a positional argument also touches still runs in the order both were written, not in declaration
order.

## Related

- [Declaring a function](declaring-a-function.md) - the form a parameter list takes.
- [Default values](default-values.md) - what happens to a parameter no argument fills.
- [Variadic parameters](variadics.md) - the one parameter that can take any number of positional arguments.
- [Coming from Rust](../../explanation/coming-from-rust.md) - Rust has no labelled arguments at all.
