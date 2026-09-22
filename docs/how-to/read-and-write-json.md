---
title: Read and write JSON
summary: Json.encode and Json.decode<T> work on any Encode/Decode type for free; write the pair by hand only for a type whose generated constructor cannot express what a document may contain.
kind: how-to
status: stable
order: 140
keywords:
  - Json
  - Encode
  - Decode
  - JsonValue
  - DecodeError
source:
  - std/json/src/lib.trb
---

> **Not built natively yet.** `Json.encode` is not built by the native back end yet, so `torb run` refuses the examples
> here that use it. `torb check` accepts them, and the rules are the language's.

`Json` reads and writes any type that implements `Encode`/`Decode`, and both are generated for an ordinary type - there
is nothing to write for the common case. Writing the pair by hand is for the type whose constructor cannot express
what a document may say, the same reason `parse` exists next to a plain constructor.

## Steps

1. **Encode and decode an ordinary type with nothing written for it.** `Encode` is generated whenever every field is
   `Encode`; `Decode` in addition when the constructor is usable from outside.

   ```trb fragment
   const text = Json.encode(User(name: "Ada", age: 36))
   const user = Json.decode<User>(text)?
   ```

2. **Match on `Json.decode`'s `Result` instead of using `?` where malformed input is expected, not a bug.** Both a
   syntax error and a document that does not fit the type answer the same `JsonError`.

   ```trb fragment
   match Json.decode<User>(text) {
     Ok(user) => print user.name
     Fail(error) => print "rejected: {error}"
   }
   ```

3. **Write `Encode` and `Decode` by hand only where a `private` field keeps the generated ones from working**,
   because a private field with no default makes the constructor unusable from outside. `encode` writes through
   `Encoder`'s methods, named after the built-in types; `decode` is a `static fn` that reads through `Decoder`
   and answers a `Result`.

   ```trb fragment
   extend Percent with Encode {
     fn encode(var encoder: Encoder) {
       encoder.int value
     }
   }

   extend Percent with Decode {
     static fn decode(var decoder: Decoder): Result<Percent, DecodeError> {
       const raw = decoder.int()?
       if raw < 0 || raw > 100 {
         return Fail DecodeError("{raw} is not a percent")
       }
       Ok Self(raw)
     }
   }
   ```

4. **Reach for `JsonValue` only for a document whose shape is not known ahead of time.** `Json.parse` answers one
   without needing a type at all; use it for a passthrough field or a preview, not for data your program already has
   a type for.

## Pitfalls

- **`Json.decode<Value>` fails on well-formed JSON that does not fit `Value`, not only on malformed text.** `isSyntaxError()`
  on the `JsonError` tells the two apart; `cause()` gives the `DecodeError` or `Utf8Error` underneath either way.
- **A field that is a `type` needs no extra work**, as long as that type is itself `Encode`/`Decode` - nesting is
  where the generated pair is most of the benefit, because a hand-written encoder would otherwise have to be written
  for every level.
- **Writing `Encode` without `Decode` is legal and common.** A type your program only ever sends outward has no
  reason to parse itself back.
- **`Decode.decode` is a `static fn`, not a method.** It takes the `Decoder` and answers `Self`; there is no
  `self` to call it on before a value exists.

## Full example

```trb check
type Percent {
  private value: Int

  static fn of(value: Int): Percent? {
    if value < 0 || value > 100 {
      return None
    }
    Some Self(value)
  }
}

extend Percent with Encode {
  fn encode(var encoder: Encoder) {
    encoder.int value
  }
}

extend Percent with Decode {
  static fn decode(var decoder: Decoder): Result<Percent, DecodeError> {
    const raw = decoder.int()?
    if raw < 0 || raw > 100 {
      return Fail DecodeError("{raw} is not a percent")
    }
    Ok Self(raw)
  }
}

const text = Json.encode(Percent.of(42)?)
print text

match Json.decode<Percent>(text) {
  Ok(percent) => print percent
  Fail(error) => print "rejected: {error}"
}
```

## Related

- [std/json](../standard-library/json.md) - `Json`, `JsonValue` and `JsonError` in full.
- [std/encoding](../standard-library/encoding.md) - `Encode`, `Decode`, `Encoder` and `Decoder`, which `Json` implements.
- [Parse text into a type](parse-text-into-a-type.md) - the same private-field-plus-factory shape, for text instead of JSON.
