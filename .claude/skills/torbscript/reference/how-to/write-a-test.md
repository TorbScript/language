---
title: Write a test
summary: Put a test in tests/*.test.trb, group related ones, and let assert show the source and the values instead of writing a matcher.
kind: how-to
status: stable
order: 50
keywords:
  - test
  - group
  - assert
  - std/test
source:
  - std/test/src/lib.trb
  - CONCEPT.md#modules-and-packages
---

A test is an ordinary function call, `test` and `group` from `std/test`, in a file the compiler runs and nothing
imports. There are no matchers: `assert` takes the condition itself and reports the source and the values it captured
when it fails.

## Steps

1. **Create the file under `tests/`, named `<something>.test.trb`.** This is one of the four places top-level code is
   allowed, so the file is a script and needs no `fn main`.

   ```text
   my-project/
   ├ src/
   ├ tests/
   ├─ vector2.test.trb
   └ project.trb
   ```

2. **Import `test` and `group`.** Neither is in the prelude, so every test file says so on its first line.

   ```trb fragment
   use test, group from "std/test"
   ```

3. **Name a `group` for the type or the function under test, and a `test` for each behaviour.** Both take a name and a
   closure; a `group` nests freely, so a suite is organized by nesting one inside another.

   ```trb fragment
   group "Vector2" {
     test "adds component-wise" {
       assert(Vector2(1.0, 2.0).plus(Vector2(3.0, 4.0)) == Vector2(4.0, 6.0))
     }
   }
   ```

4. **Write the condition as one `assert`, not a sequence of them with messages.** `assert` takes an
   `Expression<Bool>`, so a failure already shows the source text of the condition and the values it closed over -
   there is nothing to add by writing a message.

5. **Run the tests with `torb test`:**

   ```console
   $ torb test my-project/tests
   my-project/tests/vector2.test.trb
     ok      Vector2 > adds component-wise

   1 passed, 0 failed (1 file)
   ```

## Pitfalls

- **A test fails by panicking, not by returning a value.** `assert` panics on a false condition, and the test runner
  catches that panic per test - a test function never returns a `Result` or a `Bool` for the runner to check.
- **Nothing outside `tests/*.test.trb`, `src/main.trb`, a script or a receiver script may hold top-level code.** Test
  helpers that are more than one file belong in an ordinary module under `src/` and are imported like any other name.
- **`assert` needs no message argument to be useful.** `assert(actual == expected, "should be equal")` adds a second
  argument the failure text does not use; the condition alone already carries the values through the expression tree.
- **A failing assertion names the location, not a stack.** The output is one line for what failed and one for where -
  `at <path>:<line>:<column>` - which is what a search for the exact text of a failure has to match.

## Full example

```trb check
use test, group from "std/test"

type Vector2 {
  x: Float
  y: Float

  fn plus(other: Vector2): Vector2 {
    Vector2(x + other.x, y + other.y)
  }
}

group "Vector2" {
  test "adds component-wise" {
    assert(Vector2(1.0, 2.0).plus(Vector2(3.0, 4.0)) == Vector2(4.0, 6.0))
  }
}
```

## Related

- [std/test](../standard-library/test.md) - `test` and `group` in full.
- [std/expression](../standard-library/expression.md) - `assert`, and what an `Expression<Bool>` captures.
- [Verify your work](../tooling/verifying-your-work.md) - where `test` fits among `check` and `canon --check`.
- [Top-level code](../language/modules-and-packages/top-level-code.md) - why a test file needs no `fn main`.

