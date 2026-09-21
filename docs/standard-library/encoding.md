---
title: std/encoding
summary: Encode and Decode, the Encoder and Decoder a format implements, and Format for the streaming side.
kind: package
status: stable
order: 60
keywords:
  - std/encoding
  - Encode
  - Decode
  - Encoder
  - Decoder
  - Format
  - serialization
source:
  - std/encoding/src/lib.trb
---

`std/encoding` is what other languages need reflection for: serialization, configuration mapping, database rows. A type
describes itself to an `Encoder` and reads itself from a `Decoder`; a format (`Json`, a database driver) implements
those two traits and never sees a type, so there is no tree in between. `Encode` is generated for every type whose
fields are all `Encode`, `Decode` in addition if the constructor is usable from outside; the generated code is exactly
what would be written by hand. Every name below is already in scope through the prelude.

## Import

```trb fragment
use Encode, Decode, DecodeError, Format, describe from "std/encoding"
use Encoder, Decoder from "std/encoding"
```

```trb check
type Point {
  x: Int
  y: Int
}

const point = Point x: 1, y: 2
print describe(point)
```

## Declarations

<!-- torb:declarations:begin -->

### Encode, Decode

```trb fragment
public trait Encode {
  fn encode(var encoder: Encoder)
}

public trait Decode {
  static fn decode(var decoder: Decoder): Result<Self, DecodeError>
}
```

`encode` writes `self` into whatever `encoder` is; `decode` is a `static fn` through `Type.decode(decoder)` and
answers the failure a format's decoder produced. Both are generated for an ordinary type, and hand-written for a type
that needs a shape a format does not have on its own (`JsonValue`, below).

### DecodeError

```trb fragment
public type DecodeError with Show, Error {
  message: String
  path: List<String> = []

  fn inside(segment: String): DecodeError
}
```

`error.inside("email")`: a `DecodeError` collects the path on its way out of nested `record`/`sequence`/`map` calls, so
`show()` renders `"user.email: expected a string"` rather than losing where the field was.

### Encoder and the writing side

```trb fragment
public trait Encoder {
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

public trait SequenceEncoder { var fn item(value: Encode) }
public trait MapEncoder { var fn entry(key: Encode, value: Encode) }
public trait RecordEncoder { var fn field(name: String, value: Encode) }
```

The methods of `Encoder` are named after the types of the language, and a narrow number is widened before it is
written, so nothing about a format's own numbers is lost. `length` is known for a collection and unknown for a
pipeline; either way items go through `SequenceEncoder.item` one at a time, which is what lets a format stream a
sequence instead of building one in memory.

### Decoder and the reading side

```trb fragment
public trait Decoder {
  var fn nothing(): Bool
  var fn bool(): Result<Bool, DecodeError>
  var fn int(): Result<Int64, DecodeError>
  var fn unsigned(): Result<UInt64, DecodeError>
  var fn float(): Result<Float64, DecodeError>
  var fn decimal(): Result<Decimal, DecodeError>
  var fn string(): Result<String, DecodeError>
  var fn bytes(): Result<List<UInt8>, DecodeError>
  var fn sequence<Output>(read: (var items: SequenceDecoder) => Result<Output, DecodeError>): Result<Output, DecodeError>
  var fn map<Output>(read: (var entries: MapDecoder) => Result<Output, DecodeError>): Result<Output, DecodeError>
  var fn record<Output>(typeName: String, read: (var fields: RecordDecoder) => Result<Output, DecodeError>): Result<Output, DecodeError>
  var fn variant<Output>(typeName: String, read: (name: String, var fields: RecordDecoder) => Result<Output, DecodeError>): Result<Output, DecodeError>
}

public trait SequenceDecoder { var fn next<Item: Decode>(): Result<Item?, DecodeError> }
public trait MapDecoder { var fn next<Key: Decode, Value: Decode>(): Result<(Key, Value)?, DecodeError> }
public trait RecordDecoder {
  var fn field<Value: Decode>(name: String): Result<Value, DecodeError>
  var fn fieldOr<Value: Decode>(name: String, default: lazy Value): Result<Value, DecodeError>
}
```

Reading is driven by the type: it asks the decoder for what it expects and gets a `DecodeError` if something else is
there. `nothing()` is how `Option` decides between `None` and `Some`, and `fieldOr` is what a field with a default
value uses, so a field that was left out of the document is not an error.

### Format

```trb fragment
public trait Format<Failure> {
  static fn encodeAll(value: Encode): Bytes
  static fn decodeAll<Value: Decode>(bytes: Bytes): Result<Value, Failure>
  static fn items<Item: Decode>(): Stage<Bytes, Result<Item, Failure>>
  static fn encoded<Item: Encode>(): Stage<Item, Bytes>
}
```

What a whole format is, once: bytes in both directions, plus the two `Stage`s (see [std/iteration](iteration.md)) that
make it work on a `Source` of [std/stream](stream.md) - `body.through(Json.items<User>()).checked()` reads one `User`
at a time out of a byte stream, with memory of one value. `Failure` is a type parameter rather than an associated type,
because the language has no associated types: `Json` is a `Format<JsonError>`. `Encode`/`Decode` and `Encoder`/`Decoder`
stay synchronous either way; streaming happens one level up, at the element, through a resumable framer that finds
where one element ends while the element itself decodes through the ordinary `Decode`.

### `describe`

```trb fragment
public native fn describe(value: Encode): String
```

Any `Encode` value as readable text, for messages and debugging: `describe(Point(x: 1, y: 2))` is
`"Point(x: 1, y: 2)"`.

<!-- torb:declarations:end -->

## Related

- [std/json](json.md) - `Json`, the one format the standard library implements.
- [std/iteration](iteration.md) - `Stage`, which `Format.items` and `Format.encoded` answer.
- [The standard library](index.md) - the other packages.
