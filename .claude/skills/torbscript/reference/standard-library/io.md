---
title: std/io
summary: Standard input and the streams every process is started with - readLine for the short form, Source and Sink for the rest.
kind: package
status: stable
order: 140
keywords:
  - std/io
  - standard input
  - standard output
  - readLine
source:
  - std/io/src/lib.trb
---

`std/io` is the three streams every process is started with. They are ordinary `Source` and `Sink` values (see
[std/stream](stream.md)), so everything that reads a file reads standard input and everything that writes a file writes
to the screen. `print`/`printError` of [std/console](console.md) and `readLine()` here stay as the short form for the
common case: a program that prints a line does not want to think about a stream.

## Import

```trb fragment
use readLine, standardInput, standardOutput, standardError from "std/io"
```

```trb check
use readLine from "std/io"

const first = readLine()
print(first ?? "no input")
```

## Declarations

### `readLine`

```trb fragment
public native fn readLine(): String?
```

The next line of standard input without its line break, `None` at the end of the input.

### `standardInput`, `standardOutput`, `standardError`

```trb fragment
public native fn standardInput(): Source<Bytes, IoError>
public native fn standardOutput(): Sink<Bytes, IoError>
public native fn standardError(): Sink<Bytes, IoError>
```

Standard input as a stream of byte chunks. `standardOutput()` is not buffered - every `add` goes out immediately -
so `standardOutput().buffered(capacity: 4096)` is the form for a program that writes a lot of small pieces, and it says
where the flush is. `standardError()` stays unbuffered for the reason it always is: a diagnostic that is lost is worse
than a slow one.

### `lines`

```trb fragment
public fn lines(): Source<String, IoError>
```

The lines of standard input, as a stream - the same stage `File.lines` uses, over the same kind of source. Reading from
a `Source` needs `.await()` on every `next()`, so `standardInput`, `standardOutput`, `standardError` and `lines` wait on
the same milestone as [std/task](task.md), which is `status: planned`; `readLine()` does not touch `Task` at all.

## Related

- [std/stream](stream.md) - `Source` and `Sink`, and the `lines` stage this package reuses.
- [std/console](console.md) - `print` and `printError`, the short form for writing a line.
- [The standard library](index.md) - the other packages.

