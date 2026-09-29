---
title: Coming from Go
summary: What maps directly from Go, the five habits that will trip you up, and the concurrency tools that are not built yet.
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

Go and TorbScript agree that errors are values and that code has one formatted layout. From there they part: there is
no `nil`, no zero value, and a type says which interfaces it has.

## At a glance

| Go | TorbScript | Note |
|---|---|---|
| `x := 1` | `const x = 1` or `var x = 1` | you say whether it can change |
| `(value, error)`, `if err != nil` | `Result<Value, Failure>` and `?` | one return value, and `?` passes the failure on |
| `nil` | `Value?` (`Option<Value>`) | a missing value is a type |
| `type Shape interface { Area() float64 }` | `trait Shape { fn area(): Float }` | a trait, much like an interface |
| satisfying an interface by accident | `type Square with Shape` or `extend` | a type says which traits it has |
| `switch v := x.(type)` | `match x { .Circle(radius) => ... }` | a forgotten case is a compile error |
| zero values | none | every binding and field gets a value |
| exported = capitalized | `public` | the first letter means type or value, not visibility |
| `go f()` | a function returning `Task<Value>`, and `.await()` | waiting is in the type |
| `chan T`, `select` | `Channel` | designed, not runnable yet |
| `gofmt` | `torb format` | one layout |
| `go test`, `t.Errorf` | `torb test`, `assert(...)` | a failure prints the expression |
| `[]T`, `map[K]V` | `List<Item>`, `Map<Key, Value>` | the same idea |
| struct embedding | `with Trait by field` | a trait's methods forwarded to a field |

## What changes in your code

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

`?` replaces `if err != nil { return err }`. The check is still there: on a `Fail`, `?` returns it at once.

## What TorbScript does not have

No `nil`, so no nil pointer and no nil map. No zero values: nothing is left half built. `panic` cannot be recovered,
because it means a bug. `Channel` and `Stream` are designed but do not run yet
([Concurrency and streams](../../language/concurrency-and-streams/index.md)); `Task`, `.await()` and
`numbers.parallel()` run today.

## Habits to unlearn

- **`if err != nil` after every call.** Write `?`.
- **A nil map or slice.** An empty one is `[]`, always safe to use.
- **Capital letters for visibility.** Write `public`.
- **A type switch.** Give the type cases and `match` on them.
- **A goroutine fired and forgotten.** A function that waits returns a `Task`, which somebody awaits or cancels.

## Related

- [A tour of TorbScript](../tour.md) - the rest of the language in fifteen minutes.
- [Result](../../language/errors/result.md) - `Ok`, `Fail` and `?`.
- [Traits](../../language/traits/traits.md) - `with`, `extend` and how traits are named.
- [Syntax cheat sheet](../../language/syntax/cheat-sheet.md) - every form of the language on one page.
