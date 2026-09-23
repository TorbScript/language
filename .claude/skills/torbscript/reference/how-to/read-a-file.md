---
title: Read a file
summary: Read a whole file or its lines, hand the failure to the caller with the question mark operator, and turn an IoError into your own error type.
kind: how-to
status: stable
order: 10
keywords:
  - File.readText
  - IoError
  - question mark
  - std/fs
source:
  - std/fs/src/lib.trb
  - CONCEPT.md#error-handling
---

Reading a file needs `std/fs`, and the import is the point: a file that touches files says so on its first line. Every
function of `File` that can fail answers a `Result`, so the failure travels up with `?` and cannot be forgotten.

## Steps

1. **Import what you use.** `std/fs` is deliberately not in the prelude, so the import is a statement about what the file
   can reach.

   ```trb fragment
   use File, IoError from "std/fs"
   ```

2. **Read the whole file with `File.readText`.** It answers `Result<String, IoError>`. A `String` is always valid UTF-8, so
   bytes that are not are an `IoError` rather than a replacement character.

   ```trb fragment
   const text = File.readText(path)?
   ```

3. **Declare the failure in your signature.** A function that uses `?` has to answer a `Result` (or an `Option`). The
   shortest form hands the `IoError` straight on:

   ```trb fragment
   fn readConfig(path: String): Result<String, IoError>
   ```

4. **Convert to your own error type where you have one.** `?` applies `From` on the way out, and a case that wraps exactly
   one value of a unique type gets that `From` generated. So nothing has to be written for the conversion.

   ```trb fragment
   type AppError {
     case Io(cause: IoError)
     case Empty(path: String)
   }
   ```

5. **Read line by line for a file that does not fit in memory.** `File.lines` answers `Result<Iterate<String>, IoError>`
   and the file is never in memory as a whole. The pipeline is lazy, so a `take` stops reading.

   ```trb fragment
   for line in File.lines(path)? {
     print line
   }
   ```

6. **Use `File.open` and `using` only when you need the handle.** An open file has the identity of an operating-system
   handle, so it is a `shared type` with `Close`. `using` binds it to a name that cannot leave the block, and the file
   is released - and closed - where the block ends. Nothing calls `close()` by hand. Most code does not need a handle.

   ```trb fragment
   fn firstText(path: String): Result<String, IoError> {
     using file = File.open(path)?
     file.readAll()
   }
   ```

## Pitfalls

- **`File.exists` is not a precondition.** It answers a `Bool` and cannot fail, but the file can disappear between the two
  calls. Read and handle the failure instead of asking first.
- **`?` needs a `Result` around it.** Inside a function that answers a plain value, `?` is a compile error. Either change
  the signature or match on the `Result`.
- **`text.lines()` and `File.lines(path)` are different.** The first splits a `String` you already have; the second
  streams a file.
- **A `String` has no `length()`.** Use `text.byteLength()` for bytes, which is O(1), or `text.chars().count()` for
  characters.
- **Do not reach for a `try` block.** There is none. The whole error path is the `?` and the result type.

## Full example

```trb check
use File, IoError from "std/fs"

/** What can go wrong while the configuration is read. `From<IoError>` is generated for the `Io` case. */
type ConfigError with Show, Error {
  case Io(cause: IoError)
  case Empty(path: String)

  fn show(): String {
    match self {
      .Io(cause) => "{cause}"
      .Empty(path) => "{path} is empty"
    }
  }
}

/** The first line of the file, or a `ConfigError` that says why there is none. */
fn firstLine(path: String): Result<String, ConfigError> {
  const text = File.readText(path)?
  const lines = text.lines()
  match lines.first() {
    Some(line) => Ok line
    None => Fail ConfigError.Empty(path)
  }
}

fn describe(path: String): String {
  match firstLine(path) {
    Ok(line) => "first line: {line}"
    Fail(problem) => "no first line: {problem}"
  }
}

print describe("project.trb")
```

## Related

- [Result](../language/errors/result.md) - the rules of `Ok`, `Fail` and `?`.
- [std/core](../standard-library/core.md) - where `Result` and `Error` are declared.
- [Cases and match](../language/pattern-matching/cases-and-match.md) - the generated `From` of a single-value case.
- [Write a configuration file](write-a-configuration-file.md) - the other half of reading configuration.

