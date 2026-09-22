---
title: Pattern forms
summary: Every pattern the language has, from a literal to a list pattern with a rest, one form per line.
kind: reference
status: stable
order: 20
keywords:
  - wildcard
  - guard
  - alternatives
  - rest pattern
  - tuple pattern
  - list pattern
  - unread binding
source:
  - CONCEPT.md#algebraic-data-types-and-pattern-matching
  - examples/tour/src/04-adts-and-matching.trb
---

A pattern stands in a `match` arm, in a binding, in a condition and in a `for` loop, and every one of the forms below
works in every one of those places. What a pattern matches against is always the type of the value it stands next to.

## Example

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

## Syntax

```text
<literal>                         an exact value: a number, a string, a character, `true`, `false`, a case with none
<pattern> | <pattern> | ...       any of the alternatives; every alternative binds the same names, with the same types
<low>..<high>                     a half-open range
<low>..=<high>                    an inclusive range
<name>                            binds the value; a lowercase first letter, and the arm has to read it
_name                             binds and keeps the name as documentation; nothing has to read it
<name> if <condition>             binds, and requires the guard to hold as well
_                                  matches anything, binds nothing
(<pattern>, <pattern>, ...)       a tuple, matched by position
Type(<pattern>, <pattern>)        a case of the matched type, or the one constructor of `Type`, field by field
Type(<field>: <pattern>)          the same, with the field named; the labeled ones follow the positional ones
Type(<pattern>, ...)              a trailing `...` stands for every field the pattern does not name
[<pattern>, ...]                  a list of exactly that many items
[<pattern>, ..., ...rest]         a list of at least that many items; `rest` binds what is between as a `List`
```

## Rules

1. **A literal pattern matches only that exact value.** A number, a string, a character, `true`, `false`, and a member
   of a [literal type](../values-and-types/literal-types.md) are all literals.

   ```trb
   fn isZero(value: Int): Bool {
     match value {
       0 => true
       _ => false
     }
   }

   print isZero(0)
   ```

2. **Alternatives (`|`) match when any one of them does, and every alternative has to bind the same names with the
   same types.** The arm's body sees one binding, whichever alternative matched.

   ```trb
   type Shape {
     case Circle(radius: Float)
     case Square(side: Float)
   }

   fn length(shape: Shape): Float {
     match shape {
       .Circle(radius) | .Square(radius) => radius
     }
   }

   print length(Shape.Circle(2.0))
   ```

   ```trb error
   type Shape {
     case Circle(radius: Float)
     case Square(side: Float)
   }

   fn describe(shape: Shape): String {
     match shape {
       .Circle(radius) | .Square(side) => "{radius}"
     }
   }
   print describe(Shape.Circle(1.0))
   // error: `radius` is bound by the first alternative and not by this one
   // error: `side` is bound by this alternative and not by the first one
   ```

3. **A range pattern (`a..b`, `a..=b`) matches the values between its ends.** Over a built-in integer type the checker
   turns the range into the interval it covers, and a `match` that covers every interval and every remaining value is
   exhaustive. Over any other comparable type - `Char`, `Float`, a type with `Compare` - the pattern still matches, but
   it does not by itself make a `match` exhaustive, because the checker does not enumerate that type's values:

   ```trb error
   fn kind(letter: Char): String {
     match letter {
       'a'..='z' => "lower"
     }
   }
   print kind('b')
   // error: `match` does not handle `_`
   ```

4. **A binding pattern (`name`) binds the matched value under that name.** A guard (`if <condition>`) may follow any
   pattern, and the arm matches only when the guard holds too. A guarded arm never counts towards exhaustiveness, on its
   own or together with others, because the guard could always turn out false:

   ```trb error
   fn describe(value: Int): String {
     match value {
       n if n < 0 => "negative"
     }
   }
   print describe(1)
   // error: `match` does not handle `_`
   ```

5. **A binding of a refutable pattern has to be read.** In an arm of a `match`, in an `if const`/`if var` and in a
   `while const`, a name the guard or the body never reads is an error: write `_`, or `_name` to keep the name as
   documentation. This is what makes rule 4's binding a choice instead of a trap - see "What this is not" below.

   ```trb error
   fn describe(value: Int?): String {
     match value {
       Some(number) => "something"
       None => "nothing"
     }
   }
   print describe(Some(1))
   // error: `number` is never read: write `_`, or `_number` to keep the name
   ```

   ```trb check
   fn describe(value: Int?): String {
     match value {
       Some(_reason) => "something"
       None => "nothing"
     }
   }

   print describe(Some(1))
   ```

6. **`_` matches anything and binds nothing.** It is the pattern form, not a name; the same underscore in an
   expression is the implicit closure parameter, and the two never overlap because a pattern and an expression are
   never the same position.

