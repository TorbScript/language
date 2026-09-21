---
title: Construction
summary: Every type has exactly one constructor, generated from its fields in declaration order, and it never contains logic - validation and parsing are static factory functions instead.
kind: reference
status: stable
order: 30
keywords:
  - constructor
  - factory
  - default value
  - Self
source:
  - CONCEPT.md#construction
  - examples/tour/src/03-types.trb
---

A `type` cannot declare a constructor of its own. The one it gets is built from its fields, in the order they are
declared, and answers a value or nothing else.

## Example

```trb
type Email with TryFrom<String, String> {
  private value: String

  fn tryFrom(text: String): Result<Email, String> {
    if !text.contains("@") {
      return Fail "'{text}' is not an email address"
    }
    Ok Self(text)
  }

  fn address(self): String {
    value
  }
}

const email = Email.tryFrom "info@example.test"
match email {
  Ok(address) => print "Parsed: {address.address()}"
  Fail(reason) => print "Rejected: {reason}"
}
```

## Syntax

```text
<Type>(<field>, ...)                 // Positional, in declaration order
<Type>(<name>: <value>, ...)         // Labelled, any order
Self(<field>, ...)                   // The constructor, from inside the type
```

## Rules

1. **The constructor takes every field, in declaration order, and nothing else.** It cannot be written by hand, and it
   never contains logic: there is no body to put an `if` or a `print` into.

   ```trb
   type Account {
     owner: String
     var nickname: String = ""
   }

   const first = Account "Ada"
   const second = Account owner: "Alan", nickname: "al"
   print "{first} {second}"
   ```

2. **A field with a default value can be left out of the call**, positionally or by label. `Account "Ada"` leaves
   `nickname` at `""`.

3. **A field default is evaluated at every construction, in a scope without `self` and without the other fields.** So
   the order the fields are declared in is not observable from a default, and a default that depends on another field
   has to be a factory function instead of a field default.

4. **The constructor is usable from outside the type if and only if every `private` field has a default.** A `private`
   field with a default can be left out, and leaving it out is the only way outside code ever reaches this
   constructor.

   ```trb
   type Session {
     token: String
     private hits: Int = 0
   }

   const session = Session token: "abc"
   print session.token
   ```

5. **Naming a `private` field is refused wherever it is named, including by position in the constructor.** Reaching
   the constructor and naming one of its `private` fields are two different permissions - the first rule is about
   which fields have to be left out, this one is about what happens when they are not.

   ```trb error
   type Session {
     token: String
     private hits: Int = 0
   }

   const wrong = Session(token: "abc", hits: 3)
   // error: `hits` is private to `Session`, so it cannot be passed from here
   ```

6. **Inside the type, `Self(...)` is the constructor and every field can be passed**, `private` ones included - that is
   how `Email.tryFrom` builds the value nothing outside can.

7. **Everything that is not the constructor is a static factory function**: a function declared on the type that does
   not take `self`. `Email.tryFrom` answers a `Result` because a constructor cannot fail; a factory can.

## What this is not

**A constructor is not a place for validation, parsing or conversion.** Those need to fail, and a constructor in a
language without exceptions cannot. They are static factory functions that return a `Result`, next to the
constructor rather than inside it.

```trb
type Percent {
  private value: Int

  fn tryFrom(value: Int): Result<Percent, String> {
    if value < 0 || value > 100 {
      return Fail "{value} is not between 0 and 100"
    }
    Ok Self(value)
  }
}
```

```trb error
type Percent {
  private value: Int

  fn tryFrom(value: Int): Result<Percent, String> {
    if value < 0 || value > 100 {
      return Fail "{value} is not between 0 and 100"
    }
    Ok Self(value)
  }
}

const wrong = Percent(120)
// error: `Percent` cannot be constructed here: `value` is private and has no default
```

## Related

- [Fields](fields.md) - `var`, `private` and `private(var)`, the modifiers a constructor's fields carry.
- [Declaring a type](declaring-a-type.md) - fields, methods and what else is generated.
- [Copy and equality](copy-and-equality.md) - `copy`, which has the same shape as the constructor with every field
  optional.
- [Result](../errors/result.md) - what a factory function answers when construction can fail.
