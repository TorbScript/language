---
title: Errors
summary: How a function says it can fail with Result, and how a caller handles that with match or the question mark operator.
kind: guide
status: stable
order: 70
prerequisites:
  - traits.md
keywords:
  - Result
  - Option
  - question mark operator
  - panic
source:
  - CONCEPT.md#error-handling
  - examples/tour/src/06-errors.trb
---

There is no `null` and there are no exceptions. Absence is a value, `Option<Value>`, and failure is a value too,
`Result<Value, Failure>`. This page writes a function that can fail and two ways to handle what comes back.

## Goal

At the end of this page you can write a function that returns `Result`, handle it with `match`, and shorten that with
the question mark operator.

## A function that can fail

```trb
type ConfigError {
  case Missing(key: String)
  case Invalid(key: String, reason: String)
}

fn readPort(settings: Map<String, String>): Result<Int, ConfigError> {
  const raw = settings.get("port").okOr(ConfigError.Missing("port"))?
  const port = Int.tryFrom(raw).mapError { ConfigError.Invalid "port", "not a number" }?
  if port < 1 || port > 65535 {
    return Fail ConfigError.Invalid("port", "out of range")
  }
  port
}

match readPort(["port": "80a"]) {
  Ok(port) => print "Port {port}"
  Fail(.Missing(key)) => print "{key} is missing"
  Fail(.Invalid(key, reason)) => print "{key} is invalid: {reason}"
}
```

[`Result`](../language/errors/result.md)'s two cases are `Ok` and `Fail`, and the prelude imports them bare, so a
pattern writes `Ok(port)` rather than `Result.Ok(port)`. `match` has to cover both, exactly as it has to cover every
case of any other type with cases.

## The question mark operator

`?` on a line by itself unwraps an `Ok` and returns the `Fail` from the surrounding function immediately - that is
what `readPort` above already uses twice, once on an `Option` turned into a `Result` with `okOr`, once on the
`Result` that `Int.tryFrom` answers.

```trb
type AppError {
  case Config(cause: ConfigError)
  case Startup(message: String)
}

fn start(settings: Map<String, String>): Result<Void, AppError> {
  const port = readPort(settings)?
  print "Listening on {port}"
  Ok void
}

match start(["port": "8080"]) {
  Ok(_) => print "started"
  Fail(error) => print "failed to start: {error}"
}
```

`readPort` answers a `ConfigError`, but `start` answers an `AppError` - `?` converts one into the other through a
generated `From`, because `AppError.Config` is the one case that wraps a `ConfigError` and no other case does. See
[The question mark operator](../language/errors/question-mark.md) for the exact rule.

## Absence: Option

Absence is modelled the same way, with `Option<Value>` written `Value?`:

```trb
type User {
  id: Int
  name: String
}

const users = [User(1, "Ada"), User(2, "Grace")]

fn findUser(id: Int): User? {
  users.find { _.id == id }
}

const name = findUser(2)?.name ?? "nobody"
print name
```

`?.` maps over the `Option` instead of unwrapping it, and `??` gives the fallback when it is `None`. See
[Optional chaining](../language/errors/option-chaining.md).

## Panic is for bugs, not for expected failures

```trb
fn percentageOf(part: Int, total: Int): Int {
  if total == 0 {
    panic "total must not be zero"
  }
  part * 100 / total
}

print percentageOf(1, 4)
```

A `panic` prints `panic: <message>` and the call site to standard error, exits with code 101, and nothing else runs on
the way out. It is not catchable, because it says the program reached a state its author considered impossible - an
expected failure is a `Fail`, not a `panic`. See [panic](../language/errors/panic.md).

## Next

- [Collections and pipelines](collections-and-pipelines.md) - building, reading and transforming a collection.
- [Result](../language/errors/result.md) - the exact vocabulary on `Ok` and `Fail`.
- [Declaring an error type](../language/errors/error-types.md) - a type with cases, and when `From` is generated for
  it.

