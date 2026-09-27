---
title: Built-in types
summary: Every type a file has without an import - the sized numbers, Bool, Char, String, tuples, lists, maps, ranges, Option, function types, Void and Never.
kind: reference
status: stable
order: 5
keywords:
  - prelude
  - primitive types
  - overview
source:
  - CONCEPT.md#built-in-types
---

A new file starts with more than nothing: the numeric types, `Bool`, `Char`, `String`, tuples, `List`, `Map`,
`Range`, `Option`, function types, `Void` and `Never` are all in scope without a `use`. This page is the map of that
set; each type's own page has its rules.

## Example

```trb check
const someInt = 10
const someFloat = 3.14
const someChar = 'A'
const someString = "Hello"
const someBool = true
const someTuple = (1, "one")
const someList = [1, 2, 3]
const someMap = ["a": 1, "b": 2]
const someRange = 0..10
const someOption: Int? = None
const someFunction = { x: Int => x * 2 }

print "{someInt} {someFloat} {someChar} {someString} {someBool}"
print "{someTuple} {someList} {someMap} {someRange}"
print "{someOption} {someFunction(3)}"
```

## Syntax

```text
Int Int8 Int16 Int32 Int64                   signed integers; `Int` is an alias for `Int64`
UInt UInt8 UInt16 UInt32 UInt64               unsigned integers; `UInt` is an alias for `UInt64`
Float Float32 Float64                         floating point; `Float` is an alias for `Float64`
Decimal                                       exact base-ten arithmetic
Bool Char String                              a truth value, a Unicode scalar value, UTF-8 text
Void Never                                    the type with one value, the type with none
(Int, String)                                 a tuple
(lowest: Int, highest: Int)                   a labelled tuple; the labels are not part of the type
List<Item> Map<Key, Value> Set<Item>          the collection traits
Range<Int>                                    `0..10`, `0..=10`
RangeFrom<Int>, RangeTo<Int>                  `0..`, and `..10`/`..=10`
Value?                                        `Option<Value>`
(value: Int) => Int                           a function type
Array<Item, const Size: Int>                  a fixed-size array; the size is part of the type
```

## Rules

1. **A numeric type always carries its width.** `Int8` through `Int64`, `UInt8` through `UInt64`, `Float32` and
   `Float64` are the widths that exist; there is no platform-dependent integer type. See
   [Integers](integers.md) and [Floating-point numbers](floating-point.md).

2. **`Int`, `UInt` and `Float` are aliases for the 64-bit width, declared in the prelude like any other alias.** An
   integer literal without an expected type is `Int64`, and a literal with a fraction or an exponent is `Float64` -
   fixed, and independent of the aliases. See [Type aliases](type-aliases.md).

3. **A tuple's label is not part of its type.** `(lowest: Int, highest: Int)` and `(Int, Int)` are one type, and
   either may be used where the other is expected. See [Tuples](tuples.md).

4. **`Value?` is `Option<Value>`, the one way absence is written.** There is no `null` and no `nil`; a value where an
   `Option` of its type is expected becomes `Some(value)` on its own. See [Option](option.md).

5. **`Void` has exactly one value, the keyword literal `void`; `Never` has none and converts to every type.** See
   [Void and Never](void-and-never.md).

6. **An empty collection literal needs its element type from an annotation.** `[]` and `[:]` carry no element type of
   their own, so `const empty: List<String> = []` needs the annotation and `const empty = []` does not type check.

   ```trb error
   const empty = []
   print empty
   // error: Cannot infer the type of this collection
   ```

7. **`List`, `Map` and `Set` are traits, not one concrete implementation each.** A literal produces the standard
   implementation, and a signature that only needs the trait should name the trait. Which implementations exist is a
   different page's subject.

## What this is not

**A built-in type is not a keyword.** `Int`, `String` and `List` are ordinary names declared in the prelude, the same
way a name from any other package is - they can be shadowed by a file's own `type Int = Int32`, and they are looked
up the same way a project's own types are.

```trb check
const count: Int = 5
print count
```

```trb check
type Int = Int32

const count: Int = 5
print count
```

The second block is legal today: shadowing a prelude name is allowed, even though it means integer *literals* stay
`Int64` regardless (rule 2), so `Int` and the literal type disagree inside that file.

## Related

- [Bindings](bindings.md) - `const` and `var`, and how a type annotation changes what a literal is.
- [Integers](integers.md) - the eight sized integer types, their ranges, and what overflow does.
- [Floating-point numbers](floating-point.md) - `Float32`, `Float64`, and the three ways to compare them.
- [Tuples](tuples.md) - positional and labelled tuples in full.
- [Option](option.md) - `Value?`, `Some` and `None`.

