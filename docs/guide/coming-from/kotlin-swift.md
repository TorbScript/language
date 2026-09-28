---
title: Coming from Kotlin and Swift
summary: The 15 things that map directly from Kotlin or Swift, the 5 that will surprise you, and the property and coroutine machinery neither one keeps.
kind: contrast
status: stable
order: 50
keywords:
  - Kotlin
  - Swift
  - nullable
  - protocol
  - data class
  - sealed class
---

`when`/`switch` looks like `match`, an extension function looks like `extend`, and `?` after a type looks exactly like
Kotlin's own nullable marker or Swift's `Optional`. The surface is close enough on both that a habit from either one
often compiles into something that means the wrong thing rather than failing to compile at all.

## At a glance

| Kotlin / Swift | TorbScript | Why |
|---|---|---|
| `val x = 1` / `var x = 1` | `const x = 1` / `var x = 1` | `const`/`val`/`let` all freeze the binding; only `const` also freezes what it reaches |
| `String?` / `Optional<String>` | `String?` (`Option<String>`) | the same idea, one representation everywhere |
| `data class Point(val x: Int)` | `type Point { var x: Int }` | one keyword, no annotation needed |
| `sealed class Shape` / `enum Shape { case circle }` | `type Shape { case Circle(radius: Float) }` | cases and fields live in the same declaration |
| `interface Shape { fun area(): Double }` / `protocol Shape { func area() -> Double }` | `trait Area { fn area(): Float }` | nominal: a type must say `with Area` |
| `fun Circle.area()` extension | `extend Circle with Area { }` | the same idea, always visible through the trait |
| `when (shape) { is Circle -> }` / `switch shape { case .circle }` | `match shape { .Circle(radius) => ... }` | exhaustive by construction, no `else` needed |
| `fun grow() { this.r += 1 }` / `mutating func grow()` | `var fn grow() { r = r + 1 }` | mutation is marked on the declaration, `var fn` |
| `a ?: b` / `a ?? b` | `a ?? b` | the same operator, and it is the trait `OrElse` here |
| `!!` / force unwrap `!` | nothing - there is no force unwrap | `?.`, `??` and `?` are the only way past an `Option` |
| `object Registry` singleton | a `shared type` with one instance you hold | there is no keyword that hides an allocation |
| `suspend fun` / `async func` | a function returning `Task<Value>` | waiting is a type, not a keyword |
| `ktlint`/`SwiftFormat` (optional) | `torb format` (enforced) | one layout, and `--check` is a gate |
| `Hashable`, `Equatable`, `Comparable` | `Hash`, `Equals`, `Compare` | a single-method trait is named after its method |
| `@DslMarker` for a builder block | nothing to write | one implicit-receiver rule covers every configuration block |

## What changes in your code

A field replaces both a stored and a computed property, because there are no property observers or accessor blocks:

```trb run
type Rectangle {
  var width: Float
  var height: Float

  fn area(): Float {
    width * height
  }
}

var box = Rectangle 3.0, 4.0
box.width = 5.0
print box.area()
// prints 20.0
```

Where Kotlin's `var area: Double get() = width * height` or Swift's computed property would recompute `area` on every
read, here `area()` is a method you call - a field and a method never share a name, so it is never ambiguous which one
you are looking at.

## What TorbScript does not have

No property observers (`willSet`/`didSet`) and no computed properties with `get`/`set` blocks - a field is data, a
method is called. No `lateinit var` or two-phase initialization: a binding needs a value the moment it is declared.
No smart casts - nothing narrows a binding's type after an `if`; take the value apart with `match` instead. No
`suspend`/coroutine keyword, and no five-level access control - visibility is `public` or private to the file, nothing
between.

## Habits to unlearn

- **`!!` or force unwrap `!`.** There is no operator that turns an `Option` back into its value on a promise; handle
  the `None` case.
- **Stacked scope functions (`apply`, `run`, `with`, `also`, `let`) or `guard let ... else`.** There is one
  receiver-closure form, and `?` already does what a `guard` does.
- **An `else` branch added only to satisfy exhaustiveness.** `match` on a `type` with cases is exhaustive by
  construction; an unreachable `else` is a compile error, not a safety net.
- **`while (true)` / endless `for`.** The word is `loop { ... }`, and `while true` is a compile error that names it.
- **Overloading by parameter type or by label.** A name means one function; a default value or a union parameter type
  covers most of what an overload set did.

## Related

- [Coming from Kotlin](../../explanation/coming-from-kotlin.md) and
  [Coming from Swift](../../explanation/coming-from-swift.md) - the full essays, with every difference and its reason.
- [TorbScript in 15 minutes](../torbscript-in-15-minutes.md) - the rest of the language, just as quickly.
- [Receiver closures](../../language/configuration/receiver-closures.md) - the one implicit-receiver rule in full.
- [Syntax cheat sheet](../../language/syntax/cheat-sheet.md) - every form of the language, at a glance.
