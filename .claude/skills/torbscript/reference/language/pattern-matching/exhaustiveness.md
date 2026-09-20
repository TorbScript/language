---
title: Exhaustiveness
summary: A match has to cover every value of its subject, and an arm that no value can reach is a compile error, not a defensive line.
kind: reference
status: stable
order: 30
keywords:
  - exhaustive
  - unreachable arm
  - witness
  - non-exhaustive
source:
  - CONCEPT.md#algebraic-data-types-and-pattern-matching
---

`match` is an expression, so it has to produce a value for every input, and the checker proves that before the program
runs: it never falls through, and there is no `default` to fall back on.

## Example

```trb error
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
  case Empty
}

fn area(shape: Shape): Float {
  match shape {
    .Circle(radius) => 1.0
  }
}
print area(Shape.Empty)
// error: `match` does not handle `.Rectangle(_, _)` and `.Empty`
```

## Syntax

```text
match <subject> {
  <pattern1> => <expression>
  <pattern2> => <expression>
  ...
}
```

There is no syntax for exhaustiveness itself: it is a property the checker computes from the patterns and the type of
`<subject>`, not something written down.

## Rules

1. **A `match` is exhaustive when its arms without a guard cover every value the subject's type can hold.** The
   checker builds this from the type: every case of a `type` with cases, every value of an integer range, both of
   `Bool`, the one shape of a tuple or a type without cases.

2. **A public type with cases is a promise, and the checker treats it as closed.** There is no `#[non_exhaustive]` and
   no open enum: adding a case to a public type is a breaking change, and every `match` over it that the checker finds
   incomplete is where that break shows up. A library that wants to stay free to add cases later does not expose the
   type with cases; see [Coming from Rust](../../explanation/coming-from-rust.md) for the wrapper it exposes instead.

3. **A missing case is reported by example, up to three of them, with a count behind that.** The message names the
   values a `match` does not handle so a reader can copy one straight into a new arm.

   ```trb error
   fn describe(value: Int): String {
     match value {
       n if n < 0 => "negative"
     }
   }
   print describe(1)
   // error: `match` does not handle `_`
   ```

4. **An arm with a guard is never counted towards exhaustiveness**, on its own or together with every other arm,
   because the guard's condition could always be false. A `match` that only has guarded arms is never exhaustive, even
   if the guards happen to cover every case between them.

5. **An arm that no value can reach is a compile error.** An arm is unreachable when every value it could match is
   already matched by an arm above it: with value semantics a branch nothing reaches is always a mistake, never a
   defensive line, exactly as a change nothing reads afterwards is one.

   ```trb error
   fn describe(value: Int): String {
     match value {
       _ => "large"
       0 => "zero"
     }
   }
   print describe(1)
   // error: This arm is never reached
   ```

6. **The message names what already covers the unreachable arm.** One pattern above that alone makes the arm
   unreachable is named (`` `_` above already matches everything`` when it is the wildcard, `` `.Empty` above already
   matches it`` for a case); where only several arms together do, the note says that instead of listing them.

7. **A pattern that stands where there is no second arm has to match every value on its own**: the pattern of a
   `const`/`var` binding, of a `for` loop, and of a closure parameter. One that cannot is reported with the same kind
   of example an unreachable arm's note gives, naming what it does not match.

   ```trb error
   fn total(pairs: List<List<Int>>): Int {
     var sum = 0
     for [first, second] in pairs {
       sum = sum + first + second
     }
     sum
   }
   print total([[1, 2]])
   // error: The pattern of a `for` has to match every item, and `[]` does not
   ```

## What this is not

**Exhaustiveness is not a runtime check.** Nothing about it costs a cycle once the program runs: every value is
accounted for at compile time, so there is no hidden `else` branch and no possibility of a `match` falling through
silently. Contrast with a `switch` in Java, C# or TypeScript, where a missing `default` compiles and reaching the end
without a match is either a silent no-op or an exception thrown at that point.

```trb
fn area(shape: Shape): Float {
  match shape {
    .Circle(radius) => Float.pi * radius * radius
    .Rectangle(width, height) => width * height
    .Empty => 0.0
  }
}

type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
  case Empty
}

print area(Shape.Empty)
```

```trb error
type Shape {
  case Circle(radius: Float)
  case Rectangle(width: Float, height: Float)
  case Empty
}

fn area(shape: Shape): Float {
  match shape {
    .Circle(radius) => Float.pi * radius * radius
    .Rectangle(width, height) => width * height
  }
}
print area(Shape.Empty)
// error: `match` does not handle `.Empty`
```

**A `match` with only a wildcard is not exhaustive by accident; it is exhaustive because `_` covers everything.** That
is different from forgetting the cases and writing `_` to silence the checker - the checker cannot tell those apart,
which is exactly why [Cases and match](cases-and-match.md) makes an uppercase name that is not a case an error rather
than a lenient catch-all: the one place a program can genuinely ignore the rest is the wildcard, written on purpose.

## Related

- [Cases and match](cases-and-match.md) - how a case is written, and why a bare name is never one by accident.
- [Pattern forms](pattern-forms.md) - every pattern, including the ones a range and a list pattern do not fully cover.
- [Patterns in bindings and conditions](patterns-in-bindings.md) - `const`, `for`, `if const` and the irrefutable rule.
- [Coming from Rust](../../explanation/coming-from-rust.md) - the wrapper type a library uses to stay free to add cases.
