---
title: Conversions
summary: From provides Into for free, TryFrom is for a conversion that can fail, Parse is for text, and the language has exactly four coercions that apply only where a type is expected.
kind: reference
status: stable
order: 110
keywords:
  - From
  - Into
  - TryFrom
  - Parse
  - coercion
source:
  - CONCEPT.md#conversions
  - std/core/src/convert.trb
  - examples/tour/src/03-types.trb
---

A conversion between two types is a trait implementation, never a hidden rule of the language. `From` is the one to
write; `Into`, the four coercions, and `Every type has From<Self>` all follow from it without another line of code.

## Example

```trb
type Celsius {
  degrees: Float
}

type Fahrenheit {
  degrees: Float
}

extend Celsius with From<Fahrenheit> {
  fn from(value: Fahrenheit): Celsius {
    Celsius((value.degrees - 32.0) / 1.8)
  }
}

const boiling = Celsius.from Fahrenheit(212.0)
const freezing: Celsius = Fahrenheit(32.0).into()
print "{boiling.degrees} {freezing.degrees}"
```

## Syntax

```text
extend <Target> with From<Source> {
  fn from(value: Source): Target { ... }
}

value.into()                          // Free, once From is implemented
Target.tryFrom(value)                 // Result<Target, Failure>
Target.parse(text)                    // Result<Target, Failure>, text specifically
```

## Rules

1. **Implementing `From<Source>` provides `Into<Target>` for free.** `Fahrenheit(212.0).into()` exists because
   `Celsius` implements `From<Fahrenheit>`, not because anybody wrote `Into` by hand.

2. **A fallible conversion is `TryFrom`, and it answers a `Result`.** Where `From` cannot fail, `TryFrom` is for a
   conversion that can - a percentage that has to stay between 0 and 100 is `TryFrom<Int, String>`, not `From`.

   ```trb
   type Percent {
     value: Int

     fn tryFrom(value: Int): Result<Percent, String> {
       if value < 0 || value > 100 {
         return Fail "{value} is not between 0 and 100"
       }
       Ok Self(value)
     }
   }

   const percent = Percent.tryFrom 42
   print percent
   ```

3. **A conversion from text is `Parse`, not `From` or `TryFrom`.** `Int.parse "42"` and `Email.parse
   "info@example.test"` both answer a `Result`, and the argument is always a `String`.

4. **Every type has `From<Self>`, generated without being written.** A blanket implementation could not be written by
   hand - it would overlap with every other `From` - which is what lets a function ask for `Item: From<Int>` and still
   accept an `Item` itself.

5. **The language has exactly four coercions, and every one of them applies only where a type is expected.** None of
   them decides what a bare expression means and none of them solves an inference variable on its own:

   - a value where a trait it implements is expected,
   - a trait value where fewer bounds or a supertrait is expected,
   - `Never` where anything is expected,
   - a literal where a literal type that contains it is expected.

6. **There is no implicit `Some`.** A value never wraps itself into an `Option` on its own; `Some(item)` has to be
   written out.

   ```trb error
   const bad: Int? = 1
   // error: Expected `Option<Int64>`, found `Int64`
   ```

## What this is not

**`Into` is not something you implement.** Writing `extend Fahrenheit with Into<Celsius>` next to a `From<Fahrenheit>`
on `Celsius` is redundant at best; `Into` exists automatically once `From` does, in the other direction, and there is
nothing to add to it.

```trb
extend Celsius with From<Fahrenheit> {
  fn from(value: Fahrenheit): Celsius {
    Celsius((value.degrees - 32.0) / 1.8)
  }
}

const freezing: Celsius = Fahrenheit(32.0).into()
```

```trb error
const value: Int = "42"
// error: Expected `Int64`, found `String`
```

A `String` never becomes an `Int` on its own either, for the same reason: there is no coercion from one concrete type
to another, only `Int.parse("42")` written out. `Int` is an alias for `Int64`, which is the name the diagnostic uses.

## Related

- [Construction](construction.md) - the static factory functions `TryFrom` and `Parse` are examples of.
- [Declaring a type](declaring-a-type.md) - where `Self` and `extend` are introduced.
- [Result](../errors/result.md) - what every fallible conversion in this page answers with.
