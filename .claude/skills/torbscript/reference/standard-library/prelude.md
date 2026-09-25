---
title: std/prelude
summary: The package of re-exports that is in scope in every file of a project, unless project.trb names another one.
kind: package
status: stable
order: 210
keywords:
  - std/prelude
  - prelude
  - re-export
source:
  - std/prelude/src/lib.trb
---

`std/prelude` declares nothing of its own: every name it re-exports lives in a package that can also be imported
directly. [The prelude](../language/modules-and-packages/the-prelude.md) is the rule - why the pure part of the
standard library is in scope everywhere and a capability such as `std/fs` or `Clock` never is, and how a project
replaces this package with another one. What follows here is exactly what `std/prelude` re-exports today.

## Import

The prelude needs no import: it is what every other file's imports are measured against. A project names a different
one by setting `prelude` in `project.trb` (see [std/project](project.md)).

```trb check
const values: List<Int?> = [Some(1), None, Some(3)]
print(values.filterMap { _ }.sum())
```

## Declarations

### What is re-exported

```trb fragment
public use Void, Never, panic, Bool, Option, Result, Error from "std/core"
public use Option.Some, Option.None, Result.Ok, Result.Fail from "std/core"
public use Equals, Compare, Ordering, Hash, combineHashes from "std/core"
public use From, Into, TryFrom, TryInto, Show, LiteralParseError from "std/core"
public use Add, Subtract, Multiply, Divide, Remainder, Negate, OrElse from "std/core"
public use Indexed, MutableIndexed, Slice, MutableSlice from "std/core"
public use Range, RangeFrom, RangeTo, Bounds from "std/core"
public use Array, Shared, isSame from "std/core"
public use do, unless, retry, Close from "std/core"
public use Predicate, Action, Transform from "std/core"
public use Char, String from "std/text"
public use Numeric, Signed, Real, Bits, NumberParseError, NumberRangeError from "std/number"
public use Int8, Int16, Int32, Int64 from "std/number"
public use UInt8, UInt16, UInt32, UInt64 from "std/number"
public use Float32, Float64, Decimal from "std/number"
public use Int, UInt, Float from "std/number"
public use List, ArrayList, TrieList from "std/collections"
public use Map, TrieMap, HashMap, Set, TrieSet, HashSet from "std/collections"
public use Stack, ArrayStack, Queue, ArrayQueue from "std/collections"
public use Iterator, Iterate, Length from "std/iteration"
public use Stage from "std/iteration"
public use mapping, filtering, filterMapping, mappingWhile, flatMapping from "std/iteration"
public use taking, takingWhile, skipping, indexing, chunking from "std/iteration"
public use Accumulator, ListAccumulator, collector, into, listing from "std/iteration"
public use counting, summing, averaging, minBy, maxBy, joining, partitioningBy, groupingBy from "std/iteration"
public use Encode, Decode, Describe, DecodeError, Format, rendered from "std/encoding"
public use Encoder, Decoder, Describer, FieldDescription, FieldDefault, EncodedValue from "std/encoding"
public use Expression, ExpressionNode, TypeReference, SourceLocation, UnaryOperator, BinaryOperator, assert, nameOf from "std/expression"
public use Task, Channel, ChannelClosed, Cancelled, TimedOut, spawn, both from "std/task"
public use Parallel from "std/parallel"
public use List.parallel from "std/parallel"
public use Source, Sink, Bytes from "std/stream"
public use print, printError from "std/console"
public use Json, JsonValue, JsonError from "std/json"
public use Duration, Instant from "std/time"
public use Int64.seconds, Int64.milliseconds from "std/time"
public use Path, PathError from "std/path"
public use Uri, UriError from "std/uri"
```

`Path` is in because it is a value like `Duration`: it names a file and reads nothing, so a signature can say it
without an import, while `File` and `Directory` - which touch what a path names - stay behind `use ... from "std/fs"`.
`Uri` and `UriError` are in for the same reason: a URI names a resource and opens nothing, and what reaches it -
[std/http](http.md), [std/network](network.md) - stays an import, as do `UriReference`, `Urn` and the rest of
[std/uri](uri.md). `std/linear` stays out: a program that computes with vectors
imports it.

Every re-export keeps its original name, so `use Option from "std/prelude"` and `use Option from "std/core"` name the
same type. There is no `math` namespace: the power is `**`, and the constants and functions are `Real`'s members
(`Float.pi`, `angle.sine()`, `value.naturalLogarithm()`). `Result.Ok`, `Result.Fail`, `Option.Some` and `Option.None` are the one reason
these two types can be matched and constructed without their type name, in a pattern (`None =>`) as well as in an
expression (`Ok value`). `Predicate`, `Action` and `Transform` are here so that a signature taking a closure reads
the same in every file: `predicate: Predicate<Item>` rather than `predicate: (value: Item) => Bool` (see
[Predicate, Action and Transform](function-types.md)). `Cancelled`, `TimedOut` and `both` come with `Task` because a
program that spawns one needs them to say how it ended and to wait for two at once (see [std/task](task.md)). `using`
is not in the list: it is grammar, a binding form, and needs no name in scope.

## Related

- [std/core](core.md) - the largest single source of what the prelude re-exports.
- [std/project](project.md) - the `prelude` field that names a different one.
- [The standard library](index.md) - the other packages.

