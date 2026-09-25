---
title: Operators are traits
summary: An operator is a trait exactly when it is a method call, so writing one on your own type means implementing the trait it stands for - and the three that are no method call are the three that are not traits.
kind: reference
status: stable
order: 80
keywords:
  - operator overloading
  - Add
  - Power
  - precedence
  - Indexed
  - Slice
source:
  - CONCEPT.md#traits
  - std/core/src/operators.trb
  - examples/tour/src/05-traits.trb
---

> **Not built natively yet.** `??` on a type of your own is not built by the native back end yet, so `torb run` refuses
> the examples here that use it. `torb check` accepts them, and the rules are the language's.

An operator on a value of your own type is a method call in disguise: `a + b` is `a.add(b)`, and `a.add` exists only
where the type implements `Add`.

## Example

```trb check
type Vector2 with Add, Subtract, Negate, Multiply<Float> {
  x: Float
  y: Float

  fn add(other: Vector2): Vector2 {
    Vector2(x + other.x, y + other.y)
  }

  fn subtract(other: Vector2): Vector2 {
    Vector2(x - other.x, y - other.y)
  }

  fn negate(): Vector2 {
    Vector2(-x, -y)
  }

  fn multiply(other: Float): Vector2 {
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
a ** b       Power.power(a, b)
a == b       Equals.equals(a, b)         a < b   Compare.compare(a, b) == .Less
a[i]         Indexed.at(a, i)            a[i] = v    MutableIndexed.set(a, i, v)
a[from..to]  Slice.slice(a, from..to)    a[from..to] = v   MutableSlice.replace(a, from..to, v)
a ?? b       OrElse.orElse(a, b)         "{a}"   Show.show(a)
```

## Rules

1. **An operator is a trait exactly when it is a method call.** `+` is `Add.add`, `-` is `Subtract.subtract`,
   `*` is `Multiply.multiply`, `/` is `Divide.divide`, `%` is `Remainder.remainder`, unary `-` is `Negate.negate`,
   `==` is `Equals.equals`, `<`/`<=`/`>`/`>=` go through `Compare.compare`, `a ?? b` is `OrElse.orElse`, and string
   interpolation is `Show.show`. A type that does not come with the trait hears so at the operator:

   ```trb error
   const value = 1 ?? 0
   print value
   // error: `Int64` does not implement `OrElse`, so `a ?? b` has no meaning for it
   ```

2. **A binary operator's trait has a default `Other` and a default `Output`, both `Self`**
   (`trait Add<Other = Self, Output = Self>`), so `with Add` alone means "adds to itself, returns itself". Writing
   `Multiply<Float>` picks a different `Other` - `Vector2 * Float`, not `Vector2 * Vector2`.

3. **`a ** b` is `Power.power`, binds tighter than `*`, `/` and `%`, and groups to the right.** `2 * 3 ** 2` is
   `18`, and `2 ** 3 ** 2` is `2 ** 9`, which is how mathematics writes a tower. Its trait is
   `Power<Exponent = Self, Output = Self>`: every integer type is raised by an `Int` and a float by a float or by an
   `Int`, so `Int8 ** Int` is an `Int8`. On the integers a power is exact, panics on overflow the way `*` does, and
   panics on a negative exponent, whose result is no whole number; on a float it is IEEE-754 and never panics.

   ```trb run
   print(2 * 3 ** 2)
   print(2 ** 3 ** 2)
   print(2.0 ** -1)
   // prints 18
   // prints 512
   // prints 0.5
   ```

4. **A `-` directly in front of the base of `**` is an error.** Mathematics reads `-x ** 2` as `-(x ** 2)`, and a prefix
   operator binds tighter than every infix one, so a parser would read `(-x) ** 2`: the two differ in sign, and the
   language makes the author write the one that is meant. The same holds for `!`. The exponent may start with a `-`,
   because `x ** -2` means only one thing.

   ```trb error
   const x = 3
   const square = -x ** 2
   // error: A unary `-` directly in front of the base of `**` is ambiguous
   ```

