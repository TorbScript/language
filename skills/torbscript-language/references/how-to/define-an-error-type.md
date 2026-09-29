---
title: Define an error type
summary: Declare a type with one case per distinct failure, add Show and Error where a layer above needs to hand it further up, and let the generated From do the conversion at every ?.
kind: how-to
status: stable
order: 30
keywords:
  - error type
  - Error trait
  - generated From
  - cause
source:
  - CONCEPT.md#error-handling
  - CONCEPT.md#algebraic-data-types-and-pattern-matching
---

An error type is an ordinary `type` with cases, and the only design decision is which case wraps what: a case that
wraps one field of a type no sibling case wraps gets a `From` generated for it, which is what lets `?` convert between
error types without a line of code.

## Steps

1. **Declare a case per distinct source of failure**, not per call site. A case can wrap a nested error, or carry a
   `String` for a failure that has no type of its own to wrap.

   ```trb fragment
   type ConfigError {
     case Missing(key: String)
     case Invalid(key: String, reason: String)
   }
   ```

2. **Wrap a lower-level error in its own case so `?` converts it for free.** A case that wraps exactly one field, of a
   type no other case of the same error type wraps, gets `From` generated. `readPort()?` inside a function that
   answers `AppError` needs nothing else once `AppError` has a case that wraps `ConfigError` and no other case does.

   ```trb fragment
   type AppError {
     case Config(cause: ConfigError)
     case Startup(message: String)
   }
   ```

3. **Add `with Show, Error` on the type that a layer above hands further up as "some error".** `Show` is the one
   member `Error` requires and the one you write; `Error`'s own `cause()` already has a default.

   ```trb fragment
   type ConfigError with Show, Error {
     case Missing(key: String)
     case Invalid(key: String, reason: String)

     fn show(): String {
       match self {
         .Missing(key) => "{key} is missing"
         .Invalid(key, reason) => "{key} is invalid: {reason}"
       }
     }
   }
   ```

4. **Override `cause()` only on a case that wraps another error.** The default answers `None`, which is correct for
   every case that carries nothing but its own data.

   ```trb fragment
   fn cause(): Error? {
     match self {
       .Config(cause) => Some cause
       .Startup(_) => None
     }
   }
   ```

5. **Answer the precise type from a function that has one caller who needs to tell failures apart**, and reserve
   `Result<Value, Error>` for `fn main` and the layers that only report. A caller can only `match` on what the
   signature names, so widening too early takes that ability away.

## Pitfalls

- **Two cases that wrap the same type stop `From` from being generated for either one.** The conversion would be
  ambiguous, so the checker rejects it and a wrapper has to be built by naming the case explicitly
  (`Fail AppError.Config(cause)`).
- **A `String` field is not a distinct type.** `case Startup(message: String)` never gets a generated `From`, because
  the checker cannot tell which `String`-carrying case a plain `String` belongs to. Build it by name:
  `Fail AppError.Startup("nothing to do")`.
- **`with Error` alone does not compile.** `Error` requires `Show`, so both have to be written (or `Show` left to the
  generated one, where the type has no cases and every field supports it).
- **Nesting every failure into one application-wide type is not free.** A case per distinct failure source keeps every
  `?` conversion generated; a case that duplicates a type another case already wraps costs an explicit conversion at
  every site that raises it.

## Full example

```trb check
type ConfigError with Show, Error {
  case Missing(key: String)
  case Invalid(key: String, reason: String)

  fn show(): String {
    match self {
      .Missing(key) => "{key} is missing"
      .Invalid(key, reason) => "{key} is invalid: {reason}"
    }
  }
}

type AppError with Show, Error {
  case Config(cause: ConfigError)
  case Startup(message: String)

  fn show(): String {
    match self {
      .Config(cause) => "configuration problem: {cause}"
      .Startup(message) => "could not start: {message}"
    }
  }

  fn cause(): Error? {
    match self {
      .Config(cause) => cause
      .Startup(_) => None
    }
  }
}

fn readPort(): Result<Int, ConfigError> {
  Fail ConfigError.Missing("port")
}

fn start(): Result<Void, AppError> {
  const port = readPort()?               // ConfigError -> AppError through the generated From
  print "Listening on {port}"
  Ok(void)
}

fn report(error: AppError) {
  print "error: {error}"
  if const Some(cause) = error.cause() {
    print "  caused by: {cause}"
  }
}

match start() {
  Ok(_) => {}
  Fail(problem) => report problem
}
```

## Related

- [Declaring an error type](../language/errors/error-types.md) - the generated `From` and the case rules in full.
- [The Error trait](../language/errors/the-error-trait.md) - `cause()`, and when `Result<Value, Error>` is the right signature.
- [The question mark operator](../language/errors/question-mark.md) - what the generated `From` buys at every `?`.
- Read a file (skill `torbscript-standard-library`: `references/how-to/read-a-file.md`) - the same pattern applied to `IoError`.

