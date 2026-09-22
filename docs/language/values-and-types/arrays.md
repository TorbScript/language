---
title: Arrays and const parameters
summary: Array<Item, const Size> carries its length in the type, a const parameter is a value rather than a type, and there is no arithmetic over one.
kind: reference
status: stable
order: 23
keywords:
  - Array
  - const parameter
  - fixed size
source:
  - CONCEPT.md#const-parameters-and-array
  - std/core/src/array.trb
---

> **Not built natively yet.** An `Array` value, from `Array.filled` or from a literal, is not built by the native back
> end yet, so `torb run` refuses the examples here that use it. `torb check` accepts them, and the rules are the
> language's.

`Array<Item, const Size: Int>` puts a number in a type: the length is part of what the type is, checked the same way
every other type argument is. A type parameter is ordinarily a type; a `const` parameter is a value instead - a
literal, a named `const`, or another `const` parameter.

`Array` is the **inline storage primitive** of the language, not one more collection, and that is why it lives in
`std/core` next to `Option`, `Result` and `Range` rather than in `std/collections`: the language itself refers to it,
because a list literal against an expected `Array` type is written straight into its slots.

## Example

```trb check
var identity: Array<Float, 4> = Array.filled 0.0
identity[0] = 1.0
print identity[0]
```

## Syntax

```text
Array<Item, const Size: Int>             the fixed-size array type
const a: Array<Int, 3> = [1, 2, 3]       a list literal written into the inline slots
const a: Array<Int, 3> = [...half, 3]    a spread of another Array, whose size is known
Array.filled(value)                      Size comes from the expected type
Array.generated({ index => ... })        one item per slot, from its index
Array.from(items)                        an Option: the count is checked at run time
type Name<const Size: Int> { ... }       declaring a const parameter of your own
```

## Rules

1. **`Array` is a small value with a fixed layout: no heap storage, no reference count.** It lives inline, in a
   binding, a field, or another `Array`, and copying it copies its items. Everything that grows is a `List`.

2. **`Size` comes from the expected type, not from a value passed to `filled`.** `Array.filled 0.0` above produces
   an `Array<Float, 4>` because the annotation says so; the same call would produce a different length against a
   different annotation. There is nothing else for it to come from, and a call that has no expected type says so:

   ```trb error
   const zeros = Array.filled 0.0
   print zeros
   // error: Cannot infer `Size` of `Array`
   ```

3. **Every way to build one writes its size down.** A list literal is counted against `Size`; a `...` inside one is
   allowed when what it spreads is itself an `Array`, and the sizes add up; `filled` and `generated` take `Size` from
   the expected type; `from` counts at run time and answers an `Option`. There is no factory whose argument count
   *becomes* the size, because no signature could say that.

   ```trb check
   const half: Array<Int, 2> = [1, 2]
   const whole: Array<Int, 3> = [...half, 3]
   const squares: Array<Int, 3> = Array.generated { index => index * index }
   const parsed: Array<Int, 3>? = Array.from([1, 2, 3])
   print "{whole} {squares} {parsed}"
   ```

   A count that disagrees with the annotation names both numbers:

   ```trb error
   const whole: Array<Int, 4> = [1, 2, 3]
   print whole
   // error: `Array<Int64, 4>` has 4 items, and this literal has 3
   ```

4. **A `const` parameter is `Int`, `Bool`, `Char` or `String`, and inside the type it is an ordinary constant.** A
   `const` parameter can be used anywhere a value of its type is needed within the declaration, such as bounding a
   `for` loop.

5. **A const argument is a literal, a named `const`, or another const parameter - never an expression.** There is no
   arithmetic in types: the checker only compares const arguments for equality.

   ```trb check
   const size = 4
   var buffer: Array<Float, size> = Array.filled 0.0
   print buffer[0]
   ```

   ```trb error
   const size = 4
   var buffer: Array<Float, size + 1> = Array.filled 0.0
   print buffer[0]
   // error: A type argument is a type or the name of a constant, never an expression
   ```

6. **A generic type or function can itself take a `const` parameter**, written `<const Name: Type>` alongside its
   ordinary type parameters, and it is checked for equality across calls the same way a type parameter is.

7. **`Equals`, `Hash` and `Show` follow the items, and the first two are order-dependent.** Two arrays are equal when
   the items at the same index are; the length is part of the type, so two arrays that compare at all have the same one
   and there is nothing to test about it. An Array shows like a `List` (`[1, 2]`): the size is in its type and a reader
   can count.

   ```trb check
   const first: Array<Int, 2> = [1, 2]
   const same: Array<Int, 2> = [1, 2]
   const other: Array<Int, 2> = [2, 1]
   print(first == same)
   print(first == other)
   ```

## What this is not

**An out-of-bounds index that is written out as a literal is caught by the checker, not only at run time.** `identity`
below is `Array<Float, 4>`, so index `4` is out of range by one, and the assignment is rejected before the program
runs.

```trb check
var identity: Array<Float, 4> = Array.filled 0.0
identity[0] = 1.0
print identity[0]
```

```trb error
var identity: Array<Float, 4> = Array.filled 0.0
identity[4] = 1.0
print identity[0]
// error: `4` is out of bounds: an `Array<Float64, 4>` has 4 items
```

A negative index is the same mistake, and so is an index into a collection literal that stands right there - both are
known without running anything, and `get(index)` is the form that answers an `Option` instead of panicking.

```trb error
print([1, 2, 3][5])
// error: `5` is out of bounds: this literal has 3 items
```

**`Array` is not `List` with extra syntax.** `Array` never grows and never allocates on the heap; a collection whose
size is not known ahead of time is a `List`, a different type entirely, not an `Array` used differently.

## Related

- [Built-in types](built-in-types.md) - `Array` next to every other type that needs no import.
- [Ranges](ranges.md) - `Range<Int>`, what a `for` loop over `0..Rows` counts through.
- [Distinct types](distinct-types.md) - a single-field type, the other place a type wraps a value directly.
