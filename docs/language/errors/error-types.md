---
title: Declaring an error type
summary: An error type is a type with cases like any other; a case that wraps one value of a type no other case wraps gets From generated, which is what makes ? convert on its own.
kind: reference
status: stable
order: 50
keywords:
  - error type
  - generated From
  - wrapper case
source:
  - CONCEPT.md#algebraic-data-types-and-pattern-matching
  - CONCEPT.md#error-handling
---

There is no `error` keyword and no special declaration for a failure. An error type is an ordinary `type` with cases,
and the one thing worth designing on purpose is which case wraps what - because that decides which conversions `?`
gets for free.

## Example

```trb check
type ConfigError {
  case Missing(key: String)
  case Invalid(key: String, reason: String)
}

type AppError {
  case Config(cause: ConfigError)
  case Startup(message: String)
}

fn readPort(): Result<Int, ConfigError> {
  Fail ConfigError.Missing("port")
}

fn start(): Result<Void, AppError> {
  const port = readPort()?
  print "Listening on {port}"
  Ok(void)
}

print start()
```

## Syntax

```text
type <Name> {
  case <Variant>(<field>: <Type>, ...)
  ...
}

type <Name> with Show, Error {           // to carry the trait every layer can hand a failure up through
  ...
  fn show(self): String { ... }
}
```

## Rules

1. **An error type is a `type` with cases, nothing more.** Every rule of [Cases and match](../pattern-matching/cases-and-match.md) applies:
   the cases are matched by position, and the type is exhaustive at every `match` over it.

2. **A case that wraps exactly one field, of a type no other case of the same error type wraps, gets `From` generated
   for it.** `AppError.from(configError)` becomes `AppError.Config(configError)` without a line of code, which is what
   lets `?` convert between the two automatically, as [The question mark operator](question-mark.md) describes.

3. **Two cases wrapping the same type stop that case from getting `From` generated, because the conversion would be
   ambiguous.** A wrapper still has to be built explicitly, by naming the case.

   ```trb error
   type ConfigError {
     case Missing(key: String)
   }

   type AppError {
     case Config(cause: ConfigError)
     case Retry(cause: ConfigError)
   }

   fn readPort(): Result<Int, ConfigError> {
     Fail ConfigError.Missing("port")
   }

   fn start(): Result<Void, AppError> {
     const port = readPort()?
     print "Listening on {port}"
     Ok(void)
   }
   print start()
   // error: `ConfigError` does not convert into `AppError`
   ```

4. **A message-only case takes a `String` field rather than wrapping anything.** `case Startup(message: String)` has
   no `From` to generate - a `String` is not a distinct enough type for the checker to know which case it belongs to -
   so a `Startup` failure is always built by name: `Fail AppError.Startup("nothing to do")`.

5. **Adding `with Show, Error` makes the type usable wherever the failure only needs to be reported, not matched on.**
   `Show` is what a reader of the type has to write; `Error`'s own `cause()` has a default, so nothing else is
   required.

   ```trb check
   type ConfigError with Show, Error {
     case Missing(key: String)
     case Invalid(key: String, reason: String)

     fn show(self): String {
       match self {
         .Missing(key) => "{key} is missing"
         .Invalid(key, reason) => "{key} is invalid: {reason}"
       }
     }
   }

   print ConfigError.Missing("port")
   ```

## What this is not

**An error type is not required to have `Error` on it.** A `Result<Value, ConfigError>` works without it; `Error` is
only needed the moment a function wants to hand a `ConfigError` up as one of several possible failures, through
`Result<Value, Error>`, as [The Error trait](the-error-trait.md) describes. Most of the functions in this
documentation's examples so far never wrote `with Error` at all.

**Nesting every failure into one big application-wide error type is not required, and it is not free.** Every case
that wraps a distinct type gets `?` conversion for nothing; a case that duplicates a type another case already wraps
does not, so a design with two causes of the same shape costs an explicit conversion at every site that raises it. A
type with one case per distinct failure source, not per call site, is what keeps that free.

```trb error
type ConfigError {
  case Missing(key: String)
}

type IoError {
  case NotFound(path: String)
}

type AppError {
  case Config(cause: ConfigError)
  case Startup(cause: ConfigError)
}

fn readPort(): Result<Int, ConfigError> {
  Fail ConfigError.Missing("port")
}

fn start(): Result<Void, AppError> {
  const port = readPort()?
  Ok(void)
}
print start()
// error: `ConfigError` does not convert into `AppError`
```

Two cases both wrapping `ConfigError` mean neither gets `From`, exactly as rule 3 shows - the fix is one case per
distinct type, here `AppError.Config(ConfigError)` alone.

## Related

- [The question mark operator](question-mark.md) - what the generated `From` buys at every `?`.
- [Cases and match](../pattern-matching/cases-and-match.md) - the case syntax an error type uses like any other type.
- [The Error trait](the-error-trait.md) - when to add `with Error` on top.
- [Result](result.md) - `Ok` and `Fail`, which every function returning an error type answers with.
