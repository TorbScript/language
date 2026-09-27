---
title: Conversions
summary: From provides Into for free and TryFrom provides TryInto, text is a source like any other, and the language has exactly five coercions that apply only where a type is expected.
kind: reference
status: stable
order: 110
keywords:
  - From
  - Into
  - TryFrom
  - TryInto
  - TryInto
  - coercion
source:
  - CONCEPT.md#conversions
  - std/core/src/convert.trb
  - examples/tour/src/03-types.trb
---

A conversion between two types is a trait implementation, never a hidden rule of the language. `From` is the one to
write, or `TryFrom` where it can fail; `Into`, `TryInto`, the trait coercions and `Every type has From<Self>` all follow
from them without another line of code, and the fifth coercion wraps a value into the `Some` or the `Ok` that
is expected of it.

## Example

```trb
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

const boiling = Celsius.from Fahrenheit(212.0)
const freezing: Celsius = Fahrenheit(32.0).into()
print "{boiling.degrees} {freezing.degrees}"
```

## Syntax

```text
extend <Target> with From<Source> {
  static fn from(value: Source): Target { ... }
}

value.into()                          // Free, once From is implemented
Target.tryFrom(value)                 // Result<Target, Failure>
value.tryInto()                       // Free, once TryFrom is implemented
Target.tryFrom(text)                  // Text is a source like any other

extend <Foreign> with From<Mine> { ... }   // Your type into somebody else's
```

## Rules

1. **Implementing `From<Source>` provides `Into<Target>` for free.** `Fahrenheit(212.0).into()` exists because
   `Celsius` implements `From<Fahrenheit>`, not because anybody wrote `Into` by hand.

2. **A fallible conversion is `TryFrom`, and it answers a `Result`.** Where `From` cannot fail, `TryFrom` is for a
   conversion that can - a percentage that has to stay between 0 and 100 is `TryFrom<Int, String>`, not `From`.

   ```trb
   type Percent {
     value: Int

     static fn tryFrom(value: Int): Result<Percent, String> {
       if value < 0 || value > 100 {
         return Fail "{value} is not between 0 and 100"
       }
       Self value
     }
   }

   const percent = Percent.tryFrom 42
   print percent
   ```

   `TryFrom` provides `TryInto` the way `From` provides `Into`, and `tryInto()` reads both its target and its failure
   out of the `Result` that is expected of it:

   ```trb
   type Port {
     value: Int
   }

   extend Port with TryFrom<Int, String> {
     static fn tryFrom(value: Int): Result<Port, String> {
       if value < 1 {
         return Fail "{value} is not a port"
       }
       Ok Port(value)
     }
   }

   const port: Result<Port, String> = 8080.tryInto()
   print port.isOk()
   ```

   A `?` on the call says the target and not the failure, and the failure then follows from the one `TryFrom` the
   target has for this source: `const small: Port = 8080.tryInto()?` is the same conversion written in a chain. Where
   the target has several conversions from one source, the message says so and the `Result` has to be written out.

3. **A conversion from text is a `TryFrom<String, Failure>` like any other.** `Int.tryFrom "42"` and
   `Email.tryFrom "info@example.test"` both answer a `Result`, and there is no `parse` on a type: a function named
   `parse` belongs to a **format** (`Json().parse`), never to a value. A type may implement `TryFrom` once per source,
   and the call decides which one it means: the **argument**, and where the argument leaves several standing, the
   `Result` the call is expected to produce.

   ```trb check
   type Port {
     number: Int
   }

   extend Port with TryFrom<String, String> {
     static fn tryFrom(value: String): Result<Port, String> {
       Ok Port(1)
     }
   }

   extend Port with TryFrom<Int, String> {
     static fn tryFrom(value: Int): Result<Port, String> {
       Ok Port(value)
     }
   }

   const fromText = Port.tryFrom "8080"
   const fromNumber = Port.tryFrom 8080
   print "{fromText.isOk()} {fromNumber.isOk()}"
   ```

4. **Converting your own type into a foreign one is `extend Foreign with From<Mine>`.** A package owns an
   implementation when it owns the type, the trait, **or** a type named as an argument of the trait - so the package
   that declares `Celsius` may write `extend Float64 with From<Celsius>` although neither `Float64` nor `From` is
   its own. Only the top level of an argument counts: `From<List<Celsius>>` is not yours. See
   [Coherence](../traits/coherence.md).

   ```trb
   type Celsius {
     degrees: Float
   }

   extend Float64 with From<Celsius> {
     static fn from(value: Celsius): Float64 {
       value.degrees
     }
   }

   const degrees: Float64 = Celsius(21.5).into()
   print degrees
   ```

