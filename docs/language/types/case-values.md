---
title: Cases that stand for numbers
summary: A type whose cases have no fields may give each of them a fixed number, case Read = 1, and gets rawValue() and fromRawValue generated - the way to a C enum, a protocol code, a column and a Flags mask.
kind: reference
status: stable
order: 15
keywords:
  - case value
  - rawValue
  - fromRawValue
  - RawValue
  - enum
  - Flags
  - bit mask
source:
  - docs/design/FLAGS.md
  - compiler/src/semantics/checker/case-value.trb
  - std/core/src/convert.trb
  - std/collections/src/flags.trb
---

A number from outside - a C enum, a protocol code, a database column - is read into a type whose cases say what it
means, and a `match` over that type stays exhaustive. The number is written at the case, the language generates the
two directions, and nothing else about the type changes.

## Example

```trb run
type Permission {
  case Read = 1
  case Write = 2
  case Execute = 4
}

print Permission.Write.rawValue() // prints 2
print Permission.fromRawValue(4) // prints Some(Execute)
print Permission.fromRawValue(3) // prints None
```

## Syntax

```text
case <Name> = <integer literal>
```

## Rules

1. **All cases of the type have a number, or none has.** A type that numbers some of its cases and not others would
   have cases `rawValue()` cannot answer:

   ```trb error
   type Permission {
     case Read = 1
     case Write
   }

   print Permission.Read
   // error: `Write` has no value, and the other cases of `Permission` have one
   ```

2. **A case with fields stands for no number.** A number is one value, and a case with fields is many.

3. **Every number is an integer literal**, in any of the forms an integer literal has: `-1`, `0x10`, `1_000`. The
   compiler reads it; nothing is evaluated, so a constant is not one.

4. **No two cases stand for the same number**, or `fromRawValue` could not say which one it means:

   ```trb error
   type Permission {
     case Read = 1
     case Write = 2
     case All = 2
   }

   print Permission.Read
   // error: `All` stands for 2, which `Write` stands for already
   ```

5. **The type gets `RawValue` generated**: `fn rawValue(): Int`, the number of the case, and
   `static fn fromRawValue(value: Int): Self?`, the case of a number or `None`. The trait is in the prelude, so a
   function can take any such type as a bound: `fn code<Value: RawValue>(value: Value): Int`.

6. **A number is an `Int`.** A narrower type for the number is not written at the case; the conversion to one is the
   ordinary `UInt8.tryFrom(value.rawValue())`.

7. **A set of such cases is a `Flags<Case>`** of `std/collections`, stored as one mask: every case of its type
   argument stands for a power of two, which the checker holds wherever `Flags<Case>` is written.

   ```trb run
   use Flags from "std/collections"

   type Permission {
     case Read = 1
     case Write = 2
     case Execute = 4
   }

   var access: Flags<Permission> = [.Read, .Write]
   access.insert(.Execute)
   print access.contains(.Write) // prints true
   print access.bits() // prints 7
   print Flags<Permission>.fromBits(5) // prints Ok({Read, Execute})
   ```

## What this is not

**It is not a C enum.** A value of `Permission` is always one of its three cases; there is no `Permission` that holds
the number 3, and a number that no case stands for is `None` at the boundary rather than a value that goes through a
`match` unmatched. A combination of cases is a `Flags<Permission>`, a type of its own.

**It is not an ordering.** The numbers say nothing about `<`: a type gets no `Compare` from them, as no type gets one
from its structure.

## Related

- [Cases and match](../pattern-matching/cases-and-match.md) - what a case is, and why a `match` names every one.
- [Declaring a type](declaring-a-type.md) - the one keyword every data type is declared with.
- [Conversions](conversions.md) - `From` and `TryFrom`, the other way between a type and a number.
