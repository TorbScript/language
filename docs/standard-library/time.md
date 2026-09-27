---
title: std/time
summary: Instant and Duration, the two time values, Timestamp, a point on the wall clock, plus Clock and sleep, which read and wait on the clock.
kind: package
status: stable
order: 120
keywords:
  - std/time
  - Instant
  - Duration
  - Timestamp
  - Clock
  - sleep
source:
  - std/time/src/lib.trb
---

`std/time` is time: points in time (`Instant`) and the spans between them (`Duration`), and a point on the wall clock
(`Timestamp`). All three are a count of nanoseconds in an `Int64` and cost what the number costs. `Instant` and
`Duration` are values and are in the prelude; `Timestamp` is an import, and so is `Clock`, which reads the current time
and is a capability.

## Import

```trb fragment
use Instant, Duration, Timestamp from "std/time"
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
  private value: Int64
}
```

A point in time. Only the difference between two `Instant`s is meaningful, never the value on its own -
`end - start` is what `Subtract<Instant, Duration>` gives back, and `start + 2.seconds()` is a deadline. The field is
private: an `Instant` comes from `Clock.now()` and from nowhere else.

### Duration

```trb fragment
public type Duration with Compare, Show, Add, Subtract {
  private value: Int64

  fn seconds(): Float64
  fn nanoseconds(): Int64
}
```

The span between two `Instant`s, or a length of time asked for on its own; signed, so a difference taken the wrong way
round is negative. It shows as its seconds with an `s` behind them (`2.5s`), and two of them add and subtract.
`2.seconds()` and `250.milliseconds()` come from `extend Int64 { fn seconds(): Duration }` and its sibling, which the
prelude re-exports by name; mainly for sandbox and task limits. `nanoseconds()` is the exact count, which is what a
`Duration` crosses into the runtime as (`Task.within`).

### Timestamp

```trb fragment
public type Timestamp with Compare, Equals, Hash, Show, Subtract<Timestamp, Duration>, Add<Duration, Timestamp> {
  private value: Int64

  static fn fromUnixNanoseconds(nanoseconds: Int64): Timestamp
  fn unixNanoseconds(): Int64
}
```

A point on the wall clock: nanoseconds since 1970-01-01 00:00:00 UTC. It means something on its own - when a file was
last written, which is what `File.metadata` answers it for ([std/fs](fs.md)) - where an `Instant` only means something
against another reading, and it can jump, because the clock of a machine is set. It shows as RFC 3339 in UTC, with as
many digits of the second's fraction as it has: `2026-09-21T14:13:20.5Z`. The difference of two is a `Duration`, and a
`Duration` added to one is another.

```trb check
use Timestamp from "std/time"

const written = Timestamp.fromUnixNanoseconds 1_790_000_000_500_000_000
print written
print(written + 2.seconds())
```

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
