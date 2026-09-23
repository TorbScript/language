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

`std/time` is wall-clock time: points in time (`Instant`) and the spans between them (`Duration`). Both are a count of
nanoseconds in an `Int64` and cost what the number costs. `Instant` and `Duration` are values and are in the prelude;
`Clock`, which reads the current time, is a capability and stays an explicit import.

## Import

```trb fragment
use Instant, Duration from "std/time"
use Clock from "std/time"
```

```trb check
const wait = 2.seconds() + 500.milliseconds()
print wait.seconds()
```

`2.seconds()` and `500.milliseconds()` are members `std/time` adds to `Int64`, which `std/number` owns, so a file names
where they come from - except that the prelude already does (`public use Int64.seconds, Int64.milliseconds from
"std/time"`), which is why the snippet above needs no `use` at all. `Duration.seconds` is a member of the type itself
and needs nothing either way.

## Declarations

### Instant

```trb fragment
public type Instant with Compare, Subtract<Instant, Duration>, Add<Duration, Instant> {
  private storedNanoseconds: Int64
}
```

A point in time. Only the difference between two `Instant`s is meaningful, never the value on its own -
`end - start` is what `Subtract<Instant, Duration>` gives back, and `start + 2.seconds()` is a deadline. The field is
private: an `Instant` comes from `Clock.now()` and from nowhere else.

### Duration

```trb fragment
public type Duration with Compare, Show, Add, Subtract {
  private storedNanoseconds: Int64

  fn seconds(): Float64
  fn nanoseconds(): Int64
}
```

The span between two `Instant`s, or a length of time asked for on its own; signed, so a difference taken the wrong way
round is negative. It shows as its seconds with an `s` behind them (`2.5s`), and two of them add and subtract.
`2.seconds()` and `250.milliseconds()` come from `extend Int64 { fn seconds(): Duration }` and its sibling, which the
prelude re-exports by name; mainly for sandbox and task limits. `nanoseconds()` is the exact count, which is what a
`Duration` crosses into the runtime as (`Task.within`).

### Clock

```trb fragment
public native type Clock {
  static fn now(): Instant
  native static fn milliseconds(): Int64
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

## Related

- [std/task](task.md) - `Task`, which `sleep` answers.
- [std/sandbox](sandbox.md) - `SandboxCapabilities.limits`, which is a `Duration`.
- [The standard library](index.md) - the other packages.

