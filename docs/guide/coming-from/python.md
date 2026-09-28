---
title: Coming from Python
summary: The 15 things that map directly from Python, the 5 that will surprise you, and what static, value-typed code gives up on dynamism.
kind: contrast
status: stable
order: 30
keywords:
  - Python
  - duck typing
  - dataclass
  - f-string
  - comprehension
---

An f-string and a TorbScript string interpolate the same way, and a `match` statement looks familiar. What is gone is
everything that depends on a type being decided at runtime: there is no duck typing, no `__getattr__`, and nothing is
inspected that was not declared.

## At a glance

| Python | TorbScript | Why |
|---|---|---|
| `x = 1` | `const x = 1` or `var x = 1` | you say up front whether it can change |
| `f"{a} {b}"` | `"{a} {b}"` | interpolation, almost the same spelling |
| `None` | `Value?` (`Option<Value>`) | absence is a type, checked at every use |
| `try: ... except X: ...` | `Result<Value, Failure>` and `?` | a failure is in the signature |
| `@dataclass class Point:` | `type Point { var x: Int, var y: Int }` | one declaration, no decorator |
| `class Shape(Protocol): def area(self)` | `trait Area { fn area(): Float }` | nominal: a type says `with Area` |
| `[f(x) for x in xs if p(x)]` | `xs.filter({ p(_) }).map({ f(_) })` | a lazy pipeline instead of a comprehension |
| `match value: case Circle(r):` | `match value { .Circle(radius) => ... }` | exhaustive - a missing case is a compile error |
| `def f(a, b=1):` | `fn f(a: Int, b: Int = 1): Int` | every parameter and the return type are typed |
| `isinstance(x, Circle)` | `match x { .Circle(radius) => ... }` | you take the value apart, you don't ask about it |
| `a or b`, truthy values | `a ?? b` on an `Option`; `Bool` only in an `if` | `0`, `""` and `[]` are not false - only `Bool` is |
| `self.x = x` in `__init__` | fields declared once, constructor generated | there is no `__init__` to write |
| `snake_case` | `camelCase` | the standard library's own convention |
| `pip`/`requirements.txt` | `torb`/`project.trb` | one manifest, one lockfile, no virtual environment |
| `pytest`, `assert x == y` | `torb test`, `assert(x == y)` | the same idea; a failure prints the expression's source |

## What changes in your code

A comprehension becomes a pipeline, written as one stage per line:

```trb run
const names = ["ada", "alan", "grace"]
const shouted = names
  .filter({ _.byteLength() > 3 })
  .map({ _.toUpperCase() })
  .toList()

print shouted
// prints ["ALAN", "GRACE"]
```

Nothing runs until `toList()` pulls the values through, exactly as a comprehension runs all at once but with the
stages written as method calls instead of `for`/`if` inside brackets.

## What TorbScript does not have

No duck typing and no `__getattr__`/`__setattr__` - every attribute of a type is declared. No metaclasses, no
decorators, no monkey-patching a built-in type, and no dynamic `eval` of code built at runtime. No implicit truthiness:
`if items:` becomes `if !items.isEmpty()`, because only a `Bool` is legal in an `if`.

## Habits to unlearn

- **Treating an empty list, `0` or `""` as false.** Only `Bool` is allowed in an `if` or a `while`; write the check out
  (`!items.isEmpty()`, `count != 0`).
- **A default argument that is a mutable literal.** There is no shared-mutable-default bug to avoid, because every
  value is a copy - but a field default still has to be a constant expression, computed defaults go in a `static fn`.
- **`isinstance` / `hasattr` to branch on a value's shape.** Give the type cases and `match` on them; there is no
  runtime type inspection to fall back on.
- **Reaching for a dictionary with mixed value types.** A `Map<Key, Value>` has one `Value` type; a type with cases is
  what Python code reaches for `dict`-of-anything instead.
- **Indentation as the block delimiter.** Blocks are `{ }`; `torb format` still enforces consistent indentation, but
  the compiler reads the braces, not the whitespace.

## Related

- [TorbScript in 15 minutes](../torbscript-in-15-minutes.md) - the rest of the language, just as quickly.
- [Cases and match](../../language/pattern-matching/cases-and-match.md) - how a case is spelled and matched.
- [Result](../../language/errors/result.md) - `Ok`, `Fail` and `?`.
- [Syntax cheat sheet](../../language/syntax/cheat-sheet.md) - every form of the language, at a glance.
