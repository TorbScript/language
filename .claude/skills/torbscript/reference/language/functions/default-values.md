---
title: Default values
summary: A parameter default is an expression that runs at every call which omits the argument, in the scope of the declaration, without self and without the other parameters.
kind: reference
status: stable
order: 30
keywords:
  - default parameter
  - optional argument
source:
  - CONCEPT.md#arguments
  - examples/tour/src/02-functions.trb
---

A parameter can carry a default, written after its type. A call that does not supply that argument gets the default
instead, computed fresh for that call.

## Example

```trb check
fn connect(host: String, port: Int = 5432, timeout: Int = 30): String {
  "{host}:{port} (timeout {timeout}s)"
}

print connect("localhost")
print connect("localhost", timeout: 10)
```

## Syntax

```text
fn <name>(..., <parameter>: <Type> = <default expression>, ...): <ReturnType> { ... }
```

## Rules

1. **A parameter with a default may be left out of a call.** Leaving it out and passing it explicitly are the only two
   ways to fill it; there is no third value that means "use the default".

2. **The default expression is evaluated at the call site, at every call that omits the argument.** It is not computed
   once when the function is declared, and a default with a side effect runs that side effect on every such call.

   ```trb check
   fn defaultTimeout(): Int {
     print "computing the default"
     30
   }

   fn connect(host: String, timeout: Int = defaultTimeout()): String {
     "{host} timeout={timeout}"
   }

   print connect("localhost")
   print connect("localhost")
   print connect("localhost", timeout: 5)
   ```

   Running this prints `computing the default` twice, once for each call that leaves `timeout` out, and not for the
   third call, which supplies it.

3. **A default runs in the scope of the declaration, without `self` and without the other parameters.** A default
   cannot read another parameter of the same call, so `fn limits(low: Int, high: Int = low)` does not compile; a
   value that depends on another argument is computed in the body instead.

   ```trb error
   fn limits(low: Int, high: Int = low): Int {
     high
   }
   // error: Cannot find `low` here
   ```

4. **A default that is passed explicitly is not evaluated.** Only the arguments that are actually missing get their
   default computed; an argument the caller wrote, even if it repeats the default's value, replaces it entirely.

5. **Evaluation order is the arguments in written order, then the missing defaults in declaration order.** A default
   of an earlier parameter runs before a default of a later one, both after every written argument of the call.

## What this is not

**There is no positional hole that skips a defaulted parameter.** Passing `port` its default while still positioning
`timeout` needs the label, not an empty slot the way a comma-separated hole works in JavaScript.

```trb check
fn connect(host: String, port: Int = 5432, timeout: Int = 30): String {
  "{host}:{port} (timeout {timeout}s)"
}

print connect("localhost", timeout: 10)
```

```trb error
fn connect(host: String, port: Int = 5432, timeout: Int = 30): String {
  "{host}:{port} (timeout {timeout}s)"
}

print connect("localhost", , 10)
// error: Expected an expression, found `,`
```

**A default is not evaluated when the function is declared.** `fn withLimit(limit: Int = expensiveDefault())` calls
`expensiveDefault()` nowhere until a call needs it, not once at load time as a static initializer would.

## Related

- [Declaring a function](declaring-a-function.md) - the rest of the parameter list.
- [Arguments and labels](arguments.md) - how a call fills a parameter that has no default.
- [Variadic parameters](variadics.md) - the other parameter that may be missing from a call.
