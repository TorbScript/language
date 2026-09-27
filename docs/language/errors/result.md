---
title: Result
summary: A function that can fail answers Result<Value, Failure>, whose cases are Ok and Fail. The postfix question mark unwraps an Ok or returns the Fail from the surrounding function.
kind: reference
status: stable
order: 10
keywords:
  - Result
  - Ok
  - Fail
  - question mark
  - error handling
source:
  - std/core/src/result.trb
  - CONCEPT.md#error-handling
  - examples/tour/src/06-errors.trb
---

There are no exceptions. A function that can fail says so in its result type, and the caller cannot ignore it. `Result`
is an ordinary type with two cases, and the postfix `?` operator is the only thing about it that is syntax.

## Example

```trb
use File from "std/fs"
use IoError from "std/fs"

fn firstLine(path: String): Result<String, IoError> {
  const text = File.readText(path)?
  const lines = text.lines()
  match lines.first() {
    Some(line) => line
    None => ""
  }
}

print firstLine("project.trb")
```

## Syntax

```text
Result<Value, Failure>          the type
value                           the success, where a Result is expected: it becomes Ok(value)
Ok(value)      Ok value         the success case, written out
Fail(error)    Fail problem     the failure case
<expression>?                   unwrap an Ok, or return the Fail from the surrounding function
<expression> ?? <fallback>      the value, or the fallback (the fallback is lazy)
```

`Ok` and `Fail` are written bare because the prelude imports them from `Result`.

## Rules

1. **`Result<Value, Failure>` has exactly two cases**: `case Ok(value: Value)` and `case Fail(error: Failure)`. It is
   declared in `std/core` and is not built into the language.

2. **`?` unwraps an `Ok` and returns the `Fail` from the surrounding function.** It is only legal in a function whose
   result is a `Result` or an `Option`, and at the top level of an entry file or a script.

3. **`?` converts the error type through `From`.** If the surrounding function answers `Result<Value, AppError>` and the
   expression answers `Result<Value, IoError>`, the conversion `AppError.from(ioError)` is applied. A case that wraps one
   value of a unique type gets that `From` generated, so nobody writes it:

   ```trb
   use IoError from "std/fs"

   type AppError {
     case Io(cause: IoError)
     case Startup(message: String)
   }

   fn start(text: String): Result<Int, AppError> {
     if text.isEmpty() {
       return Fail AppError.Startup("nothing to do")
     }
     text.byteLength()
   }

   print start("hello")
   ```

4. **The success is the value itself.** A value of exactly `Value` where a `Result<Value, Failure>` is expected - the
   end of a body, a `return`, an argument, a binding with an annotation - becomes `Ok(value)` on its own, the fifth
   coercion of [Conversions](../types/conversions.md). `Ok` is written only where that does not happen: where the
   `Value` is not decided yet, and in the payload of a nested `Ok`. A value that is an `Option` or a `Result` itself
   wraps into the `Ok` only where it is exactly the `Value` (`Result<Int?, Failure>` from an `Int?`). The failure is
   always written: `return Fail problem`.

   ```trb
   fn percent(value: Int): Result<Int, String> {
     if value < 0 || value > 100 {
       return Fail "{value} is not a percentage"
     }
     value
   }

   print percent(42)
   print percent(142)
   ```

   A call in command position writes its argument without parentheses, which is why `return Fail problem` is the
   canonical form, and `Ok Some(x)` has parentheses on the inner call only.

5. **`??` is `OrElse.orElse`, and `Option` and `Result` both come with that trait.** The right side is `lazy`, so it is
   evaluated only when it is needed, and it is checked against the `Value` rather than against the whole `Result`.

   ```trb
   fn parseOr(text: String, fallback: Int): Int {
     Int.tryFrom(text) ?? fallback
   }

   print parseOr("12", 0)
   print parseOr("nonsense", 0)
   ```

6. **`?.` is not defined on `Result`.** It is `Option.map`, and `Option.flatMap` when the member answers an `Option`, so
   it never produces a nested `Option`. To go from a `Result` to an `Option`, call `ok()`.

7. **`Result` shares the vocabulary of `Option`, `Task` and `Iterate`**: `map`, `flatMap`, `forEach`, `orElse`,
   `toList()`. They mean the same thing everywhere, by convention rather than through higher-kinded types. `map` and
   `flatMap` work on the `Ok` side; `mapError` works on the `Fail` side.

8. **`isOk()`, `isError()`, `ok()` and `expect(message)` are the rest of the surface.** `expect` panics with the message
   and the error, and is for cases where a failure is a bug.

9. **Precise error types are the norm for a library.** A caller can only `match` on what a signature names.
   `Result<Value, Error>` - where `Error` is a trait from the prelude - is for the layers above, where the only thing left
   to do with a failure is to report it. Every error type that carries `with Error` fits into it through the ordinary
   coercion of a value to a trait-typed value.

10. **A collection of results collects into a result of a collection.** `to<Result<List<Int>, ParseError>>()` stops at the
    first `Fail`. That is what `traverse` or `sequence` is in a language with higher-kinded types, and it is an ordinary
    `From<Iterate<...>>` implementation rather than a language feature.

11. **A top-level `?` is not a panic.** In an entry file or a script it prints `error: <the error through Show>` and
    exits with **1** (the `  caused by:` lines of the chain are specified and not built yet, see
    [Errors at the top level](top-level-errors.md)). A `panic` prints `panic: <message>` and
    the site, exits with **101**, and runs nothing on the way out - no `Close`, no `using` cleanup.

## What this is not

**`Fail` is not `Err`.** The case is called `Fail` because `Error` is the name of the trait every error type implements,
and because `return Fail problem` reads as what it does. The *field* keeps the name `error`, and so do `isError` and
`mapError`, which are about the error the case carries.

```trb
fn checked(value: Int): Result<Int, String> {
  if value < 0 {
    return Fail "negative"
  }
  value
}

print checked(1)
```

```trb error
fn checked(value: Int): Result<Int, String> {
  if value < 0 {
    return Err("negative")
  }
  value
}
// error: Cannot find `Err` here
```

**`?` is not `try` and it is not a throw.** It is an early return. Nothing is unwound, nothing is caught, and there is no
handler anywhere. A function that does not want to return early matches on the `Result` instead.

**A panic is not an exception.** It cannot be caught, it aborts the process, and nothing runs while the program falls
over, because a panic means the program has a bug and running more code in a broken program is how bugs get worse. The one
exception is a sandboxed script, which the host can stop and report.

**`Result` is not for absence.** A value that may be missing is an `Option<Value>`, written `Value?`. A `Result` carries a
reason.

## Related

- [Cases and match](../pattern-matching/cases-and-match.md) - how `Ok` and `Fail` are taken apart.
- [std/core](../../standard-library/core.md) - where `Result`, `Option` and `Error` are declared.
- [Read a file](../../how-to/read-a-file.md) - the shortest complete use of `?`.
- [Define an error type](../../how-to/read-a-file.md#related) - one type with cases for a whole application.
- [Coming from Rust](../../explanation/coming-from-rust.md) - `Err` against `Fail`, and what `?` converts.
