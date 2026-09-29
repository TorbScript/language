---
title: std/encoding
summary: Encode, Decode and Describe, the Encoder, Decoder and Describer a format implements, EncodedValue and Structure for a value without its type, Format for streaming, and RFC 4648's Base64, Base32 and hexadecimal for bytes as text.
kind: package
status: stable
order: 60
keywords:
  - std/encoding
  - Encode
  - Decode
  - Describe
  - Encoder
  - Decoder
  - Describer
  - EncodedValue
  - Structure
  - Format
  - Base64Alphabet
  - Base32Alphabet
  - hexEncoded
  - hexDecoded
  - EncodingError
  - serialization
source:
  - std/encoding/src/lib.trb
  - std/encoding/src/values.trb
  - std/encoding/src/structure.trb
  - std/encoding/src/derived.trb
  - std/encoding/src/base64.trb
  - std/encoding/src/base32.trb
  - std/encoding/src/hexadecimal.trb
---

`std/encoding` is what other languages need reflection for: serialization, configuration mapping, database rows,
schemas. A value is its constructor call, and the compiler offers that call in three forms - written (`Encode`), read
(`Decode`) and described without a value (`Describe`). A format implements `Encoder`, `Decoder` or `Describer` and
never sees a type, so there is no tree in between. All three are generated for a type whose constructor is usable from
outside, over exactly its parameters; the generated code is a straight line of the steps this package declares, and
could be written by hand. The traits, `DecodeError`, `EncodedValue` and `rendered` are in scope through the prelude.

This package also has the plainer kind of encoding, unrelated to the three forms above: bytes as text and back, RFC
4648's Base64, Base32 and hexadecimal - for a format whose wire is itself text (a JWT, a `!!binary` YAML scalar, a
TOTP secret), not for a value's own `Encoder.bytes`.

## Import

```trb fragment
use Encode, Decode, Describe, DecodeError, Format, rendered from "std/encoding"
use Encoder, Decoder, Describer, EncodedValue, Structure, structureOf, Naming from "std/encoding"
use Base64Alphabet, Base32Alphabet, hexEncoded, hexDecoded, EncodingError from "std/encoding"
```

```trb check
use structureOf from "std/encoding"

type Point {
  x: Int
  y: Int
}

const point = Point x: 1, y: 2
print rendered(point)
print EncodedValue.of(point)
print structureOf<Point>()
```

## Declarations

### Encode, Decode, Describe

```trb fragment
public trait Encode {
  fn encode<Target: Encoder>(var target: Target)
}

public trait Decode {
  static fn decode<Source: Decoder>(var source: Source): Result<Self, DecodeError>
}

public trait Describe {
  static fn describe<Target: Describer>(var target: Target)
}
```

`encode` writes `self` into whatever format `target` is; `decode` is a `static fn` through `Type.decode(source)` and
answers the failure a format's decoder produced; `describe` walks the structure without a value. The format is a type
parameter, so every call is monomorphized per format and direct. A type whose constructor is closed from outside (a
capsule) is written, read and described as the source of its one conversion pair.

### DecodeError

```trb fragment
public type DecodeError with Show, Error {
  message: String
  path: List<String> = []

  fn inside(segment: String): DecodeError
}
```

`error.inside("email")`: a `DecodeError` collects the field chain on its way out of nested records, sequences and maps,
so `show()` renders `"user.email: a text is needed"` rather than losing where the field was.

### Encoder, Decoder, Describer

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
  var fn sequence(length: Int?)
  var fn map(length: Int?)
  var fn record(typeName: String)
  var fn variant(typeName: String, name: String)
  var fn field(name: String)
  var fn finish()
}

