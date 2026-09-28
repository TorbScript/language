---
title: Coming from TypeScript and JavaScript
summary: The 15 things that map directly from TypeScript and JavaScript, the 5 that will surprise you, and the type-level tricks that have no counterpart.
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

`"tcp" | "udp"` reads almost the same in both languages, and `?.`/`??` behave the same. The resemblance ends at the type
checker's door: a TypeScript type is a shape a value happens to fit, and a JavaScript value is whatever came in at
runtime; a TorbScript type is a declared, nominal thing the compiler checks ahead of time, always, with no escape
hatch.

## At a glance

| TypeScript / JavaScript | TorbScript | Why |
|---|---|---|
| `let x = 1;` / `const x = 1;` | `var x = 1` / `const x = 1` | `const` here also freezes what the value contains |
| `undefined`, `null` | `Value?` (`Option<Value>`) | one representation of absence, and it is a type |
| `try { } catch (e) { }` | `Result<Value, Failure>` and `?` | a failure is in the signature, not a side channel |
| `interface Shape { area(): number }` | `trait Area { fn area(): Float }` | a trait is nominal: a type must say `with` |
| `class Square implements Shape` | `type Square with Area { }` | one keyword for data, no inheritance at all |
| `"tcp" \| "udp"` | `"tcp" \| "udp"` | identical - a union of literals of one base type |
| `` `${a} ${b}` `` | `"{a} {b}"` | interpolation, same idea, different brace |
| `arr.map(x => x * 2)` | `arr.map({ _ * 2 })` | a lazy pipeline; nothing runs until a terminal call |
| `{ ...obj, x: 1 }` | `obj.copy(x: 1)` | every type gets `copy` for free, no manual spread |
| `switch` with `default` | `match`, exhaustive, no `default` needed | a missing case is a compile error, not a silent fallthrough |
| `function f(a: number, b = 1)` | `fn f(a: Int, b: Int = 1): Int` | every parameter is typed, always |
| `value as Type` | nothing - there are no casts | a fallible conversion answers a `Result` or an `Option` |
| `value!` | nothing - there is no non-null assertion | `?.`/`??`/`?` are the only way past an `Option` |
| `enum Color { Red, Green }` | a `type` with `case Red` and `case Green`, one per line | a case never secretly is a number |
| `npm`/`package.json` | `torb`/`project.trb` | one manifest, one lockfile, no separate bundler |

## What changes in your code

A field replaces a getter, and there is no way to intercept a plain read or write:

```trb run
type Circle {
  radius: Float

  fn area(): Float {
    3.14159 * radius * radius
  }
}

print Circle(2.0).area()
// prints 12.56636
```

Where a TypeScript class might expose `get area()` to compute a value lazily, TorbScript has no computed properties: a
field is data, and a method that computes something is called like one (`area()`, not `area`).

## What TorbScript does not have

No `any` and no structural typing - a type is exactly the fields and traits it declares, never "anything with these
properties". No union of arbitrary types, only a union of literals of one base type. No `Partial<T>`, `Pick<T, K>` or
other utility types, no index signatures (`{ [key: string]: number }`), and no ambient `declare` for an untyped
boundary - every boundary is checked.

## Habits to unlearn

- **`value as Type` or `<Type>value` to satisfy the checker.** Nothing bypasses it; a conversion that can fail answers
  a `Result` or an `Option`.
- **The non-null assertion `!`.** There is no operator that turns an `Option` back into its value on a promise.
- **Patching a built-in through its prototype.** `String.prototype.shout = ...` has no counterpart; `extend` only adds
  members a caller can see through an import.
- **`while (true)` / `for (;;)` for an endless loop.** The word is `loop { ... }`, and `while true` is a compile error
  that names it.
- **Overload signatures over one implementation.** A name means one function; a default value or a union parameter
  type replaces most of what an overload set did.

## Related

- [Coming from TypeScript](../../explanation/coming-from-typescript.md) - the full essay, with every difference and
  its reason.
- [TorbScript in 15 minutes](../torbscript-in-15-minutes.md) - the rest of the language, just as quickly.
- [Option](../../language/values-and-types/option.md) - `Some`/`None`, the one representation of absence.
- [Syntax cheat sheet](../../language/syntax/cheat-sheet.md) - every form of the language, at a glance.
