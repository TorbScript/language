---
title: Encoder and Decoder
summary: Encoder and Decoder each name every scalar the language has - bool, int, unsigned, float, decimal, string, bytes - plus the four shapes a value can take, sequence, map, record and variant.
kind: reference
status: stable
order: 30
keywords:
  - Encoder
  - Decoder
  - RecordEncoder
  - SequenceEncoder
  - MapEncoder
source:
  - std/encoding/src/lib.trb
  - CONCEPT.md#types-values-and-reflection
---

> **Not built natively yet.** `Json.encode` is not built by the native back end yet, so `torb run` refuses the examples
> here that use it. `torb check` accepts them, and the rules are the language's.

A format implements `Encoder` and `Decoder`, the two traits behind [Encode and Decode](encode-and-decode.md), and
never sees a `type`. Both name the same small vocabulary: one method per scalar the language has, and four shapes
for everything built out of them.

## Example

```trb check
use Json from "std/json"

type Point {
  x: Int
  y: Int
}

extend Point with Encode {
  fn encode(var encoder: Encoder) {
    encoder.record("Point", { fields =>
      fields.field "x", x
      fields.field "y", y
    })
  }
}

print Json.encode(Point(1, 2))
```

## Syntax

```text
trait Encoder {
  var fn nothing()
  var fn bool(value: Bool)
  var fn int(value: Int64)
  var fn unsigned(value: UInt64)
  var fn float(value: Float64)
  var fn decimal(value: Decimal)
  var fn string(value: String)
  var fn bytes(value: List<UInt8>)

  var fn sequence(length: Int?, write: (var items: SequenceEncoder) => Void)
  var fn map(length: Int?, write: (var entries: MapEncoder) => Void)
  var fn record(typeName: String, write: (var fields: RecordEncoder) => Void)
  var fn variant(typeName: String, name: String, write: (var fields: RecordEncoder) => Void)
}
```

## Rules

1. **`Encoder` has one method per scalar the language has, plus `nothing`.** `bool`, `int`, `unsigned`, `float`,
   `decimal`, `string` and `bytes` are the whole vocabulary; a format never invents a method for a type of its own.

2. **A narrow number travels widened.** `Int8`, `Int16` and `Int32` call `encoder.int` with an `Int64`, so a format
   implements the wide method once; on the way back a value that does not fit is a `DecodeError`, not a silent
   truncation.

3. **Four shapes cover everything built from scalars: `sequence`, `map`, `record` and `variant`.** A `sequence`
   writes its items one at a time through a `SequenceEncoder` - this is what lets a stream of a million records use
   the memory of one - a `map` writes key/value pairs through a `MapEncoder`, a `record` writes a type's named fields
   through a `RecordEncoder`, and a `variant` is a `record` that also carries the case's name.

4. **`Decoder` mirrors `Encoder`, method for method, but reading is driven by the type.** Each scalar method answers
   a `Result<Value, DecodeError>` instead of taking a value, because the type asks for what it expects and the bytes
   might not have it.

5. **`Decoder.nothing()` answers a `Bool`, not a value.** This is how `Option` decides between `None` and `Some`:
   consuming a "nothing" if there is one, before ever asking for the value it would otherwise contain.

6. **`SequenceDecoder.next()` and `MapDecoder.next()` answer `None` at the end.** A sequence or a map is read the
   same way a stream is: one `next()` call at a time, until the answer is absent.

## What this is not

**An `Encoder` is not a tree builder.** A format writes what it is given as it is given it - a `record` call writes
its fields the moment `fields.field` is called inside the closure - there is no intermediate document a format
assembles and then serializes, which is what lets a sequence of a million items stream through in constant memory.

```trb check
use Json from "std/json"

type Pair {
  first: Int
  second: Int
}

extend Pair with Encode {
  fn encode(var encoder: Encoder) {
    encoder.record("Pair", { fields =>
      fields.field "first", first
      fields.field "second", second
    })
  }
}

print Json.encode(Pair(1, 2))
```

```trb error
type Pair {
  first: Int
  second: Int
}

extend Pair with Encode {
  fn encode(var encoder: Encoder) {
    encoder.first
  }
}
// error: `Encoder` has no member `first`
```

## Related

- [Encode and Decode](encode-and-decode.md) - the generated pair most types never write by hand.
- [There is no reflection](no-reflection.md) - why a format never sees a type.
- [std/core](../../standard-library/core.md) - `Option`, whose `Decode` reads `nothing()` first.
