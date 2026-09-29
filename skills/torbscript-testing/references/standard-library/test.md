---
title: std/test
summary: test and group, the two functions a .test.trb file calls, with assert doing all of the checking.
kind: package
status: stable
order: 170
keywords:
  - std/test
  - test
  - group
  - assert
source:
  - std/test/src/lib.trb
---

`std/test` makes a test an ordinary function call. There are no matchers: `assert` (from std/expression (skill `torbscript-standard-library`: `references/standard-library/expression.md`))
takes an `Expression<Bool>`, so a failure shows the source of the condition and the values it captured. A test fails if
it panics, and the test runner catches that per test.

## Import

```trb fragment
use test, group from "std/test"
```

```trb check
use test from "std/test"

type Vector2 {
  x: Float
  y: Float
}

test "compares equal to itself" {
  const v = Vector2 x: 1.0, y: 2.0
  assert(v == v)
}
```

## Declarations

### `test`, `group`

```trb fragment
public native fn test(name: String, body: () => Void)
public native fn group(name: String, body: () => Void)
```

`group` names a closure of `test` calls; `test` names a closure whose body is the check. Both are ordinary calls in the
`.test.trb` files of `tests`, and nest freely - a `group` inside a `group` is how a suite is organized.

## Related

- std/expression (skill `torbscript-standard-library`: `references/standard-library/expression.md`) - `assert`, the one function a test body calls.
- The standard library (skill `torbscript-standard-library`: `references/standard-library/index.md`) - the other packages.

