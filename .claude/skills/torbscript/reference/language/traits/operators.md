---
title: Operators are traits
summary: Every operator except &&, || and ! is a trait method, so writing an operator on your own type means implementing the trait it stands for.
kind: reference
status: stable
order: 80
keywords:
  - operator overloading
  - Add
  - Indexed
  - Slice
source:
  - CONCEPT.md#traits
  - std/core/src/operators.trb
  - examples/tour/src/05-traits.trb
---

An operator on a value of your own type is a method call in disguise: `a + b` is `a.add(b)`, and `a.add` exists only
where the type implements `Add`.

## Example

```trb check
type Vector2 with Add, Subtract, Negate, Multiply<Float> {
  x: Float
  y: Float

  fn add(self, other: Vector2): Vector2 {
    Vector2(x + other.x, y + other.y)
  }

  fn subtract(self, other: Vector2): Vector2 {
    Vector2(x - other.x, y - other.y)
  }

  fn negate(self): Vector2 {
    Vector2(-x, -y)
  }

  fn multiply(self, other: Float): Vector2 {
    Vector2(x * other, y * other)
  }
}

const v = (Vector2(1.0, 2.0) + Vector2(3.0, 4.0)) * 2.0
print(-v)
```

## Syntax

```text
a + b        Add.add(a, b)          a - b        Subtract.subtract(a, b)
a * b        Multiply.multiply(a, b)     a / b   Divide.divide(a, b)
a % b        Remainder.remainder(a, b)   -a      Negate.negate(a)
a == b       Equals.equals(a, b)         a < b   Compare.compare(a, b) == .Less
a[i]         Indexed.at(a, i)            a[i] = v    MutableIndexed.set(a, i, v)
a[from..to]  Slice.slice(a, from..to)    a[from..to] = v   MutableSlice.replace(a, from..to, v)
"{a}"        Show.show(a)
```

## Rules

1. **Every operator except `&&`, `||` and `!` is one trait's method.** `+` is `Add.add`, `-` is `Subtract.subtract`,
   `*` is `Multiply.multiply`, `/` is `Divide.divide`, `%` is `Remainder.remainder`, unary `-` is `Negate.negate`,
   `==` is `Equals.equals`, `<`/`<=`/`>`/`>=` go through `Compare.compare`, and string interpolation is `Show.show`.

2. **A binary operator's trait has a default `Other` and a default `Output`, both `Self`**
   (`trait Add<Other = Self, Output = Self>`), so `with Add` alone means "adds to itself, returns itself". Writing
   `Multiply<Float>` picks a different `Other` - `Vector2 * Float`, not `Vector2 * Vector2`.

3. **`a[i]` is `Indexed.at`, which panics if the key does not exist; `a.get(i)` is the same lookup, returning an
   `Option` instead.** `at` has a default body that calls `get`, so a type only ever has to write `get`.

4. **`a[i] = v` needs `MutableIndexed`, a supertrait of `Indexed`.** It also makes `a[i]` a `var` path:
   `enemies[0].health = 5` and `groups[key].add(value)` take the element out, change it and put it back, without a
   copy.

5. **`a[from..to]` is `Slice.slice` and shares the storage of `a`, starting at index `0` again.**
   `a[from..to] = v` needs `MutableSlice`, a supertrait of `Slice`, and also makes the range a `var` path:
   `samples[0..100].sort()` works on that part of `samples` in place.

6. **`&&`, `||` and `!` are built into `Bool` and cannot be overloaded.** They short-circuit their second operand,
   which a trait method - which always evaluates its argument - cannot do.

   ```trb error
   type Flag {
     value: Bool
   }

   const a = Flag(true)
   const b = Flag(false)
   print(a && b)
   // error: Expected `Bool`, found `Flag`
   ```

## What this is not

**Not every symbol is an operator with a trait behind it.** `|` between two literals (`"tcp" | "udp"`) builds a
literal type, not `Add` or any other trait, and there are no bit operators at all - shifting and masking are the
named methods of `Bits` (`shiftedLeft`, `bitwiseAnd`), because a method needs no precedence rule and no new token.

```trb check
const mixed = (5).bitwiseAnd(3)
print mixed
```

```trb error
const mixed = 5 & 3
print mixed
// error: Expected the end of the statement, found `&`
```

## Related

- [Traits](traits.md) - `with` at the declaration, and default members.
- [Trait intersections](intersections.md) - the other job of `&`, unrelated to arithmetic.
- [Declaring a type](../types/declaring-a-type.md) - what `==` does without a hand-written `Equals`.
