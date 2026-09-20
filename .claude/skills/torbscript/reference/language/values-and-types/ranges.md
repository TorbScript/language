---
title: Ranges
summary: A Range has two optional ends and an inclusive flag; an open end is only checked when something asks for it, so iterating a range without a start or measuring one without both ends panics instead of failing to compile.
kind: reference
status: stable
order: 16
keywords:
  - Range
  - inclusive
  - open range
  - Iterable
source:
  - CONCEPT.md#built-in-types
  - std/core/src/range.trb
---

A `Range` is an ordinary value with two ends, each of which may be absent, and a flag for whether the end is
included. What an open end means is decided by whoever takes the range - a slice reads it as "to the start" or "to
the end of the value", `for` reads it as "no end at all".

## Example

```trb check
const bounded = 0..10
const inclusive = 0..=10

print bounded.contains(5)
print inclusive.contains(10)
print bounded.length()

for value in bounded {
  print value
}
```

## Syntax

```text
0..10                                     from 0 up to but not including 10
0..=10                                    from 0 up to and including 10
0..                                       from 0, no end
..10                                      no start, up to but not including 10
range.start  range.end                    each an Option<Value>
range.inclusive                           a Bool; an adjective, because it is data
```

## Rules

1. **`Range<Value>` has three fields: `start: Value?`, `end: Value?` and `inclusive: Bool`.** All four written forms
   are one type with different fields set; there is no separate type per form.

2. **`contains` needs `Value: Compare` and reads an absent end as unbounded.** A range with no start contains
   everything below its end, and a range with no end contains everything above its start.

3. **`Range<Int>` is `Iterable<Int>` and `Length`, but a range without a start has no first value and a range without
   both ends has no length.** `iterator()` panics for a range that is open at the start, `length()` panics for a
   range that is open at either end, and `0..` iterates forever rather than failing before the loop starts. This is
   checked at the point of use, not at compile time, because "has a start" is a property of a value, not of the type.

4. **`0..=10` sets `inclusive`; `0..` and `..10` leave the corresponding end `None`.** There is no fifth form and no
   way to make both ends open while also being inclusive of a nonexistent end.

5. **`Range`, like every built-in type, is declared in the prelude, not built into the grammar.** `0..10` is
   `Range(start: Some(0), end: Some(10))` written with an operator; the fields are ordinary fields.

## What this is not

**An open range is not a compile-time error waiting to happen.** `0..` type checks anywhere a `Range<Int>` is
expected; what happens depends on what the receiver does with the open end.

```trb check
const bounded = 0..10
print bounded.length()
```

```trb skip `length()` on an open range panics at runtime, which this documentation's gate does not execute
const openEnd = 0..
print openEnd.length()
```

**A range's inclusivity is not part of a separate type.** `0..10` and `0..=10` are both `Range<Int>`; `inclusive` is a
field like any other, readable and comparable, not a type-level tag.

## Related

- [Strings](strings.md) - `text[from..to]`, a `Range` passed to a slice.
- [Built-in types](built-in-types.md) - `Range` next to every other type that needs no import.
- [Integers](integers.md) - `Int`, the type `for value in 0..list.length()` counts through.
