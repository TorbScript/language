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

  static fn tryFrom(text: String): Result<Email, String> {
    if !text.contains("@") {
      return Fail "'{text}' is not an email address"
    }
    Ok Self(text)
  }

  fn address(): String {
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
   `nickname` at `""`. A field **without** a default is required, whatever its type: an optional field is `None` only
   when it is written `= None`, because "may be absent" and "may be left out of the call" are two different promises
   and only the declaration can make the second one.

   ```trb error
   type Person {
     name: String
     nickname: String?
   }

   const wrong = Person("Ada")
   // error: `Person` has no value for the field `nickname`
   ```

   The message says both ways out: write the absence at the call (`Person("Ada", nickname: None)`), or give the field
   a default and leave it out everywhere (`nickname: String? = None`).

   ```trb
   type Person {
     name: String
     nickname: String? = None
   }

   print Person("Ada")
   print Person("Alan", nickname: Some("al"))
   ```

3. **Positional arguments fill the fields from the left; a field with a default in the middle is passed over by
   naming the one behind it.**

   ```trb error
   type Server {
     host: String
     port: Int = 80
     retries: Int
   }

   const wrong = Server("a", 3)
   // error: `Server` has no value for the field `retries`
   ```

   ```trb
   type Server {
     host: String
     port: Int = 80
     retries: Int
   }

   print Server("a", retries: 3)
   ```

4. **A field default is a constant.** It is a literal, `None`, a collection literal of constants, a constructor or a
   case applied to constants, a named constant - a top-level `const`, a `static` value of a type - or an operator on
   those: what the initializer of a module's top-level `const` may be. A closure that captures nothing is one too,
   because the constructor only stores it. A call is not, a `static fn` included: `List.filled 16, None`, `Set.of()`
   and `limit.max(1)` run code, and the constructor has no body to run it in. A value that is computed comes from a
   `static fn` factory, and a collection starts from its empty literal - `[]` for a list, a `Set` or a queue, `[:]`
   for a map. A **parameter** default has no such rule and may be any expression, a call included
   ([Default values](../functions/default-values.md)): it runs at the call, which has a caller to run it in.

   ```trb error
   type Buffer {
     var slots: List<Int?> = List.filled 16, None
   }
   // error: A field default is a constant: `List.filled` is a call - compute it in a `static fn`, or start from `[]`
   ```

   ```trb
   type Buffer {
     var slots: List<Int?> = []
     var seen: Set<String> = []
     var counts: Map<String, Int> = [:]

     static fn withSlots(count: Int): Buffer {
       Buffer slots: List.filled(count, None)
     }
   }

   print Buffer.withSlots(3)
   ```

   A default never makes an object: a `shared type` has an identity, and every value needs one of its own, so that is
   a factory too. A default is read in a scope without `self` and without the other fields, so the order the fields are
   declared in is not observable from one. And a construction cannot fail, so a `?` in a default is refused - a value
   that has to be checked first comes from a factory that answers a `Result`.

   ```trb error
   fn parsed(text: String): Int? {
     None
   }

   type Probe {
     count: Int = parsed("1")?
   }
   // error: `?` cannot stand in a field default: building a value never fails
   ```

5. **The constructor is usable from outside the type if and only if every `private` field has a default.** A `private`
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

6. **Naming a `private` field is refused wherever it is named, including by position in the constructor.** Reaching
   the constructor and naming one of its `private` fields are two different permissions - the first rule is about
   which fields have to be left out, this one is about what happens when they are not.

   `private` reaches as far as the file that declares the type ([Fields](fields.md), rule 5), so the refusal is what
   another file gets - `ArrayQueue` of `std/collections` keeps its slots to itself:

   ```trb error
   use ArrayQueue from "std/collections"

   const wrong = ArrayQueue<Int>(head: 3)
   // error: `head` is private to `ArrayQueue<Int64>`, so it cannot be passed from here
   ```

7. **Inside the type, `Self(...)` is the constructor and every field can be passed**, `private` ones included - that is
   how `Email.tryFrom` builds the value nothing outside can.

8. **Everything that is not the constructor is a static factory function**: a member of the type declared `static`.
   `Email.tryFrom` answers a `Result` because a constructor cannot fail; a factory can, and a factory is where a value
   that is computed - a default the field cannot hold - comes from.

## What this is not

**A constructor is not a place for validation, parsing or conversion.** Those need to fail, and a constructor in a
language without exceptions cannot. They are static factory functions that return a `Result`, next to the
constructor rather than inside it.

```trb
type Percent {
  private value: Int

  static fn tryFrom(value: Int): Result<Percent, String> {
    if value < 0 || value > 100 {
      return Fail "{value} is not between 0 and 100"
    }
    Ok Self(value)
  }
}
```

From another file the constructor of such a type is not reachable at all, and the factory is the only way in:

```trb error
use Path from "std/path"

const wrong = Path(None, [])
// error: `Path` cannot be constructed here: `storedRoot` is private and has no default
```

## Related

- [Fields](fields.md) - `var`, `private` and `private(var)`, the modifiers a constructor's fields carry.
- [Maps and sets](../collections-and-iteration/maps-and-sets.md) - `[]` as the empty `Set`, the constant a field of one
  starts from.
- [Declaring a type](declaring-a-type.md) - fields, methods and what else is generated.
- [Copy and equality](copy-and-equality.md) - `copy`, which has the same shape as the constructor with every field
  optional.
- [Result](../errors/result.md) - what a factory function answers when construction can fail.

