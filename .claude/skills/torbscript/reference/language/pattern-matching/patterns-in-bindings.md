---
title: Patterns in bindings and conditions
summary: A pattern also stands after const and var, in the head of if and while, and in a for loop - the same vocabulary as a match arm, without the braces.
kind: reference
status: stable
order: 50
keywords:
  - if const
  - while const
  - for
  - destructuring
  - irrefutable
source:
  - CONCEPT.md#algebraic-data-types-and-pattern-matching
---

`match` is not the only place a pattern stands. `const`, `var`, `if`, `while` and `for` all take one, and the same
rules of [Pattern forms](pattern-forms.md) apply - the difference is what happens when the pattern does not match.

## Example

```trb check
fn firstPositive(numbers: List<Int>): Int? {
  if const Some(first) = numbers.find({ _ > 0 }) {
    return Some first
  }
  None
}

print firstPositive([-1, -2, 3, 4])
```

## Syntax

```text
const <pattern> [: <Type>] = <expression>       binds; the pattern has to match every value of that type
var   <pattern> [: <Type>] = <expression>        the same, and the names it binds are `var`
for <pattern> in <expression> { ... }            binds for one iteration; the pattern has to match every item
if const <pattern> = <expression> { ... }        the names bind only inside the block, only if the pattern matches
if var <pattern> = <place> { ... }               binds into the place instead of into a copy; see `if var`
while const <pattern> = <expression> { ... }     re-evaluates and re-matches before every iteration
```

## Rules

1. **`if const` and `while const` bind their names only for the body, and only when the pattern matches.** They read
   like a `match` with exactly one arm and an implicit `else` that skips the body (`if`) or ends the loop (`while`).

   ```trb check
   fn describe(id: Int, findUser: (id: Int) => String?): String {
     if const Some(name) = findUser(id) {
       return name
     }
     "no such user"
   }
   ```

2. **`const` and `var` are irrefutable positions: the pattern has to match every value of the type it is given, or
   the checker reports one it does not.** A pattern that can fail belongs in a `match` or an `if const` instead.

   ```trb error
   fn firstOf(pairs: List<(Int, Int)>): Int {
     const [first] = pairs
     first.0
   }
   print firstOf([(1, 2)])
   // error: The pattern of a binding has to match every value, and `[]` does not
   ```

3. **A tuple has exactly one shape, so a tuple pattern is irrefutable there, and a case pattern is irrefutable for a
   type with exactly one constructor.** Both bind directly after `const`/`var`, without a `match`:

   ```trb check
   type Point {
     x: Int
     y: Int
   }

   fn run() {
     const (quotient, remainder) = (7 / 2, 7 % 2)
     const Point(x, y) = Point 3, 4
     print "{quotient} {remainder} {x} {y}"
   }

   run()
   ```

4. **The variable of a `for` loop is a pattern too, and it has to match every item of what is iterated.** `for (key,
   value) in someMap` destructures each entry; a pattern that only some items match is the same error as rule 2, naming
   `"item"` instead of `"value"`.

   ```trb check
   fn total(pairs: Map<String, Int>): Int {
     var sum = 0
     for (key, value) in pairs {
       sum = sum + value
     }
     sum
   }

   print total(["a": 1, "b": 2])
   ```

5. **The name a `for` loop binds is `const` for the duration of one iteration.** To change an element in place, use a
   path into the collection (`items[index].x = 1`) or `items.update(index) { ... }`, not the loop variable.

## What this is not

**A pattern at the top level of an entry file or a script is not the same as one inside a function today.** Inside a
function body, `const (a, b) = pair` and `const Point(x, y) = point` both declare their names exactly as rule 3 shows.
Written directly at the top level of an entry file or a script, the checker accepts the same syntax but does not yet
declare the names a tuple or a case pattern introduces there - a later use of one reports it as missing rather than as
unsupported. Wrap top-level destructuring in a function until this is fixed; a plain name (`const total = ...`) is
unaffected at either level.

**`if const` is not `if let` used for control flow on its own; the condition still has to be a pattern that can fail.**
A pattern that always matches belongs after a plain `const`, and the checker says so instead of accepting a binding
that could have been a `match` with one arm:

```trb error
fn run() {
  if const value = 3 {
    print value
  }
}
run()
// error: This pattern always matches. Use `const`
```

## Related

- [Pattern forms](pattern-forms.md) - every pattern this page reuses.
- [Exhaustiveness](exhaustiveness.md) - the algorithm behind "has to match every value".
- [if var](if-var.md) - binding into a place instead of into a copy.
- [Bindings](../values-and-types/bindings.md) - `const` and `var` themselves, and the same top-level gap noted there.
