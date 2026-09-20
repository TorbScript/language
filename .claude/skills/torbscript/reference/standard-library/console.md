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

<!-- torb:declarations:begin -->

### `print`, `printError`

```trb fragment
public native fn print(...values: Show)
public native fn printError(...values: Show)
```

`print` writes its values to standard output, separated by spaces, followed by a line break. `printError` does the same
to standard error, for diagnostics, progress and anything that is not the result of the program.

<!-- torb:declarations:end -->

## Related

- [std/io](io.md) - standard input and the streams underneath the two standard output streams.
- [The standard library](index.md) - the other packages.
