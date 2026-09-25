---
title: panic
summary: panic prints panic, the message and the site to standard error, exits with 101, and runs nothing else on the way out - it is for bugs, never for an expected failure.
kind: reference
status: stable
order: 60
keywords:
  - panic
  - abort
  - exit code
  - Never
source:
  - CONCEPT.md#error-handling
  - CONCEPT.md#execution-model
  - runtime/panic.c
  - runtime/memory.c
---

Everything else on this page's neighbors is for a failure the caller can do something about. `panic` is for the other
kind: a bug, where continuing would only make the program's state worse.

## Example

```trb check
fn percentage(part: Int, total: Int): Int {
  if total == 0 {
    panic "total must not be zero"
  }
  part * 100 / total
}

print percentage(1, 4)
```

## Syntax

```text
panic <message>
```

## Rules

1. **`panic` takes a `String` message and never returns; its type is `Never`, which fits any expression, including
   one branch of an `if` whose other branch produces a value.** That is what lets `panic "..."` stand where a `Float`
   or an `Int` is expected, exactly as `percentage` above does.

2. **A panic prints `panic: <message>` to standard error, then the site as `  at <file>:<line>:<column>`, and exits
   with 101.** Both the message and the site are output, and the conformance suite between the back ends compares
   them character for character, so the format itself is part of the language rather than an implementation detail.

   **The site is the program's line, not the standard library's.** `list[5]` on a list of three panics inside the `at`
   of `ArrayList`, but the site it prints is the line of the program that wrote `list[5]`: a function of `std/` that
   can panic takes the site of its caller as a hidden argument and names it, the way Rust's `#[track_caller]` does.

   ```text
   panic: index 5 is out of bounds for a length of 3
     at src/main.trb:4:7
   ```

3. **Nothing runs on the way out of a panic.** There is no destructor, no `Close`, and no `using` cleanup, because a
   panic means the program already has a bug, and running more code in a broken program is how bugs get worse.

4. **A panic aborts the whole process, with one exception: a script running inside a `Sandbox`.** There the VM stops
   the script alone and reports a `SandboxError` to the host, because a sandboxed script has a heap of its own to walk
   away from.

5. **Integer overflow, division by zero, a remainder by zero, and indexing out of bounds all panic.** They are not
   exceptions to be caught; they are the same panic as an explicit `panic "..."` call, with a message the runtime
   supplies.

   ```trb skip 'numbers.length()' is zero at runtime, and this documentation's gate does not execute a panic to show it
   fn average(numbers: List<Int>): Int {
     numbers.sum() / numbers.length()
   }
   print average([])
   ```

6. **A program may panic only on a promise it broke, never on a property of the data it was given.** An operation
   panics where its precondition is something the program could have written as an ordinary expression -
   `index < list.length()`, `divisor != 0`, `map.containsKey(key)` - and chose not to. Where the precondition is
   invisible in the program - a byte inside of a character, the encoding of bytes from outside - there is no form that
   can fail: `readLine()` answers a `Result`, and a text is cut with `withoutPrefix`, `splitOnce` and
   `dropping(characters:)`. Every partial operation has a **total twin** beside it that answers an `Option`:

   | Panics | Answers an `Option` instead |
   |---|---|
   | `list[index]`, `map[key]` | `list.get(index)`, `map.get(key)` |
   | `list[from..to]`, `text[from..to]` | `list.part(from..to)`, `text.part(from..to)` |
   | `a + b`, `a - b`, `a * b`, `a / b`, `a % b` | `a.addedChecked(b)`, `subtractedChecked`, `multipliedChecked`, `dividedChecked`, `remainderChecked` |
   | `option.expect("...")` | `option ?? fallback` |

   The messages say what went wrong with the values that made it go wrong: `index 5 is out of bounds for a length of
   3`, `the key "Alan" is not in the map` (the key as it shows inside of another value, cut after 60 characters).

7. **A failure the compiler can see is a compile error, not a panic.** A divisor that is known to be zero, arithmetic
   on known operands that leaves the range of its type, an index or a range outside a collection literal and a key
   that none of a map literal's keys is: each of them would panic wherever it runs, so none of them compiles.

   ```trb error
   fn half(value: Int): Int {
     value / 0
   }
   // error: `/` by zero panics wherever it runs, and this divisor is zero
   ```

8. **Running out of memory is a panic with an exit code of its own, 102.** It prints
   `panic: out of memory: ...` the same way, naming the memory limit where one was reached (`TORB_MEMORY_LIMIT`, and
   the default a `dev` build has - see [torb run](../../tooling/torb-run.md)), so a script can tell a program that ran
   out of memory from one that failed an assertion. It has no site: an allocation happens inside the runtime.

## What this is not

**`panic` is not an exception, and it cannot be caught.** There is no `catch`, no `rescue`, and no handler anywhere in
the language that runs when one happens; the one place a panic is observable at all is a `Sandbox`, which reports that
the script stopped rather than resuming it.

```trb check
fn firstOf(numbers: List<Int>): Int? {
  numbers.first()
}

print(firstOf([]) ?? 0)
```

```trb check
fn firstOf(numbers: List<Int>): Int {
  numbers[0]
}

print firstOf([])
```

The first program asks for the first element through `first()`, which answers `Int?` and never panics; the second one
indexes with `[0]`, which panics on an empty list because there is nothing at that position to hand back. Both
type-check - the difference is what happens when the list actually is empty, which only the panicking one turns into
an unrecoverable stop instead of a value.

**`panic` is not for a failure a caller is expected to handle.** A missing file, a malformed input, a network error -
all of those are a `Result` or an `Option`, because the caller has a reasonable next step. `panic` is for a state the
program's own logic promised could not happen: an index computed to be in range, a case a `match` already excluded.

**A top-level `?` is not a panic.** It prints a different format and exits with a different code; see
[Errors at the top level](top-level-errors.md) for the distinction.

## Related

- [Result](result.md) - the alternative for a failure the caller can act on.
- [Errors at the top level](top-level-errors.md) - the other way a program can end with a nonzero exit code.
- [Option](../values-and-types/option.md) - `first()` and the other `Option`-returning alternatives to indexing.
