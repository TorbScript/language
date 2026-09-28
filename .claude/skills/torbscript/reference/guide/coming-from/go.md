---
title: Coming from Go
summary: The 15 things that map directly from Go, the 5 that will surprise you, and the concurrency primitives that are not built yet.
kind: contrast
status: stable
order: 40
keywords:
  - Go
  - error
  - goroutine
  - interface
  - gofmt
---

Go and TorbScript already agree that errors are values and that a project has exactly one formatted layout. From
there the languages part: Go has no generics-free escape from `interface{}`-style code, and TorbScript has no nil.

## At a glance

| Go | TorbScript | Why |
|---|---|---|
| `x := 1` / `var x = 1` | `const x = 1` / `var x = 1` | you say up front whether it can change |
| `(Value, error)` return, `if err != nil` | `Result<Value, Failure>` and `?` | one path for the failure, not a second return slot |
| `nil` (pointer, map, slice, interface) | `Value?` (`Option<Value>`) | one representation of absence, and it is a type |
| `type Shape interface { Area() float64 }` | `trait Area { fn area(): Float }` | nominal: a type must say `with Area` |
| implicit interface satisfaction | `type Square with Area { }` or `extend` | a type states which traits it has |
| `switch v := x.(type) { case Circle: }` | `match x { .Circle(radius) => ... }` | exhaustive - a missing case is a compile error |
| `func Area(s Shape) float64` | `fn area(shape: Shape): Float` | identical shape, different keyword |
| zero values (`0`, `""`, `nil`) | no zero values - every binding needs an initializer | nothing is implicitly empty |
| exported = capitalized name | `public fn`/`public var` | capitalization means binding vs. case, not visibility |
| `go func() { ... }()` | `Task<Value>` and `.await()` | waiting is in the type, not a keyword |
| `chan T`, `select` | `Channel` (designed, not runnable yet) | see below |
| `gofmt` | `torb format` | the same idea: one layout, not a discussion |
| `go test`, `t.Errorf` | `torb test`, `assert(...)` | a failed `assert` prints the expression's source |
| `[]T`, `map[K]V` | `List<Item>`, `Map<Key, Value>` | the type is a trait, the implementation is a name |
| struct embedding for reuse | `with Trait by field` (delegation) | explicit, and only for a trait's members |

## What changes in your code

`?` replaces the `if err != nil { return err }` line after every fallible call:

```trb run
fn parsePort(text: String): Result<Int, String> {
  const port = Int.tryFrom(text).mapError({ _ => "{text} is not a number" })?
  if port < 1 || port > 65535 {
    return Fail "{port} is not a port"
  }
  port
}

print parsePort("8080")
// prints Ok(8080)
```

The check is still there - `?` unwraps an `Ok` or returns the `Fail` immediately - it just costs one character instead
of three lines, and the compiler refuses a function that can fail without saying so in its return type.

## What TorbScript does not have

No `nil`, so no nil pointer dereference and no nil map or nil slice with different behavior from an empty one. No
zero values: a struct's fields all need an initializer, there is nothing left half-built. `panic` exists but is not
recoverable - there is no `recover()` - because it means the program reached a state its author considered
impossible, not a condition to handle. `Channel` and a full `Stream` are designed
([Concurrency and streams](../../language/concurrency-and-streams/index.md)) but no back end runs them yet; `Task`,
`.await()` and `numbers.parallel()` are what run today.

## Habits to unlearn

- **Checking `if err != nil` by hand.** Write `?` on the call instead; the failure is still there, just not spelled
  out every time.
- **Relying on a nil map or slice behaving like an empty one.** There is no nil collection - `[]` is what an empty one
  already is, always safe to call a method on.
- **Assuming capitalization controls visibility.** It controls whether a name binds or is a case in a pattern;
  visibility is the separate word `public`.
- **A type switch over a concrete type.** Give the type cases and `match` on them instead of `switch v := x.(type)`.
- **Starting a goroutine to fire and forget.** A function that waits answers `Task<Value>`; nothing runs detached from
  something that can `.await()` or `cancel()` it.

## Related

- [TorbScript in 15 minutes](../torbscript-in-15-minutes.md) - the rest of the language, just as quickly.
- [Result](../../language/errors/result.md) - `Ok`, `Fail` and `?`.
- [Traits](../../language/traits/traits.md) - `with`, `extend` and the trait names.
- [Syntax cheat sheet](../../language/syntax/cheat-sheet.md) - every form of the language, at a glance.

