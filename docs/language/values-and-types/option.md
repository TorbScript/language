---
title: Option
summary: Absence is a value, Some(value) or None, and there is no null and no implicit Some - a value has to be wrapped and unwrapped on purpose.
kind: reference
status: stable
order: 17
keywords:
  - Some
  - None
  - Value?
  - absence
source:
  - std/core/src/option.trb
---

`Option<Value>` is a value that may or may not be there: `Some(value)` when it is, `None` when it is not. `Value?` is
sugar for `Option<Value>`, and there is nothing else that models absence - no `null`, no `nil`, no implicit wrapping
or unwrapping.

## Example

```trb check
const found: Int? = Some 3
const missing: Int? = None

print found.isSome()
print missing.isNone()
print found.orElse(0)
print "{found} {missing}"
```

## Syntax

```text
Value?                                   Option<Value>
Some(value)                              a present value
None                                     an absent value
```

## Rules

1. **`Option<Value>` has exactly two cases, `Some(value: Value)` and `None`.** `Some` and `None` are bare in an
   expression and in a pattern, because the prelude imports both.

2. **`isSome()` and `isNone()` ask which case it is; `map`, `flatMap` and `filter` transform without unwrapping.**
   `map` runs its closure on the value if there is one and stays `None` otherwise; `flatMap` is `map` for a closure
   that itself returns an `Option`; `filter` turns `Some` into `None` when the predicate fails.

   ```trb check
   const found: Int? = Some 3
   print found.map({ _ * 2 })
   print found.flatMap({ value => if value > 0 { Some(value) } else { None } })
   print found.filter({ _ > 0 })
   ```

3. **`orElse` is the `??` operator, and its fallback is lazy.** `found ?? 0` and `found.orElse(0)` are the same call;
   the fallback expression only runs when the `Option` is `None`.

4. **`okOr` turns an `Option` into a `Result`, with a lazy error for the `None` case.** `expect` unwraps or panics
   with the message given, for the cases where absence would be a bug rather than an outcome.

   ```trb check
   const found: Int? = Some 3
   print found.okOr("missing")
   print found.expect("should be there")
   ```

5. **`toList()` turns an `Option` into a list of zero or one elements**, which is the way into a pipeline: `map`,
   `filter` and the rest of `Iterable` all follow from there once the value is a `List`.

6. **The generated `Show` writes `Some(value)` or the bare `None`.** A case with fields is written out, a case
   without one is its name - the same rule every other case follows.

## What this is not

**A value is not implicitly `Some`.** There is no place where a plain `Value` is accepted and silently wrapped into
an `Option<Value>`; the case has to be written.

```trb check
const found: Int? = Some 3
print found
```

```trb error
const found: Int? = 3
// error: Expected `Option<Int64>`, found `Int64`
```

**`None` is not `null`, and it does not compare equal to every type.** `Option<Int>` and `Option<String>` are
different types, each with its own `None`; a `None` never stands for "no value of any type".

## Related

- [Void and Never](void-and-never.md) - the other two types with no ordinary constructor.
- [Bindings](bindings.md) - how `?` on a type annotation reads as `Option`.
- [Result](../errors/result.md) - `Ok`/`Fail`, the type `Option.okOr` converts into.
