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
const nan = 0.0 / 0.0

print "{ratio.floor()} {ratio.ceiling()} {ratio.round()} {ratio.squareRoot()}"
print "{0.0 == -0.0} {nan == nan} {nan.isNaN()}"
print ratio.isCloseTo(1.5000001)
```

## Syntax

```text
Float32 Float64                          the two widths; `Float` is an alias for `Float64`
value.isCloseTo(other, tolerance: 0.000001)   comparison with a tolerance
Float64.pi  Float64.e                    the two named constants
```

## Rules

1. **`Float` is an alias for `Float64`.** A decimal literal without an expected type is `Float64`; see
   [Literals](../syntax/literals.md).

2. **Every operator on a `Float` is exactly IEEE-754.** That is `==`, `!=`, `<`, `<=`, `>` and `>=`: `nan != nan` is
   true, every comparison with a `nan` on either side is `false`, and `0.0 == -0.0` is true.

   ```trb check
   const nan = 0.0 / 0.0
   print(nan == nan)
   print(nan < 1.5)
   print(nan > 1.5)
   print(0.0 == -0.0)
   ```

3. **`compare` is a total order, and it disagrees with every operator on both of their exceptions.** `compare` puts
   `nan` above every other value and treats `-0.0` as equal to `0.0`, so a sort over floats terminates whatever pivot it
   picks and whatever is in the list - it never needs a `nan` special case at the call site.

   A `Float` is the one type where an operator and the member behind it disagree, and that is why everything in the
   standard library that *orders* values - `sort`, `sorted`, `minBy`, `maxBy` - calls `compare` instead of writing `<=`.
   Only a total order is an order at all; an IEEE `<=` would leave a `nan` wherever it happened to start.

   ```trb check
   const nan = 0.0 / 0.0
   const mixed = [1.5, nan, 0.5]

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

6. **`squareRoot`, `floor`, `ceiling`, `round` and `isNaN` are methods, not operators or free functions.** `pi` and
   `e` are named constants of `Float64`.

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
