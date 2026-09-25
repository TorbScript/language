---
title: Ranges
summary: The ends a range has are its type - Range, RangeFrom or RangeTo - so nothing is optional and nothing panics; what accepts every form takes the Bounds trait.
kind: reference
status: stable
order: 16
keywords:
  - Range
  - RangeFrom
  - RangeTo
  - Bounds
  - inclusive
source:
  - CONCEPT.md#built-in-types
  - std/core/src/range.trb
---

A range is an ordinary value, and **which ends it has is its type**: `a..b` is a `Range`, `a..` a `RangeFrom`, `..b` a
`RangeTo`. No field is optional in any of them, so `for index in ..10` and `(0..).length()` are refused where they are
written instead of failing at run time. What an open end means is decided by whoever takes the range - a slice reads it
as "from the start" or "to the end of the value" - and what accepts every form takes the `Bounds` trait.

## Example

```trb check
const bounded = 0..10
const inclusive = 0..=10
const endless = 3..
const upToTen = ..10

print bounded.contains(5)
print inclusive.contains(10)
print endless.contains(1000)
print upToTen.contains(9)
print bounded.length()

for value in bounded {
  print value
}
```

## Syntax

```text
0..10                                     Range<Int>: from 0 up to but not including 10
0..=10                                    Range<Int>: from 0 up to and including 10
0..                                       RangeFrom<Int>: from 0, no end
..10                                      RangeTo<Int>: no start, up to but not including 10
..=10                                     RangeTo<Int>: no start, up to and including 10

range.start  range.end                    the ends the type has, each a Value
range.inclusive                           a Bool on Range and RangeTo; an adjective, because it is data
```

## Rules

1. **One type per pair of ends.** `Range<Value>` has `start`, `end` and `inclusive`; `RangeFrom<Value>` has `start`;
   `RangeTo<Value>` has `end` and `inclusive`. Nothing is an `Option`, so a range that has no start has no `start` to
   read at all.

2. **`Range<Int>` is `Iterate<Int>` and `Length`, `RangeFrom<Int>` is `Iterate<Int>` and endless, `RangeTo<Int>` is
   neither.** A `for` over a range that has no start and a `length()` on a range that has no end are compile errors,
   each with the reason:

   ```trb error
   for index in ..10 {
     print index
   }
   // error: `RangeTo<Int64>` is not `Iterate`, so `for` cannot walk it
   ```

   ```trb error
   const endless = 0..
   print endless.length()
   // error: `RangeFrom<Int64>` has no member `length`
   ```

3. **`Bounds<Value>` is what every range is, and what accepts all three.** It answers `lowest()` and `highest()` as an
   `Option` each, `includesHighest()` as a `Bool`, and `contains` is its default. `Slice.slice` takes it, which is why
   all five spellings work in brackets:

   ```trb check
   const items = [1, 2, 3, 4, 5]
   print items[1..3]
   print items[1..=3]
   print items[..2]
   print items[3..]
   const text = "hello"
   print text[(text.indexOf("e") ?? text.start())..]
   ```

4. **A range of something other than an integer is bounds and nothing more.** `'a'..'z'` says what is inside of it,
   because there is no "the next value after `a`" in general - only `Int` ranges iterate.

5. **`inclusive` stays a field and does not go into the type.** `0..10` and `0..=10` are both `Range<Int>`, and
   `inclusive` is readable and comparable like any other field. Rust pulls it into the type and therefore has six range
   types; this has three, one per pair of ends, because that is the distinction a body actually has to make.

6. **The three types, like every built-in type, are declared in the prelude and not built into the grammar.** `0..10`
   is `Range(start: 0, end: 10)` written with an operator; the fields are ordinary fields.

## What this is not

**An open end is not a hole in the type system.** There is nothing to check at the point of use, because the type
already says which ends there are - and a body that needs a start takes a `Range` or a `RangeFrom` and lets the
signature refuse the rest.

**A range pattern is not a range value.** `4..=9 => "medium"` inside a `match` is syntax: it compares, it builds
nothing, and none of the three types is involved.

## Related

- [Strings](strings.md) - `text[from..to]`, a range passed to a slice.
- [Built-in types](built-in-types.md) - the ranges next to every other type that needs no import.
- [Integers](integers.md) - `Int`, the type `for value in 0..list.length()` counts through.