5. **A type may carry one operator's trait more than once, and the right operand says which implementation the
   operator reaches.** That is the overload form the language keeps - a trait with a **parameter** - and the operator
   asks the same question the named call asks: a matrix that multiplies by a vector and by a number is two
   implementations of `Multiply`, and `matrix * vector` finds the one whose `Other` is a vector. Where the operand fits
   more than one, the type the expression is expected to produce decides; where it fits none, the operands that exist
   are named.

   ```trb check
   type Scale {
     factor: Int
   }

   type Board {
     size: Int
   }

   extend Board with Multiply<Scale, Board> {
     fn multiply(other: Scale): Board {
       Board(size * other.factor)
     }
   }

   extend Board with Multiply<Board, Board> {
     fn multiply(other: Board): Board {
       Board(size * other.size)
     }
   }

   const board = Board 2
   const scaled = board * Scale(3)
   const squared = board * board
   print "{scaled.size} {squared.size}"
   ```

   ```trb error
   type Scale {
     factor: Int
   }

   type Board {
     size: Int
   }

   extend Board with Multiply<Scale, Board> {
     fn multiply(other: Scale): Board {
       Board(size * other.factor)
     }
   }

   extend Board with Multiply<Board, Board> {
     fn multiply(other: Board): Board {
       Board(size * other.size)
     }
   }

   const wrong = Board(2) * "two"
   print wrong.size
   // error: `Board` does not multiply a `String`
   ```

6. **`a[i]` is `Indexed.at`, which panics if the key does not exist; `a.get(i)` is the same lookup, returning an
   `Option` instead.** `at` has a default body that calls `get`, so a type only ever has to write `get`.

7. **`a[i] = v` needs `MutableIndexed`, a supertrait of `Indexed`.** It also makes `a[i]` a `var` path:
   `enemies[0].health = 5` and `groups[key].append(value)` take the element out, change it and put it back, without a
   copy.

8. **`a[from..to]` is `Slice.slice` and shares the storage of `a`, starting at index `0` again.**
   `a[from..to] = v` needs `MutableSlice`, a supertrait of `Slice`, and also makes the range a `var` path:
   `samples[0..100].sort { _ }` works on that part of `samples` in place.

9. **`a ?? b` is `OrElse.orElse`, whose fallback is `lazy`.** `Option<Value>` and `Result<Value, Failure>` come with
   it, and so can a type of your own:

   ```trb check
   type Setting with OrElse<String> {
     written: String

     fn orElse(fallback: lazy String): String {
       if written.isEmpty() { fallback } else { written }
     }
   }

   print(Setting("") ?? "default")
   ```

10. **Three operators are no method call, and therefore no trait.** `&&`, `||` and `!` are built into `Bool` and
    short-circuit their second operand, which a trait method - which always evaluates its argument - cannot do. `?.` is
    `Option.map`, or `flatMap` when the member answers an `Option`, so which method it is depends on the *result* type; a
    trait for it would need `Self<Output>`, the higher-kinded form this language does not have. And `?` leaves the
    **enclosing function**, which is something no method can do at all.

    ```trb error
    type Flag {
      value: Bool
    }

    const a = Flag(true)
    const b = Flag(false)
    print(a && b)
    // error: Expected `Bool`, found `Flag`
    ```

## Precedence

From the loosest to the tightest. Every row but `**` and `??` groups to the left, and a comparison does not chain at
all (`a < b < c` is an error that suggests `&&`).

```text
||                          or
&&                          and
==  !=  <  <=  >  >=        comparisons, which do not chain
??                          fallback, to the right
..  ..=                     ranges
+  -                        sum and difference
*  /  %                     product, quotient, remainder
**                          power, to the right
-  !                        prefix: not directly on the base of **
.  ?.  ()  []  ?            member, call, index, try
```

## What this is not

**Not every symbol is an operator with a trait behind it.** `|` between two literals (`"tcp" | "udp"`) builds a
literal type, not `Add` or any other trait, and there are no bit operators at all - shifting and masking are the
named methods of `Bits` (`shiftedLeft`, `bitwiseAnd`), because a method needs no precedence rule and no new token.

**`^` is no operator either, on purpose.** Half of the readers take `a ^ b` for a power and the other half for
exclusive or, so the language writes neither with it: a power is `**` and exclusive or is `bitwiseExclusiveOr`, and the
message for a `^` says both.

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
- [Optional chaining](../errors/option-chaining.md) - `??` and `?.` in full, and where `?.` does not apply.

