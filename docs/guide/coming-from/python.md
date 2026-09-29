---
title: Coming from Python
summary: What maps directly from Python, the five habits that will trip you up, and what a language checked before it runs leaves out.
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

Strings interpolate like f-strings, and `match` looks familiar. What is gone is everything decided while the program
runs: every type is known before it starts, and the compiler checks it.

## At a glance

| Python | TorbScript | Note |
|---|---|---|
| `x = 1` | `const x = 1` or `var x = 1` | you say whether it can change |
| `f"{a} {b}"` | `"{a} {b}"` | every string interpolates |
| `None` | `Value?` (`Option<Value>`) | a missing value is a type, checked at every use |
| `try: ... except X: ...` | `Result<Value, Failure>` and `?` | a failure is in the signature |
| `@dataclass class Point:` | `type Point { ... }` | one declaration, no decorator |
| `class Shape(Protocol)` | `trait Shape { fn area(): Float }` | a type says `with Shape` |
| `[f(x) for x in xs if p(x)]` | `xs.filter({ p(_) }).map({ f(_) })` | a pipeline instead of a comprehension |
| `match value: case Circle(r):` | `match value { .Circle(radius) => ... }` | a forgotten case is a compile error |
| `def f(a, b=1):` | `fn f(a: Int, b: Int = 1): Int` | every parameter has a type |
| `isinstance(x, Circle)` | `match x { .Circle(radius) => ... }` | take the value apart instead of asking |
| `a or b`, truthy values | `a ?? b`; only a `Bool` in an `if` | `0`, `""` and `[]` are not false |
| `__init__` | nothing | the constructor comes from the fields |
| `snake_case` | `camelCase` | the standard library's convention |
| `pip`, `requirements.txt` | `torb`, `project.trb` | one manifest, no virtual environment |
| `pytest`, `assert x == y` | `torb test`, `assert(x == y)` | a failure prints the expression |

## What changes in your code

```trb run
const names = ["ada", "alan", "grace"]
const shouted = names.filter({ _.byteLength() > 3 }).map({ _.toUpperCase() }).toList()
print shouted
// prints ["ALAN", "GRACE"]
```

A comprehension becomes a pipeline: each step is a method call, and nothing runs until `toList()` asks for the values.

## What TorbScript does not have

No duck typing and no `__getattr__`: every field of a type is declared. No metaclasses, no decorators, no patching of a
built-in type, and no `eval`. No truthiness: `if items:` becomes `if !items.isEmpty()`.

## Habits to unlearn

- **An empty list, `0` or `""` as false.** Write the check out: `!items.isEmpty()`, `count != 0`.
- **A computed default for a field.** A field default is a constant; compute anything else in a `static fn`.
- **`isinstance` or `hasattr` to branch.** Give the type cases and `match` on them.
- **A dictionary of mixed values.** A `Map` has one value type; a type with cases holds the different kinds.
- **Indentation as the block.** Blocks are `{ }`. `torb format` still indents them for you.

## Related

- [A tour of TorbScript](../tour.md) - the rest of the language in fifteen minutes.
- [Cases and match](../../language/pattern-matching/cases-and-match.md) - how a case is written and matched.
- [Result](../../language/errors/result.md) - `Ok`, `Fail` and `?`.
- [Syntax cheat sheet](../../language/syntax/cheat-sheet.md) - every form of the language on one page.