7. **A tuple pattern `(p1, p2, ...)` matches a tuple position by position**, and the number of positions has to equal
   the number of fields of the tuple's type.

   ```trb
   fn quadrant(point: (Int, Int)): String {
     match point {
       (0, 0) => "origin"
       (x, y) if x > 0 && y > 0 => "first"
       _ => "elsewhere"
     }
   }

   print quadrant((1, 1))
   ```

8. **`Type(...)` matches a case of the matched type, or reads the one constructor of a type backwards.** A pattern
   mirrors the constructor: a sub-pattern without a label fills the next field from the left, a labeled one names the
   field it matches, and the labeled ones follow the positional ones - the very rule the arguments of a call follow.

   ```trb check
   type Config {
     host: String
     port: Int
     secure: Bool
   }

   fn described(config: Config): String {
     match config {
       Config(host, 443, true) => "{host}, secure"
       Config(host, port, secure: false) => "{host}:{port}"
       _ => "?"
     }
   }

   print described(Config("a", 443, true))
   ```

9. **A pattern that does not name every field ends in `...`.** The `...` stands for the fields behind the ones that
   are named, it decides nothing, and it comes last and comes once. Without it a field added to a type would break
   every pattern over that type; with it a pattern says which of the two it means - the fields I need, or all of them.

   ```trb check
   type Config {
     host: String
     port: Int
     secure: Bool
   }

   fn described(config: Config): String {
     match config {
       Config(port: 443, ...) => "the secure port"
       Config(host, ...) => "over {host}"
     }
   }

   print described(Config("a", 80, false))
   ```

   ```trb error
   type Config {
     host: String
     port: Int
     secure: Bool
   }

   fn hostOf(config: Config): String {
     match config {
       Config(host) => host
     }
   }
   print hostOf(Config("a", 80, false))
   // error: `Config` has 3 fields and this pattern names 1
   ```

10. **A list pattern `[p1, p2, ...]` matches a list of exactly that many items.** `[p1, ...rest]` matches a list of at
   least that many, and `rest` binds everything from that position on as a `List` of the item type. An item can also
   follow the rest - `[first, ...middle, last]` - in which case it is matched counting from the back, and the pattern
   needs a `_` arm alongside it: the checker does not fold "at least this many, from both ends" into the exhaustiveness
   it tracks for lists of a fixed or minimum length.

   ```trb check
   fn summarize(items: List<Int>): String {
     match items {
       [] => "nothing"
       [only] => "one: {only}"
       [first, ...middle, last] => "{first}..{last}, {middle.length()} between"
       _ => "?"
     }
   }

   print summarize([1, 2, 3, 4])
   ```

## What this is not

**A pattern is not resolved against the value it is compared with; it is resolved against the scope and the first
letter of what is written.** A lowercase name always binds, however the value came to be, and an uppercase name is
always a case or a type - never a comparison with a constant of that name. That first letter is not a convention a
reader has to trust: the checker guarantees it at every declaration, so a type is spelled `Limit` and a constant
`limit` wherever either was written - see [Naming](../syntax/naming.md):

```trb
const limit = 10

fn describe(value: Int): String {
  match value {
    n if n == limit => "at the limit"
    _ => "elsewhere"
  }
}

print describe(10)
```

```trb error
const limit = 10

fn describe(value: Int): String {
  match value {
    limit => "at the limit"
    _ => "elsewhere"
  }
}
print describe(10)
// error: `limit` is never read: write `_`, or `_limit` to keep the name
// error: This arm is never reached
```

`limit` inside the pattern is a new binding that shadows the constant and matches every value, not a comparison with
it - which is exactly why renaming a constant can never silently turn an arm into a catch-all. The arm above gets a
second message for the same reason (`_ => "elsewhere"` is never reached), but the binding is reported **even when the
arm is the last one** and nothing is unreachable:

```trb error
const limit = 10

fn describe(value: Int): String {
  match value {
    0 => "zero"
    limit => "at the limit"
  }
}
print describe(10)
// error: `limit` is never read: write `_`, or `_limit` to keep the name
```

That is rule 5 doing its job: the arm is a catch-all, and the only thing that would make the binding look deliberate
is reading it. Compare with a guard instead: `n if n == limit => "at the limit"`.

**A range pattern is not a container check.** `4..=9` in a pattern is unrelated to `4..=9` as a value: the pattern
compares the matched value against the two ends, it does not ask whether the value is inside a `Range` object.

## Related

- [Cases and match](cases-and-match.md) - the case pattern in full, and the imported-case rule.
- [Exhaustiveness](exhaustiveness.md) - why every `match` has to cover every value, and what counts.
- [Patterns in bindings and conditions](patterns-in-bindings.md) - where else besides `match` a pattern stands.
- [Ranges](../values-and-types/ranges.md) - `Range` as a value, as opposed to a range pattern.
- [Naming](../syntax/naming.md) - the rule the first letter of a pattern name follows from.
