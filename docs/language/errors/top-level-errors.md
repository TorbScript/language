---
title: Errors at the top level
summary: A ? at the top level of an entry file or a script is not a panic; it is specified to print the error and exit with 1, walking cause() one line per link.
kind: reference
status: stable
order: 70
keywords:
  - top level
  - entry file
  - script
  - exit code
source:
  - CONCEPT.md#error-handling
  - CONCEPT.md#modules-and-packages
---

An entry file's top-level code and a script have no caller to hand a failure to. `?` is still legal there - it is what
turns "the program failed" into an exit code and a message on standard error, instead of a value nobody would read.

## Example

```trb check
use File, IoError from "std/fs"

fn loadConfig(path: String): Result<String, IoError> {
  File.readText path
}

const config = loadConfig("app.trb")?
print config
```

## Syntax

```text
<result-expression>?      at the top level of src/main.trb, a script, or a receiver script
```

## Rules

1. **Top-level code, and therefore a top-level `?`, is only legal in an entry file (`src/main.trb`), a script, a
   receiver script, or a `tests/*.test.trb` file.** A `src/lib.trb` consists of declarations only, because it is what
   other packages import, and importing it must not run anything.

2. **`?` at the top level behaves exactly as `?` inside a function that returns `Result<Void, Error>` does**, with one
   difference: there is no `return` to make, because there is nothing above the top level to return into. The program
   ends instead.

3. **A top-level `?` that hands back a `Fail` prints `error: <the error through Show>` to standard error and exits
   with 1.** This is a different format and a different exit code from [`panic`](panic.md), because a `Fail` reaching
   the top is an expected kind of ending, not a bug.

4. **The chain is walked through `cause()`, one `  caused by: <...>` line per link.** A failure that wraps another one
   through `Error.cause()` is specified to print the whole chain, in order, the way
   [The Error trait](the-error-trait.md) builds it - a program built today prints the first line only (see the end of
   this page):

   ```text
   error: the server did not start
     caused by: app.trb: no such file
   ```

5. **In the debug profile, every `?` that hands an error on also records where it did, and those locations print under
   the chain**, one `  at src/config.trb:12:31` line per hop. The release profile emits nothing for this, so it costs
   no error type anything there.

## What this is not

**A top-level `?` is not `panic`, and the two must not be confused when reading a stack of output.** `panic` prints
`panic: <message>` and exits with 101, from a bug the program cannot recover from; `?` prints `error: <...>` and exits
with 1, from a failure the program already turned into a value on its way up.

```trb check
fn describe(): Result<Void, String> {
  Fail "the server did not start"
}

const _ = describe()?
```

```trb check
fn describe() {
  panic "the server did not start"
}

describe()
```

Both end the program; only the message, the exit code, and what a supervising process should conclude from it differ.

**The native back end builds the first line of the report and not the chain yet.** `error: <the error>` and exit code 1
are what a compiled program prints; the `caused by:` lines and the locations of the debug profile are the specified
format that is still being built. `torb check` accepts every example on this page, which is what they are verified
against.

## Related

- [The question mark operator](question-mark.md) - `?` itself, inside a function and at the top level alike.
- [panic](panic.md) - the other way a program ends, and why the two outputs must not be mistaken for each other.
- [The Error trait](the-error-trait.md) - `cause()`, which the chain in rule 4 walks.
