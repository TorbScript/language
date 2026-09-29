---
name: torbscript-testing
description: "Writes and runs TorbScript tests: `*.test.trb` files, `test` and `group` from `std/test`, `assert` with its report of the source and the values, and `torb test` with filters, shards, JSON reports and native runs. Use it when adding, fixing or running tests of TorbScript code."
license: MIT
compatibility: "Needs the torb command, which the torbscript skill checks and installs."
---

# Testing TorbScript

The `torbscript` skill has the language and the toolchain; this skill has the tests. Paths are relative to the
directory of this file.

A test is an ordinary function call, `test` and `group` from `std/test`, in a file the compiler runs and nothing
imports. There are no matchers: `assert` takes the condition itself and reports the source and the values it captured
when it fails.

### Steps

1. **Create a file named `<something>.test.trb`, by convention under `tests/`.** The name is what makes it a test,
   wherever it lies in the package, and a test file may hold top-level code, so it is a script and needs no `fn main`.

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

5. **Run the tests with `torb test`.** Without a path it runs every test of the package the working directory is in;
   a path runs the ones below it:

   ```console
   $ torb test my-project/tests
   my-project/tests/vector2.test.trb
     ok      Vector2 > adds component-wise

   1 passed, 0 failed (1 file)
   ```

### Pitfalls

- **A test fails by panicking, not by returning a value.** `assert` panics on a false condition, and the test runner
  catches that panic per test - a test function never returns a `Result` or a `Bool` for the runner to check.
- **Nothing but a `*.test.trb` file, a program's entry, a script or a receiver script may hold top-level code.** Test
  helpers that are more than one file belong in an ordinary module and are imported like any other name.
- **A test cannot import the program.** `src/main.trb` and the `entry` of a `program` line are never importable, so
  what a test calls lives in a module - `src/<something>.trb` - that the program imports too.
- **`assert` needs no message argument to be useful.** `assert(actual == expected, "should be equal")` adds a second
  argument the failure text does not use; the condition alone already carries the values through the expression tree.
- **A failing assertion names the location, not a stack.** The output is one line for what failed and one for where -
  `at <path>:<line>:<column>` - which is what a search for the exact text of a failure has to match.

### Full example

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

### Related

- [std/test](references/standard-library/test.md) - `test` and `group` in full.
- std/expression (skill `torbscript-standard-library`: `references/standard-library/expression.md`) - `assert`, and what an `Expression<Bool>` captures.
- Verify your work (skill `torbscript`: `references/tooling/verifying-your-work.md`) - where `test` fits among `check` and `format --check`.
- Top-level code (skill `torbscript-language`: `references/language/modules-and-packages/top-level-code.md`) - why a test file needs no `fn main`.

## Pages

- [std/test](references/standard-library/test.md) - test and group, the two functions a .test.trb file calls, with assert doing all of the checking.
- [Write a test](references/how-to/write-a-test.md) - Put a test in a file called *.test.trb, by convention under tests/, group related ones, and let assert show the source and the values instead of writing a matcher.
- [torb test](references/tooling/torb-test.md) - torb test runs every *.test.trb file below the paths it is given - one binary for all of them - and prints ok or FAILED for every test call it sees, or JSON Lines for an editor, of every test or of the ones a filter names.
