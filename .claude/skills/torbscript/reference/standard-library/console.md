---
title: std/console
summary: print and printError, the two functions that write to the standard streams.
kind: package
status: stable
order: 90
keywords:
  - std/console
  - print
  - printError
  - standard output
source:
  - std/console/src/lib.trb
---

`std/console` writes to the two standard streams. `panic` is not here but in `std/core`, because half of `std/core`
calls it (`Result.expect`, every out-of-bounds index) and a package everything depends on must not depend on this one.
Both names below are already in scope through the prelude.

## Import

```trb fragment
use print, printError from "std/console"
```

```trb check
print "hello", "world"
printError "starting up"
```

## Declarations

### `print`, `printError`

```trb fragment
public native fn print(...values: Show)
public native fn printError(...values: Show)
```

`print` writes its values to standard output, separated by spaces, followed by a line break. `printError` does the same
to standard error, for diagnostics, progress and anything that is not the result of the program.

**A line is out when the call returns.** Both hand their line to the operating system before they return - into a
terminal, a pipe or a file alike - so there is nothing to flush, a line of `printError` never overtakes a line of
`print` written before it, and a program that is killed has lost nothing it printed. This is Rust's promise for its
standard output rather than C's, which keeps a pipe's output in a buffer until it is full: under C's rule a service
whose standard output is `docker logs`, the journal or `| tee` shows its lines late, and never if it is stopped. Each
line goes out in one piece, so two programs writing into one pipe interleave whole lines, never the halves of one. The
VM and a native binary share the runtime that does this, and behave the same.

The price is one call of the operating system per line. Measured on Linux, a million short lines take about 0.9 s
instead of 0.24 s into a pipe and about 1.2 s instead of 0.17 s into a file - a microsecond per line, which a program
that prints what it computes does not notice (`runtime/README.md`, "Standard output"). One that writes a great many
lines joins them and prints the text once, which is one call for all of them:

```trb check
const rows = (0..1000).map({ "row {_}" }).toList()
print rows.joined(separator: "\n")
```

## Related

- [std/io](io.md) - standard input and the streams underneath the two standard output streams.
- [The standard library](index.md) - the other packages.

