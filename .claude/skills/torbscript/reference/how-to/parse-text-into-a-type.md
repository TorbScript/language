---
title: Parse text into a type
summary: Give the type a private field nothing outside can set directly, and a static parse factory that validates the text and answers a Result instead of a bare value.
kind: how-to
status: stable
order: 40
keywords:
  - parse
  - factory function
  - private field
  - Parse trait
source:
  - CONCEPT.md#construction
  - CONCEPT.md#conversions
---

A constructor never contains logic, so it cannot reject bad input. Parsing text into a type is a static factory next
to the constructor instead: a `private` field keeps the constructor from being called with unchecked text, and the
factory is the only way in.

## Steps

1. **Give the type a `private` field with no default.** Without a default, the generated constructor is unusable from
   outside the type - the only way to build a value from outside is through a function the type itself declares.

   ```trb fragment
   type Email {
     private value: String
   }
   ```

2. **Write a static function named `parse` that answers a `Result`.** It takes the text, checks it, and calls the
   constructor from inside the type, where every field - including `private` ones - can be passed.

   ```trb fragment
   fn parse(text: String): Result<Email, EmailError> {
     if !text.contains("@") {
       return Fail EmailError.NotAnAddress(text)
     }
     Ok Self(text)
   }
   ```

3. **Add a method to read the field back out.** A `private` field is invisible outside the type, so the type needs to
   say what it hands out - the parsed text itself, or something derived from it.

   ```trb fragment
   fn address(self): String {
     value
   }
   ```

4. **Implement `Parse<Failure>` when generic code should be able to call your factory the same way it calls
   `Int.parse` or `Status.parse`.** The trait asks for exactly the signature of step 2, so nothing changes but the
   `extend` line.

   ```trb fragment
   extend Email with Parse<EmailError> {
     fn parse(text: String): Result<Email, EmailError> {
       if !text.contains("@") {
         return Fail EmailError.NotAnAddress(text)
       }
       Ok Self(text)
     }
   }
   ```

5. **Declare a precise error type for what `parse` rejects**, rather than reusing `String`. A caller can then `match`
   on why the text was rejected instead of only being able to print it.

## Pitfalls

- **A field without `private` does not block the constructor.** `Email(text)` with a public field skips `parse`
   entirely, so nothing stops an unchecked value from being built - the field has to be `private` for the factory to be
   the only way in.
- **A `private` field cannot be reached from `copy` or the constructor outside the type either.** Both take the same
  fields, so a `private` field with no default disables both from outside, not only the constructor call written out
  in an example.
- **`parse` is a name, not a keyword.** Naming the factory something else works exactly the same way; `parse` is only
  what makes it implement `Parse<Failure>` in step 4 without renaming anything.
- **The factory is still an ordinary static function.** It has no special status until it implements `Parse`, so
  calling it is always `Email.parse(text)`, never a coercion the compiler inserts on its own.

## Full example

```trb check
type EmailError with Show, Error {
  case NotAnAddress(text: String)

  fn show(self): String {
    match self {
      .NotAnAddress(text) => "'{text}' is not an email address"
    }
  }
}

type Email {
  private value: String

  fn address(self): String {
    value
  }
}

extend Email with Parse<EmailError> {
  fn parse(text: String): Result<Email, EmailError> {
    if !text.contains("@") {
      return Fail EmailError.NotAnAddress(text)
    }
    Ok Self(text)
  }
}

fn describe(text: String): String {
  match Email.parse(text) {
    Ok(email) => "parsed: {email.address()}"
    Fail(problem) => "rejected: {problem}"
  }
}

print describe("info@example.test")
print describe("not an email")
```

## Related

- [Construction](../language/types/construction.md) - why the constructor cannot validate, and what a factory is for.
- [Conversions](../language/types/conversions.md) - `From`, `Into`, `TryFrom` and `Parse` side by side.
- [Fields](../language/types/fields.md) - `private`, and what a field without a default means for the constructor.
- [Define an error type](define-an-error-type.md) - shaping the failure a `parse` factory answers.
