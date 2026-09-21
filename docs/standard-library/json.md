---
title: std/json
summary: Json for encoding and decoding, and JsonValue for the rare document whose shape is not known ahead of time.
kind: package
status: stable
order: 110
keywords:
  - std/json
  - Json
  - JsonValue
  - JsonError
  - serialization
source:
  - std/json/src/lib.trb
---

`std/json` is JSON as a `Format` (see [std/encoding](encoding.md)): `Json` reads and writes any `Encode`/`Decode` type,
and `JsonValue` is a library type for the rare document whose shape is not known ahead of time, because `Encoder`
and `Decoder` are driven by the type being read or written and cannot express "whatever is there" on their own. `Json`
is in the prelude.

## Import

```trb fragment
use Json, JsonValue, JsonError from "std/json"
```

```trb check
type User {
  name: String
  age: Int
}

const text = Json.encode User(name: "Ada", age: 36)
print text
match Json.decode<User>(text) {
  Ok(user) => print user.name
  Fail(error) => print "Rejected: {error}"
}
```

## Declarations

<!-- torb:declarations:begin -->

### Json

```trb fragment
public native type Json {
  static fn encode(value: Encode): String
  static fn decode<Value: Decode>(text: String): Result<Value, JsonError>
  static fn parse(text: String): Result<JsonValue, JsonError>
  static fn value<Value: Encode>(value: Value): JsonValue
}
```

`encode` renders any `Encode` value as JSON text; `decode<Value>` parses text and decodes it as `Value`, and fails on
malformed JSON as well as on JSON that does not fit `Value`. `parse` reads text into a `JsonValue` without knowing its
shape; `value` turns any `Encode` value into a `JsonValue` tree, for example to diff two versions of it structurally.
`extend Json with Format<JsonError>` additionally gives `Json.items<Item>()` and `Json.encoded<Item>()`, the two
`Stage`s that let a byte stream be decoded one element at a time (see [std/stream](stream.md)); a `[` at the start of
the stream opens one top-level array, and anything else is read as newline- or concatenation-separated values, which
covers NDJSON without a rule of its own.

### JsonValue

```trb fragment
public type JsonValue {
  case Null
  case Bool(value: Bool)
  case Number(value: Float64)
  case String(value: String)
  case Array(items: List<JsonValue>)
  case Object(entries: Map<String, JsonValue>)
}
```

A JSON document without a fixed shape. JSON does not distinguish an integer from a floating-point number, so every
`Number` carries a `Float64`. A `JsonValue` describes itself to any `Encoder`, not only `Json`'s own.

### JsonError

```trb fragment
public type JsonError with Show, Error {
  static fn syntax(line: Int, message: String): JsonError
  static fn decodeFailed(cause: DecodeError): JsonError
  static fn invalidText(cause: Utf8Error): JsonError
  fn isSyntaxError(): Bool
  fn cause(): Error?
}
```

What went wrong turning text into a value and back: malformed JSON text (`syntax`), JSON that parsed fine but did not
fit the requested type (`decodeFailed`), or bytes that were not UTF-8 in the first place (`invalidText`, from
[std/stream](stream.md)'s `Utf8Error`). `isSyntaxError()` tells the first case from the other two; `cause()` is the
`DecodeError` or `Utf8Error` a rejected document failed on, so a report can unwind the chain down to the field or the
byte.

<!-- torb:declarations:end -->

## Related

- [std/encoding](encoding.md) - `Encode`, `Decode` and `Format`, which `Json` implements.
- [std/stream](stream.md) - the `Stage`s `Json.items` and `Json.encoded` answer.
- [The standard library](index.md) - the other packages.
