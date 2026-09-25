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
public fn standardInput(): Source<Bytes, IoError>
public fn standardOutput(): Sink<Bytes, IoError>
public fn standardError(): Sink<Bytes, IoError>
```

Standard input as a stream of byte chunks. `standardOutput()` is not buffered - every `add` goes out immediately -
so `standardOutput().buffered(capacity: 4096)` is the form for a program that writes a lot of small pieces, and it says
where the flush is. `standardError()` stays unbuffered for the reason it always is: a diagnostic that is lost is worse
than a slow one. What a sink of standard output takes and what `print` writes go through one lock, so the two appear in
the order they were written. A read of standard input and a write to a console or a pipe run on a thread of the
blocking pool - no platform lets a poller wait for a console - so the worker that awaits it goes on meanwhile.

### `lines`

```trb fragment
public fn lines(): Source<String, IoError>
```

The lines of standard input, as a stream - the same stage `File.lines` uses, over the same kind of source. Reading from
a `Source` needs `.await()` on every `next()` (see [std/task](task.md)); `readLine()` does not touch `Task` at all.
Both read the one standard input, so a program uses one of them.

```trb check
use lines from "std/io"
use IoError from "std/fs"

fn countLines(): Task<Result<Int, IoError>> {
  var count = 0
  var input = lines()
  while const Some(_) = input.next().await()? {
    count = count + 1
  }
  Ok count
}
```

## Related

- [std/stream](stream.md) - `Source` and `Sink`, and the `lines` stage this package reuses.
- [std/console](console.md) - `print` and `printError`, the short form for writing a line.
- [The standard library](index.md) - the other packages.
