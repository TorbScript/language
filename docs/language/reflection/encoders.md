---
title: Encoder and Decoder
summary: Encoder and Decoder each name every scalar the language has - bool, int, unsigned, float, decimal, string, bytes - plus the four shapes a value can take, sequence, map, record and variant, which open and are closed by finish.
kind: reference
status: stable
order: 30
keywords:
  - Encoder
  - Decoder
  - Describer
  - field
  - finish
source:
  - std/encoding/src/lib.trb
  - CONCEPT.md#types-values-and-reflection
---

A format implements `Encoder` and `Decoder`, the two traits behind [Encode and Decode](encode-and-decode.md), and
never sees a `type`. Both name the same small vocabulary: one method per scalar the language has, and four shapes
for everything built out of them. A schema format implements `Describer`, which has the same method names without
values.

## Example

```trb check
type Point {
  x: Int
  y: Int
}

extend Point with Encode {
  fn encode<Target: Encoder>(var target: Target) {
    target.record "app/Point"
    target.field "x"
    x.encode target
    target.field "y"
    y.encode target
    target.finish()
  }
}

print Json().encode(Point(1, 2))
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

  var fn sequence(length: Int?)
  var fn map(length: Int?)
  var fn record(typeName: String)
  var fn variant(typeName: String, name: String)
  var fn field(name: String)
  var fn finish()
}
```

## Rules

1. **`Encoder` has one method per scalar the language has, plus `nothing`.** `bool`, `int`, `unsigned`, `float`,
   `decimal`, `string` and `bytes` are the whole vocabulary; a format never invents a method for a type of its own.

2. **A narrow number travels widened.** `Int8`, `Int16` and `Int32` call `int` with an `Int64`, so a format
   implements the wide method once; on the way back a value that does not fit is a `DecodeError`, not a silent
   truncation.

3. **Four shapes cover everything built from scalars, and `finish` closes the innermost one.** `sequence` and `map`
   are followed by their items (a map's as a key and then its value), `record` by its fields, each announced with
   `field` and followed by its value, and `variant` is a record that also carries the case's name. A record that
   announces no field at all is a **wrapper** around the one value it holds, and a text format writes that value
   without anything around it.

4. **The format is a bound, not a trait type.** `encode<Target: Encoder>(var target: Target)` is monomorphized per
   format, so a format hands its own encoder over as a `var` path and reads its result out of it afterwards, and the
   whole call chain is direct.

5. **`Decoder` mirrors `Encoder`, method for method, but reading is driven by the type.** Each scalar method answers
   a `Result<Value, DecodeError>` instead of taking a value, because the type asks for what it expects and the input
   might not have it. `field(name)` answers `false` where the input has no such field, and the field's default applies.

6. **`Decoder.nothing()` answers a `Bool`, not a value.** This is how `Option` decides between `None` and `Some`:
   consuming a "nothing" if there is one, before ever asking for the value it would otherwise contain.

7. **`hasNext()` both tests and positions.** It answers `true` while one more item of the open sequence or one more
   key or value of the open map follows, and the next read takes it.

## What this is not

**An `Encoder` is not a tree builder.** A format writes what it is given as it is given it - there is no intermediate
document a format assembles and then serializes, which is what lets a sequence of a million items stream through in
constant memory. `Values`, the encoder that builds an `EncodedValue`, is the one format whose output is a tree, and a
program uses it only where it wants a value without its type.

```trb check
type Pair {
  first: Int
  second: Int
}

extend Pair with Encode {
  fn encode<Target: Encoder>(var target: Target) {
    target.sequence Some(2)
    first.encode target
    second.encode target
    target.finish()
  }
}

print Json().encode(Pair(1, 2))
```

```trb error
type Pair {
  first: Int
  second: Int
}

extend Pair with Encode {
  fn encode<Target: Encoder>(var target: Target) {
    target.first
  }
}
// error: `Target` has no member `first`
```

## Related

- [Encode and Decode](encode-and-decode.md) - the generated forms most types never write by hand.
- [There is no reflection](no-reflection.md) - why a format never sees a type.
- [std/encoding](../../standard-library/encoding.md) - the vocabulary, `EncodedValue`, `Structure` and the steps a
  derived form is made of.
