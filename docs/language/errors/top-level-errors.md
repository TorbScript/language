---
title: Errors at the top level
summary: A ? at the top level of an entry file or a script is not a panic; it prints the error and exits with 1, walking cause() one line per link, and in the dev profile names every ? the failure went through.
kind: reference
status: stable
order: 70
keywords:
  - top level
  - entry file
  - script
  - exit code
  - return trace
source:
  - CONCEPT.md#error-handling
  - CONCEPT.md#modules-and-packages
  - compiler/src/ir/lower/match.trb
  - runtime/panic.c
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
<result-expression>?      at the top level of an entry file, a script, or a receiver script
```

## Rules

1. **Top-level code, and therefore a top-level `?`, is only legal in an entry file (`src/main.trb`, or the `entry`
   of a `program` line), a script, a receiver script, or a `*.test.trb` file.** A `src/lib.trb` consists of
   declarations only, because it is what other packages import, and importing it must not run anything.

2. **`?` at the top level behaves exactly as `?` inside a function that returns `Result<Void, Error>` does**, with one
   difference: there is no `return` to make, because there is nothing above the top level to return into. The program
   ends instead.

3. **A top-level `?` that hands back a `Fail` prints `error: <the error through Show>` to standard error and exits
   with 1.** This is a different format and a different exit code from [`panic`](panic.md), because a `Fail` reaching
   the top is an expected kind of ending, not a bug.

4. **The chain is walked through `cause()`, one `  caused by: <...>` line per link**, where the error arrives as the
   trait-typed `Error`. A failure that wraps another one through `Error.cause()` prints the whole chain, in order, the way
   [The Error trait](the-error-trait.md) builds it; a concrete error type is one line:

   ```text
   error: the server did not start
     caused by: app.trb: no such file
   ```

5. **In the debug profile, every `?` that hands an error on also records where it did, and those locations print under
   the chain**, one `  at <file>:<line>:<column>` line per `?`, the innermost first and the top-level `?` last - the
   way Zig's error return traces work:

   ```text
   error: there is no value for `host`
     at acme/app/src/config.trb:27:29
     at acme/app/src/config.trb:46:31
     at acme/app/src/main.trb:4:24
   ```

   The debug profile is `dev`: `torb run` and `torb test` in the VM, `torb run --native` and `torb test --native`, and
   `torb build --profile dev`. A `release` build records nothing and prints nothing - its machine code is the same as
   without the trace - and no error type carries anything for it, because the trace is kept beside the values.

6. **Only the `?`s of one chain are printed.** A failure that a `match` or a fallback handled on the way is not part of
   the next one's trace: a `?` whose operand succeeds drops what its operand recorded, and an entry belongs to a chain
   only when it was recorded after the `?` above it began. The trace keeps the last 64 entries of a thread, and a `?`
   whose operand waits for a task starts the chain again, because the task's failure was recorded on the thread it ran
   on.

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

**A trace is not a stack trace.** It names the `?`s the failure went through, not the calls that were active when it
was created: the function that returned the first `Fail` is the one the innermost line's `?` called, and a panic, which
is a bug and not a failure, prints the frames of the `dev` profile instead ([panic](panic.md)).

## Related

- [The question mark operator](question-mark.md) - `?` itself, inside a function and at the top level alike.
- [panic](panic.md) - the other way a program ends, and why the two outputs must not be mistaken for each other.
- [The Error trait](the-error-trait.md) - `cause()`, which the chain in rule 4 walks.