public trait Decoder {
  var fn nothing(): Bool
  var fn bool(): Result<Bool, DecodeError>
  var fn int(): Result<Int64, DecodeError>
  var fn unsigned(): Result<UInt64, DecodeError>
  var fn float(): Result<Float64, DecodeError>
  var fn decimal(): Result<Decimal, DecodeError>
  var fn string(): Result<String, DecodeError>
  var fn bytes(): Result<List<UInt8>, DecodeError>
  var fn sequence(): Result<Int?, DecodeError>
  var fn map(): Result<Int?, DecodeError>
  var fn hasNext(): Result<Bool, DecodeError>
  var fn record(typeName: String): Result<Void, DecodeError>
  var fn variant(typeName: String): Result<String, DecodeError>
  var fn field(name: String): Result<Bool, DecodeError>
  var fn finish(): Result<Void, DecodeError>
}

public trait Describer {
  var fn bool()
  var fn int()
  var fn unsigned()
  var fn float()
  var fn decimal()
  var fn string()
  var fn bytes()
  var fn optional()
  var fn sequence()
  var fn map()
  var fn record(typeName: String): Bool
  var fn variant(typeName: String): Bool
  var fn variantCase(name: String)
  var fn field(description: FieldDescription)
  var fn finish()
}
```

The methods are named after the types of the language, and a narrow number is widened before it is written, so nothing
about a format's own numbers is lost. `sequence`, `map`, `record` and `variant` open, and `finish` closes the innermost
of them; a record announces each field with `field` and writes its value right after. A record that announces no field
is a wrapper around its one value. Every method is free of type parameters and closures, so a format is a straight-line
state machine. `typeName` is the declaration's qualified name - package, module path, type - which is the key a format
finds a mapping for one type by.

Reading is driven by the type: it asks the decoder for what it expects and gets a `DecodeError` if something else is
there. `nothing()` is how `Option` decides between `None` and `Some`, and `field(name)` answers `false` for a field
the input does not have, so the field's default applies. `Describer.record` answers `false` for a type the target has
seen already, which is what makes a type that contains itself describe exactly once.

### EncodedValue and the shared decoder

```trb fragment
public type EncodedValue with Show, Encode {
  case Nothing
  case Bool(value: Bool)
  case Int(value: Int64)
  case Unsigned(value: UInt64)
  case Float(value: Float64)
  case String(value: String)
  case Bytes(value: List<UInt8>)
  case Sequence(items: List<EncodedValue>)
  case Mapping(entries: List<EncodedValue>)
  case Record(typeName: String, fields: List<EncodedField>)
  case Variant(typeName: String, name: String, fields: List<EncodedField>)

  static fn of<Value: Encode>(value: Value): EncodedValue
}

public fn rendered<Value: Encode>(value: Value): String
public type Values with Encoder
public type ValueDecoder with Decoder
```

`EncodedValue` is any `Encode` value held without its type: a quotation's captures, a field default in a schema, the
text of `rendered(value)`. It is built by `Values`, an ordinary `Encoder`, and it writes itself into any other one. An
exact `Decimal` is held as its text. `ValueDecoder` is the `Decoder` every buffering format shares: a text format with
unordered fields parses into an `EncodedValue` and reads a typed value out of it, with the options `lenient` (numbers
from text), `naming` and `strict` of the format.

### Structure

```trb fragment
public fn structureOf<Value: Describe>(): Structure
public type Structure
public type StructureField
public type FieldDescription
public type FieldDefault
public type Structures with Describer
```

`structureOf<Order>()` is a type's description as a tree, which is what a schema format wants to walk: a record with its
fields in declaration order, each with its doc comment and whether it is required, has a default that is a constant, or
has one computed on the way in. A type that is already being described further up is a `Reference` to its name.

### Naming

```trb fragment
public type Naming {
  case Unchanged
  case SnakeCase

  fn applied(name: String): String
}
```

How a format spells the field names a type declares. It is an option of a format (`Json(naming: .SnakeCase)`) and never
a word on the type.

### The steps a derived form is made of

`encodeField`, `decodeRequired`, `decodeHasField`, `describeField` and the rest of `src/derived.trb` are what the
compiler generates a derived `encode`, `decode` and `describe` out of: one call per field, instantiated for the field's
type and the format. They are public so that what a derived form does can be read, and so that a hand-written form can
be made of the same pieces.

### Format

```trb fragment
public trait Format<Failure> {
  static fn encodeAll<Value: Encode>(value: Value): Bytes
  static fn decodeAll<Value: Decode>(bytes: Bytes): Result<Value, Failure>
  static fn items<Item: Decode>(): Stage<Bytes, Result<Item, Failure>>
  static fn encoded<Item: Encode>(): Stage<Item, Bytes>
}
```

What a whole format is, once: bytes in both directions, plus the two `Stage`s (see [std/iteration](iteration.md)) that
make it work on a `Source` of [std/stream](stream.md) - `body.through(Json.items<User>()).checked()` reads one `User`
at a time out of a byte stream, with memory of one value. `Failure` is a type parameter rather than an associated type,
because the language has no associated types: `Json` is a `Format<JsonError>`. The members are static and use the
format's default options. `Encode`/`Decode` and `Encoder`/`Decoder` stay synchronous either way; streaming happens one
level up, at the element, through a resumable framer that finds where one element ends while the element itself
decodes through the ordinary `Decode`.

### Base64Alphabet, base64Decoded

```trb fragment
public type EncodingError with Show, Error {
  reason: String
  offset: Int
}

