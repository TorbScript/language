---
title: std/environment
summary: Environment, the one type that reads a process environment variable.
kind: package
status: stable
order: 160
keywords:
  - std/environment
  - environment variable
  - capability
source:
  - std/environment/src/lib.trb
---

`std/environment` reads the process environment. It needs the `std/environment` capability inside a sandboxed script,
and only the variables matching the patterns granted there (`environment "APP_*"`) are visible to it. It is deliberately
not in the prelude: `use Environment from "std/environment"` at the top of a file is the statement "this file reads the
environment".

## Import

```trb fragment
use Environment from "std/environment"
```

```trb check
use Environment from "std/environment"

const home = Environment.get "HOME"
print(home ?? "not set")
```

## Declarations

### Environment

```trb fragment
public native type Environment {
  static fn get(name: String): String?
}
```

`get(name)` answers `None` if the variable is not set, or if the caller is not allowed to see it - the sandbox and a
variable that does not exist look the same on purpose, so a script cannot tell a granted-but-empty capability from a
capability it never had.

## Related

- [std/sandbox](sandbox.md) - `SandboxCapabilities.environment`, which grants the patterns a script may read.
- [The standard library](index.md) - the other packages.
