---
title: Encode and Decode
summary: A value is its constructor call, offered in three forms - Encode writes it, Decode reads it back, Describe describes it without a value - so a type works with every format without hand-written serialization code.
kind: reference
status: stable
order: 20
keywords:
  - Encode
  - Decode
  - Describe
  - serialization
  - generated
source:
  - CONCEPT.md#types-values-and-reflection
  - std/encoding/src/lib.trb
  - examples/tour/src/11-data.trb
---

What reflection is normally needed for - serialization, config mapping, database rows, schemas, debug output - is
covered by the type's constructor, which the compiler offers in three generated forms, the same way `Equals`, `Hash`
and `Show` are generated for a `type` without being written.

## Example

```trb check
type User {
  name: String
  tags: List<String> = []
}

const json = Json()
const text = json.encode User(name: "Ada", tags: ["math"])
print text
print json.decode<User>(text)
```

## Syntax

```text
trait Encode   { fn encode<Target: Encoder>(var target: Target) }
trait Decode   { static fn decode<Source: Decoder>(var source: Source): Result<Self, DecodeError> }
trait Describe { static fn describe<Target: Describer>(var target: Target) }
```

## Rules

1. **All three are generated when the constructor is usable from outside, over exactly its parameters.** Every
   parameter's type has to have the trait itself. So what is written can always be read back: both forms are the one
   constructor.

2. **A `private` field with a default is not a parameter from outside, so it is in none of the three forms.** A cache
   or a memo stays out without an annotation; on the way in it takes its default. A `private(var)` field is publicly
   constructible and therefore part of all three.

3. **A capsule - a `private` field without a default - is written, read and described as the source of its one
   conversion pair.** `Self` has `From<Source>` or `TryFrom<Source, Failure>` and `Source` has `From<Self>`; a failing
   `tryFrom` becomes the `DecodeError`. Without a pair there is no `Decode` and no `Describe`, and `Encode` stays field
   by field (see [Data or capsule](../types/data-or-capsule.md)).

   ```trb check
   type ParseError {
     message: String
   }

   type Email with TryFrom<String, ParseError> {
     private value: String

     static fn tryFrom(text: String): Result<Email, ParseError> {
       if !text.contains("@") {
         return Fail ParseError("'{text}' is not an email address")
       }
       Ok Self(text)
     }
   }

   extend Email with Encode, Decode {
     fn encode<Target: Encoder>(var target: Target) {
       target.string value
     }

     static fn decode<Source: Decoder>(var source: Source): Result<Email, DecodeError> {
       const text = source.string()?
       Email.tryFrom(text).mapError { DecodeError _.message }
     }
   }

   const json = Json()
   const encoded = json.encode(Email.tryFrom("ada@example.test")?)
   print json.decode<Email>(encoded)
   ```

4. **A field with a default may be missing on the way in.** The derived `decode` evaluates the default only then, so
   a value that is present never pays for one.

5. **A format implements `Encoder`, `Decoder` or `Describer` and never sees a type.** `Json`, a TOML reader or a
   database driver is one implementation each; a type that describes itself once works with every format that exists,
   and a new format works with every type that exists, which is N + M implementations instead of N × M.

6. **What the generated code does is exactly what could be written by hand.** A record announces itself with
   `record(typeName)`, then every parameter as `field(name)` followed by its value's own `encode`, then `finish()`, in
   declaration order; a type with cases writes `variant(typeName, name)` for the case it is. `typeName` is the
   declaration's qualified name, such as `"app/orders/Price"`, which is what a format finds a mapping by.

7. **What is special about one type in one format belongs to the format.** A different spelling of every field name is
   an option of the format (`Json(naming: .SnakeCase)`); a different representation in every format is `encode` and
   `decode` written by hand. There are no annotations.

8. **There is no tree in between.** Values are written while the type describes itself: nothing is lost on the way
   (a narrow number keeps its range, a `Set` comes back as a `Set` because the target type drives the decoding), and
   a sequence can be streamed one element at a time instead of being held in memory whole.

9. **A literal type is read as its base and then checked against its members.** `"draft" | "sent"` is a `String` at run
   time, but its `decode` is its own: a text that is none of the members fails with the type's `LiteralParseError`, so
   a document cannot put a value into a literal type that `tryFrom` would refuse.

10. **A `DecodeError` names the whole chain it went wrong in.** A field follows a `.`, a position of a sequence or a
    tuple stands in brackets, an entry of a map follows a `.` with its key: `items[2].price: a value is needed`.

11. **A hand-written `encode`, `decode` or `describe` takes the format as a type parameter**, exactly as the trait
    declares it. `fn encode(var target: Encoder)` takes a trait-typed value instead and is another member: the checker
    says it does not match `Encode`.

## What this is not

**`Encode`/`Decode` are not a second, dynamically typed representation of a value.** Looking at a value without its
type uses `EncodedValue`, which is one ordinary format among many, and a document without a known shape uses a library
type such as `JsonValue`. Neither is something the language treats specially.

```trb check
use JsonValue from "std/json"

type User {
  name: String
}

const user = User "Ada"
const value: JsonValue = Json().value(user)
print value
print EncodedValue.of(user)
```

```trb error
type Handler {
  onClick: () => Void
}

const handler = Handler(onClick: {})
print Json().encode(handler)
// error: `Handler` does not implement `Encode`
```

A field that is a function breaks `Encode` the same way it breaks `Equals`, `Hash` and `Show`: a function value has
none of them, so the type gets none either.

## Related

- [There is no reflection](no-reflection.md) - why generated forms replace a type-level lookup.
- [Encoder and Decoder](encoders.md) - the scalars and the four shapes every format implements.
- [Declaring a type](../types/declaring-a-type.md) - the other members generated the same way.
- [Construction](../types/construction.md) - why the three forms need a usable constructor.

