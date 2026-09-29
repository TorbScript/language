---
title: std/json
summary: Json, a value with the options of the format, for encoding and decoding any Encode/Decode type, and JsonValue for the rare document whose shape is not known ahead of time.
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

`std/json` is JSON as a format of [std/encoding](encoding.md), written in TorbScript: `Json` reads and writes any
`Encode`/`Decode` type, and `JsonValue` is a library type for the rare document whose shape is not known ahead of time,
because `Encoder` and `Decoder` are driven by the type being read or written and cannot express "whatever is there" on
their own. `Json` is in the prelude.

## Import

```trb fragment
use Json, JsonValue, JsonError from "std/json"
```

```trb check
type User {
  name: String
  age: Int
}

const json = Json()
const text = json.encode User(name: "Ada", age: 36)
print text
match json.decode<User>(text) {
  Ok(user) => print user.name
  Fail(error) => print "Rejected: {error}"
}
```

## Declarations

### Json

```trb fragment
public type Json {
  naming: Naming = Naming.Unchanged
  strict: Bool = false

  fn encode<Value: Encode>(value: Value): String
  fn decode<Value: Decode>(text: String): Result<Value, JsonError>
  fn parse(text: String): Result<JsonValue, JsonError>
  fn value<Value: Encode>(value: Value): JsonValue
}
```

A `Json` is the format with its options: `naming` spells every field name (`Naming.SnakeCase` writes `logLabel` as
`log_label`, and reads it back), and `strict` makes a field of the input that no type asked for an error instead of
being ignored. `encode` renders any `Encode` value as JSON text; `decode<Value>` parses text and reads it as `Value`, and
fails on malformed JSON as well as on JSON that does not fit `Value`, with the field chain it went wrong in. `parse`
reads text into a `JsonValue` without knowing its shape; `value` is any `Encode` value as the `JsonValue` it would be
written as, for example to diff two versions of it structurally.

What a value is written as: a record is an object in declaration order, a record that announces no field is its one
value, a case with fields is `{"Case":{...}}` and one without is `"Case"`, a sequence is an array, a map is an object
whose keys are texts (a number key is quoted, and read back as a number), `None` is `null`, and bytes are an array of
numbers. A number without a fraction or an exponent is read as a whole number, so the whole range of `Int64` and of
`UInt64` round-trips exactly; infinity and "not a number" have no JSON and are written as `null`.

`extend Json with Format<JsonError>` additionally gives `Json.items<Item>()` and `Json.encoded<Item>()`, the two
`Stage`s that let a byte stream be decoded one element at a time (see std/stream (skill `torbscript-concurrency`: `references/standard-library/stream.md`)); a `[` at the start of
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

What went wrong turning text into a value: malformed JSON text (`syntax`, with the line it was found on), JSON that
parsed fine but did not fit the requested type (`decodeFailed`), or bytes that were not UTF-8 in the first place
(`invalidText`, from std/stream (skill `torbscript-concurrency`: `references/standard-library/stream.md`)'s `Utf8Error`). `isSyntaxError()` tells the first case from the other two;
`cause()` is the `DecodeError` or `Utf8Error` a rejected document failed on, so a report can unwind the chain down to
the field or the byte.

## Related

- [std/encoding](encoding.md) - `Encode`, `Decode`, `Naming` and `Format`, which `Json` implements.
- std/stream (skill `torbscript-concurrency`: `references/standard-library/stream.md`) - the `Stage`s `Json.items` and `Json.encoded` answer.
- [The standard library](index.md) - the other packages.

