---
title: Encode and Decode
summary: Encode and Decode are generated the same way Equals and Show are, so a type describes itself to any format's Encoder and reads itself from its Decoder without a line of hand-written serialization code.
kind: reference
status: stable
order: 20
keywords:
  - Encode
  - Decode
  - serialization
  - generated
source:
  - CONCEPT.md#types-values-and-reflection
  - std/encoding/src/lib.trb
  - examples/tour/src/11-data.trb
---

What reflection is normally needed for - serialization, config mapping, database rows, debug output - is covered by
one more generated pair, the same way `Equals`, `Hash` and `Show` are generated for a `type` without being written.

## Example

```trb check
use Json from "std/json"

type User {
  name: String
  tags: List<String> = []
}

const user = User name: "Ada", tags: ["math"]
const text = Json.encode user
print text
```

## Syntax

```text
trait Encode { fn encode(self, var encoder: Encoder) }
trait Decode { fn decode(var decoder: Decoder): Result<Self, DecodeError> }
```

## Rules

1. **`Encode` is generated for every `type` whose fields are all `Encode`.** A type describes itself to an `Encoder`
   in `encode`, and never sees the format it is written into.

2. **`Decode` is generated only if the constructor is usable from outside, and every field is `Decode`.** A type
   with a `private` field that has no default has invariants, so it writes `decode` by hand, going through the same
   factory the constructor would (see [Construction](../types/construction.md)).

   ```trb check
   use Json from "std/json"

   type ParseError {
     message: String
   }

   type Email {
     private value: String

     fn parse(text: String): Result<Email, ParseError> {
       if !text.contains("@") {
         return Fail ParseError("'{text}' is not an email address")
       }
       Ok Self(text)
     }
   }

   extend Email with Encode, Decode {
     fn encode(self, var encoder: Encoder) {
       encoder.string value
     }

     fn decode(var decoder: Decoder): Result<Email, DecodeError> {
       const text = decoder.string()?
       Email.parse(text).mapError { DecodeError _.message }
     }
   }

   const encoded = Json.encode(Email.parse("ada@example.test")?)
   print Json.decode<Email>(encoded)
   ```

3. **A format implements `Encoder` and `Decoder` and never sees a type.** `Json`, a TOML reader or a database driver
   is one implementation each; a type that describes itself once works with every format that exists, and a new
   format works with every type that exists, which is N + M implementations instead of N × M.

4. **What the generated code does is exactly what could be written by hand, and nothing about it is special.** A
   record calls `fields.field(name, value)` once per field in declaration order; a field with a default may be
   missing on the way in through `fieldOr`.

5. **A different field name, a skipped field, or a versioning scheme is written by hand.** There are no annotations:
   what is a convention of the format and not of the type is an option of the format instead
   (`Json.encode(user, naming: .SnakeCase)`).

6. **There is no tree in between.** Values are written while the type describes itself: nothing is lost on the way
   (a narrow number keeps its range, a `Set` comes back as a `Set` because the target type drives the decoding), and
   a sequence can be streamed one element at a time instead of being held in memory whole.

## What this is not

**`Encode`/`Decode` are not a second, dynamically typed representation of a value.** There is no "any value" type
in the language; looking at a document without knowing its shape uses an ordinary library type such as `JsonValue`,
which is itself only `Encode`/`Decode`, not something the language treats specially.

```trb check
use Json, JsonValue from "std/json"

type User {
  name: String
}

const user = User "Ada"
const value: JsonValue = Json.value user
print value
```

```trb error
type Handler {
  onClick: () => Void
}

const handler = Handler(onClick: {})
print Json.encode(handler)
// error: `Handler` does not implement `Encode`
```

A field that is a function breaks `Encode` the same way it breaks `Equals`, `Hash` and `Show`: a function value has
none of them, so the type gets none either.

## Related

- [There is no reflection](no-reflection.md) - why a generated pair replaces a type-level lookup.
- [Encoder and Decoder](encoders.md) - the four shapes and the six scalars every format implements.
- [Declaring a type](../types/declaring-a-type.md) - the other members generated the same way.
- [Construction](../types/construction.md) - why `Decode` needs a usable constructor.
