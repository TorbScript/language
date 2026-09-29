---
title: Errors
summary: A function that can fail returns a Result, a value that can be missing is an Option, and the caller handles both with match, the question mark or a fallback.
kind: guide
status: stable
order: 80
prerequisites:
  - traits.md
keywords:
  - Result
  - Option
  - question mark operator
  - panic
  - exception
  - null
source:
  - CONCEPT.md#error-handling
  - examples/tour/src/06-errors.trb
---

There is no `null` and there are no exceptions. A value that can be missing and a call that can fail both say so in
their type, and the compiler makes the caller handle it.

## Goal

At the end of this page you can write a function that can fail, handle what it returns, and pass a failure on with
`?`.

## A function that can fail

```trb run
type ConfigError {
  case Missing(key: String)
  case Invalid(key: String, reason: String)
}

fn readPort(settings: Map<String, String>): Result<Int, ConfigError> {
  const raw = settings.get("port").okOr(ConfigError.Missing("port"))?
  const port = Int.tryFrom(raw).mapError({ _ => ConfigError.Invalid "port", "not a number" })?
  if port < 1 || port > 65535 {
    return Fail ConfigError.Invalid("port", "out of range")
  }
  port
}

match readPort(["port": "80a"]) {
  Ok(port) => print "port {port}"
  Fail(.Missing(key)) => print "{key} is missing"
  Fail(.Invalid(key, reason)) => print "{key} is invalid: {reason}"
}
// prints port is invalid: not a number
```

`Result<Int, ConfigError>` is either `Ok` with an `Int` or `Fail` with a `ConfigError`. A `match` handles both, as it
handles the cases of any type. `settings.get` returns a `String?`, and `okOr` turns a missing value into a `Fail`.
The body ends in `port`, not `Ok(port)`: the value is wrapped for you.

## Pass a failure on with ?

`?` after a call takes the value out of an `Ok`. On a `Fail` it returns that `Fail` from the function at once.
The first `readPort` uses it twice. When the error types differ, `?` converts the error, provided the target type says how:

```trb run
type ConfigError {
  case Missing(key: String)
}

type AppError {
  case Config(cause: ConfigError)
  case Startup(message: String)
}

fn readPort(settings: Map<String, String>): Result<Int, ConfigError> {
  if settings.isEmpty() {
    return Fail ConfigError.Missing("port")
  }
  8080
}

fn start(settings: Map<String, String>): Result<Void, AppError> {
  const port = readPort(settings)?
  print "listening on {port}"
  Ok void
}

match start([:]) {
  Ok(_) => print "started"
  Fail(error) => print "failed: {error}"
}
// prints failed: Config(cause: Missing(key: "port"))
```

`AppError.Config` is the one case that holds a `ConfigError`, so `?` wraps a `ConfigError` into it by itself.

## A value that can be missing

```trb run
const ages = ["Ada": 36]
const age = ages.get("Grace") ?? 0
print age
print ages.get("Ada")
// prints 0
// prints Some(36)
```

`Int?` is short for `Option<Int>`: either `Some` with a value or `None`. `??` gives a fallback for `None`, and `?.`
reaches into the value if there is one: `findUser(2)?.name`.

## A panic is for bugs

```trb run
fn percentageOf(part: Int, total: Int): Int {
  if total == 0 {
    panic "total must not be zero"
  }
  part * 100 / total
}

print percentageOf(1, 4)
// prints 25
```

`panic` stops the program with exit code 101, and nothing can catch it. Use it for a state that should be impossible.
Bad input is an expected failure, and an expected failure is a `Fail`.

## Next

- [Collections and pipelines](collections-and-pipelines.md) - lists, maps and sets, and working through them.
- [Result](../language/errors/result.md) - everything you can do with `Ok` and `Fail`.
- [The question mark operator](../language/errors/question-mark.md) - the exact rule, and when it converts.