5. **Every type has `From<Self>`, generated without being written.** A blanket implementation could not be written by
   hand - it would overlap with every other `From` - which is what lets a function ask for `Item: From<Int>` and still
   accept an `Item` itself.

6. **The language has exactly five coercions, and every one of them applies only where a type is expected.** None of
   them decides what a bare expression means and none of them solves an inference variable on its own:

   - a value where a trait it implements is expected,
   - a trait value where fewer bounds or a supertrait is expected,
   - `Never` where anything is expected,
   - a literal where a literal type that contains it is expected,
   - a value where an `Option` or a `Result` of its type is expected (rule 8).

7. **`Into<Target>` is also a type, and `into()` on such a value is an ordinary call.** A parameter declared
   `Into<Path>` takes anything that converts into a `Path`, and `into` is the trait's one required member - the
   conversion through `From` is what happens for a receiver whose own type has no such member.

   ```trb
   type Path {
     text: String
   }

   fn open(path: Into<Path>): String {
     const target: Path = path.into()
     target.text
   }

   print open(Path("notes"))
   ```

8. **A value wraps itself into `Some` and `Ok` where one is expected.** A value of exactly `Value` where an
   `Option<Value>` or a `Result<Value, Failure>` is expected becomes `Some(value)` or `Ok(value)` - the result of a
   body and `return`, a binding with an annotation, an argument, a field, a collection element, the result of a closure
   that declares one. The coercion to a trait type applies first, so a `Square` where a `Shape?` is expected is boxed
   and wrapped. `None` and `Fail(...)` stay written.

   ```trb
   fn find(items: List<Int>, wanted: Int): Int? {
     for item in items {
       if item == wanted {
         return item
       }
     }
     None
   }

   fn halved(value: Int): Result<Int, String> {
     if value % 2 != 0 {
       return Fail "{value} is odd"
     }
     value / 2
   }

   const port: Int? = 8080
   const holes: List<Int?> = [1, None, 3]
   print "{find([1, 2, 3], 2)} {halved(8)} {port} {holes}"
   ```

   A value that already is an `Option` or a `Result` never wraps into an `Option`, so nothing becomes `Some(Some(x))`
   silently, and the payload of a written `Some(...)` or `Ok(...)` never wraps itself: the inner `Some` of a nested
   `Option` stays written. Into a `Result` a value wraps only where it is exactly the `Value` - an `Int?` becomes the
   `Ok` of a `Result<Int?, Failure>`.

   ```trb error
   const nested: Int?? = Some(1)
   // error: Expected `Option<Int64>`, found `Int64`
   ```

   The wrap never solves an inference variable: where the `Value` is still open - an argument of
   `fn wrapped<Item>(value: Item?)` - the value is checked as it is, and `Some` is written.

## What this is not

**`Into` and `TryInto` are not something you implement.** Each of them comes from a blanket implementation over the
other direction. A hand-written `Into` is refused by a rule of its own - `extend Celsius with Into<Float64>` is
"Implement `From<Celsius>` for `Float64` instead; `Into` comes from it", with the note "Write `extend Float64 with
From<Celsius>`" - because the `From` gives the `Into` for free while the `Into` gives nothing back, and coherence
allows the `From` wherever it would allow the `Into`. It is Rust's `from_over_into` lint, made a rule. A hand-written
`TryInto` overlaps its blanket, and that message names the `TryFrom` to write.

```trb error
type Celsius {
  degrees: Float
}

extend Celsius with Into<Float64> {
  fn into(): Float64 {
    degrees
  }
}
// error: Implement `From<Celsius>` for `Float64` instead; `Into` comes from it
```

```trb
extend Celsius with From<Fahrenheit> {
  static fn from(value: Fahrenheit): Celsius {
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
to another, only `Int.tryFrom("42")` written out. `Int` is an alias for `Int64`, which is the name the diagnostic uses.

## Related

- [Construction](construction.md) - the static factory functions `From` and `TryFrom` are examples of.
- [Declaring a type](declaring-a-type.md) - where `Self` and `extend` are introduced.
- [Result](../errors/result.md) - what every fallible conversion in this page answers with.
- [Coherence](../traits/coherence.md) - which package may write which implementation, and the blanket message.

