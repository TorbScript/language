---
title: Read and write JSON
summary: Json().encode and Json().decode<T> work on any Encode/Decode type for free; an option of the format spells the field names, and the pair is written by hand only where a constructor cannot say what a document may.
kind: how-to
status: stable
order: 140
keywords:
  - Json
  - Encode
  - Decode
  - JsonValue
  - DecodeError
  - SnakeCase
source:
  - std/json/src/lib.trb
---

`Json` reads and writes any type that implements `Encode`/`Decode`, and both are generated for an ordinary type - there
is nothing to write for the common case. `Json` is a value with the options of the format; writing the pair by hand is
for the type whose constructor cannot express what a document may say, the same reason `parse` exists next to a plain
constructor.

## Steps

1. **Encode and decode an ordinary type with nothing written for it.** Both are generated whenever the constructor is
   usable from outside and every parameter has them.

   ```trb fragment
   const json = Json()
   const text = json.encode(User(name: "Ada", age: 36))
   const user = json.decode<User>(text)?
   ```

2. **Match on `decode`'s `Result` instead of using `?` where malformed input is expected, not a bug.** Both a syntax
   error and a document that does not fit the type answer the same `JsonError`, and one of the second kind names the
   field chain it went wrong in (`price.currency: a value is needed`).

   ```trb fragment
   match json.decode<User>(text) {
     Ok(user) => print user.name
     Fail(error) => print "rejected: {error}"
   }
   ```

3. **Change how every field name is spelled with an option of the format, not with a word on the type.**
   `Json(naming: .SnakeCase)` writes and reads `logLabel` as `log_label`, and `Json(strict: true)` rejects a field of
   the input that no type asked for.

   ```trb fragment
   const snake = Json(naming: .SnakeCase)
   print snake.encode(setting)
   ```

4. **Write `Encode` and `Decode` by hand only where a `private` field keeps the generated ones from working**,
   because a private field with no default makes the constructor unusable from outside. `encode` writes through
   the `Encoder`'s methods, named after the built-in types; `decode` is a `static fn` that reads through the
   `Decoder` and answers a `Result`. Both take the format as a type parameter.

   ```trb fragment
   extend Percent with Encode {
     fn encode<Target: Encoder>(var target: Target) {
       target.int value
     }
   }

   extend Percent with Decode {
     static fn decode<Source: Decoder>(var source: Source): Result<Percent, DecodeError> {
       const raw = source.int()?
       if raw < 0 || raw > 100 {
         return Fail DecodeError("{raw} is not a percent")
       }
       Ok Self(raw)
     }
   }
   ```

5. **Reach for `JsonValue` only for a document whose shape is not known ahead of time.** `json.parse` answers one
   without needing a type at all; use it for a passthrough field or a preview, not for data your program already has
   a type for.

## Pitfalls

- **`decode` fails on well-formed JSON that does not fit the type, not only on malformed text.** `isSyntaxError()`
  on the `JsonError` tells the two apart; `cause()` gives the `DecodeError` or `Utf8Error` underneath either way.
- **A field that is a `type` needs no extra work**, as long as that type is itself `Encode`/`Decode` - nesting is
  where the generated pair is most of the benefit, because a hand-written encoder would otherwise have to be written
  for every level.
- **A field with a default may be missing from the document**, and takes its default; one without a default has to be
  there. A `private` field with a default is no parameter of the constructor from outside, so it is never written and
  never read.
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
  fn encode<Target: Encoder>(var target: Target) {
    target.int value
  }
}

extend Percent with Decode {
  static fn decode<Source: Decoder>(var source: Source): Result<Percent, DecodeError> {
    const raw = source.int()?
    if raw < 0 || raw > 100 {
      return Fail DecodeError("{raw} is not a percent")
    }
    Ok Self(raw)
  }
}

const json = Json()
const text = json.encode(Percent.of(42)?)
print text

match json.decode<Percent>(text) {
  Ok(percent) => print percent
  Fail(error) => print "rejected: {error}"
}
```

## Related

- [std/json](../standard-library/json.md) - `Json`, `JsonValue` and `JsonError` in full.
- [std/encoding](../standard-library/encoding.md) - `Encode`, `Decode`, `Encoder` and `Decoder`, which `Json` implements.
- [Parse text into a type](parse-text-into-a-type.md) - the same private-field-plus-factory shape, for text instead of JSON.
