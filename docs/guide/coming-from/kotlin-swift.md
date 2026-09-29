---
title: Coming from Kotlin and Swift
summary: What maps directly from Kotlin or Swift, the five habits that will trip you up, and the property and coroutine features TorbScript leaves out.
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

`when` and `switch` look like `match`, an extension looks like `extend`, and `String?` means what you expect. The
surface is close enough that a habit from either language often compiles and means something else.

## At a glance

| Kotlin / Swift | TorbScript | Note |
|---|---|---|
| `val` / `let`, `var` | `const`, `var` | `const` also freezes everything inside the value |
| `String?` / `Optional<String>` | `String?` (`Option<String>`) | the same idea |
| `data class Point(val x: Int)` / `struct Point` | `type Point { x: Int }` | one keyword |
| `sealed class Shape` / `enum Shape` | `type Shape { case Circle(radius: Float) }` | cases and fields in one declaration |
| `interface` / `protocol` | `trait` | a type says `with Shape` |
| `fun Circle.area()` / `extension Circle` | `extend Circle with Area { }` | an extension adds a trait |
| `when (shape) { is Circle -> }` / `switch` | `match shape { .Circle(radius) => ... }` | every case handled, no `else` needed |
| `mutating func grow()` | `var fn grow()` | the method says that it changes the value |
| `a ?: b` / `a ?? b` | `a ?? b` | the same |
| `!!` / `!` | nothing | there is no force unwrap |
| `object Registry` | a `shared type` you create once | no hidden singleton |
| `suspend fun` / `async func` | a function returning `Task<Value>` | waiting is in the type |
| `ktlint` / `SwiftFormat` | `torb format` | one layout, and `--check` fails a build |
| `Hashable`, `Equatable`, `Comparable` | `Hash`, `Equals`, `Compare` | a trait is named after its method |
| `@DslMarker`, scope functions | nothing | one rule for every configuration block |

## What changes in your code

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

There are no computed properties: `area()` is a method, and you call it. A field and a method never share a name, so
you always know which one you are looking at.

## What TorbScript does not have

No property observers (`willSet`, `didSet`) and no `get`/`set` blocks. No `lateinit` and no two-phase initialization.
No smart casts: take a value apart with `match` instead. No `suspend` keyword. Visibility is `public` or private to
the file, nothing in between.

## Habits to unlearn

- **`!!` or `!` to unwrap.** Handle the `None` case, or give a fallback with `??`.
- **`apply`, `run`, `let` or `guard let`.** A configuration block has one form, and `?` does what `guard` does.
- **An `else` arm only to satisfy the compiler.** A `match` over all cases needs none, and an arm that can never match
  is an error.
- **`while (true)`.** Write `loop { ... }`; `while true` is an error that names it.
- **Overloading by parameter type.** A name means one function; a default value covers most overloads.

## Related

- [Coming from Kotlin](../../explanation/coming-from-kotlin.md) and
  [Coming from Swift](../../explanation/coming-from-swift.md) - the long versions, with the reason for each difference.
- [A tour of TorbScript](../tour.md) - the rest of the language in fifteen minutes.
- [Receiver closures](../../language/configuration/receiver-closures.md) - the one rule for configuration blocks.
- [Syntax cheat sheet](../../language/syntax/cheat-sheet.md) - every form of the language on one page.
