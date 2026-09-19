---
title: Cases and match
summary: A case is a variant of a type, written Type.Case or .Case and bare only when it is imported. A match is an expression and must cover every case.
kind: reference
status: stable
order: 10
keywords:
  - case
  - match
  - variant
  - exhaustive
  - imported case
source:
  - CONCEPT.md#algebraic-data-types-and-pattern-matching
  - CONCEPT.md#modules-and-packages
  - examples/tour/src/04-adts-and-matching.trb
---

A `case` inside a `type` declares a variant. `match` takes a value apart and is an expression, so it produces a value and
has to cover every case. How a case is spelled depends on one thing only: whether the file imports it.

## Example

```trb
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
  case Empty

  fn area(self): Float {
    match self {
      .Circle(radius) => math.pi * radius * radius
      .Rectangle(width, height) => width * height
      .Empty => 0.0
    }
  }
}

const shape = Shape.Circle 2.0
print shape.area()
```

## Syntax

```text
case <Name>                                  a case without fields
case <Name>(<field>: <Type>, ...)            a case with fields

Type.Case                                    an expression, always legal
.Case                                        an expression where the expected type is known
Case                                         an expression or a pattern, only when the case is imported

match <subject> {
  <pattern> [if <guard>] => <expression>
}
```

## Rules

1. **A case is written with its type (`Shape.Circle`) or with a leading dot (`.Circle`) where the type is known.** Never
   bare, unless the file imports it: `Circle` on its own is a type, a function or a binding of that name, like every other
   name.

2. **`.Case` works wherever a type is expected**: an annotation, an argument, a field, a result, either side of `==`, an
   arm of a `match` whose result type is known, and every pattern (where the type is the type of the value being matched).

   ```trb
   type Shape {
     case Circle(radius: Float)
     case Empty
   }

   const unit: Shape = .Circle(1.0)
   var shapes: List<Shape> = []
   shapes.add .Empty
   const other = Shape.Circle 1.0
   print "{unit} {shapes} {other}"
   ```

3. **A case is imported by its path**, and only a case can be imported through a type: `use Option.Some from "std/core"`.
   Without `from`, the path is resolved in the file's own scope (`use Shape.Circle`), which is what the file that declares
   `Shape` writes. There is no `use Option.*`.

4. **An imported case needs nothing in front of it, in an expression and in a pattern.** The prelude imports `Some`,
   `None`, `Ok` and `Fail`, which is the whole reason those four are written bare:

   ```trb
   fn describe(value: Int?): String {
     match value {
       Some(found) => "there is {found}"
       None => "nothing"
     }
   }

   print describe(Some(3))
   print describe(None)
   ```

5. **In a pattern the first letter decides.** A name that starts with a lowercase letter binds; a name that starts with an
   uppercase letter never binds - it is an imported case or a type read backwards. An uppercase binding is a compile
   error, so a misspelled case is reported instead of silently becoming a catch-all. `_` and a name starting with `_` keep
   their own rule and bind.

   ```trb error
   fn describe(value: Int?): String {
     match value {
       Nome => "nothing"
       _ => "something"
     }
   }
   // error: `Nome` is not a case in scope
   ```

6. **`match` is an expression and must be exhaustive.** There are no open or non-exhaustive types: a public type with
   cases is a promise, and adding a case is a breaking change the compiler points out at every `match`.

7. **An arm that can never be reached is an error.** Like a dead change and a discarded value, an unreachable arm is
   always a mistake rather than a defensive line.

8. **Fields are matched by position, and a label that is present has to name the field at that position.**
   `Point(y: 0, x: 1)` is an error, not a silent swap.

9. **Directly inside the braces of a `match`, a line that starts with `.` starts an arm.** Everywhere else a leading `.`
   continues the line above. So the value of an arm that spans a call chain goes into a block:

   ```trb
   type Shape {
     case Circle(radius: Float)
     case Empty
   }

   fn describe(shape: Shape): String {
     match shape {
       .Circle(radius) => {
         [radius]
           .map({ "{_}" })
           .joined(separator: "")
       }
       .Empty => "empty"
     }
   }

   print describe(Shape.Empty)
   ```

10. **Every pattern form is available**: a literal, alternatives with `|`, a range, a binding with a guard, a wildcard, a
    tuple, a type read backwards, and a list pattern with a rest.

    ```trb
    fn describe(value: Int): String {
      match value {
        0 => "zero"
        1 | 2 | 3 => "small"
        4..=9 => "medium"
        n if n < 0 => "negative"
        _ => "large"
      }
    }

    print describe(7)
    ```

11. **Patterns also stand in bindings and conditions**: `const Point(x, y) = p`, `if const Some(user) = findUser(id)`,
    `while const Some(next) = queue.dequeue()`, and `for (key, value) in someMap`.

12. **`if var P = place` binds into the place**, exactly like a `var` parameter, so the subject has to be a `var` path and
    the body runs inside a `var` access to it. A `var` pattern that bound a copy would be a dead change by construction.

13. **A case that wraps exactly one value of a type no other case of the type wraps generates `From`.** That is what makes
    error types cheap: `?` converts on its own, and nobody writes the conversion.

    ```trb fragment
    type AppError {
      case Config(cause: ConfigError)
      case Io(cause: IoError)
      case Startup(message: String)
    }
    ```

## What this is not

**A bare case name is not a case.** This is the single most common mistake for anybody coming from Rust, Swift or Kotlin:

```trb
type Shape {
  case Circle(radius: Float)
  case Empty
}

const shape = Shape.Circle 2.0
const empty: Shape = .Empty
print "{shape} {empty}"
```

```trb error
type Shape {
  case Circle(radius: Float)
  case Empty
}

const shape = Circle(2.0)
// error: Cannot find `Circle` here
```

The fix is `Shape.Circle 2.0`, or `use Shape.Circle` at the top of the file and then `Circle 2.0`.

**A pattern is not resolved by the expected type.** What a name in a pattern means hangs on the `use` at the top of the
file and on its first letter, and on nothing else. That is what keeps renaming a constant from turning an arm into a
catch-all.

**`match` has no `default` and no fallthrough.** `_` is the wildcard, every arm is one arm, and there is no `break`.

**A case with fields is not a class.** It has no methods of its own; methods belong to the type, and a method that behaves
differently per case matches on `self`.

## Related

- [Declaring a type](../types/declaring-a-type.md) - fields, methods and what is generated.
- [Result](../errors/result.md) - the case pair every fallible function answers with.
- [use](../../language/index.md) - how a case is imported.
- [Why a case is never bare](../../explanation/mistakes-models-make.md) - the argument, and the trap it closes.
- [Coming from Rust](../../explanation/coming-from-rust.md) - where `Some(x)` works and `Circle(x)` does not.