public type Base64Alphabet {
  case Standard
  case UrlSafe

  fn encoded(bytes: Bytes, padded: Bool = true): String
}

public fn base64Decoded(text: String): Result<List<UInt8>, EncodingError>
```

Base64 (RFC 4648 sections 4 and 5): `Base64Alphabet.Standard` (`+`, `/`) for everywhere nothing says otherwise, and
`Base64Alphabet.UrlSafe` (`-`, `_`) for a JSON Web Token's segments and a JSON Web Key's modulus - each with or without
the `=` padding. `base64Decoded` reads either alphabet, padded or not, in the same call: the two agree on 62 of their
64 characters, so nothing is lost by not asking which one a text was written in. Whitespace inside the text is
skipped, the shape a wrapped MIME block needs.

```trb check
use Base64Alphabet, base64Decoded from "std/encoding"

const encoded = Base64Alphabet.UrlSafe.encoded "hi".bytes(), padded: false
print encoded
print base64Decoded(encoded)
```

### Base32Alphabet

```trb fragment
public type Base32Alphabet {
  case Standard
  case Extended

  fn encoded(bytes: Bytes, padded: Bool = true): String
  fn decoded(text: String): Result<List<UInt8>, EncodingError>
}
```

Base32 (RFC 4648 sections 6 and 7): `Base32Alphabet.Standard` (`A`-`Z`, `2`-`7`), what NATS NKeys and a TOTP secret are
written in, and `Base32Alphabet.Extended` (`0`-`9`, `A`-`V`, "base32hex"), which sorts the same as the bytes it stands
for. Unlike Base64's two alphabets, these are not read together - `A` means 0 in one and 10 in the other - so decoding
takes the alphabet the text was written in.

```trb check
use Base32Alphabet from "std/encoding"

print Base32Alphabet.Standard.encoded("foobar".bytes())
```

### Hexadecimal: hexEncoded, hexDecoded

```trb fragment
public fn hexEncoded(bytes: Bytes, uppercase: Bool = false): String
public fn hexDecoded(text: String): Result<List<UInt8>, EncodingError>
```

Hexadecimal (RFC 4648 section 8): two digits a byte, most significant nibble first. `hexEncoded` writes lower case
unless told otherwise; `hexDecoded` reads either case, mixed or not. `Digest.hex()` of [std/digest](digest.md) is this
in one direction only, fixed to lower case, for a hash's own printing.

```trb check
use hexEncoded, hexDecoded from "std/encoding"

print hexEncoded("hi".bytes())
print hexDecoded("6869")
```

## Related

- [std/json](json.md) - `Json`, the one format the standard library implements.
- [Encode and Decode](../language/reflection/encode-and-decode.md) - the rule the three forms are derived by.
- [std/iteration](iteration.md) - `Stage`, which `Format.items` and `Format.encoded` answer.
- [std/digest](digest.md) - `Digest.hex()`, and `Hmac`/`Hmac.pbkdf2` over SHA-256, SHA-1 and MD5.
- [The standard library](index.md) - the other packages.
