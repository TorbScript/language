---
title: std/time
summary: Instant and Duration, the two time values, plus Clock and sleep, which read and wait on the wall clock.
kind: package
status: stable
order: 120
keywords:
  - std/time
  - Instant
  - Duration
  - Clock
  - sleep
source:
  - std/time/src/lib.trb
---

`std/time` is wall-clock time: points in time (`Instant`) and the spans between them (`Duration`). `Instant` and
`Duration` are values and are in the prelude; `Clock`, which reads the current time, is a capability and stays an
explicit import.

## Import

```trb fragment
use Instant, Duration from "std/time"
use Clock from "std/time"
```

```trb check
const wait = 2.seconds()
print wait.seconds()
```

## Declarations

<!-- torb:declarations:begin -->

### Instant

```trb fragment
public native type Instant with Compare, Subtract<Instant, Duration> {}
```

A point in time. Only the difference between two `Instant`s is meaningful, never the value on its own -
`end - start` is what `Subtract<Instant, Duration>` gives back.

### Duration

```trb fragment
public native type Duration with Compare, Show {
  fn seconds(self): Float64
}
```

The span between two `Instant`s, or a length of time asked for on its own. `2.seconds()` comes from
`extend Int64 { fn seconds(self): Duration }`, mainly for sandbox and task limits.

### Clock

```trb fragment
public native type Clock {
  fn now(): Instant
  fn milliseconds(): Int64
}
```

The wall clock; needs the `std/time` capability inside a sandboxed script. `milliseconds()` is monotonic milliseconds
counted from the first reading - only differences between two readings are meaningful, as for an `Instant` - and is the
form a tool that measures its own work wants (`torb check --timings`), where an `Instant` and a `Duration` would be two
allocations per measurement.

### `sleep`

```trb fragment
public native fn sleep(seconds: Float64): Task<Void>
```

Suspends the running task for this many seconds. Needs `.await()`, like every `Task` (see [std/task](task.md)).

<!-- torb:declarations:end -->

## Related

- [std/task](task.md) - `Task`, which `sleep` answers.
- [std/sandbox](sandbox.md) - `SandboxCapabilities.limits`, which is a `Duration`.
- [The standard library](index.md) - the other packages.
