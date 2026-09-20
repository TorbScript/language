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
  - std/collections/src/array.trb
---

`Array<Item, const Size: Int>` puts a number in a type: the length is part of what the type is, checked the same way
every other type argument is. A type parameter is ordinarily a type; a `const` parameter is a value instead - a
literal, a named `const`, or another `const` parameter.

## Example

```trb check
var identity: Array<Float, 4> = Array.filled 0.0
identity[0] = 1.0
print identity[0]
```

## Syntax

```text
Array<Item, const Size: Int>             the fixed-size array type
Array.filled(value)                      Size comes from the expected type
type Name<const Size: Int> { ... }       declaring a const parameter of your own
```

## Rules

1. **`Array` is a small value with a fixed layout: no heap storage, no reference count.** It lives inline, in a
   binding, a field, or another `Array`, and copying it copies its items. Everything that grows is a `List`.

2. **`Size` comes from the expected type, not from a value passed to `filled`.** `Array.filled 0.0` above produces
   an `Array<Float, 4>` because the annotation says so; the same call would produce a different length against a
   different annotation.

3. **A `const` parameter is `Int`, `Bool`, `Char` or `String`, and inside the type it is an ordinary constant.** A
   `const` parameter can be used anywhere a value of its type is needed within the declaration, such as bounding a
   `for` loop.

4. **A const argument is a literal, a named `const`, or another const parameter - never an expression.** There is no
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
   // error: `size` is a constant, not a type
   ```

5. **A generic type or function can itself take a `const` parameter**, written `<const Name: Type>` alongside its
   ordinary type parameters, and it is checked for equality across calls the same way a type parameter is.

## What this is not

**An out-of-bounds index that is known at compile time is not caught by the checker yet, even though CONCEPT.md's
design and `std/collections`'s own doc comment say it should be.** `identity` below is `Array<Float, 4>`, so index
`4` is out of range by one; the assignment type checks today.

```trb check
var identity: Array<Float, 4> = Array.filled 0.0
identity[0] = 1.0
print identity[0]
```

```trb check
var identity: Array<Float, 4> = Array.filled 0.0
identity[4] = 1.0
print identity[0]
```

Both blocks type check identically today. The second one is the exact example `std/collections/src/array.trb`'s own
doc comment gives as a compile error; only a runtime bounds panic - not a compile-time one - protects it at the
moment.

**`Array` is not `List` with extra syntax.** `Array` never grows and never allocates on the heap; a collection whose
size is not known ahead of time is a `List`, a different type entirely, not an `Array` used differently.

## Related

- [Built-in types](built-in-types.md) - `Array` next to every other type that needs no import.
- [Ranges](ranges.md) - `Range<Int>`, what a `for` loop over `0..Rows` counts through.
- [Distinct types](distinct-types.md) - a single-field type, the other place a type wraps a value directly.
