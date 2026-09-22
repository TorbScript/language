---
title: Convert between types
summary: Implement From when the conversion cannot fail and TryFrom when it can - text is a source like any other - and call to<Target>() to collect a pipeline into any type built from one.
kind: how-to
status: stable
order: 100
keywords:
  - From
  - Into
  - TryFrom
  - TryInto
  - to
source:
  - CONCEPT.md#conversions
---

> **Not built natively yet.** A conversion through `From` is not built by the native back end yet, so `torb run` refuses
> the examples here that use it. `torb check` accepts them, and the rules are the language's.

Every conversion in the language is one of four traits, chosen by whether it can fail and in which direction it is
written. There is no separate cast syntax: implementing the trait is the whole job, and the call site is the same shape
every time.

## Steps

1. **Implement `From<Source>` on the target type when the conversion cannot fail.** Nothing needs writing on `Source`
   at all.

   ```trb fragment
   extend Celsius with From<Fahrenheit> {
     static fn from(value: Fahrenheit): Celsius {
       Celsius((value.degrees - 32.0) / 1.8)
     }
   }
   ```

2. **Call it as `Target.from(value)`, or as `value.into()` where the target type is already known.** `Into` is
   generated for every `From` implementation, so nothing else has to be written for the second form.

   ```trb fragment
   const a = Celsius.from(Fahrenheit(100.0))
   const b: Celsius = Fahrenheit(100.0).into()
   ```

3. **Implement `TryFrom<Source, Failure>` when the conversion can reject its input.** The shape is the same as `From`,
   with a `Result` in place of a bare value.

   ```trb fragment
   extend Percent with TryFrom<Int, PercentError> {
     static fn tryFrom(value: Int): Result<Percent, PercentError> {
       if value < 0 || value > 100 {
         return Fail PercentError.OutOfRange(value)
       }
       Ok Self(value)
     }
   }
   ```

4. **Text is a source like any other: `TryFrom<String, Failure>` is how a value is read from it.** A type may
   implement `TryFrom` several times, once per source, and the argument decides which one a call means -
   `Int.tryFrom("42")` reads text and `Int.tryFrom(3.0)` narrows a number. [Parse text into a
   type](parse-text-into-a-type.md) is this step on its own, with the constructor rules it depends on.

5. **Collect a pipeline into any type at all with `to<Target>()`, not only a `List`.** Every collection implements
   `From<Iterate<Item>>`, so `to<Target>()` works for `Set`, `Map` (of a pipeline of pairs), or a type of your own
   that implements the same trait.

   ```trb fragment
   const unique: Set<String> = names.to()
   ```

## Pitfalls

- **`.into()` needs the target type from context.** `const b: Celsius = Fahrenheit(100.0).into()` works because the
  binding's type says what `Into` converts to; `const b = Fahrenheit(100.0).into()` has nothing to infer it from and
  is rejected.
- **A coercion is not a call to `From`, and does not need one written.** A value converting to a trait it implements,
  a trait value converting to fewer bounds, `Never` converting to anything, and a literal converting to a literal type
  all happen only where a type is already expected - there is no `.into()` for any of the four, and writing one is a
  sign the value already fits without it.
- **There is no variance.** `Square` converting to `Shape` does not make `List<Square>` a `List<Shape>` - convert the
  items, not the collection, with `squares.map<Shape> { square => square }.toList()` when a `List<Shape>` is really
  needed; `map`'s own output type has to be named, because nothing about `List<Square>` on its own says a `Shape` is
  wanted.
- **Every type has `From<Self>` for free, and it is never written by hand.** A blanket implementation for it would
  overlap with every other `From` a type declares, so the compiler treats it as a fact about every type instead - it
  is what lets a function bound by `where Item: From<Int>` accept an `Int` argument directly.

## Full example

```trb check
type Celsius {
  degrees: Float
}

type Fahrenheit {
  degrees: Float
}

extend Celsius with From<Fahrenheit> {
  static fn from(value: Fahrenheit): Celsius {
    Celsius((value.degrees - 32.0) / 1.8)
  }
}

type PercentError with Show, Error {
  case OutOfRange(value: Int)

  fn show(): String {
    match self {
      .OutOfRange(value) => "{value} is not between 0 and 100"
    }
  }
}

type Percent {
  private value: Int
}

extend Percent with TryFrom<Int, PercentError> {
  static fn tryFrom(value: Int): Result<Percent, PercentError> {
    if value < 0 || value > 100 {
      return Fail PercentError.OutOfRange(value)
    }
    Ok Self(value)
  }
}

const boiling: Celsius = Fahrenheit(212.0).into()
print boiling

match Percent.tryFrom(120) {
  Ok(percent) => print percent
  Fail(problem) => print "rejected: {problem}"
}

const names = ["Ada", "Alan", "Ada", "Grace"]
const unique: Set<String> = names.to()
print unique.length()
```

## Related

- [Conversions](../language/types/conversions.md) - `From`, `Into`, `TryFrom`, the four coercions and `From<Self>` in full.
- [Parse text into a type](parse-text-into-a-type.md) - `TryFrom<String, Failure>` with the private field that makes it
  the only way in.
- [Pipelines](../language/collections-and-iteration/pipelines.md) - `to<Target>()` alongside the rest of a pipeline's
  terminal operations.

