---
title: Why there are no exceptions
summary: A function that can fail says so in its result type, Result<Value, Failure>, and the caller handles it with match, ??, or the question mark operator, while panic stays reserved for bugs that cannot be recovered from.
kind: explanation
status: stable
order: 40
keywords:
  - exceptions
  - throw
  - try
  - catch
  - Result
source:
  - CONCEPT.md#decision-log
  - CONCEPT.md#error-handling
  - std/core/src/error.trb
---

An exception is a second, invisible return path: nothing in a function's signature says it can throw, or what it can
throw, or that a caller ten frames up is the one who has to notice. This page argues for making that path visible
instead of hiding it in the control flow.

## The decision

**A function that can fail says so in its result type.** `Result<Value, Failure>` is a type with two cases, `Ok(value)`
and `Fail(error)`, and there is no `throw`, `try` or `catch`.

- A caller handles a `Fail` with `match`, with `??` for a fallback, or with the postfix `?`, which returns the `Fail`
  from the surrounding function early and converts the error type through `From` where the two differ.
- `Error` is a trait, not a base type: `Result<Value, Error>` is how a layer that only reports a failure accepts any
  of them, while a library keeps answering its own precise error type.
- `panic` exists next to `Result`, for bugs rather than expected failures, and it is not catchable.

```trb check
fn parsePort(text: String): Result<Int, String> {
  const value = Int.tryFrom(text).mapError { _ => "not a number" }?
  if value < 1 || value > 65535 {
    return Fail "out of range"
  }
  Ok value
}

fn describe(text: String): String {
  match parsePort(text) {
    Ok(port) => "port {port}"
    Fail(reason) => "invalid: {reason}"
  }
}

print describe("8080")
print describe("nope")
```

## Why

**Because an exception is a control-flow edge that does not appear in the signature.** A function typed
`fn loadConfig(path: String): Config` in a language with exceptions might throw `IOError`, might throw nothing, and a
caller finds out by reading the implementation or by being surprised at runtime. `fn loadConfig(path: String): Result<Config, IoError>`
says the same thing in the one place a caller actually reads: the signature.

**Because a caught exception loses the type the moment it is caught.** `catch (Exception e)` sees every failure of
every callee alike, and telling them apart again means an `instanceof` chain or a second hierarchy. `match` on a
`Result<Value, Failure>` only ever sees the `Failure` the signature names, so the compiler tells a caller when a case
was forgotten (see [Why every match is exhaustive](why-exhaustive-matches.md)) instead of a runtime `catch` finding
out.

**Because the two return paths of exceptions - "here is the value" and "something happened, unwind" - are one value
with two cases here, and one value composes the way every other value does.** A `Result` can be stored in a field,
passed to a function, held in a `List<Result<Value, Failure>>` and folded into a single result with a
[collection target](../language/generics/no-higher-kinded-types.md#related). An exception can only be thrown and
caught, which is why languages with exceptions need a second, separate vocabulary (`try`/`catch`/`finally`) instead of
reusing the one they already have for values.

**Because `panic` keeps "this cannot happen" separate from "this can happen and here is what went wrong."** An
out-of-bounds index or `Result.expect` on a `None` is a bug: nothing meaningful can happen next, so the program stops
with a message and exit code 101 rather than unwinding through handlers that were written for expected failures. See
[panic](../language/errors/panic.md).

### What was rejected

- **Exceptions with a type hierarchy** (Java checked exceptions, C#, Python). Rejected because the type of what a
  function can throw is either absent from the signature or, when checked, propagates through every intermediate
  frame by hand - exactly what `?` and `From` do for `Result` without a second syntax.
- **A single concrete error type for everything** (`anyhow::Error`-style). Rejected for the same reason a base
  exception class is: it is precise for nobody. `Error` is a trait instead, so precision stays the default and only
  the "hand it up and report it" layers ask for the trait.

## Consequences

**Every fallible call is visible at the call site.** There is no line that can fail silently underneath a name that
looks like an ordinary function call; a `Result` is a value the caller holds, and holding it without looking inside is
a discarded value.

```trb check
fn readCount(text: String): Result<Int, String> {
  Int.tryFrom(text).mapError { _ => "not a number" }
}

fn total(values: List<String>): Result<Int, String> {
  var sum = 0
  for value in values {
    sum = sum + readCount(value)?
  }
  Ok sum
}

print total(["1", "2", "3"])
```

**A library keeps its precise error type, and the trait is for the top.** `fn start(): Result<Void, AppError>` lets a
caller `match` on what went wrong; `fn main(): Result<Void, Error>` is where the only thing left to do with a failure
is print it, and `cause()` walks the chain one link at a time. See [The Error trait](../language/errors/the-error-trait.md).

**A top-level `?` is not a panic.** It prints `error: <message>` and exits with 1, which is different from a `panic`
crashing with 101 - a `Result` reaching the top of the program is an expected outcome, not a bug. See
[Errors at the top level](../language/errors/top-level-errors.md).

## Related

- [Result](../language/errors/result.md) - `Ok`, `Fail`, and the vocabulary on them.
- [The question mark operator](../language/errors/question-mark.md) - early return and the `From` conversion.
- [The Error trait](../language/errors/the-error-trait.md) - `Result<Value, Error>` and `cause()`.
- [panic](../language/errors/panic.md) - what a panic prints, and what does not run afterwards.
- [Why there is no null](why-no-null.md) - the same argument applied to absence instead of failure.

