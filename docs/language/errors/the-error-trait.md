---
title: The Error trait
summary: Error is a trait, not a base type; a failure that implements it fits into Result<Value, Error> for the layers that only need to report it, and cause() gives the chain.
kind: reference
status: stable
order: 40
keywords:
  - Error trait
  - cause
  - trait-typed value
source:
  - std/core/src/error.trb
  - CONCEPT.md#error-handling
---

Every error type in this documentation so far has been precise: `ConfigError`, `IoError`, a type named for the one
thing that can go wrong. `Error` exists for the layers above those, where the only thing left to do with a failure is
hand it further up and eventually report it.

## Example

```trb check
type ConfigError with Show, Error {
  case Missing(key: String)

  fn show(): String {
    match self {
      .Missing(key) => "missing: {key}"
    }
  }
}

fn readPort(): Result<Int, ConfigError> {
  Fail ConfigError.Missing("port")
}

fn start(): Result<Void, Error> {
  const port = readPort()?
  print "Listening on {port}"
  Ok(void)
}

print start()
```

## Syntax

```text
public trait Error with Show {
  fn cause(): Error? { None }
}

type <Name> with Show, Error {
  ...
}
```

## Rules

1. **`Error` is a trait, not a base type.** There is no class every error extends; a type becomes one by writing
   `with Error` and implementing `Show`, the one trait `Error` requires.

2. **A concrete failure becomes an `Error` value through the ordinary coercion of a value to a trait it implements.**
   `?` needs no special rule for `Result<Value, Error>`: a `ConfigError` that carries `Error` converts to it the same
   way a `Circle` converts to `Shape` wherever `Shape` is expected.

3. **`cause()` is the chain.** An error that wraps another one returns it from `cause()`, so a report can walk the
   chain link by link. The end of the chain, and any error that wraps nothing, answers `None`.

4. **The default `cause()` answers `None`, so a type with nothing to add writes `with Error` and nothing else.** Only
   a type that wraps another error overrides it.

   ```trb check
   type ConfigError with Show, Error {
     case Missing(key: String)

     fn show(): String {
       match self {
         .Missing(key) => "missing: {key}"
       }
     }
   }

   print ConfigError.Missing("port").cause()
   ```

5. **Precise error types stay the norm for a library.** A caller can only `match` on what a signature names, so a
   function that answers `Result<Config, Error>` instead of `Result<Config, ConfigError>` takes away its caller's
   ability to handle one failure differently from another. `Error` is for `fn main(): Result<Void, Error>` and the
   layers in between, not for a library's own public functions.

## What this is not

**`Error` is not `Exception`, and a value that carries it is not thrown.** There is no `throw`, no `catch`, and no
stack unwinding anywhere in the language; a value with `Error` travels exactly the way any other value does, inside a
`Result`, returned and matched on like anything else.

```trb check
type ConfigError with Show, Error {
  case Missing(key: String)

  fn show(): String {
    match self {
      .Missing(key) => "missing: {key}"
    }
  }
}

fn report(error: Error) {
  print "error: {error}"
  if const Some(cause) = error.cause() {
    print "  caused by: {cause}"
  }
}

report ConfigError.Missing("port")
```

```trb error
type ConfigError with Show, Error {
  case Missing(key: String)

  fn show(): String {
    match self {
      .Missing(key) => "missing: {key}"
    }
  }
}

fn start(): Result<Void, ConfigError> {
  try {
    Fail ConfigError.Missing("port")
  } catch error {
    print error
  }
}
print start()
// error: Cannot find `try` here
```

**`cause()` is not a stack trace.** It is one link the error itself chose to keep, not a record of where the failure
happened. In the debug profile, `?` records where a failure was handed on separately from the error value; see
[Errors at the top level](top-level-errors.md).

**Implementing `Error` is not implementing `Show` for free, or the reverse.** `Error` requires `Show`, so a type needs
both written down (or `Show` generated, where the type qualifies); `with Error` alone, without `Show`, does not
compile.

## Related

- [Result](result.md) - where a failure lives before it becomes an `Error` value.
- [Declaring an error type](error-types.md) - building the precise error types `Error` sits above.
- [std/core](../../standard-library/core.md) - the trait's declaration in full.
