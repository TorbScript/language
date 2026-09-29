---
title: Coming from TypeScript and JavaScript
summary: What maps directly from TypeScript and JavaScript, the five habits that will trip you up, and the type tricks that have no counterpart.
kind: contrast
status: stable
order: 20
keywords:
  - TypeScript
  - JavaScript
  - structural typing
  - any
  - undefined
---

`?.` and `??` behave the same, and `"tcp" | "udp"` reads the same. The difference is the type checker: a TypeScript
type is a shape a value happens to fit, and a JavaScript value is whatever arrives. A TorbScript type is declared by
name, always checked, and has no way around the check.

## At a glance

| TypeScript / JavaScript | TorbScript | Note |
|---|---|---|
| `let x = 1;` / `const x = 1;` | `var x = 1` / `const x = 1` | `const` also freezes everything inside the value |
| `undefined`, `null` | `Value?` (`Option<Value>`) | one kind of missing value, and it is a type |
| `try { } catch (e) { }` | `Result<Value, Failure>` and `?` | a failure is in the signature |
| `interface Shape { area(): number }` | `trait Shape { fn area(): Float }` | a type must say `with Shape` |
| `class Square implements Shape` | `type Square with Shape { }` | no classes, no inheritance |
| `"tcp" \| "udp"` | `"tcp" \| "udp"` | the same |
| `` `${a} ${b}` `` | `"{a} {b}"` | every string interpolates |
| `arr.map(x => x * 2)` | `arr.map({ _ * 2 })` | lazy: nothing runs until a last step asks |
| `{ ...obj, x: 1 }` | `obj.copy(x: 1)` | every type has `copy` |
| `switch` with `default` | `match` | a forgotten case is a compile error |
| `function f(a: number, b = 1)` | `fn f(a: Int, b: Int = 1): Int` | every parameter has a type |
| `value as Type` | nothing | no casts: a conversion that can fail returns a `Result` |
| `value!` | nothing | no non-null assertion |
| `enum Color { Red, Green }` | `type Color { case Red ... }` | a case is its own value |
| `npm`, `package.json` | `torb`, `project.trb` | one manifest, no bundler |

## What changes in your code

```trb run
type Circle {
  radius: Float

  fn area(): Float {
    3.0 * radius * radius
  }
}

print Circle(2.0).area()
// prints 12.0
```

There are no getters: a field is data, and a computed value is a method you call, `area()`.

## What TorbScript does not have

No `any`, and no type that is "anything with these fields": a type is exactly what it declares. No union of unrelated
types, only of literals of one type. No `Partial<T>`, `Pick<T, K>` or index signatures, and no `declare` for untyped
code.

## Habits to unlearn

- **`as` to quiet the checker.** Nothing gets past it; a conversion that can fail returns a `Result` or an `Option`.
- **`value!`.** Handle the `None` case, or give a fallback with `??`.
- **Patching a built-in through its prototype.** `extend` adds a trait, visible only where it is imported.
- **`while (true)` or `for (;;)`.** Write `loop { ... }`; `while true` is an error that names it.
- **Overload signatures.** A name means one function; a default value covers most overloads.

## Related

- Coming from TypeScript (skill `torbscript-language`: `references/explanation/coming-from-typescript.md`) - the long version, with the reason for each
  difference.
- [A tour of TorbScript](../tour.md) - the rest of the language in fifteen minutes.
- Option (skill `torbscript-language`: `references/language/values-and-types/option.md`) - `Some` and `None`, the one kind of missing value.
- [Syntax cheat sheet](../../language/syntax/cheat-sheet.md) - every form of the language on one page.

