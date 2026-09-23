---
title: Parse text into a type
summary: Give the type a private field nothing outside can set directly, and implement TryFrom<String, Failure> so that Type.tryFrom(text) validates the text and answers a Result instead of a bare value.
kind: how-to
status: stable
order: 40
keywords:
  - parse
  - factory function
  - private field
  - TryFrom
source:
  - CONCEPT.md#construction
  - CONCEPT.md#conversions
---

A constructor never contains logic, so it cannot reject bad input. Reading text into a type is a conversion instead:
a `private` field keeps the constructor from being called with unchecked text, and `TryFrom<String, Failure>` is the
only way in. Text is a source like any other, so there is no trait and no member of its own for it.

## Steps

1. **Give the type a `private` field with no default.** Without a default, the generated constructor is unusable from
   outside the type - the only way to build a value from outside is through a function the type itself declares.

   ```trb fragment
   type Email {
     private value: String
   }
   ```

2. **Implement `TryFrom<String, Failure>`.** `tryFrom` takes the text, checks it, and calls the constructor from
   inside the type, where every field - including `private` ones - can be passed.

   ```trb fragment
   extend Email with TryFrom<String, EmailError> {
     static fn tryFrom(text: String): Result<Email, EmailError> {
       if !text.contains("@") {
         return Fail EmailError.NotAnAddress(text)
       }
       Ok Self(text)
     }
   }
   ```

3. **Add a method to read the field back out.** A `private` field is invisible outside the type, so the type needs to
   say what it hands out - the text itself, or something derived from it.

   ```trb fragment
   fn address(): String {
     value
   }
   ```

4. **Call it as `Email.tryFrom(text)`, or as `tryInto()` in a chain.** Under a `?` the annotation names the target
   alone and the failure follows from the implementation:

   ```trb fragment
   fn read(text: String): Result<Email, EmailError> {
     const address: Email = text.tryInto()?
     Ok address
   }
   ```

5. **Declare a precise error type for what the conversion rejects**, rather than reusing `String`. A caller can then
   `match` on why the text was rejected instead of only being able to print it.

## Pitfalls

- **A field without `private` does not block the constructor.** `Email(text)` with a public field skips the conversion
  entirely, so nothing stops an unchecked value from being built - the field has to be `private` for the conversion to
  be the only way in.
- **A `private` field cannot be reached from `copy` or the constructor outside the type either.** Both take the same
  fields, so a `private` field with no default disables both from outside, not only the constructor call written out
  in an example.
- **There is no `parse` on a type.** A function named `parse` belongs to a *format* (`Json().parse`), never to a value,
  and a call of one on a type reports that the type has no such member.
- **The conversion is still an ordinary static function.** Calling it is always `Email.tryFrom(text)`, never a
  coercion the compiler inserts on its own.

## Full example

```trb check
type EmailError with Show, Error {
  case NotAnAddress(text: String)

  fn show(): String {
    match self {
      .NotAnAddress(text) => "'{text}' is not an email address"
    }
  }
}

type Email {
  private value: String

  fn address(): String {
    value
  }
}

extend Email with TryFrom<String, EmailError> {
  static fn tryFrom(text: String): Result<Email, EmailError> {
    if !text.contains("@") {
      return Fail EmailError.NotAnAddress(text)
    }
    Ok Self(text)
  }
}

fn describe(text: String): String {
  match Email.tryFrom(text) {
    Ok(email) => "parsed: {email.address()}"
    Fail(problem) => "rejected: {problem}"
  }
}

print describe("info@example.test")
print describe("not an email")
```

## Related

- [Construction](../language/types/construction.md) - why the constructor cannot validate, and what a factory is for.
- [Conversions](../language/types/conversions.md) - `From`, `Into`, `TryFrom` and `TryInto` side by side.
- [Fields](../language/types/fields.md) - `private`, and what a field without a default means for the constructor.
- [Define an error type](define-an-error-type.md) - shaping the failure a conversion answers.
