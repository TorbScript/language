---
title: Option
summary: "Absence is a value, Some(value) or None, and there is no null: a value wraps itself into Some where an Option is expected, and an Option is only ever unwrapped on purpose."
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
sugar for `Option<Value>`, and there is nothing else that models absence - no `null`, no `nil`, no implicit
unwrapping. A value where an `Option` of its type is expected becomes `Some(value)` on its own.

## Example

```trb check
const found: Int? = 3
const missing: Int? = None

print found.isSome()
print missing.isNone()
print found.orElse(0)
print "{found} {missing}"
```

## Syntax

```text
Value?                                   Option<Value>
value                                    a present value, where an Option<Value> is expected
Some(value)                              a present value, written out
None                                     an absent value
```

## Rules

1. **`Option<Value>` has exactly two cases, `Some(value: Value)` and `None`.** `Some` and `None` are bare in an
   expression and in a pattern, because the prelude imports both.

2. **A value where an `Option` of its type is expected becomes `Some(value)`.** The result of a body and `return`, a
   binding with an annotation, an argument, a field and a collection element all wrap a plain value, so `Some` is
   written only where the value is an `Option` itself or its type is not decided yet. `None` is always written. See
   [Conversions](../types/conversions.md), rule 8.

   ```trb check
   fn half(value: Int): Int? {
     if value % 2 != 0 {
       return None
     }
     value / 2
   }

   const scores: List<Int?> = [3, None, 5]
   print "{half(8)} {half(3)} {scores}"
   ```

3. **`isSome()` and `isNone()` ask which case it is; `map`, `flatMap` and `filter` transform without unwrapping.**
   `map` runs its closure on the value if there is one and stays `None` otherwise; `flatMap` is `map` for a closure
   that itself returns an `Option`; `filter` turns `Some` into `None` when the predicate fails.

   ```trb check
   const found: Int? = 3
   print found.map({ _ * 2 })
   print found.flatMap({ value => if value > 0 { Some value } else { None } })
   print found.filter({ _ > 0 })
   ```

4. **`orElse` is the `??` operator, and its fallback is lazy.** `found ?? 0` and `found.orElse(0)` are the same call;
   the fallback expression only runs when the `Option` is `None`.

5. **`okOr` turns an `Option` into a `Result`, with a lazy error for the `None` case.** `expect` unwraps or panics
   with the message given, for the cases where absence would be a bug rather than an outcome.

   ```trb check
   const found: Int? = 3
   print found.okOr("missing")
   print found.expect("should be there")
   ```

6. **`toList()` turns an `Option` into a list of zero or one elements**, which is the way into a pipeline: `map`,
   `filter` and the rest of `Iterate` all follow from there once the value is a `List`.

7. **The generated `Show` writes `Some(value)` or the bare `None`.** A case with fields is written out, a case
   without one is its name - the same rule every other case follows.

8. **`Value??` is an `Option` of an `Option`.** Each `?` wraps once, so `Int??` is `Option<Option<Int>>`: `None` is
   "nothing was looked up", `Some(None)` is "it was looked up and there was nothing". In an expression `??` is the
   fallback; in a type position it is two `?`. The inner `Some` is always written - an `Option` never wraps into
   another one, and the payload of a written `Some` never wraps itself - so `Some(Some(3))` is the one spelling.

   ```trb check
   fn describe(lookup: Int??): String {
     match lookup {
       Some(Some(value)) => "found {value}"
       Some(None) => "looked up, nothing there"
       None => "never looked up"
     }
   }

   print describe(Some(None))
   ```

## What this is not

**An `Option` is not implicitly its value.** A value wraps into an `Option` where one is expected, but the way back
is always written: `??`, `?`, `match` or `if const`.

```trb check
const found: Int? = 3
print(found ?? 0)
```

```trb error
const found: Int? = 3
const plain: Int = found
// error: Expected `Int64`, found `Option<Int64>`
```

**A value does not wrap twice.** An `Int` where an `Int??` is expected is an error, not `Some(Some(value))`:

```trb error
const nested: Int?? = 3
// error: Expected `Option<Option<Int64>>`, found `Int64`
```

**`None` is not `null`, and it does not compare equal to every type.** `Option<Int>` and `Option<String>` are
different types, each with its own `None`; a `None` never stands for "no value of any type".

## Related

- [Void and Never](void-and-never.md) - the other two types with no ordinary constructor.
- [Bindings](bindings.md) - how `?` on a type annotation reads as `Option`.
- [Result](../errors/result.md) - `Ok`/`Fail`, the type `Option.okOr` converts into.

