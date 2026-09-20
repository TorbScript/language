---
title: The question mark operator
summary: A postfix ? unwraps an Ok or a Some and returns the Fail or None from the surrounding function early, converting the error type through From when they differ.
kind: reference
status: stable
order: 30
keywords:
  - early return
  - postfix operator
  - From conversion
source:
  - CONCEPT.md#error-handling
  - std/core/src/result.trb
---

`?` is the only piece of syntax `Result` and `Option` get. Everything else about handling a failure - `map`, `match`,
`??` - is an ordinary method call; `?` is the one operator, and it means exactly one thing: leave now, with the
failure, unless there was none.

## Example

```trb check
use File, IoError from "std/fs"

fn firstLine(path: String): Result<String, IoError> {
  const text = File.readText(path)?
  const lines = text.lines()
  match lines.first() {
    Some(line) => Ok line
    None => Ok ""
  }
}

print firstLine("project.trb")
```

## Syntax

```text
<result-expression>?      Ok(value) becomes value; Fail(error) returns Fail(error) from the function
<option-expression>?      Some(value) becomes value; None returns None from the function
```

## Rules

1. **`?` unwraps the success case and returns the failure case from the surrounding function.** On a `Result`,
   `Ok(value)?` is `value` and `Fail(error)?` returns `Fail(error)`. On an `Option`, `Some(value)?` is `value` and
   `None?` returns `None`.

2. **The value `?` is applied to has to be an `Option` or a `Result`.** Nothing else has a success and a failure case
   for `?` to tell apart.

   ```trb error
   fn parse(text: String): Result<Int, String> {
     const n = text.byteLength()?
     Ok n
   }
   print parse("3")
   // error: `?` needs an `Option` or a `Result`, and `Int64` is not one
   ```

3. **`?` on a `Result` converts the error type through `From` when the surrounding function's failure type differs
   from the one `?` is applied to.** A case that wraps exactly one value of a type no other case of the same type
   wraps gets `From` generated for it, which is what makes this conversion free to use:

   ```trb check
   type ConfigError {
     case Missing(key: String)
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

4. **Where no `From` connects the two failure types, the mismatch is reported at the `?`, not somewhere later.**

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

5. **`?` on an `Option` needs no conversion, because `None` carries no value to convert.** It only needs the
   surrounding function to answer an `Option` itself.

6. **`?` on an `Option` inside a function that returns a `Result` is rejected, because there is no error value to
   return.** Turn the `Option` into a `Result` first with `okOr(error)`, which is exactly what the note says.

   ```trb error
   fn findFirst(numbers: List<Int>): Result<Int, String> {
     const found = numbers.find({ _ > 0 })?
     Ok found
   }
   print findFirst([1, 2])
   // error: `?` on an `Option` in a function that returns `Result<Int64, String>`
   ```

7. **`?` is legal at the top level of an entry file or a script, in addition to inside a function.** There the
   surrounding "function" is the file itself; see [Errors at the top level](top-level-errors.md) for what happens to
   the failure there.

## What this is not

**`?` is not `try`, and there is no `catch`.** It does not unwind a call stack, and nothing runs on the way out beyond
what an early `return` always runs (nothing - there are no destructors). A function that wants to look at the failure
instead of leaving on it matches on the `Result` itself:

```trb check
fn parseAll(values: List<String>): List<Int> {
  var parsed: List<Int> = []
  for value in values {
    match Int.parse(value) {
      Ok(number) => parsed.add number
      Fail(_) => {}
    }
  }
  parsed
}

print parseAll(["1", "x", "3"])
```

**`?` is not a way to turn a `Result` into an `Option`, or the reverse, on its own.** Going from a `Result` to an
`Option` is `ok()`; going the other way needs a value for the error, which is `okOr(error)`, and rule 6 above is the
checker rejecting exactly the shortcut of skipping that step.

**`?` is illegal in a function whose result is neither an `Option` nor a `Result`.** `fn total(values: List<String>):
List<Int>` cannot receive the failure `Int.parse(value)?` would hand back, and the checker rejects it there:

```trb error
fn total(values: List<String>): List<Int> {
  var result: List<Int> = []
  for value in values {
    result.add Int.parse(value)?
  }
  result
}
// error: `?` needs a function that returns an `Option` or a `Result`, and this one returns `List<Int64>`
```

This is only checked against a written result type: a function whose result is inferred - one that is not `public`
and not a trait method, see [Declaring a function](../functions/declaring-a-function.md) - still accepts a `?` that
has nowhere to go, because the checker has not yet decided the result type at the point it would need to reject it.

## Related

- [Result](result.md) - `Ok`, `Fail`, and the rest of the vocabulary `?` sits next to.
- [Declaring an error type](error-types.md) - the generated `From` that rule 3 relies on.
- [Errors at the top level](top-level-errors.md) - what a top-level `?` prints and exits with.
- [Optional chaining](option-chaining.md) - `?.` and `??`, for reading through a failure instead of leaving on it.
