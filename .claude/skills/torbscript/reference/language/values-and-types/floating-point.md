---
title: Floating-point numbers
summary: On a Float every operator is IEEE-754 and `compare` is a total order that disagrees with them on `nan` and `-0.0`, and neither Float type is Hash.
kind: reference
status: stable
order: 12
keywords:
  - Float32
  - Float64
  - nan
  - infinity
  - isInfinite
  - isFinite
  - total order
  - isCloseTo
source:
  - CONCEPT.md#built-in-types
  - std/number/src/lib.trb
---

`Float32` and `Float64` follow IEEE-754, which means `==` on them is not an equivalence relation: `nan != nan`. The
language gives a `Float` three ways to compare and keeps them deliberately different, rather than picking one and
hiding the other two.

## Example

```trb check
const ratio: Float = 1.5

print "{ratio.floor()} {ratio.ceiling()} {ratio.round()} {ratio.squareRoot()}"
print "{0.0 == -0.0} {Float64.nan == Float64.nan} {Float64.nan.isNaN()}"
print "{Float64.infinity.isInfinite()} {ratio.isFinite()}"
print ratio.isCloseTo(1.5000001)
```

## Syntax

```text
Float32 Float64                          the two widths; `Float` is an alias for `Float64`
value.isCloseTo(other, tolerance: 0.000001)   comparison with a tolerance
Float64.pi  Float64.e                    two of the named constants
Float64.nan  Float64.infinity  Float64.negativeInfinity   the other three
value.isNaN()  value.isInfinite()  value.isFinite()        what the three constants are compared against
```

## Rules

1. **`Float` is an alias for `Float64`.** A decimal literal without an expected type is `Float64`; see
   [Literals](../syntax/literals.md).

2. **Every operator on a `Float` is exactly IEEE-754.** That is `==`, `!=`, `<`, `<=`, `>` and `>=`: `nan != nan` is
   true, every comparison with a `nan` on either side is `false`, and `0.0 == -0.0` is true.

   ```trb check
   print(Float64.nan == Float64.nan)
   print(Float64.nan < 1.5)
   print(Float64.nan > 1.5)
   print(0.0 == -0.0)
   ```

3. **`compare` is a total order, and it disagrees with every operator on both of their exceptions.** `compare` puts
   `nan` above every other value and treats `-0.0` as equal to `0.0`, so a sort over floats terminates whatever pivot it
   picks and whatever is in the list - it never needs a `nan` special case at the call site.

   A `Float` is the one type where an operator and the member behind it disagree, and that is why everything in the
   standard library that *orders* values - `sort`, `sorted`, `minBy`, `maxBy` - calls `compare` instead of writing `<=`.
   Only a total order is an order at all; an IEEE `<=` would leave a `nan` wherever it happened to start.

   ```trb check
   const mixed = [1.5, Float64.nan, 0.5]

   print mixed.sorted({ _ }).toList()
   ```

4. **`Float32` and `Float64` are deliberately not `Hash`.** A float can never be a `Map` key, and there is no `nan`
   key to worry about, because the type that would need one does not have the trait a key needs.

   ```trb error
   fn needsHash<Value: Hash>(value: Value): Value {
     value
   }

   const ratio: Float = 1.5
   print needsHash(ratio)
   // error: `Float64` does not implement `Hash`
   ```

5. **Where a tolerance is meant, `isCloseTo` says so in the name.** `a.isCloseTo(b, tolerance: 0.000001)` is
   `(a - b).absolute() <= tolerance`, with `0.000001` as the default tolerance.

6. **`squareRoot`, `floor`, `ceiling`, `round`, `isNaN`, `isInfinite` and `isFinite` are methods, not operators or free
   functions.** `pi`, `e`, `nan`, `infinity` and `negativeInfinity` are named constants of `Float64` (and, all but `pi`
   and `e`, of `Float32` too).

7. **`Float64.nan` and `Float32.nan` are the one way to write `nan` as a constant.** Every other constant expression
   that produces `nan` is a compile error where it is written
   ([Top-level code](../modules-and-packages/top-level-code.md), rule 6) - `nan` is not *computed* from one of these
   two, it is the language's own name for it.

   ```trb check
   print Float64.nan
   ```

   ```trb skip a module cannot be produced inside one snippet of this documentation, which is always checked as an unimported file, where a top-level 'const' is ordinary code and not a compile-time constant; the real diagnostic is: '/' produces 'nan' here, which a compile-time constant cannot hold
   const broken: Float64 = 0.0 / 0.0
   ```

8. **`Show` spells the three values that have no digits as words: `nan`, `inf` and `-inf`.** `nan` is written in
   lower case, the way the runtime and C write it and the way the constant is named (`Float64.nan`) - a name like any
   other, not the abbreviation `NaN`. Both back ends print the same three words.

   ```trb check
   print "{Float64.nan} {Float64.infinity} {Float64.negativeInfinity}"
   ```

## What this is not

**`Float32`/`Float64` not being `Hash` is enforced wherever a bound is written on a type, not only on a function.** A
bound on a *function's* type parameter is checked, as rule 4 shows, and the same bound written on a *type's* own
parameter (`HashMap<Key: Hash, Value>`) is checked against the type argument it is given too, so this is rejected:

```trb error
use HashMap from "std/collections"

const table: HashMap<Float, String> = HashMap()
print table.length()
// error: `Float64` does not implement `Hash`
```

**Comparing for closeness is not the same question as comparing for order.** `isCloseTo` answers "close enough for
this purpose", which needs a tolerance chosen by the caller; `compare` answers "which is greater", which needs an
answer for every pair including `nan`. Neither replaces the other.

## Related

- [Integers](integers.md) - the fixed-width types a `Float` never implicitly converts with.
- [Built-in types](built-in-types.md) - `Float` in the context of every other built-in type.
- [Declaring a type](../types/declaring-a-type.md) - how `Equals`, `Hash` and `Show` are generated for a type.

